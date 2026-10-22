#include "trackexport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSaveFile>
#include <QTimeZone>
#include <QXmlStreamWriter>

#include <cmath>
#include <numbers>

namespace sp {

namespace {

QString isoTime(double unixTime)
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::lround(unixTime)), QTimeZone::UTC).toString(Qt::ISODate);
}

QString shortTime(double unixTime)
{
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(std::lround(unixTime)), QTimeZone::UTC)
               .toString(QStringLiteral("yyyy-MM-dd HH:mm"))
         + QStringLiteral(" UTC");
}

QString number(double v, int decimals)
{
    return QString::number(v, 'f', decimals);
}

double windSpeed(const TrackPoint &p)
{
    return std::hypot(p.u, p.v);
}

// Meteorological direction: where the wind blows from, degrees.
double windFrom(const TrackPoint &p)
{
    double deg = std::atan2(-p.u, -p.v) * 180.0 / std::numbers::pi;
    return deg < 0 ? deg + 360.0 : deg;
}

QString landingDescription(const PredictionRecord &rec)
{
    const SimulationResult &r = rec.prediction.result;
    const TrackPoint &land = *r.landing;
    QString text = QStringLiteral("Landing %1 at %2, %3 (%4 m), flight %5 min.")
                       .arg(shortTime(land.time), number(land.position.lat, 5), number(land.position.lon, 5))
                       .arg(qRound(land.altitude))
                       .arg(qRound((land.time - r.track.front().time) / 60.0));
    if (rec.home) {
        const Geodesic g = inverse(*rec.home, land.position);
        text += QStringLiteral(" %1 km from %2, bearing %3°.").arg(number(g.distance / 1000.0, 1), rec.homeName).arg(qRound(g.initialBearing));
    }
    return text;
}

void kmlStyle(QXmlStreamWriter &x, const QString &id, const QString &lineColor, const QString &iconColor, const QString &icon)
{
    x.writeStartElement(QStringLiteral("Style"));
    x.writeAttribute(QStringLiteral("id"), id);
    if (!lineColor.isEmpty()) {
        x.writeStartElement(QStringLiteral("LineStyle"));
        x.writeTextElement(QStringLiteral("color"), lineColor);
        x.writeTextElement(QStringLiteral("width"), QStringLiteral("3"));
        x.writeEndElement();
        x.writeStartElement(QStringLiteral("PolyStyle"));
        x.writeTextElement(QStringLiteral("color"), QStringLiteral("40") + lineColor.mid(2)); // translucent curtain
        x.writeEndElement();
    }
    if (!icon.isEmpty()) {
        x.writeStartElement(QStringLiteral("IconStyle"));
        x.writeTextElement(QStringLiteral("color"), iconColor);
        x.writeStartElement(QStringLiteral("Icon"));
        x.writeTextElement(QStringLiteral("href"), icon);
        x.writeEndElement();
        x.writeEndElement();
    }
    x.writeEndElement();
}

void kmlPoint(QXmlStreamWriter &x, const QString &name, const QString &style, GeoPoint p, double altitude,
              const QString &description, std::optional<double> time = std::nullopt)
{
    x.writeStartElement(QStringLiteral("Placemark"));
    x.writeTextElement(QStringLiteral("name"), name);
    if (!description.isEmpty())
        x.writeTextElement(QStringLiteral("description"), description);
    if (time) {
        x.writeStartElement(QStringLiteral("TimeStamp"));
        x.writeTextElement(QStringLiteral("when"), isoTime(*time));
        x.writeEndElement();
    }
    x.writeTextElement(QStringLiteral("styleUrl"), u'#' + style);
    x.writeStartElement(QStringLiteral("Point"));
    x.writeTextElement(QStringLiteral("altitudeMode"), QStringLiteral("absolute"));
    x.writeTextElement(QStringLiteral("coordinates"),
                       QStringLiteral("%1,%2,%3").arg(number(p.lon, 6), number(p.lat, 6), number(altitude, 0)));
    x.writeEndElement();
    x.writeEndElement();
}

