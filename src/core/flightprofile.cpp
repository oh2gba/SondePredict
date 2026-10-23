#include "flightprofile.h"

#include "atmosphere.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace sp {

double FlightProfile::descentRate(double altitude) const
{
    switch (descentModel) {
    case DescentModel::Legacy:
        return legacyDescentRate + std::max(altitude, 0.0) * legacyDescentGain / 1000.0;
    case DescentModel::DensityScaled:
        break;
    }
    const double ratio = atmosphere::kSeaLevelDensity / atmosphere::density(altitude);
    return seaLevelDescentRate * std::pow(ratio, descentExponent);
}

double FlightProfile::ascentShape(double altitude)
{
    // Median vertical speed relative to each flight's average, in 3 km bands (centres),
    // from 30 Jokioinen flights of Sep–Oct 2026 on SondeHub (tools/validation/tracks.py).
    static constexpr double kBand = 3000.0;
    static constexpr double kShape[] = {0.94, 0.99, 0.99, 0.87, 0.87, 0.89, 0.95, 1.02, 1.10, 1.17, 1.23, 1.18};
    constexpr int n = int(std::size(kShape));
    const double x = altitude / kBand - 0.5; // band index at the band centres
    if (x <= 0.0)
        return kShape[0];
    if (x >= n - 1)
        return kShape[n - 1];
    const int i = int(x);
    const double f = x - i;
    return kShape[i] * (1.0 - f) + kShape[i + 1] * f;
}

double FlightProfile::ascentScale(double launchAltitude) const
{
    if (!shapedAscent || burstAltitude <= launchAltitude)
        return 1.0;
    // Time through the shape, ∫dz / shape(z), compared with the time at a constant speed.
    constexpr double step = 100.0;
    double time = 0.0;
    for (double z = launchAltitude; z < burstAltitude; z += step)
        time += std::min(step, burstAltitude - z) / ascentShape(z + std::min(step, burstAltitude - z) / 2);
    return time / (burstAltitude - launchAltitude);
}

double FlightProfile::ascentRateAt(double altitude, double scale) const
{
    return shapedAscent ? ascentRate * ascentShape(altitude) * scale : ascentRate;
}

const char *FlightProfile::validationError() const
{
    if (!(ascentRate > 0.1 && ascentRate < 50.0))
        return "ascent rate must be between 0.1 and 50 m/s";
    if (!(burstAltitude > 100.0 && burstAltitude < 60000.0))
        return "burst altitude must be between 100 and 60000 m";
    if (descentModel == DescentModel::DensityScaled) {
        if (!(seaLevelDescentRate > 0.1 && seaLevelDescentRate < 200.0))
            return "descent rate must be between 0.1 and 200 m/s";
        if (!(descentExponent >= 0.0 && descentExponent <= 1.0))
            return "descent exponent must be between 0 and 1";
    } else {
        if (!(legacyDescentRate > 0.1 && legacyDescentRate < 200.0))
            return "descent rate must be between 0.1 and 200 m/s";
        if (!(legacyDescentGain >= 0.0 && legacyDescentGain < 100.0))
            return "descent gain must be between 0 and 100";
    }
    return "";
}

} // namespace sp
