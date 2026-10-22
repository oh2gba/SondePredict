// sp-cli: command-line front end to the Sonde Predict core.

#include "openmeteo.h"
#include "predictor.h"
#include "trackerlog.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QTextStream>
#include <QTimeZone>

#include <cstdio>
#include <memory>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

[[noreturn]] void die(const QString &message)
{
    QTextStream(stderr) << "sp-cli: " << message << Qt::endl;
    std::exit(1);
}

QString utc(double unixTime, const char *format = "yyyy-MM-dd HH:mm:ss")
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(unixTime), QTimeZone::UTC)
        .toString(QString::fromLatin1(format));
}

QString duration(double seconds)
{
    const int s = static_cast<int>(seconds + 0.5);
    return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QChar(u'0')).arg(s % 60, 2, 10, QChar(u'0'));
}

QString position(sp::GeoPoint p)
{
    return QStringLiteral("%1, %2").arg(p.lat, 0, 'f', 5).arg(p.lon, 0, 'f', 5);
}

// "lat,lon" or "lat,lon,alt"
bool parseLocation(const QString &text, sp::GeoPoint *point, std::optional<double> *altitude)
{
    const QStringList parts = text.split(u',');
    if (parts.size() < 2 || parts.size() > 3)
        return false;
    bool okLat, okLon;
    point->lat = parts[0].trimmed().toDouble(&okLat);
    point->lon = parts[1].trimmed().toDouble(&okLon);
    if (!okLat || !okLon || point->lat < -90 || point->lat > 90 || point->lon < -180 || point->lon > 180)
        return false;
    if (parts.size() == 3 && altitude) {
        bool okAlt;
        *altitude = parts[2].trimmed().toDouble(&okAlt);
        return okAlt;
    }
    return true;
}

double number(const QCommandLineParser &parser, const QString &name)
{
    bool ok;
    const double value = parser.value(name).toDouble(&ok);
    if (!ok)
        die(QStringLiteral("--%1 needs a number").arg(name));
    return value;
}

// Uses https_proxy / http_proxy / all_proxy (with credentials) when set, otherwise the
// operating system's proxy settings. The desktop app will get a proxy setting like v3 had.
void applyProxy()
{
    for (const char *name : {"https_proxy", "HTTPS_PROXY", "all_proxy", "ALL_PROXY", "http_proxy", "HTTP_PROXY"}) {
        const QUrl url(qEnvironmentVariable(name));
        if (!url.isValid() || url.host().isEmpty())
            continue;
        const bool socks = url.scheme().startsWith(QStringLiteral("socks"));
        QNetworkProxy proxy(socks ? QNetworkProxy::Socks5Proxy : QNetworkProxy::HttpProxy, url.host(),
                            static_cast<quint16>(url.port(socks ? 1080 : 8080)), url.userName(), url.password());
        QNetworkProxy::setApplicationProxy(proxy);
        return;
    }
    QNetworkProxyFactory::setUseSystemConfiguration(true);
}