void kmlTrack(QXmlStreamWriter &x, const QString &name, const QString &style, const std::vector<const TrackPoint *> &points)
{
    if (points.size() < 2)
        return;
    x.writeStartElement(QStringLiteral("Placemark"));
    x.writeTextElement(QStringLiteral("name"), name);
    x.writeTextElement(QStringLiteral("styleUrl"), u'#' + style);
    x.writeStartElement(QStringLiteral("gx:Track"));
    x.writeTextElement(QStringLiteral("altitudeMode"), QStringLiteral("absolute"));
    x.writeTextElement(QStringLiteral("extrude"), QStringLiteral("1"));
    for (const TrackPoint *p : points)
        x.writeTextElement(QStringLiteral("when"), isoTime(p->time));
    for (const TrackPoint *p : points) {
        x.writeTextElement(QStringLiteral("gx:coord"),
                           QStringLiteral("%1 %2 %3").arg(number(p->position.lon, 6), number(p->position.lat, 6), number(p->altitude, 0)));
    }
    // Clicking the track in Google Earth shows these values for that point.
    x.writeStartElement(QStringLiteral("ExtendedData"));
    x.writeStartElement(QStringLiteral("SchemaData"));
    x.writeAttribute(QStringLiteral("schemaUrl"), QStringLiteral("#trackData"));
    const auto array = [&](const QString &field, auto value) {
        x.writeStartElement(QStringLiteral("gx:SimpleArrayData"));
        x.writeAttribute(QStringLiteral("name"), field);
        for (const TrackPoint *p : points)
            x.writeTextElement(QStringLiteral("gx:value"), value(*p));
        x.writeEndElement();
    };
    array(QStringLiteral("altitude"), [](const TrackPoint &p) { return number(p.altitude, 0); });
    array(QStringLiteral("wind_speed"), [](const TrackPoint &p) { return number(windSpeed(p), 1); });
    array(QStringLiteral("wind_from"), [](const TrackPoint &p) { return number(windFrom(p), 0); });
    x.writeEndElement(); // SchemaData
    x.writeEndElement(); // ExtendedData
    x.writeEndElement(); // gx:Track
    x.writeEndElement(); // Placemark
}

} // namespace

QString predictionTitle(const PredictionRecord &record)
{
    return QStringLiteral("%1 %2").arg(record.siteName, shortTime(double(record.prediction.params.launchTime)));
}

QByteArray toKml(const std::vector<PredictionRecord> &records)
{
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeStartElement(QStringLiteral("kml"));
    x.writeDefaultNamespace(QStringLiteral("http://www.opengis.net/kml/2.2"));
    x.writeNamespace(QStringLiteral("http://www.google.com/kml/ext/2.2"), QStringLiteral("gx"));
    x.writeStartElement(QStringLiteral("Document"));
    x.writeTextElement(QStringLiteral("name"), QStringLiteral("Sonde Predict"));

    // KML colours are aabbggrr.
    const QString pin = QStringLiteral("http://maps.google.com/mapfiles/kml/paddle/wht-blank.png");
    const QString dot = QStringLiteral("http://maps.google.com/mapfiles/kml/shapes/placemark_circle.png");
    kmlStyle(x, QStringLiteral("ascent"), QStringLiteral("ffe5881e"), {}, {});
    kmlStyle(x, QStringLiteral("descent"), QStringLiteral("ff3539e5"), {}, {});
    kmlStyle(x, QStringLiteral("launch"), {}, QStringLiteral("ff327d2e"), dot);
    kmlStyle(x, QStringLiteral("burst"), {}, QStringLiteral("ff006cef"), dot);
    kmlStyle(x, QStringLiteral("landing"), {}, QStringLiteral("ff2828c6"), pin);
    kmlStyle(x, QStringLiteral("home"), {}, QStringLiteral("ffc06515"), pin);

    x.writeStartElement(QStringLiteral("Schema"));
    x.writeAttribute(QStringLiteral("id"), QStringLiteral("trackData"));
    for (const auto &[name, label] : {std::pair{QStringLiteral("altitude"), QStringLiteral("Altitude (m)")},
                                      std::pair{QStringLiteral("wind_speed"), QStringLiteral("Wind speed (m/s)")},
                                      std::pair{QStringLiteral("wind_from"), QStringLiteral("Wind from (°)")}}) {
        x.writeStartElement(QStringLiteral("gx:SimpleArrayField"));
        x.writeAttribute(QStringLiteral("name"), name);
        x.writeAttribute(QStringLiteral("type"), QStringLiteral("float"));
        x.writeTextElement(QStringLiteral("displayName"), label);
        x.writeEndElement();
    }
    x.writeEndElement(); // Schema

    for (std::size_t i = 0; i < records.size(); ++i) {
        const PredictionRecord &rec = records[i];
        const SimulationResult &r = rec.prediction.result;
        if (r.track.empty() || !r.landing)
            continue;

        x.writeStartElement(QStringLiteral("Folder"));
        x.writeTextElement(QStringLiteral("name"), predictionTitle(rec));
        x.writeTextElement(QStringLiteral("open"), i == 0 ? QStringLiteral("1") : QStringLiteral("0"));
        x.writeTextElement(QStringLiteral("description"), landingDescription(rec));

        std::vector<const TrackPoint *> ascent, descent;
        for (const TrackPoint &p : r.track)
            (p.phase == FlightPhase::Ascent ? ascent : descent).push_back(&p);
        if (!ascent.empty() && !descent.empty())
            descent.insert(descent.begin(), ascent.back()); // continuous at the burst
        kmlTrack(x, QStringLiteral("Ascent"), QStringLiteral("ascent"), ascent);
        kmlTrack(x, QStringLiteral("Descent"), QStringLiteral("descent"), descent);

        const TrackPoint &start = r.track.front();
        kmlPoint(x, rec.siteName, QStringLiteral("launch"), start.position, start.altitude,
                 QStringLiteral("Launch %1").arg(shortTime(start.time)), start.time);
        if (r.burst) {
            kmlPoint(x, QStringLiteral("Burst"), QStringLiteral("burst"), r.burst->position, r.burst->altitude,
                     QStringLiteral("Burst at %1 m, %2").arg(qRound(r.burst->altitude)).arg(shortTime(r.burst->time)), r.burst->time);
        }
        kmlPoint(x, QStringLiteral("Landing %1").arg(shortTime(r.landing->time).mid(11, 5)), QStringLiteral("landing"),
                 r.landing->position, r.landing->altitude, landingDescription(rec), r.landing->time);
        if (rec.home)
            kmlPoint(x, rec.homeName, QStringLiteral("home"), *rec.home, 0.0, {});
        x.writeEndElement(); // Folder
    }

    x.writeEndElement(); // Document
    x.writeEndElement(); // kml
    x.writeEndDocument();
    return out;
}

