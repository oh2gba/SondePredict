#include "simulator.h"
#include "testfields.h"

#include <QTest>

namespace {

constexpr qint64 kLaunch = 1792753200; // 2026-10-23 11:00 UTC
constexpr qint64 kHour = kLaunch / 3600;

sp::LaunchParams constantRates(double siteAltitude = 0.0)
{
    sp::LaunchParams p;
    p.site = {60.81, 23.50};
    p.siteAltitude = siteAltitude;
    p.launchTime = kLaunch;
    p.profile.ascentRate = 5.0;
    p.profile.burstAltitude = 20000.0;
    p.profile.seaLevelDescentRate = 10.0;
    p.profile.descentExponent = 0.0; // constant 10 m/s descent
    return p;
}

} // namespace

class TestSimulator : public QObject {
    Q_OBJECT
private slots:
    void uniformEastWind()
    {
        sp::WindField field(0.25);
        sptest::fill(field, {60.0, 22.0}, {61.5, 26.0}, kHour, kHour + 3, sptest::uniform(10.0, 0.0));

        const sp::SimulationResult r = sp::simulate(constantRates(), field);
        QVERIFY2(r.complete(), qPrintable(r.error));
        QVERIFY(r.burst);
        QVERIFY(qAbs(r.burst->time - kLaunch - 4000.0) <= 1.0);
        QCOMPARE(r.burst->altitude, 20000.0);
        QVERIFY(qAbs(r.landing->time - kLaunch - 6000.0) <= 1.0);
        QCOMPARE(r.landing->altitude, 0.0);

        // 6000 s at 10 m/s due east: 60 km along the parallel.
        QVERIFY(qAbs(r.landing->position.lat - 60.81) < 1e-6);
        const double distance = sp::inverse({60.81, 23.50}, r.landing->position).distance;
        QVERIFY2(qAbs(distance - 60000.0) < 15.0, qPrintable(QString::number(distance)));
    }

    void landsOnTerrain()
    {
        sp::WindField field(0.25);
        sptest::fill(field, {60.0, 22.0}, {61.5, 26.0}, kHour, kHour + 3, sptest::uniform(0.0, 5.0), 300.0);

        sp::LaunchParams p = constantRates();
        p.siteAltitude.reset(); // start from the ground elevation
        const sp::SimulationResult r = sp::simulate(p, field);
        QVERIFY2(r.complete(), qPrintable(r.error));
        QCOMPARE(r.track.front().altitude, 300.0);
        QCOMPARE(r.landing->altitude, 300.0);
        // Ascent (20000-300)/5 = 3940 s, descent (20000-300)/10 = 1970 s.
        QVERIFY(qAbs(r.landing->time - kLaunch - 5910.0) <= 1.0);
        QVERIFY(r.landing->position.lat > 60.81);
    }

    void recordsTrackRegularly()
    {
        sp::WindField field(0.25);
        sptest::fill(field, {60.0, 22.0}, {61.5, 26.0}, kHour, kHour + 3, sptest::uniform(1.0, 1.0));
        const sp::SimulationResult r = sp::simulate(constantRates(), field);
        QVERIFY(r.track.size() > 590 && r.track.size() < 610); // ~6000 s every 10 s
        for (std::size_t i = 1; i < r.track.size(); ++i)
            QVERIFY(r.track[i].time > r.track[i - 1].time);
    }

    void reportsMissingData()
    {
        sp::WindField field(0.25);
        // Only the launch cell: the 60 km drift leaves the loaded area.
        sptest::fill(field, {60.81, 23.50}, {60.81, 23.50}, kHour, kHour + 3, sptest::uniform(10.0, 0.0));
        const sp::SimulationResult r = sp::simulate(constantRates(), field);
        QVERIFY(r.landing); // still flown with borrowed data
        QVERIFY(!r.missing.empty());
        QVERIFY(!r.complete());
    }

    void flagsFlightAboveTopLevel()
    {
        sp::WindField field(0.25);
        sptest::fill(field, {60.0, 22.0}, {61.5, 26.0}, kHour, kHour + 4, sptest::uniform(1.0, 0.0));
        sp::LaunchParams p = constantRates();
        p.profile.burstAltitude = 35000.0; // test profiles stop at 32 km
        QVERIFY(sp::simulate(p, field).aboveTopLevel);
    }

    void rejectsInvalidProfile()
    {
        sp::WindField field(0.25);
        sptest::fill(field, {60.0, 22.0}, {61.5, 26.0}, kHour, kHour + 3, sptest::uniform(1.0, 0.0));
        sp::LaunchParams p = constantRates();
        p.profile.ascentRate = -1.0;
        QVERIFY(!sp::simulate(p, field).error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestSimulator)
#include "tst_simulator.moc"
