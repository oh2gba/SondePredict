#include "atmosphere.h"

#include <QTest>

using namespace sp::atmosphere;

class TestAtmosphere : public QObject {
    Q_OBJECT
private slots:
    void densityTable_data()
    {
        QTest::addColumn<double>("altitude");
        QTest::addColumn<double>("expected");
        // US Standard Atmosphere 1976, geometric altitude.
        QTest::newRow("sea level") << 0.0 << 1.2250;
        QTest::newRow("5 km") << 5000.0 << 0.73612;
        QTest::newRow("11 km") << 11000.0 << 0.36392;
        QTest::newRow("20 km") << 20000.0 << 0.088910;
        QTest::newRow("30 km") << 30000.0 << 0.018410;
        QTest::newRow("40 km") << 40000.0 << 0.0039957;
    }

    void densityTable()
    {
        QFETCH(double, altitude);
        QFETCH(double, expected);
        const double rho = density(altitude);
        QVERIFY2(qAbs(rho - expected) / expected < 0.003,
                 qPrintable(QStringLiteral("density(%1) = %2, expected %3").arg(altitude).arg(rho).arg(expected)));
    }

    void densityDecreasesWithAltitude()
    {
        double previous = density(0.0);
        for (double z = 500.0; z <= 60000.0; z += 500.0) {
            const double rho = density(z);
            QVERIFY(rho < previous);
            previous = rho;
        }
    }

    void heightConversionRoundTrip()
    {
        for (double h : {0.0, 1500.0, 12000.0, 30652.0}) {
            QVERIFY(qAbs(geopotentialFromGeometric(geometricFromGeopotential(h)) - h) < 1e-6);
        }
        // At 30 km the difference is about 140 m.
        const double z = geometricFromGeopotential(30000.0);
        QVERIFY(z > 30130.0 && z < 30150.0);
    }
};

QTEST_GUILESS_MAIN(TestAtmosphere)
#include "tst_atmosphere.moc"
