#include "testfields.h"
#include "trackexport.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

namespace {

constexpr qint64 kLaunch = 1792753200; // 2026-10-23 11:00 UTC

sp::PredictionRecord makeRecord(const QString &site, bool withHome)
{
    sp::WindField field(0.25);
    sptest::fill(field, {60.0, 22.0}, {61.5, 26.0}, kLaunch / 3600, kLaunch / 3600 + 4, sptest::uniform(-6.0, 4.0));
    sp::PredictionRecord rec;
    rec.siteName = site;
    if (withHome) {
        rec.homeName = QStringLiteral("Home");
        rec.home = sp::GeoPoint{60.2055, 24.6559};
    }
    rec.prediction.params.site = {60.814, 23.499};
    rec.prediction.params.siteAltitude = 104.0;
    rec.prediction.params.launchTime = kLaunch;
    rec.prediction.result = sp::simulate(rec.prediction.params, field);
    return rec;
}

// Counts elements by qualified name and collects text of the given element names.
struct XmlStats {
    QMap<QString, int> count;
    QStringList names;
    QString error;
};

XmlStats scan(const QByteArray &xml)
{
    XmlStats stats;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        if (r.readNext() == QXmlStreamReader::StartElement) {
            const QString key = r.namespaceUri().toString() + u'|' + r.name().toString();
            ++stats.count[key];
            if (r.name() == QLatin1String("name"))
                stats.names << r.readElementText();
        }
    }
    if (r.hasError())
        stats.error = r.errorString();
    return stats;
}

const QString kGpx = QStringLiteral("http://www.topografix.com/GPX/1/1|");

} // namespace

class TestTrackExport : public QObject {
    Q_OBJECT
private slots:
    void gpxStructure()
    {
        const sp::PredictionRecord rec = makeRecord(QStringLiteral("Jokioinen"), true);
        const QByteArray gpx = sp::toGpx({rec});
        const XmlStats s = scan(gpx);
        QVERIFY2(s.error.isEmpty(), qPrintable(s.error));
        QCOMPARE(s.count.value(kGpx + QStringLiteral("wpt")), 2);
        QCOMPARE(s.count.value(kGpx + QStringLiteral("trk")), 1);
        QCOMPARE(s.count.value(kGpx + QStringLiteral("trkpt")), int(rec.prediction.result.track.size()));
        QVERIFY(s.names.contains(QStringLiteral("Landing Jokioinen 11:00")));
        QVERIFY(gpx.contains("<time>2026-10-23T11:00:00Z</time>"));
        // Waypoints come before tracks (GPX schema order).
        QVERIFY(gpx.indexOf("<wpt") < gpx.indexOf("<trk>"));
    }

    void writesFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("out.gpx"));
        QCOMPARE(sp::writeFile(path, "<gpx/>"), QString());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("<gpx/>"));
        QVERIFY(!sp::writeFile(dir.filePath(QStringLiteral("missing/dir/out.gpx")), "x").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTrackExport)
#include "tst_trackexport.moc"
