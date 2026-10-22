#include "predictor.h"

#include <QDateTime>
#include <QPointer>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

namespace sp {

Predictor::Predictor(WindSource *source, std::shared_ptr<WindField> field, QObject *parent)
    : QObject(parent)
    , m_source(source)
    , m_field(std::move(field))
{
}

void Predictor::start(const LaunchParams &params)
{
    ++m_generation;
    m_running = true;
    m_current = Prediction{params, {}, 0, 0};
    m_requested.clear();

    if (const char *error = params.profile.validationError(); *error) {
        const int generation = m_generation;
        QMetaObject::invokeMethod(this, [this, generation, message = QString::fromUtf8(error)]() {
            if (generation == m_generation)
                fail(message);
        }, Qt::QueuedConnection);
        return;
    }

    // Flight window plus margin; later passes widen it if the flight needs more.
    const double duration = estimatedFlightTime(params.profile, params.siteAltitude.value_or(0.0));
    m_firstHour = params.launchTime / 3600;
    m_lastHour = static_cast<qint64>(std::floor((params.launchTime + duration * 1.25 + 1800.0) / 3600.0)) + 1;

    // A 4×4 block of nodes around the launch site covers the first part of the ascent.
    const NodeIndex cell = m_field->cellOf(params.site);
    std::vector<NodeIndex> nodes;
    for (int di = -1; di <= 2; ++di) {
        for (int dj = -1; dj <= 2; ++dj)
            nodes.push_back(m_field->wrap({cell.lat + di, cell.lon + dj}));
    }

    emit progress(tr("Fetching winds around the launch site"));
    fetch(std::move(nodes), m_firstHour, m_lastHour);
}

void Predictor::cancel()
{
    ++m_generation;
    m_running = false;
}

void Predictor::fetch(std::vector<NodeIndex> nodes, qint64 firstHour, qint64 lastHour)
{
    std::vector<NodeIndex> needed;
    for (const NodeIndex &node : nodes) {
        bool complete = true;
        for (qint64 hour = firstHour; hour <= lastHour; ++hour) {
            m_requested.insert({node, hour});
            complete = complete && m_field->contains({node, hour});
        }
        if (!complete)
            needed.push_back(node);
    }

    if (needed.empty()) {
        // Everything is cached; still continue asynchronously so callers always get
        // finished()/failed() after start() has returned.
        const int generation = m_generation;
        QMetaObject::invokeMethod(this, [this, generation]() {
            if (generation == m_generation)
                pass();
        }, Qt::QueuedConnection);
        return;
    }

    m_current.nodesFetched += static_cast<int>(needed.size());
    const int generation = m_generation;
    QPointer<Predictor> self(this);
    m_source->fetch(needed, firstHour, lastHour, *m_field, [self, generation](const QString &error) {
        if (!self || generation != self->m_generation)
            return;
        if (!error.isEmpty())
            self->fail(error);
        else
            self->pass();
    });
}

void Predictor::pass()
{
    ++m_current.passes;
    emit progress(tr("Calculating track (pass %1)").arg(m_current.passes));

    m_current.result = simulate(m_current.params, *m_field);
    const SimulationResult &result = m_current.result;

    if (result.missing.empty()) {
        if (!result.error.isEmpty()) {
            fail(result.error);
            return;
        }
        m_running = false;
        emit finished(m_current);
        return;
    }

    if (m_current.passes >= kMaxPasses) {
        fail(tr("could not load all wind data after %1 passes").arg(kMaxPasses));
        return;
    }

    // If everything still missing was already asked for, the source simply has no data
    // for it (typically a time beyond the forecast range).
    const auto unrequested = std::find_if(result.missing.begin(), result.missing.end(),
                                          [this](const NodeKey &key) { return !m_requested.contains(key); });
    if (unrequested == result.missing.end()) {
        const QDateTime when = QDateTime::fromSecsSinceEpoch(result.missing.begin()->hour * 3600, QTimeZone::UTC);
        fail(tr("no wind data available for %1 UTC (outside the forecast range?)")
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
    fetch({nodes.begin(), nodes.end()}, m_firstHour, m_lastHour);
}

void Predictor::fail(const QString &error)
{
    m_running = false;
    emit failed(error);
}

} // namespace sp
