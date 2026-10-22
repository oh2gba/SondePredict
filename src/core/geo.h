#pragma once

namespace sp {

struct GeoPoint {
    double lat = 0.0; // degrees, WGS84
    double lon = 0.0; // degrees, WGS84
};

struct Geodesic {
    double distance = 0.0;       // metres
    double initialBearing = 0.0; // degrees clockwise from true north, [0, 360)
};

// Moves a point by a small north/east displacement (metres), using the local radii of
// curvature of the WGS84 ellipsoid. Accurate for the steps the simulator takes (< 1 km).
GeoPoint offset(GeoPoint p, double northMetres, double eastMetres);

// Distance and initial bearing between two points on the WGS84 ellipsoid (Vincenty's
// inverse formula; falls back to a spherical solution for nearly antipodal points).
Geodesic inverse(GeoPoint from, GeoPoint to);

// Normalises a longitude to [-180, 180).
double normalizeLon(double lon);

} // namespace sp
