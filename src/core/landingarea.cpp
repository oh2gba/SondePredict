#include "landingarea.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace sp {

namespace {

constexpr double kDeg = std::numbers::pi / 180.0;

// Scale from 1σ to the radius containing `probability` of a 2-D normal distribution.
double sigmaScale(double probability)
{
    return std::sqrt(-2.0 * std::log(1.0 - std::clamp(probability, 0.01, 0.999)));
}

// Local flat approximation around `origin`: metres east and north.
struct Local {
    GeoPoint origin;
    double metresPerDegLat;
    double metresPerDegLon;

    explicit Local(GeoPoint o)
        : origin(o)
    {
        const GeoPoint n = offset(o, 1000.0, 0.0);
        const GeoPoint e = offset(o, 0.0, 1000.0);
        metresPerDegLat = 1000.0 / (n.lat - o.lat);
        metresPerDegLon = 1000.0 / normalizeLon(e.lon - o.lon);
    }
    double east(GeoPoint p) const { return normalizeLon(p.lon - origin.lon) * metresPerDegLon; }
    double north(GeoPoint p) const { return (p.lat - origin.lat) * metresPerDegLat; }
};

} // namespace

double LandingArea::majorAxis(double probability) const
{
    return 2.0 * majorSigma * sigmaScale(probability);
}

double LandingArea::minorAxis(double probability) const
{
    return 2.0 * minorSigma * sigmaScale(probability);
}

std::vector<GeoPoint> LandingArea::outline(double probability, int points) const
{
    const double k = sigmaScale(probability);
    const double a = majorSigma * k;
    const double b = minorSigma * k;
    const double theta = majorBearing * kDeg; // from north towards east
    std::vector<GeoPoint> out;
    out.reserve(static_cast<std::size_t>(points) + 1);
    for (int i = 0; i <= points; ++i) {
        const double t = 2.0 * std::numbers::pi * i / points;
        const double along = a * std::cos(t);
        const double across = b * std::sin(t);
        const double north = along * std::cos(theta) - across * std::sin(theta);
        const double east = along * std::sin(theta) + across * std::cos(theta);
        out.push_back(offset(centre, north, east));
    }
    return out;
}

LandingAreaResult estimateLandingArea(const LaunchParams &params, const WindField &field, const FlightSpread &spread)
{
    LandingAreaResult result;
    if (!spread.enabled || spread.samples < 3)
        return result;

    std::mt19937 rng(20160217); // fixed seed: the same prediction always gives the same area
    std::normal_distribution<double> normal(0.0, 1.0);

    LandingArea area;
    for (int i = 0; i < spread.samples; ++i) {
        LaunchParams p = params;
        p.timeStep = 5.0;
        p.outputInterval = 1e9; // only launch, burst and landing are needed
        FlightProfile &f = p.profile;
        f.ascentRate = std::max(0.5, f.ascentRate + spread.ascentRate * normal(rng));
        f.burstAltitude = std::clamp(f.burstAltitude + spread.burstAltitude * normal(rng),
                                     f.burstAltitude * 0.6, f.burstAltitude * 1.4);
        const double descentFactor = std::clamp(1.0 + spread.descentRate * normal(rng), 0.5, 1.5);
        f.seaLevelDescentRate *= descentFactor;
        f.legacyDescentRate *= descentFactor;
        f.legacyDescentGain *= descentFactor;

        const SimulationResult r = simulate(p, field);
        result.missing.insert(r.missing.begin(), r.missing.end());
        if (r.landing)
            area.landings.push_back(r.landing->position);
    }
    if (area.landings.size() < 3)
        return result;

    // Mean and covariance in a local east/north frame.
    const Local frame(area.landings.front());
    double me = 0, mn = 0;
    for (const GeoPoint &g : area.landings) {
        me += frame.east(g);
        mn += frame.north(g);
    }
    const double n = static_cast<double>(area.landings.size());
    me /= n;
    mn /= n;
    double cee = 0, cnn = 0, cen = 0;
    for (const GeoPoint &g : area.landings) {
        const double de = frame.east(g) - me, dn = frame.north(g) - mn;
        cee += de * de;
        cnn += dn * dn;
        cen += de * dn;
    }
    cee /= n - 1;
    cnn /= n - 1;
    cen /= n - 1;

    // Eigenvalues of [[cnn, cen], [cen, cee]] in (north, east) order.
    const double trace = cnn + cee;
    const double det = cnn * cee - cen * cen;
    const double disc = std::sqrt(std::max(0.0, trace * trace / 4.0 - det));
    const double l1 = trace / 2.0 + disc, l2 = std::max(0.0, trace / 2.0 - disc);
    // Direction of the major axis: angle from north towards east.
    double bearing = 0.5 * std::atan2(2.0 * cen, cnn - cee) / kDeg;
    if (bearing < 0)
        bearing += 180.0;

    area.centre = offset(frame.origin, mn, me);
    area.majorSigma = std::sqrt(l1);
    area.minorSigma = std::sqrt(l2);
    area.majorBearing = std::fmod(bearing, 180.0);
    area.samples = static_cast<int>(area.landings.size());
    result.area = std::move(area);
    return result;
}

} // namespace sp
