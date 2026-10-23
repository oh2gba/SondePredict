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

// One site launching at each of `hours`:minute.
sp::ScheduleRule rule(const QString &site, const QString &home, QList<int> days, QList<int> hours, int minute = 15)
{
    sp::BatchSite s{site, {}};
    for (int h : hours)
        s.times << QTime(h, minute);
    return sp::ScheduleRule{{s}, home, days, 0, false};
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
        const sp::BatchPlan plan = sp::planBatch(rule(QStringLiteral("Jokioinen"), QStringLiteral("Home"), {1, 0, 2}, {23, 11}), settings(), kNow);
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
        const sp::BatchPlan plan = sp::planBatch(rule(QStringLiteral("Jokioinen"), QString(), {0, 14, 15, 20}, {11}, 0), settings(), kNow);
        QCOMPARE(plan.jobs.size(), std::size_t(3)); // day 15 at 11:00 is 14 d 18 h ahead: kept
        QCOMPARE(plan.skipped, 1);                  // day 20 is not
        QVERIFY(!plan.jobs[0].home);
    }

    void reportsUnusableRules()
    {
        QVERIFY(!sp::planBatch(rule(QStringLiteral("Nowhere"), {}, {0}, {11}), settings(), kNow).error.isEmpty());
        QVERIFY(!sp::planBatch(rule(QStringLiteral("Jokioinen"), {}, {}, {11}), settings(), kNow).error.isEmpty());
        QVERIFY(!sp::planBatch(rule(QStringLiteral("Jokioinen"), {}, {0}, {}), settings(), kNow).error.isEmpty());
        QVERIFY(!sp::planBatch(sp::ScheduleRule{{}, {}, {0}, 0, false}, settings(), kNow).error.isEmpty());
    }

    void plansSeveralSitesWithTheirOwnBalloons()
    {
        sp::AppSettings s = settings();
        sp::SiteBalloon sodankyla;
        sodankyla.ascentRate = 4.6;
        sodankyla.burstAltitude = 28700;
        sodankyla.descentRate = 5.0;
        sodankyla.burstSd = 2200;
        s.locations << sp::Location{QStringLiteral("Sodankylä"), {67.367, 26.629}, 179.0, 30.0, sodankyla};
        const sp::ScheduleRule twoSites{{{QStringLiteral("Jokioinen"), {QTime(5, 30), QTime(11, 30), QTime(23, 30)}},
                                         {QStringLiteral("Sodankylä"), {QTime(11, 15), QTime(23, 15)}},
                                         {QStringLiteral("Gone"), {QTime(12, 0)}}},
                                        QStringLiteral("Home"), {0, 1}, 0, false};
        const sp::BatchPlan plan = sp::planBatch(twoSites, s, kNow);
        QVERIFY(plan.error.isEmpty()); // a site that no longer exists is skipped
        QCOMPARE(plan.jobs.size(), std::size_t(10));
        // In time order across the sites.
        QCOMPARE(utc(plan.jobs[0].params.launchTime), QStringLiteral("10-22 05:30"));
        QCOMPARE(plan.jobs[1].siteName, QStringLiteral("Sodankylä"));
        QCOMPARE(utc(plan.jobs[1].params.launchTime), QStringLiteral("10-22 11:15"));
        QCOMPARE(plan.jobs[2].siteName, QStringLiteral("Jokioinen"));
        // Each site with its balloon: Jokioinen the general one, Sodankylä its own.
        QCOMPARE(plan.jobs[2].params.profile.ascentRate, 5.7);
        QCOMPARE(plan.jobs[1].params.profile.ascentRate, 4.6);
        QCOMPARE(plan.jobs[1].params.profile.burstAltitude, 28700.0);
        QCOMPARE(plan.jobs[1].params.profile.seaLevelDescentRate, 5.0);
        QCOMPARE(plan.jobs[1].spread.burstAltitude, 2200.0);
        QCOMPARE(plan.jobs[2].spread.burstAltitude, s.spread.burstAltitude);
        QCOMPARE(*plan.jobs[1].params.siteAltitude, 179.0);
    }

    void runsAllJobsSharingTheCache()
    {
        sptest::FakeSource source(sptest::uniform(8.0, 2.0));
        auto field = std::make_shared<sp::WindField>(0.25);
        sp::BatchRunner runner(&source, field);
        QSignalSpy started(&runner, &sp::BatchRunner::jobStarted);
        QSignalSpy done(&runner, &sp::BatchRunner::jobFinished);
        QSignalSpy finished(&runner, &sp::BatchRunner::finished);

        runner.start(sp::planBatch(rule(QStringLiteral("Jokioinen"), QStringLiteral("Home"), {0}, {11, 12, 13}), settings(), kNow).jobs);
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
        const auto jobs = sp::planBatch(rule(QStringLiteral("Jokioinen"), QString(), {0}, {11, 23}), settings(), kNow).jobs;
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

    void stopsWhenTheLimitIsReached()
    {
        // The source says "quota used up" after the first job's downloads: the batch stops
        // instead of failing the remaining jobs one by one.
        const auto jobs = sp::planBatch(rule(QStringLiteral("Jokioinen"), QString(), {0, 1, 2}, {11, 23}), settings(), kNow).jobs;
        QCOMPARE(jobs.size(), std::size_t(6));

        sptest::FakeSource counting(sptest::uniform(8.0, 2.0));
        {
            sp::BatchRunner first(&counting, std::make_shared<sp::WindField>(0.25));
            QSignalSpy done(&first, &sp::BatchRunner::finished);
            first.start({jobs[0]});
            QVERIFY(done.wait(10000));
        }

        sptest::FakeSource source(sptest::uniform(8.0, 2.0));
        source.failAfter = counting.fetchCount;
        source.failWithLimit = true;
        sp::BatchRunner runner(&source, std::make_shared<sp::WindField>(0.25));
        QSignalSpy failed(&runner, &sp::BatchRunner::jobFailed);
        QSignalSpy limit(&runner, &sp::BatchRunner::limitReached);
        QSignalSpy finished(&runner, &sp::BatchRunner::finished);
        runner.start(jobs);
        QVERIFY(finished.wait(10000));
        QCOMPARE(limit.count(), 1);
        QCOMPARE(failed.count(), 1);                     // only the job that hit the limit
        const int succeeded = finished.first().at(0).toInt();
        QCOMPARE(limit.first().at(0).toInt(), 6 - succeeded - 1); // the rest were skipped
        QVERIFY(succeeded >= 1);
        QVERIFY(!runner.isRunning());
    }

    void cancelStops()
    {
        sptest::FakeSource source(sptest::uniform(8.0, 2.0));
        sp::BatchRunner runner(&source, std::make_shared<sp::WindField>(0.25));
        QSignalSpy finished(&runner, &sp::BatchRunner::finished);
        QSignalSpy done(&runner, &sp::BatchRunner::jobFinished);
        runner.start(sp::planBatch(rule(QStringLiteral("Jokioinen"), {}, {0, 1}, {11, 23}, 0), settings(), kNow).jobs);
        runner.cancel();
        QTest::qWait(300);
        QCOMPARE(done.count(), 0);
        QCOMPARE(finished.count(), 0);
        QVERIFY(!runner.isRunning());
    }
};

QTEST_GUILESS_MAIN(TestBatch)
#include "tst_batch.moc"
