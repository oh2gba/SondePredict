#include "geo.h"

#include <QTest>

class TestGeo : public QObject {
    Q_OBJECT
private slots:
    void vincentyReferenceCase()
    {
        // Vincenty's own test line: Flinders Peak → Buninyong.
        const sp::GeoPoint flinders{-(37 + 57 / 60.0 + 3.72030 / 3600), 144 + 25 / 60.0 + 29.52440 / 3600};
        const sp::GeoPoint buninyong{-(37 + 39 / 60.0 + 10.15610 / 3600), 143 + 55 / 60.0 + 35.38390 / 3600};
        const sp::Geodesic g = sp::inverse(flinders, buninyong);
        QVERIFY(qAbs(g.distance - 54972.271) < 0.01);
        QVERIFY(qAbs(g.initialBearing - (306 + 52 / 60.0 + 5.37 / 3600)) < 1e-4);
    }

    void coincidentPoints()
    {
        const sp::Geodesic g = sp::inverse({60.8, 23.5}, {60.8, 23.5});
        QCOMPARE(g.distance, 0.0);
    }

    void offsetRoundTrip()
    {
        const sp::GeoPoint start{60.8, 23.5};
        const sp::GeoPoint north = sp::offset(start, 1000.0, 0.0);
        const sp::Geodesic gn = sp::inverse(start, north);
        QVERIFY(qAbs(gn.distance - 1000.0) < 0.01);
        QVERIFY(qAbs(gn.initialBearing) < 1e-6 || qAbs(gn.initialBearing - 360.0) < 1e-6);

        const sp::GeoPoint east = sp::offset(start, 0.0, 1000.0);
        const sp::Geodesic ge = sp::inverse(start, east);
        QVERIFY(qAbs(ge.distance - 1000.0) < 0.01);
        QVERIFY(qAbs(ge.initialBearing - 90.0) < 0.01);
    }

    void offsetAcrossDateLine()
    {
        const sp::GeoPoint p = sp::offset({0.0, 179.999}, 0.0, 1000.0);
        QVERIFY(p.lon < -179.99);
    }

    void normalizeLon()
    {
        QCOMPARE(sp::normalizeLon(190.0), -170.0);
        QCOMPARE(sp::normalizeLon(-190.0), 170.0);
        QCOMPARE(sp::normalizeLon(180.0), -180.0);
        QCOMPARE(sp::normalizeLon(23.5), 23.5);
    }
};

QTEST_GUILESS_MAIN(TestGeo)
#include "tst_geo.moc"
