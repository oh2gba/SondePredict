#include "predictor.h"
#include "testfields.h"

#include <QSignalSpy>
#include <QTest>

namespace {

constexpr qint64 kLaunch = 1792753200; // 2026-10-23 11:00 UTC

sp::LaunchParams launch()
{
    sp::LaunchParams p;
    p.site = {60.81, 23.50};
    p.siteAltitude = 104.0;
    p.launchTime = kLaunch;
    return p; // default profile
}

} // namespace

class TestPredictor : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<sp::Prediction>();
    }

    void fetchesAlongTheTrackUntilComplete()
    {
        // Strong westerly that also varies with altitude, so the track crosses many cells.
        sptest::FakeSource source([](sp::NodeIndex, qint64, double alt) { return sp::LevelWind{alt, 15.0 + alt / 1000.0, 3.0}; });
        auto field = std::make_shared<sp::WindField>(0.25);
        sp::Predictor predictor(&source, field);
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        QSignalSpy failed(&predictor, &sp::Predictor::failed);

        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        QCOMPARE(failed.count(), 0);

        const auto prediction = finished.takeFirst().at(0).value<sp::Prediction>();
        QVERIFY(prediction.result.complete());
        QVERIFY(prediction.passes >= 2);
        QVERIFY(prediction.passes <= 4);
        QVERIFY(prediction.nodesFetched > 4);
        QVERIFY(!predictor.isRunning());
        const double distance = sp::inverse({60.81, 23.50}, prediction.result.landing->position).distance;
        QVERIFY(distance > 100000.0); // drifted well beyond the four starting grid points
    }

    void fallsBackToTheNextSource()
    {
        sptest::FakeSource regional(sptest::uniform(10.0, 0.0));
        regional.failAfter = 0;
        sptest::FakeSource global(sptest::uniform(10.0, 0.0));
        sp::Predictor predictor({{{{&regional, std::make_shared<sp::WindField>(0.1)}}, QStringLiteral("meps"), QStringLiteral("MEPS"), {}},
                                 {{{&global, std::make_shared<sp::WindField>(0.25)}}, QStringLiteral("ecmwf_ifs025"), QStringLiteral("ECMWF"), {}}});
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        QSignalSpy failed(&predictor, &sp::Predictor::failed);

        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        QCOMPARE(failed.count(), 0);
        const auto prediction = finished.takeFirst().at(0).value<sp::Prediction>();
        QCOMPARE(prediction.model, QStringLiteral("ecmwf_ifs025"));
        QVERIFY(prediction.fallbackNote.startsWith(QStringLiteral("MEPS: simulated network error")));
        QVERIFY(prediction.result.complete());
        QCOMPARE(regional.fetchCount, 1);
        QVERIFY(global.fetchCount >= 1);
    }

    void usesTheFirstSourceThatCoversTheLaunch()
    {
        sptest::FakeSource regional(sptest::uniform(10.0, 0.0));
        sptest::FakeSource global(sptest::uniform(10.0, 0.0));
        bool regionalCovers = false;
        sp::Predictor predictor({{{{&regional, std::make_shared<sp::WindField>(0.1)}}, QStringLiteral("meps"), QStringLiteral("MEPS"),
                                  [&regionalCovers](const sp::LaunchParams &) { return regionalCovers; }},
                                 {{{&global, std::make_shared<sp::WindField>(0.25)}}, QStringLiteral("ecmwf_ifs025"), QStringLiteral("ECMWF"), {}}});
        QSignalSpy finished(&predictor, &sp::Predictor::finished);

        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        auto prediction = finished.takeFirst().at(0).value<sp::Prediction>();
        QCOMPARE(prediction.model, QStringLiteral("ecmwf_ifs025"));
        QVERIFY(prediction.fallbackNote.isEmpty());
        QCOMPARE(regional.fetchCount, 0);

        regionalCovers = true;
        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        prediction = finished.takeFirst().at(0).value<sp::Prediction>();
        QCOMPARE(prediction.model, QStringLiteral("meps"));
        QVERIFY(regional.fetchCount >= 1);
    }

    void averagesTheParts()
    {
        sptest::FakeSource east(sptest::uniform(10.0, 0.0));
        sptest::FakeSource north(sptest::uniform(0.0, 10.0));
        sp::Predictor predictor({{{{&east, std::make_shared<sp::WindField>(0.1)}, {&north, std::make_shared<sp::WindField>(0.25)}},
                                  QStringLiteral("a+b"), QStringLiteral("A + B"), {}}});
        predictor.setSpread({});
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        const auto prediction = finished.takeFirst().at(0).value<sp::Prediction>();
        QCOMPARE(prediction.model, QStringLiteral("a+b"));
        QVERIFY(prediction.result.complete());
        // Both fields were fetched along the track, and the flight went north-east.
        QVERIFY(east.fetchCount >= 2 && north.fetchCount >= 2);
        const double bearing = sp::inverse(launch().site, prediction.result.landing->position).initialBearing;
        QVERIFY2(std::abs(bearing - 45.0) < 3.0, qPrintable(QString::number(bearing)));
    }

    void quotaUsedUpFallsBackToTheRegionalModelAlone()
    {
        sptest::FakeSource regional(sptest::uniform(10.0, 0.0));
        sptest::FakeSource global(sptest::uniform(10.0, 0.0));
        global.failAfter = 0;
        global.failWithLimit = true;
        auto regionalField = std::make_shared<sp::WindField>(0.1);
        auto globalField = std::make_shared<sp::WindField>(0.25);
        sp::Predictor predictor({{{{&regional, regionalField}, {&global, globalField}}, QStringLiteral("meps+g"), QStringLiteral("MEPS + G"), {}},
                                 {{{&regional, regionalField}}, QStringLiteral("meps"), QStringLiteral("MEPS"), {}},
                                 {{{&global, globalField}}, QStringLiteral("g"), QStringLiteral("G"), {}}});
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        const auto prediction = finished.takeFirst().at(0).value<sp::Prediction>();
        QCOMPARE(prediction.model, QStringLiteral("meps"));
        QVERIFY(!predictor.limitReached());
    }

    void aFailedSourceIsNotTriedAgain()
    {
        sptest::FakeSource regional(sptest::uniform(10.0, 0.0));
        regional.failAfter = 0;
        sptest::FakeSource global(sptest::uniform(10.0, 0.0));
        auto regionalField = std::make_shared<sp::WindField>(0.1);
        auto globalField = std::make_shared<sp::WindField>(0.25);
        sp::Predictor predictor({{{{&regional, regionalField}, {&global, globalField}}, QStringLiteral("meps+g"), QStringLiteral("MEPS + G"), {}},
                                 {{{&regional, regionalField}}, QStringLiteral("meps"), QStringLiteral("MEPS"), {}},
                                 {{{&global, globalField}}, QStringLiteral("g"), QStringLiteral("G"), {}}});
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.takeFirst().at(0).value<sp::Prediction>().model, QStringLiteral("g"));
        QCOMPARE(regional.fetchCount, 1); // not again for "MEPS alone"
    }

    void batchPrefetchOnlyWhereHoursAreFree()
    {
        for (const bool free : {true, false}) {
            sptest::FakeSource source(sptest::uniform(10.0, 0.0));
            source.hoursAreFree = free;
            sp::Predictor predictor(&source, std::make_shared<sp::WindField>(0.25));
            predictor.setSpread({});
            QSignalSpy finished(&predictor, &sp::Predictor::finished);
            predictor.start(launch(), kLaunch + 12 * 3600);
            QVERIFY(finished.wait(5000));
            // Hours for the launch 12 h later only from a source where they cost nothing.
            QCOMPARE(source.lastHourRequested > kLaunch / 3600 + 12, free);
        }
    }

    void aLongQuotaWaitFallsBackInsteadOfWaiting()
    {
        sptest::FakeSource regional(sptest::uniform(10.0, 0.0));
        sptest::FakeSource global(sptest::uniform(10.0, 0.0));
        global.quotaWaitMs = 40 * 60 * 1000; // this hour's free calls used up
        auto regionalField = std::make_shared<sp::WindField>(0.1);
        auto globalField = std::make_shared<sp::WindField>(0.25);
        bool regionalCovers = true;
        sp::Predictor predictor({{{{&regional, regionalField}, {&global, globalField}}, QStringLiteral("meps+g"), QStringLiteral("MEPS + G"),
                                  [&](const sp::LaunchParams &) { return regionalCovers; }},
                                 {{{&regional, regionalField}}, QStringLiteral("meps"), QStringLiteral("MEPS"),
                                  [&](const sp::LaunchParams &) { return regionalCovers; }},
                                 {{{&global, globalField}}, QStringLiteral("g"), QStringLiteral("G"), {}}});
        predictor.setSpread({});
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.takeFirst().at(0).value<sp::Prediction>().model, QStringLiteral("meps"));
        QCOMPARE(global.refusedWaits, 1);

        // Beyond the regional model nothing else can serve it: then the global one waits.
        regionalCovers = false;
        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        QCOMPARE(finished.takeFirst().at(0).value<sp::Prediction>().model, QStringLiteral("g"));
        QCOMPARE(global.maxWaitMs, qint64(-1));
    }

    void inputErrorsDoNotFallBack()
    {
        sptest::FakeSource regional(sptest::uniform(10.0, 0.0));
        sptest::FakeSource global(sptest::uniform(10.0, 0.0));
        sp::Predictor predictor({{{{&regional, std::make_shared<sp::WindField>(0.1)}}, QStringLiteral("meps"), QStringLiteral("MEPS"), {}},
                                 {{{&global, std::make_shared<sp::WindField>(0.25)}}, QStringLiteral("ecmwf_ifs025"), QStringLiteral("ECMWF"), {}}});
        QSignalSpy failed(&predictor, &sp::Predictor::failed);
        sp::LaunchParams p = launch();
        p.profile.ascentRate = -1.0;
        predictor.start(p);
        QVERIFY(failed.wait(5000));
        QCOMPARE(regional.fetchCount + global.fetchCount, 0);
    }

    void reusesCachedData()
    {
        sptest::FakeSource source(sptest::uniform(10.0, 0.0));
        auto field = std::make_shared<sp::WindField>(0.25);
        sp::Predictor predictor(&source, field);
        QSignalSpy finished(&predictor, &sp::Predictor::finished);

        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        const int fetches = source.fetchCount;

        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        QCOMPARE(source.fetchCount, fetches); // second run needed no downloads
        QCOMPARE(finished.last().at(0).value<sp::Prediction>().nodesFetched, 0);
    }

    void failsBeyondForecastRange()
    {
        // Data only for the first hour after launch; the flight takes ~2.5 h.
        sptest::FakeSource source(sptest::uniform(5.0, 0.0), 0.0, kLaunch / 3600);
        auto field = std::make_shared<sp::WindField>(0.25);
        sp::Predictor predictor(&source, field);
        QSignalSpy failed(&predictor, &sp::Predictor::failed);

        predictor.start(launch());
        QVERIFY(failed.wait(5000));
        QVERIFY(failed.takeFirst().at(0).toString().contains(QStringLiteral("forecast range")));
        QVERIFY(!predictor.isRunning());
    }

    void landingAreaFetchFailureKeepsPrediction()
    {
        const auto wind = [](sp::NodeIndex, qint64, double alt) { return sp::LevelWind{alt, 20.0 + alt / 1000.0, 5.0}; };
        sp::FlightSpread wide;
        wide.burstAltitude = 5000.0; // varied flights reach well beyond the nominal track

        // How many fetches does the prediction itself need?
        sptest::FakeSource counting(wind);
        sp::Predictor first(&counting, std::make_shared<sp::WindField>(0.25));
        first.setSpread({false});
        QSignalSpy firstDone(&first, &sp::Predictor::finished);
        first.start(launch());
        QVERIFY(firstDone.wait(5000));

        // Same again with the landing area on, and every fetch after those failing.
        sptest::FakeSource failing(wind);
        failing.failAfter = counting.fetchCount;
        sp::Predictor predictor(&failing, std::make_shared<sp::WindField>(0.25));
        predictor.setSpread(wide);
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        QSignalSpy failed(&predictor, &sp::Predictor::failed);
        predictor.start(launch());
        QVERIFY(finished.wait(5000));
        QCOMPARE(failed.count(), 0);
        QVERIFY(failing.fetchCount > counting.fetchCount); // the area stage did try to fetch
        const auto prediction = finished.first().at(0).value<sp::Prediction>();
        QVERIFY(prediction.result.landing);
        QVERIFY(prediction.landingArea); // estimated from the winds already loaded
    }

    void cancelSuppressesResult()
    {
        sptest::FakeSource source(sptest::uniform(5.0, 0.0));
        sp::Predictor predictor(&source, std::make_shared<sp::WindField>(0.25));
        QSignalSpy finished(&predictor, &sp::Predictor::finished);
        predictor.start(launch());
        predictor.cancel();
        QTest::qWait(200);
        QCOMPARE(finished.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestPredictor)
#include "tst_predictor.moc"
