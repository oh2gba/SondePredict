#include "batch.h"
#include "testfields.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

namespace {

// 2026-10-22 17:00 UTC
const qint64 kNow = QDateTime(QDate(2026, 10, 22), QTime(17, 0), QTimeZone::UTC).toSecsSinceEpoch();

sp::AppSettings settings()
{
    sp::AppSettings s;
    s.locations << sp::Location{QStringLiteral("Jokioinen"), {60.814, 23.499}, 104.0, 30.0}
                << sp::Location{QStringLiteral("Home"), {60.2055, 24.6559}, 25.0, 40.0};
    s.profile.ascentRate = 5.7;
    return s;
}

QString utc(qint64 t)
{
    return QDateTime::fromSecsSinceEpoch(t, QTimeZone::UTC).toString(QStringLiteral("MM-dd HH:mm"));
}

} // namespace

class TestBatch : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<sp::Prediction>();
        qRegisterMetaType<sp::BatchJob>();
    }

    void plansDaysTimesHours()
    {
        const sp::ScheduleRule rule{QStringLiteral("Jokioinen"), QStringLiteral("Home"), {1, 0, 2}, {23, 11}, 15, 0, false};
        const sp::BatchPlan plan = sp::planBatch(rule, settings(), kNow);
        QVERIFY(plan.error.isEmpty());
        QCOMPARE(plan.jobs.size(), std::size_t(6));
        QCOMPARE(utc(plan.jobs[0].params.launchTime), QStringLiteral("10-22 11:15")); // today, even if past (as v3)
        QCOMPARE(utc(plan.jobs[1].params.launchTime), QStringLiteral("10-22 23:15"));
        QCOMPARE(utc(plan.jobs[5].params.launchTime), QStringLiteral("10-24 23:15"));
        QCOMPARE(plan.jobs[0].siteName, QStringLiteral("Jokioinen"));
        QCOMPARE(plan.jobs[0].homeName, QStringLiteral("Home"));
        QVERIFY(plan.jobs[0].home);
        QCOMPARE(*plan.jobs[0].params.siteAltitude, 104.0);
        QCOMPARE(plan.jobs[0].params.profile.ascentRate, 5.7);
    }

    void skipsBeyondForecastRange()
    {
        const sp::ScheduleRule rule{QStringLiteral("Jokioinen"), QString(), {0, 14, 15, 20}, {11}, 0, 0, false};
        const sp::BatchPlan plan = sp::planBatch(rule, settings(), kNow);
        QCOMPARE(plan.jobs.size(), std::size_t(3)); // day 15 at 11:00 is 14 d 18 h ahead: kept
        QCOMPARE(plan.skipped, 1);                  // day 20 is not
        QVERIFY(!plan.jobs[0].home);
    }

    void reportsUnusableRules()
    {
        QVERIFY(!sp::planBatch({QStringLiteral("Nowhere"), {}, {0}, {11}, 0, 0, false}, settings(), kNow).error.isEmpty());
        QVERIFY(!sp::planBatch({QStringLiteral("Jokioinen"), {}, {}, {11}, 0, 0, false}, settings(), kNow).error.isEmpty());
        QVERIFY(!sp::planBatch({QStringLiteral("Jokioinen"), {}, {0}, {}, 0, 0, false}, settings(), kNow).error.isEmpty());
    }

    void runsAllJobsSharingTheCache()
    {
        sptest::FakeSource source(sptest::uniform(8.0, 2.0));
        auto field = std::make_shared<sp::WindField>(0.25);
        sp::BatchRunner runner(&source, field);
        QSignalSpy started(&runner, &sp::BatchRunner::jobStarted);
        QSignalSpy done(&runner, &sp::BatchRunner::jobFinished);
        QSignalSpy finished(&runner, &sp::BatchRunner::finished);

        const sp::ScheduleRule rule{QStringLiteral("Jokioinen"), QStringLiteral("Home"), {0}, {11, 12, 13}, 15, 0, false};
        runner.start(sp::planBatch(rule, settings(), kNow).jobs);
        QVERIFY(finished.wait(10000));
        QCOMPARE(started.count(), 3);
        QCOMPARE(done.count(), 3);
        QCOMPARE(finished.first().at(0).toInt(), 3);
        QCOMPARE(finished.first().at(1).toInt(), 0);
        QVERIFY(!runner.isRunning());

        // The first job fetched the hours of all three launches, so the others download nothing.
        const auto first = done.at(0).at(2).value<sp::Prediction>();
        const auto third = done.at(2).at(2).value<sp::Prediction>();
        QVERIFY(first.nodesFetched > 0);
        QCOMPARE(third.nodesFetched, 0);
        QCOMPARE(done.at(1).at(2).value<sp::Prediction>().nodesFetched, 0);
    }

    void continuesAfterAFailure()
    {
        // Wind data ends 4 h after the first launch: the 11:15 flight works, 23:15 fails.
        const sp::ScheduleRule rule{QStringLiteral("Jokioinen"), QString(), {0}, {11, 23}, 15, 0, false};
        const auto jobs = sp::planBatch(rule, settings(), kNow).jobs;
        sptest::FakeSource source(sptest::uniform(8.0, 2.0), 0.0, jobs[0].params.launchTime / 3600 + 4);
        sp::BatchRunner runner(&source, std::make_shared<sp::WindField>(0.25));
        QSignalSpy failed(&runner, &sp::BatchRunner::jobFailed);
        QSignalSpy finished(&runner, &sp::BatchRunner::finished);
        runner.start(jobs);
        QVERIFY(finished.wait(10000));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().at(0).toInt(), 1);
        QCOMPARE(finished.first().at(0).toInt(), 1);
        QCOMPARE(finished.first().at(1).toInt(), 1);
    }

    void cancelStops()
    {
        sptest::FakeSource source(sptest::uniform(8.0, 2.0));
        sp::BatchRunner runner(&source, std::make_shared<sp::WindField>(0.25));
        QSignalSpy finished(&runner, &sp::BatchRunner::finished);
        QSignalSpy done(&runner, &sp::BatchRunner::jobFinished);
        runner.start(sp::planBatch({QStringLiteral("Jokioinen"), {}, {0, 1}, {11, 23}, 0, 0, false}, settings(), kNow).jobs);
        runner.cancel();
        QTest::qWait(300);
        QCOMPARE(done.count(), 0);
        QCOMPARE(finished.count(), 0);
        QVERIFY(!runner.isRunning());
    }
};

QTEST_GUILESS_MAIN(TestBatch)
#include "tst_batch.moc"
