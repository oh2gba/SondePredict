#include "landingarea.h"
#include "testfields.h"

#include <QTest>

namespace {

constexpr qint64 kLaunch = 1792753200; // 2026-10-23 11:00 UTC

sp::LaunchParams params()
{
    sp::LaunchParams p;
    p.site = {60.81, 23.50};
    p.siteAltitude = 0.0;
    p.launchTime = kLaunch;
    p.profile.ascentRate = 5.0;
    p.profile.burstAltitude = 20000.0;
    p.profile.seaLevelDescentRate = 10.0;
    p.profile.descentExponent = 0.0;
    return p;
}

sp::WindField eastWind()
{
    sp::WindField field(0.25);
    sptest::fill(field, {59.5, 21.0}, {62.0, 28.0}, kLaunch / 3600, kLaunch / 3600 + 5, sptest::uniform(10.0, 0.0));
    return field;
}

} // namespace

class TestLandingArea : public QObject {
    Q_OBJECT
private slots:
    void alongTheWind()
    {
        // In a steady east wind, a longer or shorter flight only moves the landing east or west.
        const sp::WindField field = eastWind();
        sp::FlightSpread spread;
        spread.ascentRate = 0.0;
        spread.descentRate = 0.0;
        spread.burstAltitude = 1500.0;
        const sp::LandingAreaResult r = sp::estimateLandingArea(params(), field, spread);
        QVERIFY(r.area);
        QVERIFY(r.missing.empty());
        QCOMPARE(r.area->samples, spread.samples);
        QVERIFY2(qAbs(r.area->majorBearing - 90.0) < 1.0, qPrintable(QString::number(r.area->majorBearing)));
        QVERIFY(r.area->minorSigma < 50.0);
        // Burst ±1500 m changes the flight by ±(300 + 150) s, i.e. ±4.5 km at 10 m/s.
        QVERIFY2(r.area->majorSigma > 3000.0 && r.area->majorSigma < 6000.0, qPrintable(QString::number(r.area->majorSigma)));
        // The centre is close to the nominal 60 km east.
        const sp::Geodesic g = sp::inverse(params().site, r.area->centre);
        QVERIFY2(qAbs(g.distance - 60000.0) < 2000.0, qPrintable(QString::number(g.distance)));
    }

    void deterministic()
    {
        const sp::WindField field = eastWind();
        const auto a = sp::estimateLandingArea(params(), field, {});
        const auto b = sp::estimateLandingArea(params(), field, {});
        QVERIFY(a.area && b.area);
        QCOMPARE(a.area->majorSigma, b.area->majorSigma);
        QCOMPARE(a.area->centre.lat, b.area->centre.lat);
    }

    void outlineMatchesAxes()
    {
        sp::LandingArea area;
        area.centre = {60.0, 25.0};
        area.majorSigma = 4000.0;
        area.minorSigma = 1000.0;
        area.majorBearing = 45.0;
        const auto outline = area.outline(0.95, 72);
        QCOMPARE(outline.size(), std::size_t(73));
        // Point 0 lies on the major axis (NE), point 18 on the minor axis (SE... or NW).
        const double k = std::sqrt(-2.0 * std::log(0.05));
        const sp::Geodesic g0 = sp::inverse(area.centre, outline[0]);
        QVERIFY(qAbs(g0.distance - 4000.0 * k) < 5.0);
        QVERIFY(qAbs(g0.initialBearing - 45.0) < 0.5);
        const sp::Geodesic g18 = sp::inverse(area.centre, outline[18]);
        QVERIFY(qAbs(g18.distance - 1000.0 * k) < 5.0);
        QCOMPARE(area.majorAxis(), 2.0 * 4000.0 * k);
    }

    void disabledOrTooFew()
    {
        const sp::WindField field = eastWind();
        sp::FlightSpread off;
        off.enabled = false;
        QVERIFY(!sp::estimateLandingArea(params(), field, off).area);
        sp::FlightSpread two;
        two.samples = 2;
        QVERIFY(!sp::estimateLandingArea(params(), field, two).area);
    }

    void reportsMissingWind()
    {
        sp::WindField field(0.25);
        sptest::fill(field, {60.81, 23.50}, {60.81, 23.50}, kLaunch / 3600, kLaunch / 3600 + 5, sptest::uniform(10.0, 0.0));
        const auto r = sp::estimateLandingArea(params(), field, {});
        QVERIFY(!r.missing.empty());
    }
};

QTEST_GUILESS_MAIN(TestLandingArea)
#include "tst_landingarea.moc"
