#include "flightprofile.h"

#include "atmosphere.h"

#include <algorithm>
#include <cmath>

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
