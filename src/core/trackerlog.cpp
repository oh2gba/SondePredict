#include "trackerlog.h"

#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QTimeZone>

#include <algorithm>

namespace sp {

const LoggedPoint *TrackerLog::highest() const
{
    if (points.empty())
        return nullptr;
    return &*std::max_element(points.begin(), points.end(),
                              [](const LoggedPoint &a, const LoggedPoint &b) { return a.altitude < b.altitude; });
}

TrackerLog readTrackerCsv(const QString &path)
{
    TrackerLog log;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        log.error = QStringLiteral("cannot open %1: %2").arg(path, file.errorString());
        return log;
    }

    QTextStream in(&file);
    const QStringList header = in.readLine().split(u',');
    const auto column = [&header](const char *name) { return header.indexOf(QString::fromLatin1(name)); };
    const qsizetype cDate = column("Date"), cTime = column("Time"), cLat = column("Latitude"),
                    cLon = column("Longitude"), cAlt = column("Altitude");
    if (cDate < 0 || cTime < 0 || cLat < 0 || cLon < 0 || cAlt < 0) {
        log.error = QStringLiteral("%1 is not a tracker CSV (needs Date, Time, Latitude, Longitude, Altitude)").arg(path);
        return log;
    }
    const qsizetype needed = std::max({cDate, cTime, cLat, cLon, cAlt});

    while (!in.atEnd()) {
        const QStringList f = in.readLine().split(u',');
        if (f.size() <= needed)
            continue;
        const QDateTime when = QDateTime::fromString(f[cDate] + u'T' + f[cTime], Qt::ISODate);
        bool okLat, okLon, okAlt;
        const LoggedPoint p{QDateTime(when.date(), when.time(), QTimeZone::UTC).toSecsSinceEpoch(),
                            {f[cLat].toDouble(&okLat), f[cLon].toDouble(&okLon)},
                            f[cAlt].toDouble(&okAlt)};
        if (when.isValid() && okLat && okLon && okAlt)
            log.points.push_back(p);
    }
    std::stable_sort(log.points.begin(), log.points.end(),
                     [](const LoggedPoint &a, const LoggedPoint &b) { return a.time < b.time; });
    if (log.points.empty())
        log.error = QStringLiteral("%1 contains no usable points").arg(path);
    return log;
}

std::optional<TrackComparison> compare(const SimulationResult &prediction, const TrackerLog &log)
{
    const LoggedPoint *top = log.highest();
    if (!top || &log.points.back() == top)
        return std::nullopt; // never seen descending
    const LoggedPoint &reference = log.points.back();

    // Find where the predicted descent passes the reference altitude.
    const auto &track = prediction.track;
    for (std::size_t i = 1; i < track.size(); ++i) {
        const TrackPoint &a = track[i - 1];
        const TrackPoint &b = track[i];
        if (b.phase != FlightPhase::Descent || a.phase != FlightPhase::Descent)
            continue;
        if (a.altitude >= reference.altitude && b.altitude <= reference.altitude) {
            const double f = a.altitude > b.altitude ? (a.altitude - reference.altitude) / (a.altitude - b.altitude) : 0.0;
            TrackPoint at = b;
            at.time = a.time + f * (b.time - a.time);
            at.position = {a.position.lat + f * (b.position.lat - a.position.lat),
                           a.position.lon + f * normalizeLon(b.position.lon - a.position.lon)};
            at.altitude = reference.altitude;

            TrackComparison c{reference, at, inverse(at.position, reference.position).distance,
                              at.time - static_cast<double>(reference.time), std::nullopt, std::nullopt};
            if (prediction.burst) {
                c.burstDistance = inverse(prediction.burst->position, top->position).distance;
                c.burstAltitudeError = prediction.burst->altitude - top->altitude;
            }
            return c;
        }
    }
    return std::nullopt;
}

} // namespace sp
