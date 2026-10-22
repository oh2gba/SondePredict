#include "batch.h"

#include <QDateTime>
#include <QTimeZone>

#include <algorithm>

namespace sp {

BatchPlan planBatch(const ScheduleRule &rule, const AppSettings &settings, qint64 now, int maxDaysAhead)
{
    BatchPlan plan;
    const Location *site = settings.location(rule.siteName);
    if (!site) {
        plan.error = QStringLiteral("the batch launch location \"%1\" does not exist").arg(rule.siteName);
        return plan;
    }
    if (rule.days.isEmpty() || rule.hours.isEmpty()) {
        plan.error = QStringLiteral("the batch has no days or no hours selected");
        return plan;
    }
    const Location *home = settings.location(rule.homeName);

    const QDate today = QDateTime::fromSecsSinceEpoch(now, QTimeZone::UTC).date();
    const qint64 limit = now + qint64(maxDaysAhead) * 86400;
    for (int day : rule.days) {
        for (int hour : rule.hours) {
            const QDateTime launch(today.addDays(day), QTime(hour, std::clamp(rule.minute, 0, 59)), QTimeZone::UTC);
            if (!launch.isValid())
                continue;
            if (launch.toSecsSinceEpoch() > limit) {
                ++plan.skipped;
                continue;
            }
            BatchJob job;
            job.params.site = site->position;
            job.params.siteAltitude = site->altitude;
            job.params.launchTime = launch.toSecsSinceEpoch();
            job.params.profile = settings.profile;
            job.siteName = site->name;
            if (home) {
                job.homeName = home->name;
                job.home = home->position;
            }
            plan.jobs.push_back(job);
        }
    }
    std::sort(plan.jobs.begin(), plan.jobs.end(),
              [](const BatchJob &a, const BatchJob &b) { return a.params.launchTime < b.params.launchTime; });
    plan.jobs.erase(std::unique(plan.jobs.begin(), plan.jobs.end(),
                                [](const BatchJob &a, const BatchJob &b) { return a.params.launchTime == b.params.launchTime; }),
                    plan.jobs.end());
    return plan;
}

BatchRunner::BatchRunner(WindSource *source, std::shared_ptr<WindField> field, QObject *parent)
    : QObject(parent)
    , m_predictor(source, std::move(field))
{
    connect(&m_predictor, &Predictor::progress, this, &BatchRunner::progress);
    connect(&m_predictor, &Predictor::finished, this, [this](const Prediction &prediction) {
        ++m_succeeded;
        emit jobFinished(int(m_index), m_jobs[m_index], prediction);
        ++m_index;
        next();
    });
    connect(&m_predictor, &Predictor::failed, this, [this](const QString &error) {
        ++m_failed;
        emit jobFailed(int(m_index), m_jobs[m_index], error);
        ++m_index;
        next();
    });
}

void BatchRunner::start(std::vector<BatchJob> jobs)
{
    m_predictor.cancel();
    m_jobs = std::move(jobs);
    m_index = 0;
    m_succeeded = 0;
    m_failed = 0;
    m_running = true;
    next();
}

void BatchRunner::cancel()
{
    m_predictor.cancel();
    m_running = false;
}

void BatchRunner::next()
{
    if (!m_running)
        return;
    if (m_index >= m_jobs.size()) {
        m_running = false;
        emit finished(m_succeeded, m_failed);
        return;
    }
    emit jobStarted(int(m_index), int(m_jobs.size()), m_jobs[m_index]);

    // Fetch enough forecast hours for the launches of the next 12 hours too (jobs are sorted).
    const qint64 launch = m_jobs[m_index].params.launchTime;
    qint64 coverUntil = launch;
    for (std::size_t i = m_index + 1; i < m_jobs.size() && m_jobs[i].params.launchTime <= launch + 12 * 3600; ++i)
        coverUntil = m_jobs[i].params.launchTime;
    m_predictor.start(m_jobs[m_index].params, coverUntil);
}

} // namespace sp
