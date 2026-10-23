#include "openmeteo.h"

#include "atmosphere.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLockFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <numbers>

namespace sp {

namespace openmeteo {

namespace {

constexpr double kDeg = std::numbers::pi / 180.0;

QString coordinate(double value)
{
    return QString::number(value, 'f', 4);
}

LevelWind toLevel(double altitude, double speed, double directionFrom)
{
    // Meteorological direction is where the wind blows from.
    return {altitude, -speed * std::sin(directionFrom * kDeg), -speed * std::cos(directionFrom * kDeg)};
}

bool valueAt(const QJsonObject &hourly, const QString &name, qsizetype i, double *out)
{
    const QJsonValue v = hourly.value(name).toArray().at(i);
    if (!v.isDouble())
        return false;
    *out = v.toDouble();
    return true;
}

} // namespace

const std::vector<int> &pressureLevels()
{
    static const std::vector<int> levels{1000, 975, 950, 925, 900, 850, 800, 750, 700, 650,
                                         600, 550, 500, 450, 400, 350, 300, 250, 200, 150,
                                         100, 70, 50, 40, 30, 20, 15, 10};
    return levels;
}

const std::vector<ModelInfo> &models()
{
    // All reach 10–15 hPa (about 30 km) and are global. Order: how well their forecasts made
    // a day ahead matched the FMI Jokioinen soundings of October 2026 (see tools/validation).
    static const std::vector<ModelInfo> list{
        {QStringLiteral("ecmwf_ifs025"), QStringLiteral("ECMWF IFS (Europe)")},
        {QStringLiteral("gem_global"), QStringLiteral("GEM (Environment Canada)")},
        {QStringLiteral("ukmo_global_deterministic_10km"), QStringLiteral("UK Met Office global")},
        {QStringLiteral("gfs_global"), QStringLiteral("GFS (NOAA, USA)")},
        {QStringLiteral("meteofrance_arpege_world"), QStringLiteral("ARPEGE (Météo-France)")},
    };
    return list;
}

QString modelName(const QString &id)
{
    for (const ModelInfo &m : models()) {
        if (m.id == id)
            return m.name;
    }
    return id;
}

int variableCount()
{
    return 2 + 3 * static_cast<int>(pressureLevels().size());
}

QString describeWait(int seconds)
{
    if (seconds <= 90)
        return QStringLiteral("%1 s").arg(seconds);
    // Only the hourly budget makes waits this long.
    return QStringLiteral("%1 min (this hour's free calls are nearly used up; earlier predictions and batches "
                          "from this connection count too)").arg((seconds + 59) / 60);
}

double requestWeight(int locations, int variables, double days)
{
    return locations * std::max(1.0, variables / 10.0) * std::max(1.0, days / 14.0);
}

QUrl buildUrl(const OpenMeteoOptions &options, const WindField &field,
              const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour)
{
    QStringList lats, lons;
    for (const NodeIndex &n : nodes) {
        const GeoPoint p = field.position(n);
        lats << coordinate(p.lat);
        lons << coordinate(p.lon);
    }

    QStringList variables{QStringLiteral("wind_speed_10m"), QStringLiteral("wind_direction_10m")};
    for (int level : pressureLevels()) {
        variables << QStringLiteral("wind_speed_%1hPa").arg(level)
                  << QStringLiteral("wind_direction_%1hPa").arg(level)
                  << QStringLiteral("geopotential_height_%1hPa").arg(level);
    }

    const auto isoHour = [](qint64 hour) {
        return QDateTime::fromSecsSinceEpoch(hour * 3600, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-ddTHH:mm"));
    };

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("latitude"), lats.join(u','));
    query.addQueryItem(QStringLiteral("longitude"), lons.join(u','));
    query.addQueryItem(QStringLiteral("models"), options.model);
    query.addQueryItem(QStringLiteral("hourly"), variables.join(u','));
    query.addQueryItem(QStringLiteral("wind_speed_unit"), QStringLiteral("ms"));
    query.addQueryItem(QStringLiteral("timeformat"), QStringLiteral("unixtime"));
    query.addQueryItem(QStringLiteral("timezone"), QStringLiteral("GMT"));
    query.addQueryItem(QStringLiteral("start_hour"), isoHour(firstHour));
    query.addQueryItem(QStringLiteral("end_hour"), isoHour(lastHour));
    if (!options.apiKey.isEmpty())
        query.addQueryItem(QStringLiteral("apikey"), options.apiKey);

    QUrl url(options.baseUrl);
    url.setQuery(query);
    return url;
}

QString parseInto(const QByteArray &json, const std::vector<NodeIndex> &nodes, WindField &field)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (doc.isNull())
        return QStringLiteral("invalid JSON from Open-Meteo: %1").arg(parseError.errorString());

