#pragma once

#include "windsource.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <memory>

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
    int maxNodesPerRequest = 100; // keeps the URL a sensible length
    int rateLimitRetries = 2;
    int rateLimitRetryDelayMs = 61000;
};

namespace openmeteo {

// Pressure levels requested from GFS, hPa, surface to top (1000 → 10 hPa).
const std::vector<int> &pressureLevels();

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
               WindField &field, Done done) override;

    // Total number of HTTP requests made, for diagnostics.
    int requestCount() const { return m_requestCount; }

signals:
    // Open-Meteo's rate limit was hit; the request is retried after `seconds`.
    void rateLimited(int seconds);

private:
    struct Batch;
    void send(const QUrl &url, std::vector<NodeIndex> chunk, WindField *field, std::shared_ptr<Batch> batch,
              int attempt);

    QNetworkAccessManager *m_network;
    OpenMeteoOptions m_options;
    int m_requestCount = 0;
};

} // namespace sp
