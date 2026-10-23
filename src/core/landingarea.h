#pragma once

#include "geo.h"
#include "simulator.h"

#include <set>
#include <vector>

namespace sp {

// How much real flights differ from the configured balloon (one standard deviation).
struct FlightSpread {
    bool enabled = true;
    // Defaults: the spread of Jokioinen flights on SondeHub (robust, 2026).
    double ascentRate = 0.35;    // m/s
    double burstAltitude = 2500; // m
    double descentRate = 0.18;   // fraction of the descent speed (fitted descents: ± 18 %)
    int samples = 50;
    // Forecast wind errors: each varied flight also gets a steady wind error of this size
    // (m/s, one standard deviation per component). Set per prediction from how far ahead
    // the launch is (forecastWindSigma) when windUncertainty is on.
    bool windUncertainty = true;
    double windSigma = 0.0;
};

// The wind error to add for a launch `leadHours` after the prediction is made (0 for launches
// in the past, which use the forecast made closest to them), for a sonde falling at
// `seaLevelDescentRate` near the ground. Calibrated on SondeHub flights from Jokioinen, Tallinn,
// Stavanger and Greifswald so that about 95 % of real landings fall inside the 95 % area
// (tools/validation/stations.py).
double forecastWindSigma(double leadHours, double seaLevelDescentRate = 11.7);

// Where the sonde is likely to land, from re-flying the prediction with varied balloon
// parameters through the same winds. Forecast wind errors come on top of this.
struct LandingArea {
    GeoPoint centre;              // mean landing point
    double majorSigma = 0.0;      // m, 1σ along the long axis
    double minorSigma = 0.0;      // m, 1σ along the short axis
    double majorBearing = 0.0;    // degrees from north of the long axis, [0, 180)
    int samples = 0;
    std::vector<GeoPoint> landings; // the simulated landing points (not stored)

    // Axis lengths (full width, m) of the ellipse containing `probability` of landings.
    double majorAxis(double probability = 0.95) const;
    double minorAxis(double probability = 0.95) const;
    // Outline of that ellipse.
    std::vector<GeoPoint> outline(double probability = 0.95, int points = 72) const;
    // Whether a point lies inside that ellipse.
    bool contains(GeoPoint p, double probability = 0.95) const;
};

struct LandingAreaResult {
    std::optional<LandingArea> area; // nothing if too few flights landed
    std::set<NodeKey> missing;       // wind nodes the varied flights needed but were not loaded
};

// Deterministic (fixed seed): the same inputs always give the same area. Varied flights use
// a 5 s time step, which moves landings by metres, not kilometres.
LandingAreaResult estimateLandingArea(const LaunchParams &params, const WindSampler &field, const FlightSpread &spread);

} // namespace sp
