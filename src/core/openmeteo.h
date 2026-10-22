#pragma once

#include "windsource.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>
#include <utility>
#include <vector>

class QNetworkAccessManager;

namespace sp {

// The free API allows 600 / 5 000 / 10 000 weighted calls per minute / hour / day for
// non-commercial use. One grid point with our 86 variables weighs about 8.6 calls, so a
// prediction typically costs 150–250. With a subscription, set baseUrl to
// https://customer-api.open-meteo.com/v1/gfs and apiKey; a self-hosted server works too.
struct OpenMeteoOptions {
    QString baseUrl = QStringLiteral("https://api.open-meteo.com/v1/gfs");
    QString model = QStringLiteral("gfs_global");
    QString apiKey;
    int maxNodesPerRequest = 50; // ≈ 430 calls, so one request always fits in a minute's budget
    int rateLimitRetries = 2;
    int rateLimitRetryDelayMs = 61000;
    // Requests are spread out to stay under the free API's limits (600 calls a minute and
    // 5,000 an hour), with some room for other programs. Not applied with an API key.
    double callsPerMinute = 550;
    double callsPerHour = 4800;
};

// Spreads requests over time so the calls in any minute and any hour stay within budget.
class RateBudget {
public:
    RateBudget(double perMinute, double perHour);

    // Books `calls` and returns when they may be sent (ms since the epoch): `nowMs`, or later
    // once enough earlier calls have left the windows. Bookings keep their order. A single
    // request bigger than a whole budget is let through once the windows are empty.
    qint64 reserve(double calls, qint64 nowMs);

private:
    bool fits(double calls, qint64 at) const;

    double m_perMinute;
    double m_perHour;
    std::vector<std::pair<qint64, double>> m_booked; // send time, calls; in time order
};

namespace openmeteo {

// Pressure levels requested from GFS, hPa, surface to top (1000 → 10 hPa).
const std::vector<int> &pressureLevels();

// Variables requested per grid point (10 m wind plus speed, direction and height per level).
int variableCount();

// How many calls Open-Meteo counts for a request: locations × max(1, variables / 10) ×
// max(1, days / 14), fractions included.
double requestWeight(int locations, int variables, double days);

QUrl buildUrl(const OpenMeteoOptions &options, const WindField &field,
              const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour);

// Parses an Open-Meteo response for `nodes` (same order as requested) into `field`.
// Returns an empty string on success, otherwise the reason it failed.
QString parseInto(const QByteArray &json, const std::vector<NodeIndex> &nodes, WindField &field);

} // namespace openmeteo

class OpenMeteoClient : public QObject, public WindSource {
    Q_OBJECT
public:
    explicit OpenMeteoClient(QNetworkAccessManager *network, OpenMeteoOptions options = {},
                             QObject *parent = nullptr);

    void fetch(const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour,
               std::shared_ptr<WindField> field, Done done) override;

    // Total number of HTTP requests made, for diagnostics.
    int requestCount() const { return m_requestCount; }

signals:
    // Open-Meteo's rate limit was hit; the request is retried after `seconds`.
    void rateLimited(int seconds);
    // A request waits `seconds` so as not to exceed the free API's limits.
    void paced(int seconds);

private:
    struct Batch;
    void send(const QUrl &url, std::vector<NodeIndex> chunk, std::shared_ptr<WindField> field, std::shared_ptr<Batch> batch,
              int attempt);

    QNetworkAccessManager *m_network;
    OpenMeteoOptions m_options;
    int m_requestCount = 0;
};

} // namespace sp
