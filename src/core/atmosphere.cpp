#include "atmosphere.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace sp::atmosphere {

namespace {

constexpr double kEarthRadius = 6356766.0; // metres, as used by the 1976 standard
constexpr double kG0 = 9.80665;
constexpr double kAirGasConstant = 287.053; // J/(kg·K)

struct Layer {
    double baseHeight;      // geopotential metres
    double baseTemperature; // K
    double lapseRate;       // K per geopotential metre
    double basePressure;    // Pa
};

constexpr std::array<Layer, 7> kLayers{{
    {0.0, 288.15, -0.0065, 101325.0},
    {11000.0, 216.65, 0.0, 22632.06},
    {20000.0, 216.65, 0.001, 5474.889},
    {32000.0, 228.65, 0.0028, 868.0187},
    {47000.0, 270.65, 0.0, 110.9063},
    {51000.0, 270.65, -0.0028, 66.93887},
    {71000.0, 214.65, -0.002, 3.956420},
}};

} // namespace

double geometricFromGeopotential(double h)
{
    return kEarthRadius * h / (kEarthRadius - h);
}

double geopotentialFromGeometric(double z)
{
    return kEarthRadius * z / (kEarthRadius + z);
}

double density(double geometricAltitude)
{
    const double h = std::clamp(geopotentialFromGeometric(geometricAltitude), 0.0, 84852.0);

    const Layer *layer = &kLayers.front();
    for (const Layer &candidate : kLayers) {
        if (h >= candidate.baseHeight)
            layer = &candidate;
    }

    const double dh = h - layer->baseHeight;
    const double temperature = layer->baseTemperature + layer->lapseRate * dh;
    double pressure;
    if (layer->lapseRate == 0.0) {
        pressure = layer->basePressure * std::exp(-kG0 * dh / (kAirGasConstant * layer->baseTemperature));
    } else {
        pressure = layer->basePressure
                 * std::pow(layer->baseTemperature / temperature, kG0 / (kAirGasConstant * layer->lapseRate));
    }
    return pressure / (kAirGasConstant * temperature);
}

} // namespace sp::atmosphere
