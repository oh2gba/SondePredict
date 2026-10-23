#pragma once

#include "predictor.h"
#include "simulator.h"
#include "windsource.h"

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

class QNetworkAccessManager;

namespace sp {

// MEPS, the MetCoOp model run by MET Norway with SMHI and FMI: a 2.5 km grid over the Nordic
// countries and the Baltic, 65 model levels up to 10 hPa (~31 km), hourly to 66 hours ahead,
// a new run every 3 hours. Read from MET Norway's THREDDS server over OPeNDAP: free, no key,
// no quota. Winds are used on the model's own levels, not interpolated to standard pressure
// levels, which keeps the detail between them (jet stream, tropopause).
namespace meps {

constexpr int kNx = 949;
constexpr int kNy = 1069;
constexpr int kLevels = 65;
constexpr int kForecastHours = 66;
constexpr int kRunInterval = 3; // hours

// The model's Lambert conformal grid, every `stride`-th point: node (lat = j, lon = i) is
// grid point (y = j · stride, x = i · stride).
class LambertGrid : public GridGeometry {
public:
    explicit LambertGrid(int stride = 2);
    int stride() const { return m_stride; }

    void toGrid(GeoPoint p, double &x, double &y) const override;
    GeoPoint position(NodeIndex n) const override;

    // Full-resolution grid coordinates (x along the 949 columns, y along the 1069 rows).
    static void project(GeoPoint p, double &x, double &y);
    static GeoPoint unproject(double x, double y);
    // Angle from the grid's x axis to east at a longitude, radians.
    static double rotation(double lon);

private:
    int m_stride;
};

// Whether a point is inside the model area with at least `marginKm` to the edges.
bool covers(GeoPoint p, double marginKm = 150.0);

// Whether MEPS can likely serve a launch: from inside its area, and landing before the end of
// the newest run published by `now` (unix seconds). Flights that drift out or runs that are
// late fall back to the next source anyway.
bool coversLaunch(const LaunchParams &params, qint64 now);

// Display name for predictions made with MEPS.
QString displayName();

// Readable name for a stored model id: "meps", an Open-Meteo id, or "meps+<id>" (averaged).
QString modelName(const QString &model);

// The wind sources in the order the app uses them. With MEPS on: inside its area, MEPS
// averaged with the global model (if `average`), then MEPS alone (so predictions go on when
// Open-Meteo's quota is used up), then the global model everywhere.
std::vector<WindBackend> standardBackends(WindSource *meps, std::shared_ptr<WindField> mepsField,
                                          WindSource *global, std::shared_ptr<WindField> globalField,
                                          const QString &globalModel, bool useMeps, bool average);

// A decoded DAP2 binary (.dods) response: each array by name, values in C order.
struct DapData {
    std::map<QString, std::vector<double>> arrays;
    QString error; // empty = success
};
DapData parseDods(const QByteArray &body);

// Model-level data for a block of columns: values indexed [hour][level][row][column], level 0
// at the top, as served.
struct Block {
    int hours = 0;
    int rows = 0;
    int columns = 0;
    std::vector<double> ap, b;                    // hybrid coefficients: p = ap + b · ps (Pa)
    std::vector<double> xWind, yWind;             // m/s along the grid axes
    std::vector<double> temperature, humidity;    // K, kg/kg
    std::vector<double> surfacePressure;          // [hour][row][column], Pa
    std::vector<double> surfaceGeopotential;      // [row][column], m²/s²
};

// One column's winds by geometric altitude, rotated to east/north. Level heights come from
// the hydrostatic equation with virtual temperature, from the surface up. Returns nothing
// for missing or implausible data.
std::vector<LevelWind> columnProfile(const Block &block, int hour, int row, int column, double lon,
                                     double *groundElevation = nullptr);

} // namespace meps

struct MepsOptions {
    QString baseUrl = QStringLiteral("https://thredds.met.no/thredds/dodsC/meps25epsarchive");
    int stride = 2;        // every 2nd grid point: 5 km, plenty for a balloon and a quarter of the data
    int margin = 20;       // nodes fetched around a first request, 100 km: the server's cost is per hour, not per area
    int followUpMargin = 4; // ...and around nodes next to ones already loaded (a later pass along the track)
    int maxBoxNodes = 48;  // larger areas are split into boxes of at most 48 × 48 nodes
    int maxParallel = 3;
    int timeoutMs = 90000;
    int retries = 4;           // after "too many requests" or a temporary server error...
    int retryDelayMs = 3000;   // ...waiting 3, 6, 12, 24 s (or as long as the server asks)
};

class MepsClient : public QObject, public WindSource {
    Q_OBJECT
public:
    explicit MepsClient(QNetworkAccessManager *network, MepsOptions options = {}, QObject *parent = nullptr);

    // The grid a WindField must use with this client.
    std::shared_ptr<const meps::LambertGrid> geometry() const { return m_geometry; }

    // Uses the newest run available at the first hour (or now, for future hours), so past
    // dates get the forecast that was current then. Hours beyond the run's 66 are left out.
    // Loads a generous area around the nodes: the server takes about a second per variable
    // and hour whatever the area, so one request per hour serves the whole flight.
    void fetch(const std::vector<NodeIndex> &nodes, qint64 firstHour, qint64 lastHour,
               std::shared_ptr<WindField> field, Done done) override;

    // Reference time (unix seconds) of the run used by the last fetch; 0 before any.
    qint64 lastRun() const { return m_lastRun; }
    int requestCount() const { return m_requestCount; }

    // URLs, for tests and diagnostics.
    QString runFile(qint64 run) const;     // model levels
    QString surfaceFile(qint64 run) const; // surface fields (surface geopotential)

private:
    struct Job;
    struct Box {
        int x0 = 0, y0 = 0;        // first node
        int columns = 0, rows = 0; // nodes
    };

    void findRun(qint64 candidate, int stepsLeft, std::function<void(qint64 run)> then);
    void startJob(std::shared_ptr<Job> job);
    void fetchHour(std::shared_ptr<Job> job, Box box, int hour);
    void loadGeopotential(qint64 run, int stepsBack);
    void get(const QUrl &url, std::function<void(int status, const QByteArray &body, const QString &error)> then, int attempt = 0);
    void pump();

    QNetworkAccessManager *m_network;
    MepsOptions m_options;
    std::shared_ptr<const meps::LambertGrid> m_geometry;
    std::set<qint64> m_availableRuns;
    std::map<qint64, qint64> m_missingRuns;        // run → when it was found missing (ms)
    std::vector<double> m_geopotential;            // surface, whole area at the stride: [node y][node x]
    std::vector<std::function<void(const QString &error)>> m_geopotentialWaiting;
    std::vector<std::function<void()>> m_queue;    // requests waiting for a free slot
    int m_active = 0;
    qint64 m_lastRun = 0;
    int m_requestCount = 0;
};

} // namespace sp
