#include "meps.h"

#include <QTest>
#include <QtEndian>

#include <cmath>
#include <cstring>

namespace {

// A .dods response: the DDS text, the marker, then XDR data in DDS order.
QByteArray dods(const QByteArray &dds, const QByteArray &data)
{
    return dds + "\nData:\n" + data;
}

QByteArray xdrArray(const std::vector<float> &values)
{
    QByteArray out(8 + 4 * qsizetype(values.size()), Qt::Uninitialized);
    char *p = out.data();
    qToBigEndian<quint32>(quint32(values.size()), p);
    qToBigEndian<quint32>(quint32(values.size()), p + 4);
    p += 8;
    for (float v : values) {
        quint32 bits;
        std::memcpy(&bits, &v, 4);
        qToBigEndian(bits, p);
        p += 4;
    }
    return out;
}

QByteArray xdrArray(const std::vector<double> &values)
{
    QByteArray out(8 + 8 * qsizetype(values.size()), Qt::Uninitialized);
    char *p = out.data();
    qToBigEndian<quint32>(quint32(values.size()), p);
    qToBigEndian<quint32>(quint32(values.size()), p + 4);
    p += 8;
    for (double v : values) {
        quint64 bits;
        std::memcpy(&bits, &v, 8);
        qToBigEndian(bits, p);
        p += 8;
    }
    return out;
}

} // namespace

class TestMeps : public QObject {
    Q_OBJECT
private slots:
    void projectionMatchesTheServersCoordinates()
    {
        // latitude/longitude of grid points as served by thredds.met.no for MEPS.
        const struct { int y, x; double lat, lon; } points[] = {
            {0, 0, 50.31961636316946, 0.2782806572089621},
            {534, 474, 63.30041608681219, 17.50088926973835},
            {1068, 948, 71.57600888469909, 54.24126162807411},
            {300, 700, 57.51013452435938, 26.557856115849354},
        };
        for (const auto &p : points) {
            double x = 0.0, y = 0.0;
            sp::meps::LambertGrid::project({p.lat, p.lon}, x, y);
            QVERIFY2(std::abs(x - p.x) < 1e-3 && std::abs(y - p.y) < 1e-3, qPrintable(QStringLiteral("%1 %2").arg(x).arg(y)));
            const sp::GeoPoint back = sp::meps::LambertGrid::unproject(p.x, p.y);
            QVERIFY(std::abs(back.lat - p.lat) < 1e-5); // about a metre
            QVERIFY(std::abs(back.lon - p.lon) < 1e-5);
        }
    }

    void strideGridNodes()
    {
        const sp::meps::LambertGrid grid(2);
        const sp::GeoPoint p = grid.position({267, 237}); // full-resolution (534, 474)
        QVERIFY(std::abs(p.lat - 63.30041608681219) < 1e-5);
        double x = 0.0, y = 0.0;
        grid.toGrid(p, x, y);
        QVERIFY(std::abs(x - 237.0) < 1e-6 && std::abs(y - 267.0) < 1e-6);
    }

    void coverage()
    {
        QVERIFY(sp::meps::covers({60.81, 23.50}));  // Jokioinen
        QVERIFY(sp::meps::covers({59.33, 18.07}));  // Stockholm
        QVERIFY(sp::meps::covers({69.65, 18.96}));  // Tromsø
        QVERIFY(!sp::meps::covers({52.52, 13.40})); // Berlin
        QVERIFY(!sp::meps::covers({64.14, -21.9})); // Reykjavík

        sp::LaunchParams p;
        p.site = {60.81, 23.50};
        const qint64 now = 1792670400; // 2026-10-22 12:00 UTC: newest run 09 UTC, to 2026-10-25 03 UTC
        p.launchTime = now + 24 * 3600;
        QVERIFY(sp::meps::coversLaunch(p, now));
        p.launchTime = now + 64 * 3600;
        QVERIFY(!sp::meps::coversLaunch(p, now));
        p.launchTime = now - 30 * 86400; // the archive
        QVERIFY(sp::meps::coversLaunch(p, now));
    }

    void rotationTurnsGridWindsToEastNorth()
    {
        QCOMPARE(sp::meps::LambertGrid::rotation(15.0), 0.0);
        // East of 15°E true north leans towards the grid's −x, so grid x points south of east.
        QVERIFY(sp::meps::LambertGrid::rotation(25.0) > 0.0);
        QVERIFY(std::abs(sp::meps::LambertGrid::rotation(25.0) - std::sin(63.3 * M_PI / 180.0) * 10.0 * M_PI / 180.0) < 1e-12);
    }

