#include "trackexport.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSaveFile>
#include <QStringList>
#include <QTimeZone>
#include <QXmlStreamWriter>

#include <cmath>

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

} // namespace

QString predictionTitle(const PredictionRecord &record)
{
    return QStringLiteral("%1 %2").arg(record.siteName, shortTime(double(record.prediction.params.launchTime)));
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