    if (doc.isObject() && doc.object().value(QStringLiteral("error")).toBool())
        return QStringLiteral("Open-Meteo: %1").arg(doc.object().value(QStringLiteral("reason")).toString());

    QJsonArray locations;
    if (doc.isArray())
        locations = doc.array();
    else
        locations.append(doc.object());

    if (static_cast<std::size_t>(locations.size()) != nodes.size())
        return QStringLiteral("Open-Meteo returned %1 locations, expected %2").arg(locations.size()).arg(nodes.size());

    for (qsizetype n = 0; n < locations.size(); ++n) {
        const QJsonObject location = locations.at(n).toObject();
        const double elevation = location.value(QStringLiteral("elevation")).toDouble();
        const QJsonObject hourly = location.value(QStringLiteral("hourly")).toObject();
        const QJsonArray times = hourly.value(QStringLiteral("time")).toArray();
        if (times.isEmpty())
            return QStringLiteral("Open-Meteo response has no hourly data");

        for (qsizetype i = 0; i < times.size(); ++i) {
            std::vector<LevelWind> levels;
            double speed, direction, height;

            if (valueAt(hourly, QStringLiteral("wind_speed_10m"), i, &speed)
                && valueAt(hourly, QStringLiteral("wind_direction_10m"), i, &direction)) {
                levels.push_back(toLevel(elevation + 10.0, speed, direction));
            }
            for (int level : pressureLevels()) {
                if (!valueAt(hourly, QStringLiteral("wind_speed_%1hPa").arg(level), i, &speed)
                    || !valueAt(hourly, QStringLiteral("wind_direction_%1hPa").arg(level), i, &direction)
                    || !valueAt(hourly, QStringLiteral("geopotential_height_%1hPa").arg(level), i, &height)) {
                    continue;
                }
                const double altitude = atmosphere::geometricFromGeopotential(height);
                // Pressure levels below the terrain are extrapolated by the model; skip them.
                if (altitude <= elevation + 10.0)
                    continue;
                levels.push_back(toLevel(altitude, speed, direction));
            }
            if (levels.empty())
                continue;

            const qint64 hour = static_cast<qint64>(times.at(i).toDouble()) / 3600;
            field.insert({nodes[static_cast<std::size_t>(n)], hour}, elevation, std::move(levels));
        }
    }
    return {};
}

} // namespace openmeteo

RateBudget::RateBudget(double perMinute, double perHour, QString logFile)
    : m_perMinute(perMinute)
    , m_perHour(perHour)
    , m_logFile(std::move(logFile))
{
}

QString RateBudget::defaultLogFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
         + QStringLiteral("/SondePredict/openmeteo-usage.log");
}

