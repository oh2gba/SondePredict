#include "predictionstore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <atomic>

namespace sp {

namespace {

std::atomic<int> s_connectionCounter{0};

// Each entry migrates the schema from version (index) to (index + 1).
const char *const kMigrations[] = {
    // 0 → 1
    "CREATE TABLE predictions ("
    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "  created_at INTEGER NOT NULL,"
    "  launch_time INTEGER NOT NULL,"
    "  site_name TEXT NOT NULL,"
    "  site_lat REAL NOT NULL, site_lon REAL NOT NULL, site_alt REAL,"
    "  home_name TEXT NOT NULL DEFAULT '',"
    "  home_lat REAL, home_lon REAL,"
    "  manual INTEGER NOT NULL,"
    "  model TEXT NOT NULL DEFAULT '',"
    "  ascent_rate REAL NOT NULL, burst_altitude REAL NOT NULL,"
    "  descent_model TEXT NOT NULL, descent_rate REAL NOT NULL, descent_exponent REAL NOT NULL,"
    "  legacy_descent_rate REAL NOT NULL, legacy_descent_gain REAL NOT NULL,"
    "  time_step REAL NOT NULL, output_interval REAL NOT NULL,"
    "  burst_time REAL, burst_lat REAL, burst_lon REAL, burst_alt REAL,"
    "  landing_time REAL NOT NULL, landing_lat REAL NOT NULL, landing_lon REAL NOT NULL, landing_alt REAL NOT NULL,"
    "  distance_from_home REAL, bearing_from_home REAL,"
    "  above_top_level INTEGER NOT NULL, passes INTEGER NOT NULL, nodes_fetched INTEGER NOT NULL"
    ");"
    "CREATE INDEX predictions_launch ON predictions(launch_time, site_name, home_name);"
    "CREATE TABLE track_points ("
    "  prediction_id INTEGER NOT NULL REFERENCES predictions(id) ON DELETE CASCADE,"
    "  seq INTEGER NOT NULL,"
    "  time REAL NOT NULL, lat REAL NOT NULL, lon REAL NOT NULL, alt REAL NOT NULL,"
    "  u REAL NOT NULL, v REAL NOT NULL, phase INTEGER NOT NULL,"
    "  PRIMARY KEY (prediction_id, seq)"
    ") WITHOUT ROWID;",
    // 1 → 2: landing area (phase 5)
    "ALTER TABLE predictions ADD COLUMN area_lat REAL;"
    "ALTER TABLE predictions ADD COLUMN area_lon REAL;"
    "ALTER TABLE predictions ADD COLUMN area_major_sigma REAL;"
    "ALTER TABLE predictions ADD COLUMN area_minor_sigma REAL;"
    "ALTER TABLE predictions ADD COLUMN area_bearing REAL;"
    "ALTER TABLE predictions ADD COLUMN area_samples INTEGER;",
};

constexpr int kMigrationCount = static_cast<int>(std::size(kMigrations));
static_assert(kMigrationCount == PredictionStore::kSchemaVersion);

// Qt binds a null QString as SQL NULL; the text columns are NOT NULL, so bind "" instead.
QString text(const QString &s)
{
    return s.isNull() ? QStringLiteral("") : s;
}

QVariant optionalValue(const std::optional<double> &v)
{
    return v ? QVariant(*v) : QVariant(QMetaType(QMetaType::Double));
}

std::optional<double> optionalDouble(const QVariant &v)
{
    return v.isNull() ? std::nullopt : std::optional<double>(v.toDouble());
}

} // namespace

PredictionStore::PredictionStore()
    : m_connection(QStringLiteral("sp-store-%1").arg(s_connectionCounter++))
{
}

PredictionStore::~PredictionStore()
{
    close();
}

QString PredictionStore::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
         + QStringLiteral("/SondePredict/predictions.sqlite");
}

bool PredictionStore::open(const QString &path)
{
    close();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    if (!db.isValid()) {
        m_error = QStringLiteral("the Qt SQLite driver is not available");
        QSqlDatabase::removeDatabase(m_connection);
        return false;
    }
    db.setDatabaseName(path);
    if (!db.open()) {
        m_error = QStringLiteral("cannot open %1: %2").arg(path, db.lastError().text());
        return false;
    }
    QSqlQuery(db).exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    if (!migrate()) {
        close();
        return false;
    }
    return true;
}

