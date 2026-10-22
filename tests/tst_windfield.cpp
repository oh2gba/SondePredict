#include "testfields.h"
#include "windfield.h"

#include <QTest>

class TestWindField : public QObject {
    Q_OBJECT
private slots:
    void emptyFieldHasNoSample()
    {
        sp::WindField field;
        QVERIFY(!field.sample({60.0, 23.0}, 1000.0, 0.0));
    }

    void interpolatesInAltitude()
    {
        sp::WindField field(0.25);
        const sp::NodeKey key{{240, 92}, 1000};
        field.insert(key, 50.0, {{20000.0, 20.0, -2.0}, {0.0, 0.0, 0.0}, {10000.0, 10.0, -1.0}}); // unsorted on purpose
        const sp::GeoPoint at = field.position(key.node);

        auto s = field.sample(at, 5000.0, 1000 * 3600.0);
        QVERIFY(s);
        QVERIFY(qAbs(s->u - 5.0) < 1e-9);
        QVERIFY(qAbs(s->v + 0.5) < 1e-9);
        QCOMPARE(s->groundElevation, 50.0);
        QVERIFY(!s->aboveTopLevel);

        s = field.sample(at, 25000.0, 1000 * 3600.0);
        QCOMPARE(s->u, 20.0);
        QVERIFY(s->aboveTopLevel);
    }

    void interpolatesInTime()
    {
        sp::WindField field(0.25);
        sptest::fill(field, {60.0, 23.0}, {60.0, 23.0}, 1000, 1001,
                     [](sp::NodeIndex, qint64 hour, double alt) { return sp::LevelWind{alt, hour == 1000 ? 0.0 : 10.0, 0.0}; });
        const auto s = field.sample({60.1, 23.1}, 3000.0, 1000 * 3600.0 + 900.0);
        QVERIFY(qAbs(s->u - 2.5) < 1e-9);
    }

    void interpolatesBilinearly()
    {
        sp::WindField field(0.25);
        // u grows with the latitude index, v with the longitude index.
        sptest::fill(field, {60.0, 23.0}, {60.0, 23.0}, 1000, 1000,
                     [](sp::NodeIndex n, qint64, double alt) { return sp::LevelWind{alt, double(n.lat), double(n.lon)}; });
        const auto s = field.sample({60.0 + 0.25 * 0.3, 23.0 + 0.25 * 0.6}, 1000.0, 1000 * 3600.0);
        QVERIFY(qAbs(s->u - (240 + 0.3)) < 1e-9);
        QVERIFY(qAbs(s->v - (92 + 0.6)) < 1e-9);
    }

    void reportsMissingNodesAndApproximates()
    {
        sp::WindField field(0.25);
        field.insert({{240, 92}, 1000}, 0.0, sptest::profile(sptest::uniform(7.0, 1.0), {240, 92}, 1000));

        std::set<sp::NodeKey> missing;
        const auto s = field.sample({60.6, 23.6}, 1000.0, 1000 * 3600.0 + 1800.0, &missing);
        QVERIFY(s);
        QCOMPARE(s->u, 7.0); // borrowed from the only loaded node
        QVERIFY(missing.contains({{242, 94}, 1000}));
        QVERIFY(missing.contains({{243, 95}, 1001}));
        QVERIFY(!missing.contains({{240, 92}, 1000}));
    }

    void exactGridHitNeedsOnlyOneNode()
    {
        sp::WindField field(0.25);
        field.insert({{240, 92}, 1000}, 0.0, sptest::profile(sptest::uniform(3.0, 0.0), {240, 92}, 1000));
        std::set<sp::NodeKey> missing;
        field.sample(field.position({240, 92}), 1000.0, 1000 * 3600.0, &missing);
        QVERIFY(missing.empty());
    }

    void wrapsLongitude()
    {
        sp::WindField field(0.25);
        QCOMPARE(field.wrap({0, 720}).lon, -720);
        QCOMPARE(field.wrap({0, -721}).lon, 719);
        QCOMPARE(field.cellOf({0.1, 179.9}).lon, 719);
        QCOMPARE(field.position({0, 720}).lon, -180.0);

        field.insert({{0, 720}, 5}, 0.0, {});
        QVERIFY(field.contains({{0, -720}, 5}));
    }
};

QTEST_GUILESS_MAIN(TestWindField)
#include "tst_windfield.moc"
