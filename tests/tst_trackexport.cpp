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

const QString kKml = QStringLiteral("http://www.opengis.net/kml/2.2|");
const QString kGx = QStringLiteral("http://www.google.com/kml/ext/2.2|");
const QString kGpx = QStringLiteral("http://www.topografix.com/GPX/1/1|");

} // namespace

class TestTrackExport : public QObject {
    Q_OBJECT
private slots:
    void kmlStructure()
    {
        const std::vector<sp::PredictionRecord> records{makeRecord(QStringLiteral("Jokioinen"), true),
                                                         makeRecord(QStringLiteral("Tikkakoski"), false)};
        const QByteArray kml = sp::toKml(records);
        const XmlStats s = scan(kml);
        QVERIFY2(s.error.isEmpty(), qPrintable(s.error));

        QCOMPARE(s.count.value(kKml + QStringLiteral("Folder")), 2);
        QCOMPARE(s.count.value(kGx + QStringLiteral("Track")), 4); // ascent + descent each
        // Every gx:Track needs as many <when> as <gx:coord>, and as many values per data array.
        const int whens = s.count.value(kKml + QStringLiteral("when")) - 3 * 2; // minus point TimeStamps
        QCOMPARE(whens, s.count.value(kGx + QStringLiteral("coord")));
        QCOMPARE(s.count.value(kGx + QStringLiteral("value")), 3 * s.count.value(kGx + QStringLiteral("coord")));
        // Landing, burst, launch for both; home for the first only.
        QCOMPARE(s.count.value(kKml + QStringLiteral("Point")), 7);
        QVERIFY(s.names.contains(QStringLiteral("Jokioinen 2026-10-23 11:00 UTC")));
        QVERIFY(s.names.contains(QStringLiteral("Home")));
        QVERIFY(kml.contains("altitudeMode>absolute"));
    }

    void kmlDescentStartsAtBurst()
    {
        const sp::PredictionRecord rec = makeRecord(QStringLiteral("Jokioinen"), false);
        const QByteArray kml = sp::toKml({rec});
        const QString burst = QStringLiteral("%1 %2 28000").arg(rec.prediction.result.burst->position.lon, 0, 'f', 6)
                                  .arg(rec.prediction.result.burst->position.lat, 0, 'f', 6);
        QCOMPARE(QString::fromUtf8(kml).count(burst), 2); // end of ascent, start of descent
    }

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
        const QString path = dir.filePath(QStringLiteral("out.kml"));
        QCOMPARE(sp::writeFile(path, "<kml/>"), QString());
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("<kml/>"));
        QVERIFY(!sp::writeFile(dir.filePath(QStringLiteral("missing/dir/out.kml")), "x").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestTrackExport)
#include "tst_trackexport.moc"