void RateBudget::loadLog(qint64 nowMs)
{
    QFile file(m_logFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    m_booked.clear();
    while (!file.atEnd()) {
        const QList<QByteArray> parts = file.readLine().trimmed().split(' ');
        bool okTime = false, okCalls = false;
        if (parts.size() != 2)
            continue;
        const qint64 at = parts[0].toLongLong(&okTime);
        const double calls = parts[1].toDouble(&okCalls);
        if (okTime && okCalls && at > nowMs - 3600 * 1000)
            m_booked.emplace_back(at, calls);
    }
    std::sort(m_booked.begin(), m_booked.end());
}

void RateBudget::appendLog(qint64 at, double calls)
{
    // Rewrite the file now and then so it only holds the last hour.
    const bool compact = QFileInfo(m_logFile).size() > 64 * 1024;
    QFile file(m_logFile);
    if (!file.open(compact ? (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)
                           : (QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)))
        return;
    QTextStream out(&file);
    if (compact) {
        for (const auto &[time, booked] : m_booked) {
            if (time != at || booked != calls)
                out << time << ' ' << booked << '\n';
        }
    }
    out << at << ' ' << calls << '\n';
}

bool RateBudget::fits(double calls, qint64 at) const
{
    double minute = 0, hour = 0;
    for (const auto &[time, booked] : m_booked) {
        if (time > at - 3600 * 1000)
            hour += booked;
        if (time > at - 60 * 1000)
            minute += booked;
    }
    return (minute == 0 || minute + calls <= m_perMinute) && (hour == 0 || hour + calls <= m_perHour);
}

qint64 RateBudget::reserve(double calls, qint64 nowMs)
{
    // With a shared log: hold its lock, read what everyone booked, book, append.
    std::unique_ptr<QLockFile> lock;
    if (!m_logFile.isEmpty()) {
        QDir().mkpath(QFileInfo(m_logFile).absolutePath());
        lock = std::make_unique<QLockFile>(m_logFile + QStringLiteral(".lock"));
        lock->setStaleLockTime(10000);
        lock->tryLock(2000); // carry on without it rather than hang
        loadLog(nowMs);
    }
    std::erase_if(m_booked, [nowMs](const auto &b) { return b.first <= nowMs - 3600 * 1000; });

    // Send no earlier than the last booking, then at the first moment the windows allow:
    // either right away or when some earlier booking leaves the minute or hour window.
    const qint64 base = m_booked.empty() ? nowMs : std::max(nowMs, m_booked.back().first);
    std::vector<qint64> candidates{base};
    for (const auto &b : m_booked) {
        for (qint64 window : {qint64(60 * 1000), qint64(3600 * 1000)}) {
            if (b.first + window > base)
                candidates.push_back(b.first + window);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    qint64 at = candidates.back(); // the last candidate always empties the windows
    for (qint64 candidate : candidates) {
        if (fits(calls, candidate)) {
            at = candidate;
            break;
        }
    }
    m_booked.emplace_back(at, calls);
    if (!m_logFile.isEmpty())
        appendLog(at, calls);
    return at;
}

namespace {

// One budget for the whole program, so it survives the client being recreated, and shared
// with earlier runs and other Sonde Predict programs through the usage log.
RateBudget &freeApiBudget(const OpenMeteoOptions &options)
{
    static RateBudget budget(options.callsPerMinute, options.callsPerHour, RateBudget::defaultLogFile());
    return budget;
}

} // namespace

struct OpenMeteoClient::Batch {
    int pending = 0;
    FetchResult result;
    Done done;
};

OpenMeteoClient::OpenMeteoClient(QNetworkAccessManager *network, OpenMeteoOptions options, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_options(std::move(options))
{
}

void OpenMeteoClient::fetch(const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour,
                            std::shared_ptr<WindField> field, Done done)
{
    if (nodes.empty()) {
        done({});
        return;
    }

    auto batch = std::make_shared<Batch>();
    batch->done = std::move(done);

    const std::size_t chunkSize = static_cast<std::size_t>(std::max(1, m_options.maxNodesPerRequest));
    const double totalCalls = openmeteo::requestWeight(int(nodes.size()), openmeteo::variableCount(), double(lastHour - firstHour + 1) / 24.0);
    log(QStringLiteral("Open-Meteo %1: %2 grid points × %3 hours in %4 request(s), about %5 of the free calls")
            .arg(openmeteo::modelName(m_options.model)).arg(nodes.size()).arg(lastHour - firstHour + 1)
            .arg((nodes.size() + chunkSize - 1) / chunkSize).arg(qRound(totalCalls)));
    for (std::size_t start = 0; start < nodes.size(); start += chunkSize) {
        std::vector<NodeIndex> chunk(nodes.begin() + static_cast<std::ptrdiff_t>(start),
                                     nodes.begin() + static_cast<std::ptrdiff_t>(std::min(nodes.size(), start + chunkSize)));
        const QUrl url = openmeteo::buildUrl(m_options, *field, chunk, firstHour, lastHour);
        ++batch->pending;

        if (!m_options.apiKey.isEmpty() || m_options.callsPerMinute <= 0) {
            send(url, std::move(chunk), field, batch, 0);
            continue;
        }
        const double days = double(lastHour - firstHour + 1) / 24.0;
        const double calls = openmeteo::requestWeight(int(chunk.size()), openmeteo::variableCount(), days);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const qint64 delay = freeApiBudget(m_options).reserve(calls, now) - now;
        if (delay <= 0) {
            send(url, std::move(chunk), field, batch, 0);
            continue;
        }
        emit paced(int((delay + 999) / 1000));
        log(QStringLiteral("Open-Meteo: waiting %1 to stay within the free limit").arg(openmeteo::describeWait(int((delay + 999) / 1000))));
        QTimer::singleShot(std::chrono::milliseconds(delay), this, [this, url, chunk = std::move(chunk), field, batch]() mutable {
            send(url, std::move(chunk), field, batch, 0);
        });
    }
}

void OpenMeteoClient::send(const QUrl &url, std::vector<NodeIndex> chunk, std::shared_ptr<WindField> field,
                           std::shared_ptr<Batch> batch, int attempt)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("SondePredict/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(30000);

    ++m_requestCount;
    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, chunk = std::move(chunk), field, batch, attempt, started]() {
        reply->deleteLater();
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const double seconds = double(QDateTime::currentMSecsSinceEpoch() - started) / 1000.0;

        const QJsonObject json = QJsonDocument::fromJson(body).object();
        const QString reason = json.value(QStringLiteral("reason")).toString();

        // Hourly or daily limit: waiting a minute won't help, so tell the caller to stop.
        if (status == 429 && (reason.contains(QLatin1String("hourly"), Qt::CaseInsensitive)
                              || reason.contains(QLatin1String("daily"), Qt::CaseInsensitive))) {
            const bool daily = reason.contains(QLatin1String("daily"), Qt::CaseInsensitive);
            if (batch->result.error.isEmpty() || !batch->result.limitReached) {
                batch->result.limitReached = true;
                batch->result.error = daily
                    ? QStringLiteral("Open-Meteo's daily limit for this internet connection is used up; try again tomorrow")
                    : QStringLiteral("Open-Meteo's hourly limit for this internet connection is used up; try again within the hour");
            }
            log(QStringLiteral("Open-Meteo: %1 limit used up (%2)").arg(daily ? QStringLiteral("daily") : QStringLiteral("hourly"), reason));
            if (--batch->pending == 0)
                batch->done(batch->result);
            return;
        }

        // The per-minute limit (600 weighted calls); wait it out and try again.
        if (status == 429 && attempt < m_options.rateLimitRetries) {
            const int seconds = m_options.rateLimitRetryDelayMs / 1000;
            emit rateLimited(seconds);
            log(QStringLiteral("Open-Meteo: per-minute limit reached, retrying in %1 s").arg(seconds));
            QTimer::singleShot(m_options.rateLimitRetryDelayMs, this, [this, url, chunk, field, batch, attempt]() {
                send(url, chunk, field, batch, attempt + 1);
            });
            return;
        }

        QString error;
        if (reply->error() != QNetworkReply::NoError) {
            // Open-Meteo explains 4xx errors in a JSON body; prefer that over Qt's text.
            error = json.contains(QStringLiteral("reason"))
                ? QStringLiteral("Open-Meteo: %1").arg(reason)
                : QStringLiteral("could not reach Open-Meteo: %1").arg(reply->errorString());
        } else {
            error = openmeteo::parseInto(body, chunk, *field);
        }
        if (error.isEmpty())
            log(QStringLiteral("Open-Meteo: %1 grid points loaded (%2 kB in %3 s)")
                    .arg(chunk.size()).arg(body.size() / 1024).arg(seconds, 0, 'f', 1));
        else
            log(QStringLiteral("Open-Meteo: request failed: %1").arg(error));
        if (batch->result.error.isEmpty())
            batch->result.error = error;
        if (--batch->pending == 0)
            batch->done(batch->result);
    });
}

} // namespace sp
