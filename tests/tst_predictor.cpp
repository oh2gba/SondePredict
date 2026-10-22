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
        QVERIFY(prediction.nodesFetched > 16);
        QVERIFY(!predictor.isRunning());
        const double distance = sp::inverse({60.81, 23.50}, prediction.result.landing->position).distance;
        QVERIFY(distance > 100000.0); // drifted well beyond the initial 4×4 block
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
