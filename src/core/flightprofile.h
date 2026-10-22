#pragma once

namespace sp {

enum class DescentModel {
    // v(h) = v0 · (ρ0 / ρ(h))^k. k = 0.5 is classic terminal velocity; RS41 flights from
    // Jokioinen fit k ≈ 0.24 (see PLAN.md).
    DensityScaled,
    // Sonde Predict v3: v(h) = rate + h · gain / 1000.
    Legacy,
};

struct FlightProfile {
    double ascentRate = 5.5;       // m/s
    double burstAltitude = 28000;  // m above mean sea level

    DescentModel descentModel = DescentModel::DensityScaled;
    double seaLevelDescentRate = 12.0; // m/s, DensityScaled
    double descentExponent = 0.24;     // k, DensityScaled
    double legacyDescentRate = 40.0;   // m/s, Legacy (v3 default)
    double legacyDescentGain = 1.0;    // Legacy (v3 default)

    // Descent speed (positive, m/s) at a geometric altitude.
    double descentRate(double altitude) const;

    // Empty when valid, otherwise a human-readable reason.
    const char *validationError() const;
};

} // namespace sp
