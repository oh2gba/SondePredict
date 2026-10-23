#include "batch.h"

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <tuple>

namespace sp {

BatchPlan planBatch(const ScheduleRule &rule, const AppSettings &settings, qint64 now, int maxDaysAhead)
{
    BatchPlan plan;
    if (rule.sites.isEmpty() || rule.days.isEmpty()) {
        plan.error = QStringLiteral("the batch has no launch sites or no days selected");
        return plan;
    }
    const Location *home = settings.location(rule.homeName);
    const QDate today = QDateTime::fromSecsSinceEpoch(now, QTimeZone::UTC).date();
    const qint64 limit = now + qint64(maxDaysAhead) * 86400;
    QStringList missing;
    for (const BatchSite &batchSite : rule.sites) {
        const Location *site = settings.location(batchSite.siteName);
        if (!site) {
            missing << batchSite.siteName;
            continue;
        }
        for (int day : rule.days) {
            for (const QTime &time : batchSite.times) {
                const QDateTime launch(today.addDays(day), time, QTimeZone::UTC);
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
                job.params.profile = settings.profileFor(site);
                job.spread = settings.spreadFor(site);
                job.siteName = site->name;
                if (home) {
                    job.homeName = home->name;
                    job.home = home->position;
                }
                plan.jobs.push_back(job);
            }
        }
    }
    if (plan.jobs.empty() && !missing.isEmpty()) {
        plan.error = QStringLiteral("the batch launch location \"%1\" does not exist").arg(missing.join(QStringLiteral("\", \"")));
        return plan;
    }
    if (plan.jobs.empty() && plan.skipped == 0) {
        plan.error = QStringLiteral("the batch's launch sites have no launch times");
        return plan;
    }
    std::sort(plan.jobs.begin(), plan.jobs.end(), [](const BatchJob &a, const BatchJob &b) {
        return std::tie(a.params.launchTime, a.siteName) < std::tie(b.params.launchTime, b.siteName);
    });
    plan.jobs.erase(std::unique(plan.jobs.begin(), plan.jobs.end(),
                                [](const BatchJob &a, const BatchJob &b) {
                                    return a.params.launchTime == b.params.launchTime && a.siteName == b.siteName;
                                }),
                    plan.jobs.end());
    return plan;
}

BatchRunner::BatchRunner(WindSource *source, std::shared_ptr<WindField> field, QObject *parent)
    : BatchRunner(std::vector<WindBackend>{WindBackend{{{source, std::move(field)}}, {}, {}, {}}}, parent)
{
}

BatchRunner::BatchRunner(std::vector<WindBackend> backends, QObject *parent)
    : QObject(parent)
    , m_predictor(std::move(backends))
{
    connect(&m_predictor, &Predictor::progress, this, &BatchRunner::progress);
    connect(&m_predictor, &Predictor::activity, this, &BatchRunner::activity);
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
        if (m_predictor.limitReached()) {
            // The other launches would fail the same way; stop here.
            const int skipped = int(m_jobs.size() - m_index);
            m_index = m_jobs.size();
            emit limitReached(skipped, error);
        }
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
    FlightSpread spread = m_jobs[m_index].spread;
    spread.enabled = spread.enabled && m_areaEnabled;
    m_predictor.setSpread(spread);
    m_predictor.start(m_jobs[m_index].params, coverUntil);
}

} // namespace sp
