#pragma once

#include "predictor.h"
#include "settings.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace sp {

// One prediction of a batch (v3 "Start a batch").
struct BatchJob {
    LaunchParams params;
    QString siteName;
    QString homeName;
    std::optional<GeoPoint> home;
};

struct BatchPlan {
    std::vector<BatchJob> jobs; // in launch-time order
    QString error;              // set when the rule cannot be used at all
    int skipped = 0;            // launches beyond the forecast range
};

// Expands a schedule rule into launches: for each day offset (0 = today, UTC) and each hour,
// a launch at hour:minute UTC with the current balloon profile. Launches more than
// `maxDaysAhead` days after `now` (unix seconds) are skipped.
BatchPlan planBatch(const ScheduleRule &rule, const AppSettings &settings, qint64 now, int maxDaysAhead = 15);

// Runs batch jobs one after another through a Predictor sharing one wind cache, so
// launches on the same day reuse downloaded data. A failed job does not stop the batch.
class BatchRunner : public QObject {
    Q_OBJECT
public:
    BatchRunner(WindSource *source, std::shared_ptr<WindField> field, QObject *parent = nullptr);

    void start(std::vector<BatchJob> jobs);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void jobStarted(int index, int total, const sp::BatchJob &job);
    void jobFinished(int index, const sp::BatchJob &job, const sp::Prediction &prediction);
    void jobFailed(int index, const sp::BatchJob &job, const QString &error);
    void progress(const QString &message);
    void finished(int succeeded, int failed);

private:
    void next();

    Predictor m_predictor;
    std::vector<BatchJob> m_jobs;
    std::size_t m_index = 0;
    int m_succeeded = 0;
    int m_failed = 0;
    bool m_running = false;
};

} // namespace sp