bool PredictionStore::isOpen() const
{
    return QSqlDatabase::contains(m_connection) && QSqlDatabase::database(m_connection, false).isOpen();
}

void PredictionStore::close()
{
    if (!QSqlDatabase::contains(m_connection))
        return;
    {
        QSqlDatabase db = QSqlDatabase::database(m_connection, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_connection);
}

int PredictionStore::schemaVersion() const
{
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    return q.exec(QStringLiteral("PRAGMA user_version")) && q.next() ? q.value(0).toInt() : -1;
}

bool PredictionStore::migrate()
{
    const int version = schemaVersion();
    if (version < 0) {
        m_error = QStringLiteral("cannot read the database version");
        return false;
    }
    if (version > kSchemaVersion) {
        m_error = QStringLiteral("the prediction database was written by a newer Sonde Predict (schema %1)").arg(version);
        return false;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    for (int v = version; v < kSchemaVersion; ++v) {
        db.transaction();
        QSqlQuery q(db);
        for (const QString &statement : QString::fromLatin1(kMigrations[v]).split(u';', Qt::SkipEmptyParts)) {
            if (statement.trimmed().isEmpty())
                continue;
            if (!q.exec(statement)) {
                m_error = QStringLiteral("database upgrade to version %1 failed: %2").arg(v + 1).arg(q.lastError().text());
                db.rollback();
                return false;
            }
        }
        q.exec(QStringLiteral("PRAGMA user_version = %1").arg(v + 1));
        db.commit();
    }
    return true;
}

qint64 PredictionStore::save(const PredictionRecord &record, bool replaceExisting)
{
    const SimulationResult &r = record.prediction.result;
    const LaunchParams &p = record.prediction.params;
    if (!r.landing || r.track.empty()) {
        m_error = QStringLiteral("only landed predictions can be saved");
        return 0;
    }

    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    if (!db.isOpen()) {
        m_error = QStringLiteral("the prediction database is not open");
        return 0;
    }
    db.transaction();
    QSqlQuery q(db);

    if (replaceExisting) {
        q.prepare(QStringLiteral("DELETE FROM predictions WHERE launch_time = ? AND site_name = ? AND home_name = ?"));
        q.addBindValue(p.launchTime);
        q.addBindValue(text(record.siteName));
        q.addBindValue(text(record.homeName));
        if (!q.exec()) {
            m_error = q.lastError().text();
            db.rollback();
            return 0;
        }
    }

    std::optional<double> distance, bearing;
    if (record.home) {
        const Geodesic g = inverse(*record.home, r.landing->position);
        distance = g.distance;
        bearing = g.initialBearing;
    }

    q.prepare(QStringLiteral(
        "INSERT INTO predictions (created_at, launch_time, site_name, site_lat, site_lon, site_alt,"
        " home_name, home_lat, home_lon, manual, model, ascent_rate, burst_altitude, descent_model,"
        " descent_rate, descent_exponent, legacy_descent_rate, legacy_descent_gain, time_step, output_interval,"
        " burst_time, burst_lat, burst_lon, burst_alt, landing_time, landing_lat, landing_lon, landing_alt,"
        " distance_from_home, bearing_from_home, above_top_level, passes, nodes_fetched,"
        " area_lat, area_lon, area_major_sigma, area_minor_sigma, area_bearing, area_samples)"
        " VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(record.createdAt ? record.createdAt : QDateTime::currentSecsSinceEpoch());
    q.addBindValue(p.launchTime);
    q.addBindValue(text(record.siteName));
    q.addBindValue(p.site.lat);
    q.addBindValue(p.site.lon);
    q.addBindValue(optionalValue(p.siteAltitude));
    q.addBindValue(text(record.homeName));
    q.addBindValue(optionalValue(record.home ? std::optional(record.home->lat) : std::nullopt));
    q.addBindValue(optionalValue(record.home ? std::optional(record.home->lon) : std::nullopt));
    q.addBindValue(record.manual ? 1 : 0);
    q.addBindValue(text(record.model));
    q.addBindValue(p.profile.ascentRate);
    q.addBindValue(p.profile.burstAltitude);
    q.addBindValue(p.profile.descentModel == DescentModel::Legacy ? QStringLiteral("legacy") : QStringLiteral("density"));
    q.addBindValue(p.profile.seaLevelDescentRate);
    q.addBindValue(p.profile.descentExponent);
    q.addBindValue(p.profile.legacyDescentRate);
    q.addBindValue(p.profile.legacyDescentGain);
    q.addBindValue(p.timeStep);
    q.addBindValue(p.outputInterval);
    q.addBindValue(optionalValue(r.burst ? std::optional(r.burst->time) : std::nullopt));
    q.addBindValue(optionalValue(r.burst ? std::optional(r.burst->position.lat) : std::nullopt));
    q.addBindValue(optionalValue(r.burst ? std::optional(r.burst->position.lon) : std::nullopt));
    q.addBindValue(optionalValue(r.burst ? std::optional(r.burst->altitude) : std::nullopt));
    q.addBindValue(r.landing->time);
    q.addBindValue(r.landing->position.lat);
    q.addBindValue(r.landing->position.lon);
    q.addBindValue(r.landing->altitude);
    q.addBindValue(optionalValue(distance));
    q.addBindValue(optionalValue(bearing));
    q.addBindValue(r.aboveTopLevel ? 1 : 0);
    q.addBindValue(record.prediction.passes);
    q.addBindValue(record.prediction.nodesFetched);
    const std::optional<LandingArea> &area = record.prediction.landingArea;
    q.addBindValue(optionalValue(area ? std::optional(area->centre.lat) : std::nullopt));
    q.addBindValue(optionalValue(area ? std::optional(area->centre.lon) : std::nullopt));
    q.addBindValue(optionalValue(area ? std::optional(area->majorSigma) : std::nullopt));
    q.addBindValue(optionalValue(area ? std::optional(area->minorSigma) : std::nullopt));
    q.addBindValue(optionalValue(area ? std::optional(area->majorBearing) : std::nullopt));
    q.addBindValue(area ? QVariant(area->samples) : QVariant(QMetaType(QMetaType::Int)));
    if (!q.exec()) {
        m_error = q.lastError().text();
        db.rollback();
        return 0;
    }
    const qint64 id = q.lastInsertId().toLongLong();

    q.prepare(QStringLiteral("INSERT INTO track_points (prediction_id, seq, time, lat, lon, alt, u, v, phase)"
                             " VALUES (?,?,?,?,?,?,?,?,?)"));
    for (std::size_t i = 0; i < r.track.size(); ++i) {
        const TrackPoint &t = r.track[i];
        q.addBindValue(id);
        q.addBindValue(static_cast<qint64>(i));
        q.addBindValue(t.time);
        q.addBindValue(t.position.lat);
        q.addBindValue(t.position.lon);
        q.addBindValue(t.altitude);
        q.addBindValue(t.u);
        q.addBindValue(t.v);
        q.addBindValue(t.phase == FlightPhase::Ascent ? 0 : 1);
        if (!q.exec()) {
            m_error = q.lastError().text();
            db.rollback();
            return 0;
        }
    }

    if (!db.commit()) {
        m_error = db.lastError().text();
        return 0;
    }
    return id;
}

std::vector<PredictionSummary> PredictionStore::list() const
{
    std::vector<PredictionSummary> out;
    QSqlQuery q(QSqlDatabase::database(m_connection, false));
    if (!q.exec(QStringLiteral(
            "SELECT id, created_at, launch_time, site_name, home_name, manual, landing_lat, landing_lon,"
            " landing_time, distance_from_home, bearing_from_home"
            " FROM predictions ORDER BY launch_time DESC, created_at DESC, id DESC"))) {
        m_error = q.lastError().text();
        return out;
    }
    while (q.next()) {
        PredictionSummary s;
        s.id = q.value(0).toLongLong();
        s.createdAt = q.value(1).toLongLong();
        s.launchTime = q.value(2).toLongLong();
        s.siteName = q.value(3).toString();
        s.homeName = q.value(4).toString();
        s.manual = q.value(5).toBool();
        s.landing = {q.value(6).toDouble(), q.value(7).toDouble()};
        s.landingTime = q.value(8).toDouble();
        s.distanceFromHome = optionalDouble(q.value(9));
        s.bearingFromHome = optionalDouble(q.value(10));
        out.push_back(s);
    }
    return out;
}

std::optional<PredictionRecord> PredictionStore::load(qint64 id) const
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT created_at, launch_time, site_name, site_lat, site_lon, site_alt, home_name, home_lat, home_lon,"
        " manual, model, ascent_rate, burst_altitude, descent_model, descent_rate, descent_exponent,"
        " legacy_descent_rate, legacy_descent_gain, time_step, output_interval, above_top_level, passes,"
        " nodes_fetched, burst_time, area_lat, area_lon, area_major_sigma, area_minor_sigma, area_bearing,"
        " area_samples FROM predictions WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec() || !q.next()) {
        m_error = q.lastError().isValid() ? q.lastError().text() : QStringLiteral("no prediction %1").arg(id);
        return std::nullopt;
    }

    PredictionRecord rec;
    rec.id = id;
    rec.createdAt = q.value(0).toLongLong();
    LaunchParams &p = rec.prediction.params;
    p.launchTime = q.value(1).toLongLong();
    rec.siteName = q.value(2).toString();
    p.site = {q.value(3).toDouble(), q.value(4).toDouble()};
    p.siteAltitude = optionalDouble(q.value(5));
    rec.homeName = q.value(6).toString();
    if (!q.value(7).isNull() && !q.value(8).isNull())
        rec.home = GeoPoint{q.value(7).toDouble(), q.value(8).toDouble()};
    rec.manual = q.value(9).toBool();
    rec.model = q.value(10).toString();
    p.profile.ascentRate = q.value(11).toDouble();
    p.profile.burstAltitude = q.value(12).toDouble();
    p.profile.descentModel = q.value(13).toString() == QLatin1String("legacy") ? DescentModel::Legacy : DescentModel::DensityScaled;
    p.profile.seaLevelDescentRate = q.value(14).toDouble();
    p.profile.descentExponent = q.value(15).toDouble();
    p.profile.legacyDescentRate = q.value(16).toDouble();
    p.profile.legacyDescentGain = q.value(17).toDouble();
    p.timeStep = q.value(18).toDouble();
    p.outputInterval = q.value(19).toDouble();
    SimulationResult &r = rec.prediction.result;
    r.aboveTopLevel = q.value(20).toBool();
    rec.prediction.passes = q.value(21).toInt();
    rec.prediction.nodesFetched = q.value(22).toInt();
    const std::optional<double> burstTime = optionalDouble(q.value(23));
    if (!q.value(24).isNull()) {
        LandingArea area;
        area.centre = {q.value(24).toDouble(), q.value(25).toDouble()};
        area.majorSigma = q.value(26).toDouble();
        area.minorSigma = q.value(27).toDouble();
        area.majorBearing = q.value(28).toDouble();
        area.samples = q.value(29).toInt();
        rec.prediction.landingArea = area;
    }

    QSqlQuery t(db);
    t.prepare(QStringLiteral("SELECT time, lat, lon, alt, u, v, phase FROM track_points WHERE prediction_id = ? ORDER BY seq"));
    t.addBindValue(id);
    if (!t.exec()) {
        m_error = t.lastError().text();
        return std::nullopt;
    }
    while (t.next()) {
        r.track.push_back({t.value(0).toDouble(), {t.value(1).toDouble(), t.value(2).toDouble()}, t.value(3).toDouble(),
                           t.value(4).toDouble(), t.value(5).toDouble(),
                           t.value(6).toInt() == 0 ? FlightPhase::Ascent : FlightPhase::Descent});
    }
    if (r.track.empty()) {
        m_error = QStringLiteral("prediction %1 has no track").arg(id);
        return std::nullopt;
    }
    r.landing = r.track.back();
    // The burst point is recorded in the track at the burst time.
    if (burstTime) {
        for (const TrackPoint &tp : r.track) {
            if (tp.time == *burstTime) {
                r.burst = tp;
                break;
            }
        }
    }
    return rec;
}

bool PredictionStore::remove(const QList<qint64> &ids)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM predictions WHERE id = ?"));
    for (qint64 id : ids) {
        q.addBindValue(id);
        if (!q.exec()) {
            m_error = q.lastError().text();
            db.rollback();
            return false;
        }
    }
    return db.commit();
}

} // namespace sp
