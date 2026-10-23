#include "meps.h"

#include "atmosphere.h"
#include "openmeteo.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QTimeZone>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>

namespace sp {

namespace meps {

namespace {

// The grid as given in the files' projection_lambert: a sphere, one standard parallel.
constexpr double kRadius = 6371000.0;
constexpr double kPhi1 = 63.3 * std::numbers::pi / 180.0;
constexpr double kLon0 = 15.0 * std::numbers::pi / 180.0;
constexpr double kX0 = -1060084.0;
constexpr double kY0 = -1332517.9;
constexpr double kDx = 2500.0;
constexpr double kRd = 287.05;    // J/(kg·K), dry air
constexpr double kG = 9.80665;    // m/s²

const double kN = std::sin(kPhi1);
const double kF = std::cos(kPhi1) * std::pow(std::tan(std::numbers::pi / 4 + kPhi1 / 2), kN) / kN;
const double kRho0 = kRadius * kF / std::pow(std::tan(std::numbers::pi / 4 + kPhi1 / 2), kN);

double radians(double degrees)
{
    return degrees * std::numbers::pi / 180.0;
}

double degrees(double radians)
{
    return radians * 180.0 / std::numbers::pi;
}

} // namespace


LambertGrid::LambertGrid(int stride)
    : m_stride(std::max(1, stride))
{
}

void LambertGrid::toGrid(GeoPoint p, double &x, double &y) const
{
    project(p, x, y);
    x /= m_stride;
    y /= m_stride;
}

GeoPoint LambertGrid::position(NodeIndex n) const
{
    return unproject(double(n.lon) * m_stride, double(n.lat) * m_stride);
}

void LambertGrid::project(GeoPoint p, double &x, double &y)
{
    const double rho = kRadius * kF / std::pow(std::tan(std::numbers::pi / 4 + radians(p.lat) / 2), kN);
    const double theta = rotation(p.lon);
    x = (rho * std::sin(theta) - kX0) / kDx;
    y = (kRho0 - rho * std::cos(theta) - kY0) / kDx;
}

GeoPoint LambertGrid::unproject(double x, double y)
{
    const double px = kX0 + x * kDx;
    const double dy = kRho0 - (kY0 + y * kDx);
    const double rho = std::hypot(px, dy);
    const double theta = std::atan2(px, dy);
    const double lat = 2.0 * std::atan(std::pow(kRadius * kF / rho, 1.0 / kN)) - std::numbers::pi / 2;
    return {degrees(lat), normalizeLon(degrees(kLon0 + theta / kN))};
}

double LambertGrid::rotation(double lon)
{
    return kN * radians(normalizeLon(lon - 15.0));
}

bool covers(GeoPoint p, double marginKm)
{
    double x = 0.0, y = 0.0;
    LambertGrid::project(p, x, y);
    const double margin = marginKm * 1000.0 / kDx;
    return x >= margin && y >= margin && x <= kNx - 1 - margin && y <= kNy - 1 - margin;
}

bool coversLaunch(const LaunchParams &params, qint64 now)
{
    if (!covers(params.site))
        return false;
    // Runs appear about 2 h 40 min after their reference time.
    constexpr qint64 interval = kRunInterval * 3600;
    const qint64 newestRun = (now - 3 * 3600) / interval * interval;
    const double landing = double(params.launchTime) + estimatedFlightTime(params.profile, params.siteAltitude.value_or(0.0));
    return landing <= double(newestRun + kForecastHours * 3600);
}

QString displayName()
{
    return QStringLiteral("MEPS 2.5 km (MET Norway)");
}

QString modelName(const QString &model)
{
    if (model == QLatin1String("meps"))
        return displayName();
    if (model.startsWith(QLatin1String("meps+")))
        return QStringLiteral("MEPS + %1, averaged").arg(openmeteo::modelName(model.mid(5)));
    return openmeteo::modelName(model.isEmpty() ? QStringLiteral("gfs_global") : model);
}

std::vector<WindBackend> standardBackends(WindSource *meps, std::shared_ptr<WindField> mepsField,
                                          WindSource *global, std::shared_ptr<WindField> globalField,
                                          const QString &globalModel, bool useMeps, bool average)
{
    std::vector<WindBackend> backends;
    if (useMeps) {
        const auto covers = [](const LaunchParams &p) { return coversLaunch(p, QDateTime::currentSecsSinceEpoch()); };
        if (average) {
            const QString model = QStringLiteral("meps+") + globalModel;
            backends.push_back({{{meps, mepsField}, {global, globalField}}, model, modelName(model), covers});
        }
        backends.push_back({{{meps, mepsField}}, QStringLiteral("meps"), displayName(), covers});
    }
    backends.push_back({{{global, globalField}}, globalModel, openmeteo::modelName(globalModel), {}});
    return backends;
}

DapData parseDods(const QByteArray &body)
{
    DapData out;
    const qsizetype marker = body.indexOf("\nData:\n");
    if (marker < 0) {
        // Errors come back as text: "Error { code = 404; message = "..."; };"
        static const QRegularExpression message(QStringLiteral("message\\s*=\\s*\"([^\"]*)\""));
        const QRegularExpressionMatch m = message.match(QString::fromUtf8(body.left(2000)));
        out.error = m.hasMatch() ? m.captured(1) : QStringLiteral("not an OPeNDAP data response");
        return out;
    }

    // The DDS lists the variables in the order their data follows. Arrays are sent as their
    // length (twice, 32-bit) and then the values; scalars as a bare value. All big-endian.
    static const QRegularExpression declaration(
        QStringLiteral("^\\s*(Byte|Int16|UInt16|Int32|UInt32|Float32|Float64|String|Url)\\s+(\\w+)((?:\\s*\\[[^\\]]*\\])*)\\s*;"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression dimension(QStringLiteral("\\[(?:[^=\\]]*=)?\\s*(\\d+)\\s*\\]"));

    const QString dds = QString::fromUtf8(body.left(marker));
    const char *data = body.constData() + marker + 7;
    const char *end = body.constData() + body.size();
    auto it = declaration.globalMatch(dds);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString type = m.captured(1);
        if (type == QLatin1String("Byte") || type == QLatin1String("String") || type == QLatin1String("Url")) {
            out.error = QStringLiteral("unsupported OPeNDAP type %1").arg(type);
            return out;
        }
        const bool isDouble = type == QLatin1String("Float64");
        const int size = isDouble ? 8 : 4;

        qint64 count = 1;
        const bool isArray = !m.captured(3).trimmed().isEmpty();
        auto dims = dimension.globalMatch(m.captured(3));
        while (dims.hasNext())
            count *= dims.next().captured(1).toLongLong();

        if (isArray) {
            if (end - data < 8 || qFromBigEndian<quint32>(data) != quint64(count)) {
                out.error = QStringLiteral("truncated or inconsistent OPeNDAP data for %1").arg(m.captured(2));
                return out;
            }
            data += 8;
        }
        if (end - data < count * size) {
            out.error = QStringLiteral("truncated OPeNDAP data for %1").arg(m.captured(2));
            return out;
        }
        std::vector<double> values(static_cast<std::size_t>(count));
        for (qint64 i = 0; i < count; ++i, data += size) {
            if (isDouble) {
                const quint64 bits = qFromBigEndian<quint64>(data);
                double v;
                std::memcpy(&v, &bits, 8);
                values[std::size_t(i)] = v;
            } else if (type == QLatin1String("Float32")) {
                const quint32 bits = qFromBigEndian<quint32>(data);
                float v;
                std::memcpy(&v, &bits, 4);
                values[std::size_t(i)] = v;
            } else if (type.startsWith(u'U')) {
                values[std::size_t(i)] = qFromBigEndian<quint32>(data);
            } else {
                values[std::size_t(i)] = qFromBigEndian<qint32>(data);
            }
        }
        out.arrays[m.captured(2)] = std::move(values);
    }
    return out;
}

std::vector<LevelWind> columnProfile(const Block &block, int hour, int row, int column, double lon,
                                     double *groundElevation)
{
    const int L = int(block.ap.size());
    if (L < 2 || int(block.b.size()) != L)
        return {};
    const auto at = [&](const std::vector<double> &v, int level) {
        return v[std::size_t(((std::size_t(hour) * L + level) * block.rows + row) * block.columns + column)];
    };
    const double ps = block.surfacePressure[std::size_t((std::size_t(hour) * block.rows + row) * block.columns + column)];
    const double zs = block.surfaceGeopotential[std::size_t(row * block.columns + column)] / kG;
    if (!(ps > 30000.0 && ps < 115000.0) || !(zs > -500.0 && zs < 9000.0))
        return {};

    const double angle = LambertGrid::rotation(lon);
    const double ca = std::cos(angle), sa = std::sin(angle);

    std::vector<LevelWind> levels(std::size_t(L), LevelWind{});
    double z = zs;           // geopotential height of the level below
    double pBelow = ps;
    double tvBelow = 0.0;
    for (int k = L - 1; k >= 0; --k) {
        const double p = block.ap[std::size_t(k)] + block.b[std::size_t(k)] * ps;
        const double t = at(block.temperature, k);
        const double q = at(block.humidity, k);
        const double gx = at(block.xWind, k);
        const double gy = at(block.yWind, k);
        if (!(p > 0.0 && p < pBelow + 1.0) || !(t > 150.0 && t < 350.0) || !(q >= -0.01 && q < 0.1)
            || !(std::abs(gx) < 250.0) || !(std::abs(gy) < 250.0))
            return {};
        const double tv = t * (1.0 + 0.608 * std::max(q, 0.0));
        // Lowest level: its own temperature down to the surface; above: the layer mean.
        const double layerTv = k == L - 1 ? tv : 0.5 * (tv + tvBelow);
        z += kRd * layerTv / kG * std::log(pBelow / p);
        levels[std::size_t(k)] = {atmosphere::geometricFromGeopotential(z), gx * ca + gy * sa, -gx * sa + gy * ca};
        pBelow = p;
        tvBelow = tv;
    }
    if (groundElevation)
        *groundElevation = atmosphere::geometricFromGeopotential(zs);
    return levels;
}

} // namespace meps

struct MepsClient::Job {
    std::set<NodeIndex> nodes;
    qint64 firstHour = 0;
    qint64 lastHour = 0;
    std::shared_ptr<WindField> field;
    Done done;
    qint64 run = 0;  // unix seconds
    int t0 = 0;      // first and last forecast hour fetched
    int t1 = 0;
    int pending = 0; // requests still loading
    FetchResult result;
    qint64 startedMs = 0;
    qint64 bytes = 0;
};

namespace {

QString runName(qint64 run)
{
    return QDateTime::fromSecsSinceEpoch(run, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-dd HH 'UTC'"));
}

} // namespace

MepsClient::MepsClient(QNetworkAccessManager *network, MepsOptions options, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_options(std::move(options))
    , m_geometry(std::make_shared<meps::LambertGrid>(m_options.stride))
{
}

QString MepsClient::runFile(qint64 run) const
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(run, QTimeZone::UTC);
    return QStringLiteral("%1/%2/meps_det_ml_%3Z.ncml")
        .arg(m_options.baseUrl, t.toString(QStringLiteral("yyyy/MM/dd")), t.toString(QStringLiteral("yyyyMMdd'T'HH")));
}

QString MepsClient::surfaceFile(qint64 run) const
{
    const QDateTime t = QDateTime::fromSecsSinceEpoch(run, QTimeZone::UTC);
    return QStringLiteral("%1/%2/meps_det_2_5km_%3Z.nc")
        .arg(m_options.baseUrl, t.toString(QStringLiteral("yyyy/MM/dd")), t.toString(QStringLiteral("yyyyMMdd'T'HH")));
}

void MepsClient::fetch(const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour,
                       std::shared_ptr<WindField> field, Done done)
{
    if (nodes.empty()) {
        done({});
        return;
    }
    auto job = std::make_shared<Job>();
    job->nodes = {nodes.begin(), nodes.end()};
    job->firstHour = firstHour;
    job->lastHour = lastHour;
    job->field = std::move(field);
    job->done = std::move(done);
    job->startedMs = QDateTime::currentMSecsSinceEpoch();

    // The run issued at or before the first hour; for the future, the newest one. Runs are
    // published about 2 h 40 min after their time, so look back up to 12 hours.
    constexpr qint64 interval = meps::kRunInterval * 3600;
    const qint64 newest = std::min(firstHour * 3600, QDateTime::currentSecsSinceEpoch());
    findRun(newest / interval * interval, 5, [this, job](qint64 run) {
        if (run <= 0) {
            job->done({run < 0 ? QStringLiteral("cannot reach MET Norway's THREDDS server")
                               : QStringLiteral("no MEPS run is available for this time"),
                       false});
            return;
        }
        job->run = run;
        if (m_lastRun != run)
            log(QStringLiteral("MEPS: using the run of %1").arg(runName(run)));
        m_lastRun = run;
        startJob(job);
    });
}

void MepsClient::findRun(qint64 candidate, int stepsLeft, std::function<void(qint64)> then)
{
    if (stepsLeft <= 0) {
        then(0);
        return;
    }
    constexpr qint64 interval = meps::kRunInterval * 3600;
    if (m_availableRuns.contains(candidate)) {
        then(candidate);
        return;
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (const auto it = m_missingRuns.find(candidate); it != m_missingRuns.end() && nowMs - it->second < 10 * 60 * 1000) {
        findRun(candidate - interval, stepsLeft - 1, std::move(then));
        return;
    }
    get(QUrl(runFile(candidate) + QStringLiteral(".dds")),
        [this, candidate, stepsLeft, then = std::move(then)](int status, const QByteArray &body, const QString &) mutable {
            if (status == 200 && body.contains("x_wind_ml")) {
                m_availableRuns.insert(candidate);
                m_missingRuns.erase(candidate);
                then(candidate);
            } else if (status >= 400 && status < 500) {
                log(QStringLiteral("MEPS: the run of %1 is not published yet").arg(runName(candidate)));
                m_missingRuns[candidate] = QDateTime::currentMSecsSinceEpoch();
                findRun(candidate - meps::kRunInterval * 3600, stepsLeft - 1, std::move(then));
            } else {
                then(-1);
            }
        });
}

void MepsClient::startJob(std::shared_ptr<Job> job)
{
    const qint64 runHour = job->run / 3600;
    job->t0 = int(std::max<qint64>(0, job->firstHour - runHour));
    job->t1 = int(std::min<qint64>(meps::kForecastHours, job->lastHour - runHour));
    if (job->t0 > job->t1) {
        job->done({QStringLiteral("MEPS forecasts reach only %1 hours ahead").arg(meps::kForecastHours), false});
        return;
    }

    // The requested nodes plus a margin, clipped to the model area.
    const int s = m_geometry->stride();
    const int lastX = (meps::kNx - 1) / s;
    const int lastY = (meps::kNy - 1) / s;
    int x0 = std::numeric_limits<int>::max(), y0 = x0, x1 = std::numeric_limits<int>::min(), y1 = x1;
    for (const NodeIndex &n : job->nodes) {
        x0 = std::min(x0, n.lon);
        x1 = std::max(x1, n.lon);
        y0 = std::min(y0, n.lat);
        y1 = std::max(y1, n.lat);
    }
    // A first request (around the launch site) gets a wide margin so that one download
    // usually serves the whole flight; later passes only fill in at the edge of what is
    // loaded, so a narrow margin keeps them from downloading the same area again.
    bool followUp = false;
    for (const NodeIndex &n : job->nodes) {
        for (int dy = -1; dy <= 1 && !followUp; ++dy) {
            for (int dx = -1; dx <= 1 && !followUp; ++dx)
                followUp = job->field->contains({{n.lat + dy, n.lon + dx}, job->firstHour});
        }
        if (followUp)
            break;
    }
    const int margin = followUp ? m_options.followUpMargin : m_options.margin;
    x0 = std::max(0, x0 - margin);
    y0 = std::max(0, y0 - margin);
    x1 = std::min(lastX, x1 + margin);
    y1 = std::min(lastY, y1 + margin);
    if (x0 > x1 || y0 > y1) {
        job->done({QStringLiteral("the flight leaves the MEPS model area"), false});
        return;
    }
    std::vector<Box> boxes;
    const int size = std::max(1, m_options.maxBoxNodes);
    for (int y = y0; y <= y1; y += size) {
        for (int x = x0; x <= x1; x += size)
            boxes.push_back({x, y, std::min(size, x1 - x + 1), std::min(size, y1 - y + 1)});
    }

    log(QStringLiteral("MEPS: %1 grid points (%2 × %3, %4 km apart) for forecast hours +%5 to +%6: %7 request(s)")
            .arg((x1 - x0 + 1) * (y1 - y0 + 1)).arg(x1 - x0 + 1).arg(y1 - y0 + 1).arg(s * 2.5)
            .arg(job->t0).arg(job->t1).arg(boxes.size() * std::size_t(job->t1 - job->t0 + 1)));
    m_geopotentialWaiting.push_back([this, job, boxes](const QString &error) {
        if (!error.isEmpty()) {
            job->done({error, false});
            return;
        }
        job->pending = int(boxes.size()) * (job->t1 - job->t0 + 1);
        for (const Box &box : boxes) {
            for (int hour = job->t0; hour <= job->t1; ++hour)
                fetchHour(job, box, hour);
        }
    });
    if (!m_geopotential.empty()) {
        auto waiting = std::move(m_geopotentialWaiting);
        m_geopotentialWaiting.clear();
        for (auto &then : waiting)
            then({});
    } else if (m_geopotentialWaiting.size() == 1) {
        loadGeopotential(job->run, 0);
    }
}

void MepsClient::loadGeopotential(qint64 run, int stepsBack)
{
    // The surface height never changes: load it once for the whole area. A run's surface
    // file appears a little after its model levels, so an earlier run's will do.
    const int s = m_geometry->stride();
    QString query = QStringLiteral("surface_geopotential.surface_geopotential[0:1:0][0:1:0][0:%1:%2][0:%1:%3]")
                        .arg(s).arg(meps::kNy - 1).arg(meps::kNx - 1);
    query.replace(u'[', QLatin1String("%5B")).replace(u']', QLatin1String("%5D"));
    const qint64 from = run - qint64(stepsBack) * meps::kRunInterval * 3600;
    if (stepsBack == 0)
        log(QStringLiteral("MEPS: loading the model's surface height (once per session)"));
    get(QUrl(surfaceFile(from) + QStringLiteral(".dods?") + query),
        [this, run, stepsBack](int status, const QByteArray &body, const QString &error) {
            QString failure;
            if (status == 200) {
                meps::DapData dap = meps::parseDods(body);
                const auto it = dap.arrays.find(QStringLiteral("surface_geopotential"));
                const std::size_t expected = std::size_t((meps::kNy - 1) / m_geometry->stride() + 1)
                                           * std::size_t((meps::kNx - 1) / m_geometry->stride() + 1);
                if (dap.error.isEmpty() && it != dap.arrays.end() && it->second.size() == expected)
                    m_geopotential = std::move(it->second);
                else
                    failure = QStringLiteral("MEPS surface height: %1").arg(dap.error.isEmpty() ? QStringLiteral("unexpected data") : dap.error);
            } else if (status >= 400 && status < 500 && stepsBack < 8) {
                loadGeopotential(run, stepsBack + 1);
                return;
            } else {
                failure = QStringLiteral("MEPS surface height request failed: %1").arg(error);
            }
            auto waiting = std::move(m_geopotentialWaiting);
            m_geopotentialWaiting.clear();
            for (auto &then : waiting)
                then(failure);
        });
}

void MepsClient::fetchHour(std::shared_ptr<Job> job, Box box, int hour)
{
    const int s = m_geometry->stride();
    const QString area = QStringLiteral("[%1:%2:%3][%4:%5:%6]")
                             .arg(box.y0 * s).arg(s).arg((box.y0 + box.rows - 1) * s)
                             .arg(box.x0 * s).arg(s).arg((box.x0 + box.columns - 1) * s);
    const QString time = QStringLiteral("[%1:1:%1]").arg(hour);
    QStringList parts;
    for (const char *name : {"x_wind_ml", "y_wind_ml", "air_temperature_ml", "specific_humidity_ml"})
        parts << QStringLiteral("%1.%1%2[0:1:%3]%4").arg(QLatin1String(name), time).arg(meps::kLevels - 1).arg(area);
    parts << QStringLiteral("surface_air_pressure.surface_air_pressure%1[0:1:0]%2").arg(time, area);
    parts << QStringLiteral("ap") << QStringLiteral("b");
    QString query = parts.join(u',');
    query.replace(u'[', QLatin1String("%5B")).replace(u']', QLatin1String("%5D"));

    const qint64 started = QDateTime::currentMSecsSinceEpoch();
    get(QUrl(runFile(job->run) + QStringLiteral(".dods?") + query), [this, job, box, hour, started](int status, const QByteArray &body, const QString &error) {
        QString failure;
        job->bytes += body.size();
        if (status != 200) {
            failure = QStringLiteral("MEPS data request failed: %1").arg(error);
        } else if (const meps::DapData dap = meps::parseDods(body); !dap.error.isEmpty()) {
            failure = QStringLiteral("MEPS data: %1").arg(dap.error);
        } else {
            meps::Block block;
            block.hours = 1;
            block.rows = box.rows;
            block.columns = box.columns;
            const auto take = [&dap](const char *name) {
                const auto it = dap.arrays.find(QLatin1String(name));
                return it == dap.arrays.end() ? std::vector<double>{} : it->second;
            };
            block.ap = take("ap");
            block.b = take("b");
            block.xWind = take("x_wind_ml");
            block.yWind = take("y_wind_ml");
            block.temperature = take("air_temperature_ml");
            block.humidity = take("specific_humidity_ml");
            block.surfacePressure = take("surface_air_pressure");
            const int domainColumns = (meps::kNx - 1) / m_geometry->stride() + 1;
            block.surfaceGeopotential.reserve(std::size_t(box.rows * box.columns));
            for (int r = 0; r < box.rows; ++r) {
                for (int c = 0; c < box.columns; ++c)
                    block.surfaceGeopotential.push_back(m_geopotential[std::size_t((box.y0 + r) * domainColumns + box.x0 + c)]);
            }
            const std::size_t columns = std::size_t(box.rows) * std::size_t(box.columns);
            if (block.ap.size() != meps::kLevels || block.b.size() != meps::kLevels
                || block.xWind.size() != meps::kLevels * columns || block.yWind.size() != meps::kLevels * columns
                || block.temperature.size() != meps::kLevels * columns || block.humidity.size() != meps::kLevels * columns
                || block.surfacePressure.size() != columns) {
                failure = QStringLiteral("MEPS data: unexpected array sizes");
            } else {
                const qint64 unixHour = job->run / 3600 + hour;
                for (int r = 0; r < box.rows; ++r) {
                    for (int c = 0; c < box.columns; ++c) {
                        const NodeIndex node{box.y0 + r, box.x0 + c};
                        double ground = 0.0;
                        std::vector<LevelWind> levels = meps::columnProfile(block, 0, r, c, m_geometry->position(node).lon, &ground);
                        if (!levels.empty())
                            job->field->insert({node, unixHour}, ground, std::move(levels));
                    }
                }
            }
        }
        if (failure.isEmpty())
            log(QStringLiteral("MEPS: hour +%1 loaded (%2 kB in %3 s)").arg(hour).arg(body.size() / 1024)
                    .arg(double(QDateTime::currentMSecsSinceEpoch() - started) / 1000.0, 0, 'f', 1));
        else
            log(QStringLiteral("MEPS: hour +%1 failed: %2").arg(hour).arg(failure));
        if (!failure.isEmpty() && job->result.error.isEmpty())
            job->result.error = failure;
        if (--job->pending == 0) {
            if (job->result.error.isEmpty())
                log(QStringLiteral("MEPS: done, %1 MB in %2 s").arg(double(job->bytes) / 1e6, 0, 'f', 1)
                        .arg(double(QDateTime::currentMSecsSinceEpoch() - job->startedMs) / 1000.0, 0, 'f', 1));
            job->done(job->result);
        }
    });
}

void MepsClient::get(const QUrl &url, std::function<void(int, const QByteArray &, const QString &)> then, int attempt)
{
    m_queue.push_back([this, url, then = std::move(then), attempt]() {
        QNetworkRequest request(url);
        // MET Norway asks API users to identify themselves.
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("SondePredict/%1 github.com/oh2gba/SondePredict").arg(QCoreApplication::applicationVersion()));
        request.setTransferTimeout(m_options.timeoutMs);
        ++m_requestCount;
        ++m_active;
        QNetworkReply *reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, then, attempt]() {
            reply->deleteLater();
            --m_active;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // The server limits how much one address may ask at once (429) and is sometimes
            // briefly unavailable: wait and try again.
            if ((status == 429 || status == 502 || status == 503) && attempt < m_options.retries) {
                bool ok = false;
                const int retryAfter = reply->rawHeader("Retry-After").toInt(&ok);
                const int delayMs = ok && retryAfter > 0 ? std::min(retryAfter, 60) * 1000 : m_options.retryDelayMs << attempt;
                log(status == 429 ? QStringLiteral("MEPS: MET Norway's server is busy (too many requests); retrying in %1 s").arg(delayMs / 1000)
                                  : QStringLiteral("MEPS: server error %1; retrying in %2 s").arg(status).arg(delayMs / 1000));
                QTimer::singleShot(delayMs, this, [this, url, then, attempt]() { get(url, then, attempt + 1); });
                pump();
                return;
            }
            const QByteArray body = reply->readAll();
            // Keep messages short: Qt's error strings repeat the (long) URL.
            QString error;
            if (status == 429)
                error = QStringLiteral("MET Norway's server is busy (too many requests); try again in a minute");
            else if (status > 0)
                error = QStringLiteral("HTTP %1 %2").arg(status).arg(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString());
            else
                error = reply->errorString();
            then(status, body, error.trimmed());
            pump();
        });
    });
    pump();
}

void MepsClient::pump()
{
    while (m_active < std::max(1, m_options.maxParallel) && !m_queue.empty()) {
        auto next = std::move(m_queue.front());
        m_queue.erase(m_queue.begin());
        next();
    }
}

} // namespace sp
