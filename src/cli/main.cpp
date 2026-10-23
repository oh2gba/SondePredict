// sp-cli: command-line front end to the Sonde Predict core.

#include "batch.h"
#include "meps.h"
#include "network.h"
#include "openmeteo.h"
#include "predictionstore.h"
#include "predictor.h"
#include "settings.h"
#include "trackexport.h"
#include "trackerlog.h"
#include "v3import.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QTextStream>
#include <QTimeZone>

#include <cstdio>
#include <cstdlib>
#include <functional>
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

struct ResolvedLocation {
    QString name; // empty for plain coordinates
    sp::GeoPoint point;
    std::optional<double> altitude;
};

// A saved location's name, or "lat,lon[,alt]".
ResolvedLocation resolveLocation(const QString &text, const sp::AppSettings &settings, const char *option)
{
    ResolvedLocation r;
    if (const sp::Location *l = settings.location(text.trimmed())) {
        r.name = l->name;
        r.point = l->position;
        r.altitude = l->altitude;
        return r;
    }
    if (!parseLocation(text, &r.point, &r.altitude))
        die(QStringLiteral("--%1: \"%2\" is neither a saved location nor lat,lon[,alt]").arg(QLatin1String(option), text));
    return r;
}

double number(const QCommandLineParser &parser, const QString &name)
{
    bool ok;
    const double value = parser.value(name).toDouble(&ok);
    if (!ok)
        die(QStringLiteral("--%1 needs a number").arg(name));
    return value;
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


namespace {

void printPrediction(const sp::Prediction &prediction, const std::optional<sp::GeoPoint> &home, int requests)
{
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
    if (const auto &area = prediction.landingArea) {
        out() << "Landing area (95%): " << QString::number(area->majorAxis() / 1000.0, 'f', 1) << " x "
              << QString::number(area->minorAxis() / 1000.0, 'f', 1) << " km, long axis " << qRound(area->majorBearing)
              << "°, from " << area->samples << " varied flights\n";
    }
    out() << "Wind data: " << sp::meps::modelName(prediction.model)
          << ", " << prediction.nodesFetched << " grid points fetched, " << requests
          << " requests, " << prediction.passes << " passes\n";
    if (!prediction.fallbackNote.isEmpty())
        out() << "Fell back from " << prediction.fallbackNote << '\n';
    if (r.aboveTopLevel)
        out() << "Note: the flight went above the highest wind level (10 hPa); the top wind was held constant.\n";
}

void printComparison(const sp::SimulationResult &r, const QString &logPath)
{
    const sp::TrackerLog log = sp::readTrackerCsv(logPath);
    if (!log.error.isEmpty())
        die(log.error);
    const auto c = sp::compare(r, log);
    if (!c) {
        out() << "Comparison: the log has no descent, or the prediction never reaches its last altitude.\n";
        return;
    }
    out() << "Compared at " << qRound(c->reference.altitude) << " m on the way down (last logged point):\n"
          << "  position error " << QString::number(c->horizontalError / 1000.0, 'f', 1) << " km, time error "
          << QString::number(c->timeError / 60.0, 'f', 1) << " min\n";
    if (c->burstDistance) {
        out() << "  burst: " << QString::number(*c->burstDistance / 1000.0, 'f', 1)
              << " km from the highest logged point, altitude " << (*c->burstAltitudeError >= 0 ? "+" : "")
              << qRound(*c->burstAltitudeError) << " m\n";
    }
}

int listLocations(const sp::AppSettings &settings)
{
    if (settings.locations.isEmpty())
        out() << "No saved locations. Import them from v3 with --import-v3 config.ini.\n";
    for (const sp::Location &l : settings.locations) {
        out() << qSetFieldWidth(24) << Qt::left << l.name << qSetFieldWidth(0) << position(l.position) << "  "
              << qRound(l.altitude) << " m  (highlight within " << l.warningDistanceKm << " km)\n";
    }
    return 0;
}

int listPredictions(const QString &dbPath)
{
    sp::PredictionStore store;
    if (!store.open(dbPath))
        die(store.lastError());
    const auto predictions = store.list();
    if (predictions.empty())
        out() << "No saved predictions in " << dbPath << ".\n";
    for (const sp::PredictionSummary &p : predictions) {
        out() << qSetFieldWidth(6) << Qt::right << p.id << qSetFieldWidth(0) << "  " << utc(double(p.launchTime), "yyyy-MM-dd HH:mm")
              << "  " << p.siteName << " → " << position(p.landing);
        if (p.distanceFromHome)
            out() << "  " << QString::number(*p.distanceFromHome / 1000.0, 'f', 1) << " km from " << p.homeName;
        out() << (p.manual ? "" : "  [batch]") << '\n';
    }
    return 0;
}

int importV3(const QString &v3Path, const QString &configPath)
{
    QString error;
    sp::AppSettings settings = sp::AppSettings::load(configPath, &error);
    if (!error.isEmpty())
        die(error);
    const sp::V3ImportResult result = sp::importV3Config(v3Path, settings);
    if (!result.error.isEmpty())
        die(result.error);
    if (!settings.save(configPath, &error))
        die(error);
    for (const QString &message : result.messages)
        out() << message << '\n';
    out() << "Settings saved to " << configPath << '\n';
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sp-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Predict a radiosonde flight path and landing point using GFS winds.\n"
        "Balloon, wind and network settings come from the settings file unless given here."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
        {QStringLiteral("launch"), QStringLiteral("Launch site: a saved location name or lat,lon[,alt]. Without altitude the ground elevation is used. Defaults to the last manual-run site."), QStringLiteral("site")},
        {QStringLiteral("home"), QStringLiteral("Your location: a saved location name or lat,lon. Defaults to the last manual-run location."), QStringLiteral("location")},
        {QStringLiteral("time"), QStringLiteral("Launch time in UTC, e.g. 2026-10-23T11:15."), QStringLiteral("utc")},
        {QStringLiteral("ascent"), QStringLiteral("Ascent rate, m/s."), QStringLiteral("m/s")},
        {QStringLiteral("burst"), QStringLiteral("Burst altitude, m."), QStringLiteral("m")},
        {QStringLiteral("descent"), QStringLiteral("Descent rate at sea level, m/s (density model)."), QStringLiteral("m/s")},
        {QStringLiteral("exponent"), QStringLiteral("Descent density exponent k (density model)."), QStringLiteral("k")},
        {QStringLiteral("legacy"), QStringLiteral("Use the Sonde Predict v3 descent model: rate + altitude·gain/1000.")},
        {QStringLiteral("constant-ascent"), QStringLiteral("Climb at the same speed all the way (instead of the measured shape).")},
        {QStringLiteral("density"), QStringLiteral("Use the density-scaled descent model.")},
        {QStringLiteral("legacy-rate"), QStringLiteral("v3 descent rate, m/s."), QStringLiteral("m/s")},
        {QStringLiteral("legacy-gain"), QStringLiteral("v3 descent gain."), QStringLiteral("gain")},
        {QStringLiteral("grid"), QStringLiteral("Wind grid spacing in degrees."), QStringLiteral("deg")},
        {QStringLiteral("model"), QStringLiteral("Global model via Open-Meteo, e.g. ecmwf_ifs025 or gfs_global."), QStringLiteral("name")},
        {QStringLiteral("meps"), QStringLiteral("Use MET Norway's MEPS (2.5 km) inside its area, the global model elsewhere.")},
        {QStringLiteral("no-meps"), QStringLiteral("Use only the global model.")},
        {QStringLiteral("meps-only"), QStringLiteral("Inside the MEPS area, use MEPS alone instead of averaging it with the global model.")},
        {QStringLiteral("api-url"), QStringLiteral("Open-Meteo endpoint, e.g. a subscription or self-hosted server."), QStringLiteral("url")},
        {QStringLiteral("api-key"), QStringLiteral("Open-Meteo API key (subscription)."), QStringLiteral("key")},
        {QStringLiteral("csv"), QStringLiteral("Write the predicted track to a CSV file."), QStringLiteral("file")},
        {QStringLiteral("compare"), QStringLiteral("Compare with a received flight (RS41Tracker CSV)."), QStringLiteral("file")},
        {QStringLiteral("gpx"), QStringLiteral("Write the prediction(s) as GPX for GPS and phone apps."), QStringLiteral("file")},
        {QStringLiteral("save"), QStringLiteral("Store the prediction(s) in the prediction database.")},
        {QStringLiteral("batch"), QStringLiteral("Run the batch set up in the settings (days × launch hours).")},
        {QStringLiteral("no-area"), QStringLiteral("Skip the landing-area estimate.")},
        {QStringLiteral("wind-sigma"), QStringLiteral("Validation: forecast wind error for the landing area, m/s (default: from the lead time)."), QStringLiteral("m/s")},
        {QStringLiteral("verbose"), QStringLiteral("Show what happens: wind model and run, downloads, waits, each pass.")},
        {QStringLiteral("export"), QStringLiteral("Export stored predictions (ids from --list) with --gpx and exit."), QStringLiteral("ids")},
        {QStringLiteral("replay"), QStringLiteral("Validation: run the predictions listed in a CSV file (id,launch UTC,lat,lon,alt,ascent,burst,descent,exponent,shaped[,wind sigma]) "
                                                  "sharing one wind cache, and print id,burst lat,lon,time,landing lat,lon,time,model[,area lat,lon,major σ,minor σ,bearing] per line."), QStringLiteral("file")},
        {QStringLiteral("list"), QStringLiteral("List stored predictions and exit.")},
        {QStringLiteral("locations"), QStringLiteral("List saved locations and exit.")},
        {QStringLiteral("import-v3"), QStringLiteral("Import a Sonde Predict v3 config.ini into the settings and exit."), QStringLiteral("config.ini")},
        {QStringLiteral("config"), QStringLiteral("Settings file (default %1).").arg(sp::AppSettings::defaultPath()), QStringLiteral("file")},
        {QStringLiteral("db"), QStringLiteral("Prediction database (default %1).").arg(sp::PredictionStore::defaultPath()), QStringLiteral("file")},
    });
    parser.process(app);

    const QString configPath = parser.isSet(QStringLiteral("config")) ? parser.value(QStringLiteral("config")) : sp::AppSettings::defaultPath();
    const QString dbPath = parser.isSet(QStringLiteral("db")) ? parser.value(QStringLiteral("db")) : sp::PredictionStore::defaultPath();

    if (parser.isSet(QStringLiteral("import-v3")))
        return importV3(parser.value(QStringLiteral("import-v3")), configPath);
    if (parser.isSet(QStringLiteral("list")))
        return listPredictions(dbPath);

    QString settingsError;
    const sp::AppSettings settings = sp::AppSettings::load(configPath, &settingsError);
    if (!settingsError.isEmpty())
        die(settingsError);
    if (parser.isSet(QStringLiteral("locations")))
        return listLocations(settings);

    // Stored predictions to GPX, no network needed.
    sp::PredictionStore store;
    const bool needStore = parser.isSet(QStringLiteral("save")) || parser.isSet(QStringLiteral("export"));
    if (needStore && !store.open(dbPath)) // before spending API quota on results that cannot be saved
        die(store.lastError());

    const auto writeExports = [&parser](const std::vector<sp::PredictionRecord> &records) {
        if (!parser.isSet(QStringLiteral("gpx")))
            return;
        const QString path = parser.value(QStringLiteral("gpx"));
        if (const QString error = sp::writeFile(path, sp::toGpx(records)); !error.isEmpty())
            die(error);
        out() << "GPX written to " << path << '\n';
    };

    if (parser.isSet(QStringLiteral("export"))) {
        if (!parser.isSet(QStringLiteral("gpx")))
            die(QStringLiteral("--export needs --gpx"));
        std::vector<sp::PredictionRecord> records;
        for (const QString &id : parser.value(QStringLiteral("export")).split(u',', Qt::SkipEmptyParts)) {
            auto record = store.load(id.trimmed().toLongLong());
            if (!record)
                die(QStringLiteral("no stored prediction %1 (see --list)").arg(id.trimmed()));
            records.push_back(std::move(*record));
        }
        writeExports(records);
        return 0;
    }

    // Balloon options override the settings, including a launch location's own balloon values.
    const auto withOptions = [&parser](sp::FlightProfile p) {
        if (parser.isSet(QStringLiteral("ascent"))) p.ascentRate = number(parser, QStringLiteral("ascent"));
        if (parser.isSet(QStringLiteral("burst"))) p.burstAltitude = number(parser, QStringLiteral("burst"));
        if (parser.isSet(QStringLiteral("descent"))) p.seaLevelDescentRate = number(parser, QStringLiteral("descent"));
        if (parser.isSet(QStringLiteral("exponent"))) p.descentExponent = number(parser, QStringLiteral("exponent"));
        if (parser.isSet(QStringLiteral("legacy"))) p.descentModel = sp::DescentModel::Legacy;
        if (parser.isSet(QStringLiteral("constant-ascent"))) p.shapedAscent = false;
        if (parser.isSet(QStringLiteral("density"))) p.descentModel = sp::DescentModel::DensityScaled;
        if (parser.isSet(QStringLiteral("legacy-rate"))) p.legacyDescentRate = number(parser, QStringLiteral("legacy-rate"));
        if (parser.isSet(QStringLiteral("legacy-gain"))) p.legacyDescentGain = number(parser, QStringLiteral("legacy-gain"));
        if (const char *error = p.validationError(); *error)
            die(QString::fromUtf8(error));
        return p;
    };
    const sp::FlightProfile profile = withOptions(settings.profile);

    const double grid = parser.isSet(QStringLiteral("grid")) ? number(parser, QStringLiteral("grid")) : settings.wind.gridStep;
    if (grid < 0.1 || grid > 2.0)
        die(QStringLiteral("--grid must be between 0.1 and 2 degrees"));

    sp::OpenMeteoOptions options;
    options.model = parser.isSet(QStringLiteral("model")) ? parser.value(QStringLiteral("model")) : settings.wind.model;
    if (!settings.wind.apiUrl.isEmpty()) options.baseUrl = settings.wind.apiUrl;
    options.apiKey = settings.wind.apiKey;
    if (parser.isSet(QStringLiteral("api-url"))) options.baseUrl = parser.value(QStringLiteral("api-url"));
    if (parser.isSet(QStringLiteral("api-key"))) options.apiKey = parser.value(QStringLiteral("api-key"));

    if (const QString proxyError = sp::applyProxy(settings.network); !proxyError.isEmpty())
        QTextStream(stderr) << "sp-cli: " << proxyError << Qt::endl;

    QNetworkAccessManager network;
    sp::OpenMeteoClient client(&network, options);
    auto field = std::make_shared<sp::WindField>(grid);
    sp::MepsClient mepsClient(&network);
    const bool useMeps = parser.isSet(QStringLiteral("meps")) || (settings.wind.useMeps && !parser.isSet(QStringLiteral("no-meps")));
    const std::vector<sp::WindBackend> backends = sp::meps::standardBackends(
        &mepsClient, std::make_shared<sp::WindField>(mepsClient.geometry()), &client, field, options.model, useMeps,
        settings.wind.averageMeps && !parser.isSet(QStringLiteral("meps-only")));
    const bool verbose = parser.isSet(QStringLiteral("verbose"));
    const auto activity = [](const QString &line) {
        QTextStream(stderr) << QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss")) << "  " << line << Qt::endl;
    };
    if (verbose) {
        client.setLogger(activity);
        mepsClient.setLogger(activity);
    }
    const auto requests = [&]() { return client.requestCount() + mepsClient.requestCount(); };
    sp::FlightSpread spread = settings.spread;
    if (parser.isSet(QStringLiteral("no-area")))
        spread.enabled = false;
    // With --verbose the activity lines already say this.
    QObject::connect(&client, &sp::OpenMeteoClient::rateLimited, [verbose](int seconds) {
        if (verbose)
            return;
        QTextStream(stderr) << "Open-Meteo rate limit reached, retrying in " << seconds << " s..." << Qt::endl;
    });
    QObject::connect(&client, &sp::OpenMeteoClient::paced, [verbose](int seconds) {
        if (verbose)
            return;
        QTextStream(stderr) << "Waiting " << sp::openmeteo::describeWait(seconds) << " to stay within Open-Meteo's free limit..." << Qt::endl;
    });

    const auto saveRecord = [&](const sp::PredictionRecord &record) {
        const qint64 id = store.save(record, settings.general.overwritePredictions);
        if (!id)
            die(QStringLiteral("could not save the prediction: %1").arg(store.lastError()));
        return id;
    };

    // ------------------------------------------------------------ batch (v3 "Start a batch")
    if (parser.isSet(QStringLiteral("batch"))) {
        if (!settings.schedule)
            die(QStringLiteral("no batch is set up; create one in the app (Settings → Batch) or import it with --import-v3"));
        sp::BatchPlan plan = sp::planBatch(*settings.schedule, settings, QDateTime::currentSecsSinceEpoch());
        for (sp::BatchJob &job : plan.jobs)
            job.params.profile = withOptions(job.params.profile);
        if (!plan.error.isEmpty())
            die(plan.error);
        if (plan.jobs.empty())
            die(QStringLiteral("all batch launches are beyond the forecast range"));

        auto *runner = new sp::BatchRunner(backends, &app);
        if (verbose)
            QObject::connect(runner, &sp::BatchRunner::activity, activity);
        runner->setSpread(spread);
        auto records = std::make_shared<std::vector<sp::PredictionRecord>>();
        QObject::connect(runner, &sp::BatchRunner::jobStarted, [](int index, int total, const sp::BatchJob &job) {
            QTextStream(stderr) << "Batch " << index + 1 << '/' << total << ": " << job.siteName << ' '
                                << utc(double(job.params.launchTime), "yyyy-MM-dd HH:mm") << " UTC..." << Qt::endl;
        });
        QObject::connect(runner, &sp::BatchRunner::jobFinished, [&, records](int, const sp::BatchJob &job, const sp::Prediction &prediction) {
            sp::PredictionRecord record;
            record.siteName = job.siteName;
            record.homeName = job.homeName;
            record.home = job.home;
            record.manual = false;
            record.model = prediction.model;
            record.prediction = prediction;
            const sp::TrackPoint &land = *prediction.result.landing;
            out() << utc(double(job.params.launchTime), "yyyy-MM-dd HH:mm") << "  " << job.siteName << " → " << position(land.position);
            if (job.home)
                out() << "  " << QString::number(sp::inverse(*job.home, land.position).distance / 1000.0, 'f', 1) << " km from " << job.homeName;
            if (parser.isSet(QStringLiteral("save")))
                out() << "  [saved as " << saveRecord(record) << ']';
            out() << Qt::endl;
            records->push_back(std::move(record));
        });
        QObject::connect(runner, &sp::BatchRunner::jobFailed, [](int, const sp::BatchJob &job, const QString &error) {
            out() << utc(double(job.params.launchTime), "yyyy-MM-dd HH:mm") << "  " << job.siteName << "  failed: " << error << Qt::endl;
        });
        QObject::connect(runner, &sp::BatchRunner::limitReached, [](int skipped, const QString &message) {
            out() << message << ". Batch stopped; " << skipped << " more launch(es) not predicted." << Qt::endl;
        });
        QObject::connect(runner, &sp::BatchRunner::finished, [&, records](int succeeded, int failed) {
            writeExports(*records);
            out() << succeeded << " predicted, " << failed << " failed, " << requests() << " requests" << Qt::endl;
            QCoreApplication::exit(failed ? 2 : 0);
        });
        runner->start(plan.jobs);
        return app.exec();
    }

    // ------------------------------------------------------------ replay (validation)
    if (parser.isSet(QStringLiteral("replay"))) {
        QFile file(parser.value(QStringLiteral("replay")));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            die(QStringLiteral("cannot read %1").arg(file.fileName()));
        struct Job {
            QString id;
            sp::LaunchParams params;
            std::optional<double> windSigma; // optional 11th column
        };
        auto jobs = std::make_shared<std::vector<Job>>();
        while (!file.atEnd()) {
            const QStringList f = QString::fromUtf8(file.readLine()).trimmed().split(u',');
            if (f.size() < 10 || f[0].startsWith(u'#'))
                continue;
            Job job;
            job.id = f[0];
            job.params.launchTime = QDateTime::fromString(f[1], Qt::ISODate).toSecsSinceEpoch();
            job.params.site = {f[2].toDouble(), f[3].toDouble()};
            if (!f[4].trimmed().isEmpty())
                job.params.siteAltitude = f[4].toDouble(); // empty: the ground under the site
            job.params.profile = profile;
            job.params.profile.ascentRate = f[5].toDouble();
            job.params.profile.burstAltitude = f[6].toDouble();
            job.params.profile.descentModel = sp::DescentModel::DensityScaled;
            job.params.profile.seaLevelDescentRate = f[7].toDouble();
            job.params.profile.descentExponent = f[8].toDouble();
            job.params.profile.shapedAscent = f[9].trimmed() == QLatin1String("1");
            if (f.size() > 10 && !f[10].trimmed().isEmpty())
                job.windSigma = f[10].toDouble();
            jobs->push_back(job);
        }
        auto *predictor = new sp::Predictor(backends, &app);
        // The landing area too when it is asked for (validation of its size).
        predictor->setSpread(spread);
        if (parser.isSet(QStringLiteral("wind-sigma")))
            predictor->setWindSigmaOverride(number(parser, QStringLiteral("wind-sigma")));
        if (verbose)
            QObject::connect(predictor, &sp::Predictor::activity, activity);
        auto index = std::make_shared<std::size_t>(0);
        auto next = std::make_shared<std::function<void()>>();
        *next = [=]() {
            if (*index >= jobs->size()) {
                QCoreApplication::exit(0);
                return;
            }
            if (const auto sigma = (*jobs)[*index].windSigma)
                predictor->setWindSigmaOverride(sigma);
            predictor->start((*jobs)[*index].params);
        };
        const auto point = [](const std::optional<sp::TrackPoint> &p) {
            return p ? QStringLiteral("%1,%2,%3").arg(p->position.lat, 0, 'f', 5).arg(p->position.lon, 0, 'f', 5)
                                                 .arg(utc(p->time, "yyyy-MM-dd'T'HH:mm:ss"))
                     : QStringLiteral(",,");
        };
        QObject::connect(predictor, &sp::Predictor::finished, &app, [=](const sp::Prediction &prediction) {
            out() << (*jobs)[*index].id << ',' << point(prediction.result.burst) << ',' << point(prediction.result.landing)
                  << ',' << prediction.model;
            if (const auto &a = prediction.landingArea)
                out() << ',' << QString::number(a->centre.lat, 'f', 5) << ',' << QString::number(a->centre.lon, 'f', 5) << ','
                      << QString::number(a->majorSigma, 'f', 0) << ',' << QString::number(a->minorSigma, 'f', 0) << ','
                      << QString::number(a->majorBearing, 'f', 1);
            out() << Qt::endl;
            ++*index;
            (*next)();
        });
        QObject::connect(predictor, &sp::Predictor::failed, &app, [=](const QString &error) {
            out() << (*jobs)[*index].id << ",,,,,,,failed: " << error << Qt::endl;
            ++*index;
            (*next)();
        });
        QMetaObject::invokeMethod(&app, [next]() { (*next)(); }, Qt::QueuedConnection);
        return app.exec();
    }

    // ------------------------------------------------------------ single prediction
    // Launch site and your location: options, else the last manual run from the settings.
    const QString siteText = parser.isSet(QStringLiteral("launch")) ? parser.value(QStringLiteral("launch")) : settings.manualRun.siteName;
    if (siteText.isEmpty() || !parser.isSet(QStringLiteral("time")))
        die(QStringLiteral("--launch and --time are required (see --help)"));
    const ResolvedLocation site = resolveLocation(siteText, settings, "launch");

    std::optional<ResolvedLocation> home;
    const QString homeText = parser.isSet(QStringLiteral("home")) ? parser.value(QStringLiteral("home")) : settings.manualRun.homeName;
    if (!homeText.isEmpty())
        home = resolveLocation(homeText, settings, "home");

    // A saved location may have its own balloon values.
    const sp::Location *siteLocation = site.name.isEmpty() ? nullptr : settings.location(site.name);
    sp::LaunchParams params;
    params.site = site.point;
    params.siteAltitude = site.altitude;
    params.profile = withOptions(settings.profileFor(siteLocation));

    QDateTime launch = QDateTime::fromString(parser.value(QStringLiteral("time")), Qt::ISODate);
    if (!launch.isValid())
        die(QStringLiteral("--time must look like 2026-10-23T11:15"));
    if (launch.timeSpec() == Qt::LocalTime)
        launch = QDateTime(launch.date(), launch.time(), QTimeZone::UTC);
    params.launchTime = launch.toSecsSinceEpoch();

    sp::Predictor predictor(backends);
    sp::FlightSpread siteSpread = settings.spreadFor(siteLocation);
    siteSpread.enabled = spread.enabled;
    predictor.setSpread(siteSpread);
    if (parser.isSet(QStringLiteral("wind-sigma")))
        predictor.setWindSigmaOverride(number(parser, QStringLiteral("wind-sigma")));
    if (verbose)
        QObject::connect(&predictor, &sp::Predictor::activity, activity);
    QObject::connect(&predictor, &sp::Predictor::progress, [verbose](const QString &message) {
        if (verbose)
            return; // the activity lines say more
        QTextStream(stderr) << message << "..." << Qt::endl;
    });
    QObject::connect(&predictor, &sp::Predictor::failed, [](const QString &error) { die(error); });
    QObject::connect(&predictor, &sp::Predictor::finished, &app, [&](const sp::Prediction &prediction) {
        printPrediction(prediction, home ? std::optional(home->point) : std::nullopt, requests());

        sp::PredictionRecord record;
        record.siteName = site.name.isEmpty() ? position(site.point) : site.name;
        if (home) {
            record.homeName = home->name.isEmpty() ? position(home->point) : home->name;
            record.home = home->point;
        }
        record.model = prediction.model;
        record.prediction = prediction;

        if (parser.isSet(QStringLiteral("csv"))) {
            const QString path = parser.value(QStringLiteral("csv"));
            if (!writeCsv(path, prediction.result))
                die(QStringLiteral("cannot write %1").arg(path));
            out() << "Track written to " << path << '\n';
        }
        writeExports({record});
        if (parser.isSet(QStringLiteral("compare")))
            printComparison(prediction.result, parser.value(QStringLiteral("compare")));
        if (parser.isSet(QStringLiteral("save")))
            out() << "Saved as prediction " << saveRecord(record) << " in " << dbPath << '\n';
        out().flush();
        QCoreApplication::quit();
    });

    predictor.start(params);
    return app.exec();
}