bool writeCsv(const QString &path, const sp::SimulationResult &result)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream csv(&file);
    csv << "time_utc,latitude,longitude,altitude_m,wind_u_ms,wind_v_ms,phase\n";
    for (const sp::TrackPoint &p : result.track) {
        csv << utc(p.time, "yyyy-MM-ddTHH:mm:ssZ") << ',' << QString::number(p.position.lat, 'f', 6) << ','
            << QString::number(p.position.lon, 'f', 6) << ',' << QString::number(p.altitude, 'f', 1) << ','
            << QString::number(p.u, 'f', 2) << ',' << QString::number(p.v, 'f', 2) << ','
            << (p.phase == sp::FlightPhase::Ascent ? "ascent" : "descent") << '\n';
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sp-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SP_VERSION));

    const sp::FlightProfile defaults;
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Predict a radiosonde flight path and landing point using GFS winds."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {QStringLiteral("launch"), QStringLiteral("Launch site as lat,lon[,alt]. Altitude defaults to the ground elevation."), QStringLiteral("lat,lon[,alt]")},
        {QStringLiteral("time"), QStringLiteral("Launch time in UTC, e.g. 2026-10-23T11:15."), QStringLiteral("utc")},
        {QStringLiteral("ascent"), QStringLiteral("Ascent rate, m/s (default %1).").arg(defaults.ascentRate), QStringLiteral("m/s")},
        {QStringLiteral("burst"), QStringLiteral("Burst altitude, m (default %1).").arg(defaults.burstAltitude), QStringLiteral("m")},
        {QStringLiteral("descent"), QStringLiteral("Descent rate at sea level, m/s (default %1).").arg(defaults.seaLevelDescentRate), QStringLiteral("m/s")},
        {QStringLiteral("exponent"), QStringLiteral("Descent density exponent k (default %1).").arg(defaults.descentExponent), QStringLiteral("k")},
        {QStringLiteral("legacy"), QStringLiteral("Use the Sonde Predict v3 descent model: rate + altitude·gain/1000.")},
        {QStringLiteral("legacy-rate"), QStringLiteral("v3 descent rate, m/s (default %1).").arg(defaults.legacyDescentRate), QStringLiteral("m/s")},
        {QStringLiteral("legacy-gain"), QStringLiteral("v3 descent gain (default %1).").arg(defaults.legacyDescentGain), QStringLiteral("gain")},
        {QStringLiteral("home"), QStringLiteral("Your location, to report distance and bearing to the landing point."), QStringLiteral("lat,lon")},
        {QStringLiteral("grid"), QStringLiteral("Wind grid spacing in degrees (default 0.25)."), QStringLiteral("deg"), QStringLiteral("0.25")},
        {QStringLiteral("model"), QStringLiteral("Open-Meteo model (default gfs_global)."), QStringLiteral("name"), QStringLiteral("gfs_global")},
        {QStringLiteral("api-url"), QStringLiteral("Open-Meteo endpoint, e.g. a subscription or self-hosted server."), QStringLiteral("url")},
        {QStringLiteral("api-key"), QStringLiteral("Open-Meteo API key (subscription)."), QStringLiteral("key")},
        {QStringLiteral("csv"), QStringLiteral("Write the predicted track to a CSV file."), QStringLiteral("file")},
        {QStringLiteral("compare"), QStringLiteral("Compare with a received flight (RS41Tracker CSV)."), QStringLiteral("file")},
    });
    parser.process(app);

    if (!parser.isSet(QStringLiteral("launch")) || !parser.isSet(QStringLiteral("time")))
        die(QStringLiteral("--launch and --time are required (see --help)"));

    sp::LaunchParams params;
    if (!parseLocation(parser.value(QStringLiteral("launch")), &params.site, &params.siteAltitude))
        die(QStringLiteral("--launch must be lat,lon or lat,lon,alt"));

    QDateTime launch = QDateTime::fromString(parser.value(QStringLiteral("time")), Qt::ISODate);
    if (!launch.isValid())
        die(QStringLiteral("--time must look like 2026-10-23T11:15"));
    if (launch.timeSpec() == Qt::LocalTime)
        launch = QDateTime(launch.date(), launch.time(), QTimeZone::UTC);
    params.launchTime = launch.toSecsSinceEpoch();

    sp::FlightProfile &profile = params.profile;
    if (parser.isSet(QStringLiteral("ascent"))) profile.ascentRate = number(parser, QStringLiteral("ascent"));
    if (parser.isSet(QStringLiteral("burst"))) profile.burstAltitude = number(parser, QStringLiteral("burst"));
    if (parser.isSet(QStringLiteral("descent"))) profile.seaLevelDescentRate = number(parser, QStringLiteral("descent"));
    if (parser.isSet(QStringLiteral("exponent"))) profile.descentExponent = number(parser, QStringLiteral("exponent"));
    if (parser.isSet(QStringLiteral("legacy"))) profile.descentModel = sp::DescentModel::Legacy;
    if (parser.isSet(QStringLiteral("legacy-rate"))) profile.legacyDescentRate = number(parser, QStringLiteral("legacy-rate"));
    if (parser.isSet(QStringLiteral("legacy-gain"))) profile.legacyDescentGain = number(parser, QStringLiteral("legacy-gain"));
    if (const char *error = profile.validationError(); *error)
        die(QString::fromUtf8(error));

    std::optional<sp::GeoPoint> home;
    if (parser.isSet(QStringLiteral("home"))) {
        sp::GeoPoint p;
        if (!parseLocation(parser.value(QStringLiteral("home")), &p, nullptr))
            die(QStringLiteral("--home must be lat,lon"));
        home = p;
    }

    const double grid = number(parser, QStringLiteral("grid"));
    if (grid < 0.1 || grid > 2.0)
        die(QStringLiteral("--grid must be between 0.1 and 2 degrees"));

    sp::OpenMeteoOptions options;
    options.model = parser.value(QStringLiteral("model"));
    if (parser.isSet(QStringLiteral("api-url"))) options.baseUrl = parser.value(QStringLiteral("api-url"));
    if (parser.isSet(QStringLiteral("api-key"))) options.apiKey = parser.value(QStringLiteral("api-key"));

    applyProxy();
    QNetworkAccessManager network;
    sp::OpenMeteoClient client(&network, options);
    sp::Predictor predictor(&client, std::make_shared<sp::WindField>(grid));

    QObject::connect(&client, &sp::OpenMeteoClient::rateLimited, [](int seconds) {
        QTextStream(stderr) << "Open-Meteo rate limit reached, retrying in " << seconds << " s..." << Qt::endl;
    });
    QObject::connect(&predictor, &sp::Predictor::progress, [](const QString &message) {
        QTextStream(stderr) << message << "..." << Qt::endl;
    });
    QObject::connect(&predictor, &sp::Predictor::failed, [](const QString &error) { die(error); });
    QObject::connect(&predictor, &sp::Predictor::finished, &app, [&](const sp::Prediction &prediction) {
        const sp::SimulationResult &r = prediction.result;
        const sp::TrackPoint &start = r.track.front();
        const sp::TrackPoint &land = *r.landing;

        out() << "Launch   " << position(start.position) << "  " << qRound(start.altitude) << " m  "
              << utc(start.time, "yyyy-MM-dd HH:mm") << " UTC\n";
        if (r.burst) {
            out() << "Burst    " << position(r.burst->position) << "  " << qRound(r.burst->altitude) << " m  "
                  << utc(r.burst->time) << " UTC  (+" << duration(r.burst->time - start.time) << ")\n";
        }
        out() << "Landing  " << position(land.position) << "  " << qRound(land.altitude) << " m  "
              << utc(land.time) << " UTC  (+" << duration(land.time - start.time) << ")\n";

        const sp::Geodesic fromLaunch = sp::inverse(start.position, land.position);
        out() << "From launch site: " << QString::number(fromLaunch.distance / 1000.0, 'f', 1) << " km, bearing "
              << qRound(fromLaunch.initialBearing) << "°\n";
        if (home) {
            const sp::Geodesic fromHome = sp::inverse(*home, land.position);
            out() << "From your location: " << QString::number(fromHome.distance / 1000.0, 'f', 1)
                  << " km, bearing " << qRound(fromHome.initialBearing) << "°\n";
        }
        out() << "Wind data: " << prediction.nodesFetched << " grid points fetched, " << client.requestCount()
              << " requests, " << prediction.passes << " passes\n";
        if (r.aboveTopLevel)
            out() << "Note: the flight went above the highest wind level (10 hPa); the top wind was held constant.\n";

        if (parser.isSet(QStringLiteral("csv"))) {
            const QString path = parser.value(QStringLiteral("csv"));
            if (!writeCsv(path, r))
                die(QStringLiteral("cannot write %1").arg(path));
            out() << "Track written to " << path << '\n';
        }

        if (parser.isSet(QStringLiteral("compare"))) {
            const sp::TrackerLog log = sp::readTrackerCsv(parser.value(QStringLiteral("compare")));
            if (!log.error.isEmpty())
                die(log.error);
            const auto c = sp::compare(r, log);
            if (!c) {
                out() << "Comparison: the log has no descent, or the prediction never reaches its last altitude.\n";
            } else {
                out() << "Compared at " << qRound(c->reference.altitude) << " m on the way down (last logged point):\n"
                      << "  position error " << QString::number(c->horizontalError / 1000.0, 'f', 1) << " km, time error "
                      << QString::number(c->timeError / 60.0, 'f', 1) << " min\n";
                if (c->burstDistance) {
                    out() << "  burst: " << QString::number(*c->burstDistance / 1000.0, 'f', 1) << " km from the highest logged point, altitude "
                          << (*c->burstAltitudeError >= 0 ? "+" : "") << qRound(*c->burstAltitudeError) << " m\n";
                }
            }
        }
        out().flush();
        QCoreApplication::quit();
    });

    predictor.start(params);
    return app.exec();
}
