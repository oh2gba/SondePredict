#pragma once

#include "geo.h"

#include <QByteArray>
#include <QDate>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QNetworkAccessManager;

namespace sp {

// How a launch site's balloons have behaved lately, from the flights SondeHub's receivers
// heard (https://sondehub.org): launch, burst and last position of each.
struct RecentFlightStats {
    int flights = 0;
    double ascentRate = 0.0;    // m/s, median of each flight's average to burst
    double ascentSd = 0.0;      // m/s, robust: (90th − 10th percentile) / 2.56
    double burstAltitude = 0.0; // m, median
    double burstSd = 0.0;       // m, robust
    QDate first, last;          // dates of the flights used
};

namespace sondehub {

// One flight as SondeHub's launch-site summary lists it: first heard, highest, last heard.
struct FlightSummary {
    double firstTime = 0.0, firstAltitude = 0.0;
    GeoPoint firstPosition;
    double burstTime = 0.0, burstAltitude = 0.0;
};

// Keys ("launchsites/02963/2026/10/22/X4747665.json") from an S3 bucket listing.
std::vector<QString> parseListing(const QByteArray &xml);
// A summary file; nothing if it is not a flight that started at `site` (within 15 km, heard
// below 2 500 m) or has no plausible burst.
std::optional<FlightSummary> parseSummary(const QByteArray &json, GeoPoint site);
// Medians and robust spreads; nothing for fewer than 5 flights.
std::optional<RecentFlightStats> summarize(const std::vector<FlightSummary> &flights);

} // namespace sondehub

class SondeHubClient : public QObject {
    Q_OBJECT
public:
    explicit SondeHubClient(QNetworkAccessManager *network, QObject *parent = nullptr);

    using Done = std::function<void(const std::optional<RecentFlightStats> &stats, const QString &error)>;
    // Statistics of the flights from `station` (WMO number) in the last `days` days.
    void recentFlights(const QString &station, GeoPoint site, int days, Done done);

private:
    struct Job;
    void get(const QString &url, std::function<void(bool ok, const QByteArray &body)> then);
    void pump();

    QNetworkAccessManager *m_network;
    std::vector<std::function<void()>> m_queue;
    int m_active = 0;
};

} // namespace sp
