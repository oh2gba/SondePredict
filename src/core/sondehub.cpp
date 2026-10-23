#include "sondehub.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <map>
#include <memory>

namespace sp {

namespace {

const QString kBucket = QStringLiteral("https://sondehub-history.s3.amazonaws.com");

double percentile(std::vector<double> xs, double q)
{
    std::sort(xs.begin(), xs.end());
    const double pos = q * double(xs.size() - 1);
    const std::size_t i = std::size_t(pos);
    const double f = pos - double(i);
    return i + 1 < xs.size() ? xs[i] * (1.0 - f) + xs[i + 1] * f : xs.back();
}

} // namespace

namespace sondehub {

std::vector<QString> parseListing(const QByteArray &xml)
{
    static const QRegularExpression key(QStringLiteral("<Key>([^<]+)</Key>"));
    std::vector<QString> keys;
    auto it = key.globalMatch(QString::fromUtf8(xml));
    while (it.hasNext())
        keys.push_back(it.next().captured(1));
    return keys;
}

std::optional<FlightSummary> parseSummary(const QByteArray &json, GeoPoint site)
{
    const QJsonArray points = QJsonDocument::fromJson(json).array();
    if (points.size() < 3)
        return std::nullopt;
    const auto time = [](const QJsonObject &p) {
        return double(QDateTime::fromString(p.value(QStringLiteral("datetime")).toString(), Qt::ISODateWithMs).toMSecsSinceEpoch()) / 1000.0;
    };
    const QJsonObject first = points.first().toObject();
    QJsonObject top = first;
    for (const QJsonValue &v : points) {
        if (v.toObject().value(QStringLiteral("alt")).toDouble() > top.value(QStringLiteral("alt")).toDouble())
            top = v.toObject();
    }
    FlightSummary s;
    s.firstTime = time(first);
    s.firstAltitude = first.value(QStringLiteral("alt")).toDouble();
    s.firstPosition = {first.value(QStringLiteral("lat")).toDouble(), first.value(QStringLiteral("lon")).toDouble()};
    s.burstTime = time(top);
    s.burstAltitude = top.value(QStringLiteral("alt")).toDouble();
    // A flight from this site, heard from low down, with a plausible burst after a plausible climb.
    if (s.firstAltitude > 2500.0 || inverse(site, s.firstPosition).distance > 15000.0)
        return std::nullopt;
    if (!(s.burstAltitude > 15000.0 && s.burstAltitude < 45000.0) || s.burstTime <= s.firstTime)
        return std::nullopt;
    const double rate = (s.burstAltitude - s.firstAltitude) / (s.burstTime - s.firstTime);
    if (!(rate > 1.0 && rate < 15.0))
        return std::nullopt;
    return s;
}

std::optional<RecentFlightStats> summarize(const std::vector<FlightSummary> &flights)
{
    if (flights.size() < 5)
        return std::nullopt;
    std::vector<double> rates, bursts;
    RecentFlightStats out;
    double firstTime = flights.front().firstTime, lastTime = firstTime;
    for (const FlightSummary &f : flights) {
        rates.push_back((f.burstAltitude - f.firstAltitude) / (f.burstTime - f.firstTime));
        bursts.push_back(f.burstAltitude);
        firstTime = std::min(firstTime, f.firstTime);
        lastTime = std::max(lastTime, f.firstTime);
    }
    out.flights = int(flights.size());
    out.ascentRate = percentile(rates, 0.5);
    out.ascentSd = (percentile(rates, 0.9) - percentile(rates, 0.1)) / 2.563;
    out.burstAltitude = percentile(bursts, 0.5);
    out.burstSd = (percentile(bursts, 0.9) - percentile(bursts, 0.1)) / 2.563;
    out.first = QDateTime::fromSecsSinceEpoch(qint64(firstTime), QTimeZone::UTC).date();
    out.last = QDateTime::fromSecsSinceEpoch(qint64(lastTime), QTimeZone::UTC).date();
    return out;
}

} // namespace sondehub

struct SondeHubClient::Job {
    GeoPoint site;
    Done done;
    int pending = 0;
    bool failed = false;
    std::map<QString, sondehub::FlightSummary> flights; // by serial (a flight can be listed on two days)
};

SondeHubClient::SondeHubClient(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

void SondeHubClient::recentFlights(const QString &station, GeoPoint site, int days, Done done)
{
    auto job = std::make_shared<Job>();
    job->site = site;
    job->done = std::move(done);
    const auto finishOne = [job]() {
        if (--job->pending > 0)
            return;
        std::vector<sondehub::FlightSummary> flights;
        for (const auto &[serial, f] : job->flights)
            flights.push_back(f);
        const auto stats = sondehub::summarize(flights);
        job->done(stats, stats ? QString()
                       : job->failed ? QStringLiteral("could not reach SondeHub")
                                     : QStringLiteral("SondeHub has too few recent flights from this site"));
    };

    const QDate today = QDateTime::currentDateTimeUtc().date();
    job->pending = days;
    for (int d = 0; d < days; ++d) {
        const QDate day = today.addDays(-d);
        const QString prefix = QStringLiteral("launchsites/%1/%2/").arg(station, day.toString(QStringLiteral("yyyy/MM/dd")));
        get(kBucket + QStringLiteral("/?prefix=") + prefix, [this, job, finishOne](bool ok, const QByteArray &body) {
            if (!ok) {
                job->failed = true;
                finishOne();
                return;
            }
            for (const QString &key : sondehub::parseListing(body)) {
                ++job->pending;
                const QString serial = key.section(u'/', -1).chopped(5);
                get(kBucket + u'/' + key, [job, finishOne, serial](bool ok, const QByteArray &summary) {
                    if (ok) {
                        if (const auto f = sondehub::parseSummary(summary, job->site))
                            job->flights.emplace(serial, *f);
                    }
                    finishOne();
                });
            }
            finishOne();
        });
    }
}

void SondeHubClient::get(const QString &url, std::function<void(bool, const QByteArray &)> then)
{
    m_queue.push_back([this, url, then = std::move(then)]() {
        QNetworkRequest request{QUrl(url)};
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("SondePredict/%1 github.com/oh2gba/SondePredict").arg(QCoreApplication::applicationVersion()));
        request.setTransferTimeout(20000);
        ++m_active;
        QNetworkReply *reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, then]() {
            reply->deleteLater();
            --m_active;
            const bool ok = reply->error() == QNetworkReply::NoError;
            then(ok, ok ? reply->readAll() : QByteArray());
            pump();
        });
    });
    pump();
}

void SondeHubClient::pump()
{
    while (m_active < 8 && !m_queue.empty()) {
        auto next = std::move(m_queue.front());
        m_queue.erase(m_queue.begin());
        next();
    }
}

} // namespace sp
