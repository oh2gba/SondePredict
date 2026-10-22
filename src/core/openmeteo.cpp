#include "openmeteo.h"

#include "atmosphere.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
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

int variableCount()
{
    return 2 + 3 * static_cast<int>(pressureLevels().size());
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

RateBudget::RateBudget(double perMinute, double perHour)
    : m_perMinute(perMinute)
    , m_perHour(perHour)
{
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
    for (qint64 at : candidates) {
        if (fits(calls, at)) {
            m_booked.emplace_back(at, calls);
            return at;
        }
    }
    m_booked.emplace_back(candidates.back(), calls); // unreachable: the last candidate empties the windows
    return candidates.back();
}

namespace {

// One budget for the whole program, so it survives the client being recreated.
RateBudget &freeApiBudget(const OpenMeteoOptions &options)
{
    static RateBudget budget(options.callsPerMinute, options.callsPerHour);
    return budget;
}

} // namespace

struct OpenMeteoClient::Batch {
    int pending = 0;
    QString error;
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
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, chunk = std::move(chunk), field, batch, attempt]() {
        reply->deleteLater();
        const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // The free tier allows 600 weighted calls per minute; wait it out and try again.
        if (status == 429 && attempt < m_options.rateLimitRetries) {
            const int seconds = m_options.rateLimitRetryDelayMs / 1000;
            emit rateLimited(seconds);
            QTimer::singleShot(m_options.rateLimitRetryDelayMs, this, [this, url, chunk, field, batch, attempt]() {
                send(url, chunk, field, batch, attempt + 1);
            });
            return;
        }

        QString error;
        if (reply->error() != QNetworkReply::NoError) {
            // Open-Meteo explains 4xx errors in a JSON body; prefer that over Qt's text.
            const QJsonObject json = QJsonDocument::fromJson(body).object();
            error = json.contains(QStringLiteral("reason"))
                ? QStringLiteral("Open-Meteo: %1").arg(json.value(QStringLiteral("reason")).toString())
                : QStringLiteral("could not reach Open-Meteo: %1").arg(reply->errorString());
        } else {
            error = openmeteo::parseInto(body, chunk, *field);
        }
        if (batch->error.isEmpty())
            batch->error = error;
        if (--batch->pending == 0)
            batch->done(batch->error);
    });
}

} // namespace sp
