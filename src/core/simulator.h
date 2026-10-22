#pragma once

#include "flightprofile.h"
#include "geo.h"
#include "windfield.h"

#include <QString>

#include <optional>
#include <set>
#include <vector>

namespace sp {

struct LaunchParams {
    GeoPoint site;
    std::optional<double> siteAltitude; // m above MSL; ground elevation of the grid if unset
    qint64 launchTime = 0;              // unix seconds, UTC
    FlightProfile profile;
    double timeStep = 1.0;              // s
    double outputInterval = 10.0;       // s between recorded track points
    double maxFlightTime = 12 * 3600.0; // s, safety stop
};

enum class FlightPhase { Ascent, Descent };

struct TrackPoint {
    double time = 0.0; // unix seconds
    GeoPoint position;
    double altitude = 0.0; // m above MSL
    double u = 0.0;        // wind east, m/s
    double v = 0.0;        // wind north, m/s
    FlightPhase phase = FlightPhase::Ascent;
};

struct SimulationResult {
    std::vector<TrackPoint> track; // launch, every outputInterval, burst, landing
    std::optional<TrackPoint> burst;
    std::optional<TrackPoint> landing;
    bool aboveTopLevel = false; // flew above the highest wind level at some point
    std::set<NodeKey> missing;  // nodes that were approximated because they were not loaded
    QString error;

    bool complete() const { return landing.has_value() && missing.empty() && error.isEmpty(); }
};

// Flies the balloon through `field`. Missing wind nodes are approximated by the nearest
// loaded node and listed in `missing`, so the caller can fetch them and fly again.
SimulationResult simulate(const LaunchParams &params, const WindField &field);

// Flight duration in seconds from the vertical profile alone (no wind needed); used to
// decide which forecast hours to fetch. Assumes landing at the launch altitude.
double estimatedFlightTime(const FlightProfile &profile, double launchAltitude);

} // namespace sp