    void parsesBinaryOpendap()
    {
        const QByteArray dds = "Dataset {\n"
                               "    Float64 ap[hybrid = 2];\n"
                               "    Structure {\n"
                               "        Float32 x_wind_ml[time = 1][hybrid = 2][y = 1][x = 2];\n"
                               "    } x_wind_ml;\n"
                               "    Float64 p0;\n"
                               "} meps.ncml;";
        QByteArray scalar(8, Qt::Uninitialized);
        const double p0 = 100000.0;
        quint64 bits;
        std::memcpy(&bits, &p0, 8);
        qToBigEndian(bits, scalar.data());
        const sp::meps::DapData d = sp::meps::parseDods(
            dods(dds, xdrArray(std::vector<double>{1000.0, 0.0}) + xdrArray(std::vector<float>{1.5f, -2.f, 3.f, 4.25f}) + scalar));
        QVERIFY2(d.error.isEmpty(), qPrintable(d.error));
        QCOMPARE(d.arrays.at(QStringLiteral("ap")), (std::vector<double>{1000.0, 0.0}));
        QCOMPARE(d.arrays.at(QStringLiteral("x_wind_ml")), (std::vector<double>{1.5, -2.0, 3.0, 4.25}));
        QCOMPARE(d.arrays.at(QStringLiteral("p0")), (std::vector<double>{100000.0}));

        // Truncated data and server errors are reported, not misread.
        QVERIFY(!sp::meps::parseDods(dods(dds, xdrArray(std::vector<double>{1000.0, 0.0}))).error.isEmpty());
        const auto error = sp::meps::parseDods("Error {\n    code = 404;\n    message = \"No such file\";\n};");
        QCOMPARE(error.error, QStringLiteral("No such file"));
    }

    void columnHeightsAndWinds()
    {
        // Three levels in an isothermal 250 K dry atmosphere over ground at 100 m:
        // z = zs + Rd·T/g · ln(ps/p).
        sp::meps::Block b;
        b.hours = 1;
        b.rows = 1;
        b.columns = 1;
        b.ap = {10000.0, 0.0, 0.0};
        b.b = {0.0, 0.5, 0.99};
        b.surfacePressure = {100000.0};
        b.surfaceGeopotential = {100.0 * 9.80665};
        b.temperature = {250.0, 250.0, 250.0};
        b.humidity = {0.0, 0.0, 0.0};
        b.xWind = {10.0, 5.0, 1.0};
        b.yWind = {0.0, 0.0, 0.0};
        double ground = 0.0;
        const auto levels = sp::meps::columnProfile(b, 0, 0, 0, 15.0, &ground);
        QCOMPARE(levels.size(), std::size_t(3));
        QVERIFY(std::abs(ground - 100.0) < 0.1);
        const double scale = 287.05 * 250.0 / 9.80665;
        const double expected[] = {100.0 + scale * std::log(10.0), 100.0 + scale * std::log(2.0), 100.0 + scale * std::log(1 / 0.99)};
        for (int k = 0; k < 3; ++k) {
            // Geometric altitude is a little above the geopotential height.
            QVERIFY2(levels[k].altitude >= expected[k] && levels[k].altitude < expected[k] * 1.003,
                     qPrintable(QStringLiteral("%1 vs %2").arg(levels[k].altitude).arg(expected[k])));
        }
        QCOMPARE(levels[0].u, 10.0); // at 15°E grid x is east
        QCOMPARE(levels[0].v, 0.0);

        // At 25°E the same grid wind has a northward part.
        const auto rotated = sp::meps::columnProfile(b, 0, 0, 0, 25.0);
        QVERIFY(rotated[0].v < -1.0 && rotated[0].u < 10.0 && std::abs(std::hypot(rotated[0].u, rotated[0].v) - 10.0) < 1e-9);

        // Fill values make the column unusable.
        b.temperature[1] = 9.96921e36;
        QVERIFY(sp::meps::columnProfile(b, 0, 0, 0, 15.0).empty());
    }

    void fileNames()
    {
        sp::MepsClient client(nullptr);
        QCOMPARE(client.runFile(1792670400),
                 QStringLiteral("https://thredds.met.no/thredds/dodsC/meps25epsarchive/2026/10/22/meps_det_ml_20261022T12Z.ncml"));
        QCOMPARE(client.surfaceFile(1792670400),
                 QStringLiteral("https://thredds.met.no/thredds/dodsC/meps25epsarchive/2026/10/22/meps_det_2_5km_20261022T12Z.nc"));
    }
};

QTEST_GUILESS_MAIN(TestMeps)
#include "tst_meps.moc"
