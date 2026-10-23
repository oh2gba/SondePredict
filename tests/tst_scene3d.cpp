#include "profilewidget.h"
#include "scene3d.h"
#include "testfields.h"

#include <QDir>
#include <QSignalSpy>
#include <QTest>

#include <cmath>
#include <numbers>

namespace {

constexpr qint64 kLaunch = 1792753200; // 2026-10-23 11:00 UTC

sp::PredictionRecord record(double burst = sp::FlightProfile{}.burstAltitude, qint64 launch = kLaunch)
{
    sp::WindField field(0.25);
    sptest::fill(field, {60.0, 22.0}, {62.0, 26.0}, launch / 3600, launch / 3600 + 4, sptest::uniform(0.0, 10.0));
    sp::PredictionRecord rec;
    rec.siteName = QStringLiteral("Jokioinen");
    rec.homeName = QStringLiteral("Home");
    rec.home = sp::GeoPoint{61.0, 23.6};
    rec.prediction.params.site = {60.814, 23.499};
    rec.prediction.params.siteAltitude = 104.0;
    rec.prediction.params.launchTime = launch;
    rec.prediction.params.profile.burstAltitude = burst;
    rec.prediction.result = sp::simulate(rec.prediction.params, field);
    rec.prediction.landingArea = sp::LandingArea{rec.prediction.result.landing->position, 3000, 1000, 0, 50, {}};
    return rec;
}

int vertexCount(const QQuick3DGeometry &g)
{
    return g.stride() ? int(g.vertexData().size() / g.stride()) : 0;
}

} // namespace

class TestScene3D : public QObject {
    Q_OBJECT
private slots:
    void tileMath()
    {
        // Tile 0/0/0 covers the world; Helsinki at zoom 10 is tile x 581, y 296.
        QCOMPARE(GroundTexture::tileX(-180.0, 0), 0.0);
        QVERIFY(qAbs(GroundTexture::tileY(0.0, 0) - 0.5) < 1e-12);
        QCOMPARE(int(GroundTexture::tileX(24.94, 10)), 582);
        QCOMPARE(int(GroundTexture::tileY(60.17, 10)), 296);
        const GeoBox box{60.0, 22.0, 62.0, 26.0};
        const int z = GroundTexture::zoomFor(box);
        QVERIFY(z >= 7 && z <= 10);
    }

    void reprojectKeepsCorners()
    {
        // A 2×2-tile mosaic with one colour per tile: the reprojected corners keep them.
        const int z = 8, x0 = 144, y0 = 72;
        QImage mosaic(512, 512, QImage::Format_RGB32);
        mosaic.fill(Qt::red);
        for (int y = 0; y < 256; ++y)
            for (int x = 256; x < 512; ++x)
                mosaic.setPixel(x, y, qRgb(0, 0, 255));
        const auto lonAt = [](double tx) { return tx / 256.0 * 360.0 - 180.0; };
        const auto latAt = [](double ty) { return std::atan(std::sinh(std::numbers::pi * (1 - 2 * ty / 256.0))) * 180.0 / std::numbers::pi; };
        const GeoBox box{latAt(y0 + 1.9), lonAt(x0 + 0.1), latAt(y0 + 0.1), lonAt(x0 + 1.9)};
        const QImage out = GroundTexture::reproject(mosaic, x0, y0, z, box, QSize(100, 100));
        QCOMPARE(QColor(out.pixel(2, 2)), QColor(Qt::red));     // north-west tile
        QCOMPARE(QColor(out.pixel(97, 2)), QColor(Qt::blue));   // north-east tile
        QCOMPARE(QColor(out.pixel(2, 97)), QColor(Qt::red));    // south-west tile
    }

    void modelBuildsScene()
    {
        Scene3DModel model;
        model.setFetchGround(false);
        QSignalSpy reset(&model, &Scene3DModel::viewReset);
        model.setPredictions({record()});
        QVERIFY(model.hasData());
        QCOMPARE(reset.count(), 1);
        QCOMPARE(model.tracks().size(), 1);
        QCOMPARE(model.areas().size(), 1);
        // launch, burst, landing and home (inside the ground box)
        QCOMPARE(model.markers().size(), 4);
        QVERIFY(qAbs(model.maxAltitude() - float(sp::FlightProfile{}.burstAltitude / 100.0)) < 0.5f); // burst height in 100 m units
        // A northward flight: the landing lies at negative z (north) of the launch.
        const QVariantMap track = model.tracks().first().toMap();
        const QVector3D launch = track[QStringLiteral("ascent")].toList().first().value<QVector3D>();
        const QVector3D land = track[QStringLiteral("descent")].toList().last().value<QVector3D>();
        QVERIFY(land.z() < launch.z() - 100.0f);
        QVERIFY(qAbs(land.x() - launch.x()) < 1.0f);
        // The ground is roughly square and covers the track.
        QVERIFY(model.groundSize().width() > 0.75 * model.groundSize().height());
        QVERIFY(model.groundSize().height() / 2 > qAbs(land.z()));

        // The flight goes north: look at it from the side (east or west), slightly turned.
        QVERIFY(qAbs(qAbs(model.viewYaw()) - 90.0f) <= 25.0f);

        model.setPredictions({});
        QVERIFY(!model.hasData());
    }

    void toSceneScale()
    {
        Scene3DModel model;
        model.setFetchGround(false);
        model.setPredictions({record()});
        const QVector3D a = model.toScene({61.0, 23.5}, 1000.0);
        const QVector3D b = model.toScene(sp::offset({61.0, 23.5}, 10000.0, 0.0), 1000.0);
        QVERIFY(qAbs(a.y() - 10.0f) < 1e-4f);              // 1 km = 10 units
        QVERIFY(qAbs((a.z() - b.z()) - 100.0f) < 0.05f);    // 10 km north = 100 units towards -z
    }

    void geometries()
    {
        QVariantList points;
        for (int i = 0; i < 5; ++i)
            points << QVariant::fromValue(QVector3D(i * 10.0f, i * 5.0f, 0));
        TubeGeometry tube;
        tube.setPoints(points);
        QCOMPARE(vertexCount(tube), 4 * 24);
        CurtainGeometry curtain;
        curtain.setPoints(points);
        curtain.setVerticalScale(2.0f);
        QCOMPARE(vertexCount(curtain), 4 * 6);
        QCOMPARE(curtain.boundsMax().y(), 40.0f); // top point 20 × 2
        AreaGeometry area;
        area.setPoints(points);
        QCOMPARE(vertexCount(area), 4 * 3);
    }

    void profileAcceptsPredictions()
    {
        ProfileWidget profile;
        profile.resize(600, 300);
        profile.setPredictions({record(), record()});
        QVERIFY(!profile.grab().isNull());
        profile.setPredictions({});
        QVERIFY(!profile.grab().isNull());

        // Several predictions, each in its own colour; saved when SP_TEST_SCREENSHOTS is set.
        profile.resize(900, 360);
        profile.setPredictions({record(), record(27000, kLaunch + 43200), record(34000, kLaunch + 86400)});
        const QImage several = profile.grab().toImage();
        QVERIFY(!several.isNull());
        if (const QString dir = qEnvironmentVariable("SP_TEST_SCREENSHOTS"); !dir.isEmpty())
            QVERIFY(several.save(QDir(dir).filePath(QStringLiteral("profile-several.png"))));
    }
};

QTEST_MAIN(TestScene3D)
#include "tst_scene3d.moc"
