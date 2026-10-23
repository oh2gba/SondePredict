#include "predictor.h"

#include <QDateTime>
#include <QPointer>
#include <QTimeZone>

#include <algorithm>
#include <cmath>
#include <limits>

namespace sp {

Predictor::Predictor(WindSource *source, std::shared_ptr<WindField> field, QObject *parent)
    : Predictor(std::vector<WindBackend>{WindBackend{{{source, std::move(field)}}, {}, {}, {}}}, parent)
{
}

Predictor::Predictor(std::vector<WindBackend> backends, QObject *parent)
    : QObject(parent)
    , m_backends(std::move(backends))
{
}

void Predictor::start(const LaunchParams &params, qint64 alsoCoverUntil)
{
    ++m_generation;
    m_running = true;
    m_current = Prediction{params, {}, 0, 0, std::nullopt, {}, {}};
    m_alsoCoverUntil = alsoCoverUntil;
    m_limitReached = false;
    m_failedSources.clear();
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    emit activity(tr("Prediction for %1 UTC from %2, %3: ascent %4 m/s, burst %5 m")
                      .arg(QDateTime::fromSecsSinceEpoch(params.launchTime, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm")))
                      .arg(params.site.lat, 0, 'f', 4).arg(params.site.lon, 0, 'f', 4)
                      .arg(params.profile.ascentRate).arg(params.profile.burstAltitude, 0, 'f', 0));

    if (const char *error = params.profile.validationError(); *error) {
        const int generation = m_generation;
        QMetaObject::invokeMethod(this, [this, generation, message = QString::fromUtf8(error)]() {
            if (generation == m_generation)
                fail(message, false);
        }, Qt::QueuedConnection);
        return;
    }

    // The first backend that covers the launch (the last one always serves as a fallback).
    m_backend = 0;
    while (m_backend + 1 < m_backends.size() && !usable(m_backends[m_backend])) {
        emit activity(tr("%1: not used, the launch is outside its area or beyond its forecast range").arg(m_backends[m_backend].name));
        ++m_backend;
    }
    begin();
}

bool Predictor::usable(const WindBackend &backend) const
{
    if (backend.covers && !backend.covers(m_current.params))
        return false;
    // A source that already failed for this prediction would fail again.
    return std::none_of(backend.parts.begin(), backend.parts.end(),
                        [this](const WindBackend::Part &part) { return m_failedSources.contains(part.source); });
}

const WindSampler &Predictor::sampler() const
{
    if (m_blend)
        return *m_blend;
    return *m_parts.front().field;
}

void Predictor::begin()
{
    const WindBackend &backend = m_backends[m_backend];
    m_parts = backend.parts;
    m_blend.reset();
    if (m_parts.size() > 1) {
        std::vector<std::shared_ptr<const WindField>> fields;
        for (const WindBackend::Part &part : m_parts)
            fields.push_back(part.field);
        m_blend = std::make_unique<BlendedWind>(std::move(fields));
    }
    m_current.model = backend.model;
    m_current.result = {};
    m_current.passes = 0;
    m_current.landingArea.reset();
    m_requested.clear();
    m_areaStage = false;
    m_limitReached = false;
    const LaunchParams &params = m_current.params;

    // Flight window plus margin; later passes widen it if the flight needs more.
    const double duration = estimatedFlightTime(params.profile, params.siteAltitude.value_or(0.0));
    const auto lastHourFor = [duration](qint64 launch) {
        return static_cast<qint64>(std::floor((double(launch) + duration * 1.25 + 1800.0) / 3600.0)) + 1;
    };
    m_firstHour = params.launchTime / 3600;
    m_lastHour = lastHourFor(params.launchTime);
    m_coverLastHour = lastHourFor(std::max(params.launchTime, m_alsoCoverUntil));

    // Start with the four grid points around the launch site: a first flight through them
    // shows where the balloon goes, and the next pass fetches the points along that path.
    // (A 4×4 block here cost about a third more calls for the same result.)
    std::set<NodeKey> keys;
    for (std::size_t layer = 0; layer < m_parts.size(); ++layer) {
        const WindField &field = *m_parts[layer].field;
        const NodeIndex cell = field.cellOf(params.site);
        for (int di = 0; di <= 1; ++di) {
            for (int dj = 0; dj <= 1; ++dj) {
                for (qint64 hour = m_firstHour; hour <= m_lastHour; ++hour)
                    keys.insert({field.wrap({cell.lat + di, cell.lon + dj}), hour, int(layer)});
            }
        }
    }

    emit activity(tr("Wind data: %1, forecast hours %2 to %3 UTC")
                      .arg(backend.name.isEmpty() ? backend.model : backend.name)
                      .arg(QDateTime::fromSecsSinceEpoch(m_firstHour * 3600, QTimeZone::UTC).toString(QStringLiteral("dd HH:mm")))
                      .arg(QDateTime::fromSecsSinceEpoch(m_lastHour * 3600, QTimeZone::UTC).toString(QStringLiteral("dd HH:mm"))));
    emit progress(tr("Fetching winds around the launch site"));
    fetch(keys);
}

void Predictor::cancel()
{
    ++m_generation;
    m_running = false;
}

void Predictor::fetch(const std::set<NodeKey> &keys)
{
    // Per part: the nodes not loaded yet and the hours they lack. Sources where extra hours
    // are free load the whole window (plus the hours of a batch's later launches); others
    // only the hours that are missing.
    std::vector<std::set<NodeIndex>> needed(m_parts.size());
    std::vector<std::pair<qint64, qint64>> hours(m_parts.size(), {std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::min()});
    for (const NodeKey &key : keys) {
        const std::size_t layer = std::size_t(key.layer);
        const WindField &field = *m_parts[layer].field;
        const bool free = m_parts[layer].source->extraHoursAreFree();
        const qint64 last = free ? std::max(m_lastHour, m_coverLastHour) : m_lastHour;
        for (qint64 hour = m_firstHour; hour <= last; ++hour) {
            m_requested.insert({key.node, hour, key.layer});
            if (!field.contains({key.node, hour})) {
                needed[layer].insert(key.node);
                hours[layer].first = std::min(hours[layer].first, free ? m_firstHour : hour);
                hours[layer].second = std::max(hours[layer].second, free ? last : hour);
            }
        }
    }

    m_pendingFetches = 0;
    m_fetchResult = {};
    m_failedSource = nullptr;
    for (const auto &nodes : needed)
        m_pendingFetches += nodes.empty() ? 0 : 1;

    if (m_pendingFetches == 0) {
        // Everything is cached; still continue asynchronously so callers always get
        // finished()/failed() after start() has returned.
        const int generation = m_generation;
        QMetaObject::invokeMethod(this, [this, generation]() {
            if (generation == m_generation)
                afterFetch();
        }, Qt::QueuedConnection);
        return;
    }

    const int generation = m_generation;
    QPointer<Predictor> self(this);
    for (std::size_t layer = 0; layer < m_parts.size(); ++layer) {
        if (needed[layer].empty())
            continue;
        m_current.nodesFetched += static_cast<int>(needed[layer].size());
        WindSource *source = m_parts[layer].source;
        source->fetch({needed[layer].begin(), needed[layer].end()}, hours[layer].first, hours[layer].second, m_parts[layer].field,
                      [self, generation, source](const FetchResult &result) {
            if (!self || generation != self->m_generation)
                return;
            if (!result.error.isEmpty() && self->m_fetchResult.error.isEmpty()) {
                self->m_fetchResult = result;
                self->m_failedSource = source;
            }
            if (--self->m_pendingFetches > 0)
                return;
            // The landing-area fetch is optional: the prediction itself is complete by then, so
            // finish it with the area estimated from the winds already loaded.
            if (!self->m_fetchResult.error.isEmpty() && !self->m_areaStage) {
                self->m_limitReached = self->m_fetchResult.limitReached;
                self->m_failedSources.insert(self->m_failedSource);
                self->fail(self->m_fetchResult.error);
            } else {
                self->afterFetch();
            }
        });
    }
}

void Predictor::afterFetch()
{
    if (m_areaStage)
        estimateAreaAndFinish();
    else
        pass();
}

void Predictor::pass()
{
    ++m_current.passes;
    emit progress(tr("Calculating track (pass %1)").arg(m_current.passes));

    m_current.result = simulate(m_current.params, sampler());
    const SimulationResult &result = m_current.result;
    if (result.landing) {
        const Geodesic g = inverse(m_current.params.site, result.landing->position);
        emit activity(tr("Pass %1: landing %2 km from the launch site at %3°%4")
                          .arg(m_current.passes).arg(g.distance / 1000.0, 0, 'f', 1).arg(qRound(g.initialBearing))
                          .arg(result.missing.empty() ? tr(", all wind data loaded")
                                                      : tr("; %1 grid point-hours still missing").arg(result.missing.size())));
    }

    if (result.missing.empty()) {
        if (!result.error.isEmpty()) {
            fail(result.error);
            return;
        }
        estimateAreaAndFinish();
        return;
    }

    if (m_current.passes >= kMaxPasses) {
        fail(tr("could not load all wind data after %1 passes").arg(kMaxPasses));
        return;
    }

    // If everything still missing was already asked for, the source simply has no data
    // for it (typically a time beyond the forecast range or a place outside its area).
    const auto unrequested = std::find_if(result.missing.begin(), result.missing.end(),
                                          [this](const NodeKey &key) { return !m_requested.contains(key); });
    if (unrequested == result.missing.end()) {
        const NodeKey &first = *result.missing.begin();
        m_failedSources.insert(m_parts[std::size_t(first.layer)].source);
        const QDateTime when = QDateTime::fromSecsSinceEpoch(first.hour * 3600, QTimeZone::UTC);
        fail(tr("no wind data available for %1 UTC (outside the forecast range or area?)")
                 .arg(when.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        return;
    }

    std::set<NodeIndex> nodes;
    for (const NodeKey &key : result.missing) {
        nodes.insert(key.node);
        m_firstHour = std::min(m_firstHour, key.hour);
        m_lastHour = std::max(m_lastHour, key.hour);
    }

    emit progress(tr("Fetching winds along the track (%1 grid points)").arg(nodes.size()));
    fetch(result.missing);
}

void Predictor::estimateAreaAndFinish()
{
    if (m_spread.enabled) {
        emit progress(tr("Estimating the landing area"));
        FlightSpread spread = m_spread;
        const double lead = double(m_current.params.launchTime - QDateTime::currentSecsSinceEpoch()) / 3600.0;
        spread.windSigma = m_windSigmaOverride ? *m_windSigmaOverride : spread.windUncertainty ? forecastWindSigma(lead) : 0.0;
        const LandingAreaResult area = estimateLandingArea(m_current.params, sampler(), spread);
        m_current.landingArea = area.area;
        if (area.area)
            emit activity(tr("Landing area from %1 varied flights: %2 × %3 km (95%)%4")
                              .arg(area.area->samples).arg(area.area->majorAxis() / 1000.0, 0, 'f', 1)
                              .arg(area.area->minorAxis() / 1000.0, 0, 'f', 1)
                              .arg(m_areaStage ? tr(", with the extra winds") : QString()));

        // Varied flights can reach grid points the nominal one did not; fetch those once.
        std::set<NodeKey> keys;
        std::set<std::pair<int, NodeIndex>> nodes;
        qint64 first = m_firstHour, last = m_lastHour;
        for (const NodeKey &key : area.missing) {
            if (m_requested.contains(key))
                continue;
            keys.insert(key);
            nodes.insert({key.layer, key.node});
            first = std::min(first, key.hour);
            last = std::max(last, key.hour);
        }
        if (!m_areaStage && !keys.empty()) {
            m_areaStage = true;
            m_firstHour = first;
            m_lastHour = last;
            emit progress(tr("Fetching winds for the landing area (%1 grid points)").arg(nodes.size()));
            fetch(keys);
            return;
        }
    }
    m_running = false;
    emit activity(tr("Done in %1 s: %2 passes, %3 grid points downloaded")
                      .arg(double(QDateTime::currentMSecsSinceEpoch() - m_startedMs) / 1000.0, 0, 'f', 1)
                      .arg(m_current.passes).arg(m_current.nodesFetched));
    const Prediction result = m_current; // a slot may start the next prediction
    emit finished(result);
}

void Predictor::fail(const QString &error, bool tryNextSource)
{
    // Copy: `error` may refer to m_current, which a slot starting the next prediction overwrites.
    const QString message = error;
    if (tryNextSource) {
        // Try the next wind source that can serve this flight (for example the global model
        // when MEPS's server failed or the flight leaves its area).
        std::size_t next = m_backend + 1;
        while (next < m_backends.size() && next + 1 < m_backends.size() && !usable(m_backends[next]))
            ++next;
        if (next < m_backends.size() && usable(m_backends[next])) {
            const WindBackend &failed = m_backends[m_backend];
            const QString name = failed.name.isEmpty() ? failed.model : failed.name;
            m_current.fallbackNote = tr("%1: %2").arg(name, message);
            m_backend = next;
            emit progress(tr("%1 could not be used (%2); trying %3").arg(name, message, m_backends[m_backend].name));
            emit activity(tr("%1 could not be used (%2); trying %3 instead").arg(name, message, m_backends[m_backend].name));
            const int generation = m_generation;
            QMetaObject::invokeMethod(this, [this, generation]() {
                if (generation == m_generation)
                    begin();
            }, Qt::QueuedConnection);
            return;
        }
    }
    m_running = false;
    emit activity(tr("Failed: %1").arg(message));
    emit failed(message);
}

} // namespace sp
