#pragma once

namespace sp::atmosphere {

// Air density (kg/m³) at a geometric altitude above mean sea level, from the
// US Standard Atmosphere 1976 (valid to 86 km; clamped outside 0–86 km).
double density(double geometricAltitude);

inline constexpr double kSeaLevelDensity = 1.225;

// GFS reports geopotential height; the simulator works in geometric altitude.
double geometricFromGeopotential(double geopotentialHeight);
double geopotentialFromGeometric(double geometricAltitude);

} // namespace sp::atmosphere