QByteArray toGpx(const std::vector<PredictionRecord> &records)
{
    QByteArray out;
    QXmlStreamWriter x(&out);
    x.setAutoFormatting(true);
    x.writeStartDocument();
    x.writeStartElement(QStringLiteral("gpx"));
    x.writeDefaultNamespace(QStringLiteral("http://www.topografix.com/GPX/1/1"));
    x.writeAttribute(QStringLiteral("version"), QStringLiteral("1.1"));
    x.writeAttribute(QStringLiteral("creator"), QStringLiteral("Sonde Predict %1").arg(QCoreApplication::applicationVersion()));

    const auto waypoint = [&x](GeoPoint p, double altitude, double time, const QString &name, const QString &desc) {
        x.writeStartElement(QStringLiteral("wpt"));
        x.writeAttribute(QStringLiteral("lat"), number(p.lat, 6));
        x.writeAttribute(QStringLiteral("lon"), number(p.lon, 6));
        x.writeTextElement(QStringLiteral("ele"), number(altitude, 0));
        x.writeTextElement(QStringLiteral("time"), isoTime(time));
        x.writeTextElement(QStringLiteral("name"), name);
        x.writeTextElement(QStringLiteral("desc"), desc);
        x.writeEndElement();
    };

    // GPX wants all waypoints before the tracks.
    for (const PredictionRecord &rec : records) {
        const SimulationResult &r = rec.prediction.result;
        if (!r.landing)
            continue;
        const QString when = shortTime(double(rec.prediction.params.launchTime));
        waypoint(r.landing->position, r.landing->altitude, r.landing->time,
                 QStringLiteral("Landing %1 %2").arg(rec.siteName, when.mid(11, 5)), landingDescription(rec));
        if (r.burst) {
            waypoint(r.burst->position, r.burst->altitude, r.burst->time,
                     QStringLiteral("Burst %1 %2").arg(rec.siteName, when.mid(11, 5)),
                     QStringLiteral("Burst at %1 m").arg(qRound(r.burst->altitude)));
        }
    }
    for (const PredictionRecord &rec : records) {
        const SimulationResult &r = rec.prediction.result;
        if (r.track.empty() || !r.landing)
            continue;
        x.writeStartElement(QStringLiteral("trk"));
        x.writeTextElement(QStringLiteral("name"), predictionTitle(rec));
        x.writeStartElement(QStringLiteral("trkseg"));
        for (const TrackPoint &p : r.track) {
            x.writeStartElement(QStringLiteral("trkpt"));
            x.writeAttribute(QStringLiteral("lat"), number(p.position.lat, 6));
            x.writeAttribute(QStringLiteral("lon"), number(p.position.lon, 6));
            x.writeTextElement(QStringLiteral("ele"), number(p.altitude, 0));
            x.writeTextElement(QStringLiteral("time"), isoTime(p.time));
            x.writeEndElement();
        }
        x.writeEndElement(); // trkseg
        x.writeEndElement(); // trk
    }
    x.writeEndElement(); // gpx
    x.writeEndDocument();
    return out;
}

QString writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
    file.write(data);
    if (!file.commit())
        return QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
    return {};
}

} // namespace sp
