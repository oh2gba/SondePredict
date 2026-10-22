#pragma once

#include "geo.h"
#include "simulator.h"

#include <QString>

#include <optional>
#include <vector>

namespace sp {

struct LoggedPoint {
    qint64 time = 0; // unix seconds, UTC
    GeoPoint position;
    double altitude = 0.0;
};

// A received flight, e.g. the CSV written by RS41Tracker
// (Date,Time,Latitude,Longitude,Altitude,... with UTC timestamps).
struct TrackerLog {
    std::vector<LoggedPoint> points; // in time order
    QString error;

    const LoggedPoint *highest() const;
};

TrackerLog readTrackerCsv(const QString &path);

struct TrackComparison {
    LoggedPoint reference;      // last logged point while descending
    TrackPoint predicted;       // predicted track at the same altitude on the way down
    double horizontalError = 0; // m
    double timeError = 0;       // s, predicted minus logged
    std::optional<double> burstDistance;      // m between predicted burst and highest logged point
    std::optional<double> burstAltitudeError; // m, predicted minus logged
};

// Compares a prediction with a logged flight at the last logged altitude during descent.
// Returns nothing if the log has no descent or the prediction never reaches that altitude.
std::optional<TrackComparison> compare(const SimulationResult &prediction, const TrackerLog &log);

} // namespace sp
