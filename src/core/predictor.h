#pragma once

#include "landingarea.h"
#include "simulator.h"
#include "windsource.h"

#include <QObject>

#include <functional>
#include <memory>
#include <vector>

namespace sp {

struct Prediction {
    LaunchParams params;
    SimulationResult result;
    int passes = 0;       // simulate/fetch rounds needed
    int nodesFetched = 0; // grid nodes downloaded for this prediction
    std::optional<LandingArea> landingArea;
    QString model;        // the wind model used (WindBackend::model)
    QString fallbackNote; // why a preferred wind model was not used, if it was not
};

// One or more wind sources, each with its own grid; with several, their winds are averaged
// (put the finest grid first: it gives the ground elevation). A predictor tries its backends
// in order and uses the first that covers the launch; if that one fails, the next.
struct WindBackend {
    struct Part {
        WindSource *source = nullptr;
        std::shared_ptr<WindField> field;
    };
    std::vector<Part> parts;
    QString model; // stored with predictions, e.g. "meps", "ecmwf_ifs025" or "meps+ecmwf_ifs025"
    QString name;  // for messages, e.g. "MEPS"
    // Whether to use this backend for a launch; empty = everywhere.
    std::function<bool(const LaunchParams &)> covers;
};

// Runs a prediction: fetches the wind data the flight needs, re-flying the track until
// no data is missing. Reuses one WindField so several predictions share downloaded data.
class Predictor : public QObject {
    Q_OBJECT
public:
    Predictor(WindSource *source, std::shared_ptr<WindField> field, QObject *parent = nullptr);
    explicit Predictor(std::vector<WindBackend> backends, QObject *parent = nullptr);

    // Starts a prediction; finished() or failed() is always emitted later, never from here.
    // `alsoCoverUntil` (unix seconds) widens the forecast hours fetched from sources where
    // extra hours are free (Open-Meteo charges per location and variable), so that later
    // launches up to that time find their data cached.
    void start(const LaunchParams &params, qint64 alsoCoverUntil = 0);
    void cancel();
    bool isRunning() const { return m_running; }

    // Variation used to estimate the landing area after each prediction (disable to skip).
    void setSpread(const FlightSpread &spread) { m_spread = spread; }
    // A fixed forecast wind error for the landing area instead of the one from the lead time
    // (validation; 0 = none).
    void setWindSigmaOverride(std::optional<double> sigma) { m_windSigmaOverride = sigma; }

    // Whether the last failure was the wind source's hourly or daily limit.
    bool limitReached() const { return m_limitReached; }

    static constexpr int kMaxPasses = 8;

signals:
    void progress(const QString &message);  // short, for a status bar
    void activity(const QString &line);     // more detail, for an activity log
    void finished(const sp::Prediction &prediction);
    void failed(const QString &error);

private:
    void begin();
    // Fetches what is not loaded yet; keys' layer says which part they belong to.
    void fetch(const std::set<NodeKey> &keys);
    void afterFetch();
    void pass();
    void estimateAreaAndFinish();
    void fail(const QString &error, bool tryNextSource = true);

    const WindSampler &sampler() const;
    bool usable(const WindBackend &backend) const;

    std::vector<WindBackend> m_backends;
    std::size_t m_backend = 0;               // in use
    std::vector<WindBackend::Part> m_parts;  // of the backend in use
    std::unique_ptr<BlendedWind> m_blend;    // when it has several parts
    int m_pendingFetches = 0;
    FetchResult m_fetchResult;              // first failure of the fetches in flight
    WindSource *m_failedSource = nullptr;   // ...and whose it was
    std::set<WindSource *> m_failedSources; // during this prediction
    Prediction m_current;
    qint64 m_alsoCoverUntil = 0;
    qint64 m_coverLastHour = 0; // last hour to load from sources where extra hours are free
    qint64 m_startedMs = 0;
    std::set<NodeKey> m_requested; // everything asked for during this prediction (with layer)
    qint64 m_firstHour = 0;
    qint64 m_lastHour = 0;
    int m_generation = 0;
    bool m_running = false;
    bool m_areaStage = false; // fetching extra winds for the landing area
    bool m_limitReached = false;
    FlightSpread m_spread;
    std::optional<double> m_windSigmaOverride;
};

} // namespace sp
