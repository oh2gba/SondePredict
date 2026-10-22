#include "simulator.h"

#include <algorithm>
#include <cmath>

namespace sp {

namespace {

double verticalSpeed(const FlightProfile &profile, FlightPhase phase, double altitude)
{
    return phase == FlightPhase::Ascent ? profile.ascentRate : -profile.descentRate(altitude);
}

GeoPoint lerp(GeoPoint a, GeoPoint b, double f)
{
    return {a.lat + f * (b.lat - a.lat), normalizeLon(a.lon + f * normalizeLon(b.lon - a.lon))};
}

} // namespace

double estimatedFlightTime(const FlightProfile &profile, double launchAltitude)
{
    const double ascent = std::max(0.0, profile.burstAltitude - launchAltitude) / profile.ascentRate;
    double descent = 0.0;
    constexpr double kStep = 50.0; // m
    for (double h = profile.burstAltitude; h > launchAltitude; h -= kStep)
        descent += std::min(kStep, h - launchAltitude) / profile.descentRate(h);
    return ascent + descent;
}

SimulationResult simulate(const LaunchParams &params, const WindField &field)
{
    SimulationResult result;
    if (const char *error = params.profile.validationError(); *error) {
        result.error = QString::fromUtf8(error);
        return result;
    }

    const double dt = params.timeStep;
    double t = static_cast<double>(params.launchTime);
    GeoPoint pos = params.site;

    const auto first = field.sample(pos, 0.0, t, &result.missing);
    if (!first) {
        result.error = QStringLiteral("no wind data loaded");
        return result;
    }
    double altitude = params.siteAltitude.value_or(first->groundElevation);
    FlightPhase phase = FlightPhase::Ascent;

    const auto record = [&](const WindSample &wind) {
        result.track.push_back({t, pos, altitude, wind.u, wind.v, phase});
    };
    record(*first);
    double nextRecord = t + params.outputInterval;

    const double end = t + params.maxFlightTime;
    while (t < end) {
        // Midpoint (RK2) step: wind and vertical speed taken half a step ahead.
        const WindSample w1 = *field.sample(pos, altitude, t, &result.missing);
        const double z1 = verticalSpeed(params.profile, phase, altitude);
        const GeoPoint midPos = offset(pos, w1.v * dt / 2, w1.u * dt / 2);
        const double midAltitude = altitude + z1 * dt / 2;
        const WindSample w2 = *field.sample(midPos, midAltitude, t + dt / 2, &result.missing);
        const double z2 = verticalSpeed(params.profile, phase, midAltitude);

        const GeoPoint newPos = offset(pos, w2.v * dt, w2.u * dt);
        const double newAltitude = altitude + z2 * dt;
        result.aboveTopLevel = result.aboveTopLevel || w2.aboveTopLevel;

        if (phase == FlightPhase::Descent) {
            const double ground = field.sample(newPos, newAltitude, t + dt, &result.missing)->groundElevation;
            if (newAltitude <= ground) {
                const double f = altitude > newAltitude ? std::clamp((altitude - ground) / (altitude - newAltitude), 0.0, 1.0) : 1.0;
                pos = lerp(pos, newPos, f);
                t += f * dt;
                altitude = ground;
                record(w2);
                result.landing = result.track.back();
                return result;
            }
        }

        pos = newPos;
        altitude = newAltitude;
        t += dt;

        if (phase == FlightPhase::Ascent && altitude >= params.profile.burstAltitude) {
            altitude = params.profile.burstAltitude;
            record(w2);
            result.burst = result.track.back();
            phase = FlightPhase::Descent;
            nextRecord = t + params.outputInterval;
            continue;
        }

        if (t >= nextRecord) {
            record(w2);
            nextRecord += params.outputInterval;
        }
    }

    result.error = QStringLiteral("flight did not land within %1 hours").arg(params.maxFlightTime / 3600.0);
    return result;
}

} // namespace sp
