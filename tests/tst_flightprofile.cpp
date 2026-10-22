#include "flightprofile.h"
#include "simulator.h"

#include <QTest>

class TestFlightProfile : public QObject {
    Q_OBJECT
private slots:
    void densityScaledDescent()
    {
        sp::FlightProfile p;
        p.seaLevelDescentRate = 12.0;
        p.descentExponent = 0.24;
        QVERIFY(qAbs(p.descentRate(0.0) - 12.0) < 1e-3);
        // Matches the 2019 Jokioinen flight within its scatter (≈ 26 m/s at 22–27 km).
        QVERIFY(p.descentRate(25000.0) > 23.0 && p.descentRate(25000.0) < 29.0);

        p.descentExponent = 0.0;
        QCOMPARE(p.descentRate(20000.0), 12.0);
    }

    void legacyDescentMatchesV3()
    {
        sp::FlightProfile p;
        p.descentModel = sp::DescentModel::Legacy;
        p.legacyDescentRate = 40.0;
        p.legacyDescentGain = 1.0;
        QCOMPARE(p.descentRate(0.0), 40.0);
        QCOMPARE(p.descentRate(10000.0), 50.0);
    }

    void estimatedFlightTime()
    {
        sp::FlightProfile p;
        p.ascentRate = 5.0;
        p.burstAltitude = 20000.0;
        p.seaLevelDescentRate = 10.0;
        p.descentExponent = 0.0;
        QVERIFY(qAbs(sp::estimatedFlightTime(p, 0.0) - 6000.0) < 1e-6);
        QVERIFY(qAbs(sp::estimatedFlightTime(p, 1000.0) - (3800.0 + 1900.0)) < 1e-6);
    }

    void validation()
    {
        sp::FlightProfile p;
        QCOMPARE(QString::fromUtf8(p.validationError()), QString());
        p.ascentRate = 0.0;
        QVERIFY(*p.validationError());
        p = {};
        p.burstAltitude = 70000.0;
        QVERIFY(*p.validationError());
        p = {};
        p.descentExponent = 2.0;
        QVERIFY(*p.validationError());
    }
};

QTEST_GUILESS_MAIN(TestFlightProfile)
#include "tst_flightprofile.moc"
