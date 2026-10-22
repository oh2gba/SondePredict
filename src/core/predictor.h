#pragma once

#include "simulator.h"
#include "windsource.h"

#include <QObject>

#include <memory>

namespace sp {

struct Prediction {
    LaunchParams params;
    SimulationResult result;
    int passes = 0;       // simulate/fetch rounds needed
    int nodesFetched = 0; // grid nodes downloaded for this prediction
};

// Runs a prediction: fetches the wind data the flight needs, re-flying the track until
// no data is missing. Reuses one WindField so several predictions share downloaded data.
class Predictor : public QObject {
    Q_OBJECT
public:
    Predictor(WindSource *source, std::shared_ptr<WindField> field, QObject *parent = nullptr);

    // Starts a prediction; finished() or failed() is always emitted later, never from here.
    // `alsoCoverUntil` (unix seconds) widens the forecast hours fetched so that later
    // launches up to that time find their data cached; Open-Meteo charges per location and
    // variable, so the extra hours cost nothing.
    void start(const LaunchParams &params, qint64 alsoCoverUntil = 0);
    void cancel();
    bool isRunning() const { return m_running; }

    static constexpr int kMaxPasses = 8;

signals:
    void progress(const QString &message);
    void finished(const sp::Prediction &prediction);
    void failed(const QString &error);

private:
    void fetch(std::vector<NodeIndex> nodes, qint64 firstHour, qint64 lastHour);
    void pass();
    void fail(const QString &error);

    WindSource *m_source;
    std::shared_ptr<WindField> m_field;
    Prediction m_current;
    std::set<NodeKey> m_requested; // everything asked for during this prediction
    qint64 m_firstHour = 0;
    qint64 m_lastHour = 0;
    int m_generation = 0;
    bool m_running = false;
};

} // namespace sp
