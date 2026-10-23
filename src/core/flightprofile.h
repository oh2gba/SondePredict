#pragma once

namespace sp {

enum class DescentModel {
    // v(h) = v0 · (ρ0 / ρ(h))^k. k = 0.5 is classic terminal velocity; RS41 descents from
    // Jokioinen (no parachute) fit v0 ≈ 11.7 m/s, k ≈ 0.33 (tools/validation/tracks.py).
    DensityScaled,
    // Sonde Predict v3: v(h) = rate + h · gain / 1000.
    Legacy,
};

struct FlightProfile {
    // Defaults: Jokioinen, Finland, from the 345 flights of July–October 2026 on SondeHub
    // (median ascent 4.9 m/s, burst 34.4 km); see tools/validation.
    double ascentRate = 4.9;       // m/s, average from launch to burst
    double burstAltitude = 34300;  // m above mean sea level
    // Real balloons climb about 13 % slower than average at 9–18 km (around the cold
    // tropopause) and up to 20 % faster above 25 km (SondeHub tracks of Jokioinen flights).
    // On, the ascent follows that shape; the average, and so the burst time, stay the same.
    bool shapedAscent = true;

    DescentModel descentModel = DescentModel::DensityScaled;
    double seaLevelDescentRate = 11.7; // m/s, DensityScaled; fitted to 30 Jokioinen descents
    double descentExponent = 0.33;     // k, DensityScaled; (Sep–Oct 2026, SondeHub tracks)
    double legacyDescentRate = 40.0;   // m/s, Legacy (v3 default)
    double legacyDescentGain = 1.0;    // Legacy (v3 default)

    // Descent speed (positive, m/s) at a geometric altitude.
    double descentRate(double altitude) const;

    // Ascent speed at an altitude for a flight from `launchAltitude`. `scale` is
    // ascentScale(launchAltitude); pass it in when calling this often.
    double ascentRateAt(double altitude, double scale) const;
    // Factor that keeps the shaped ascent's average at ascentRate (1 when not shaped).
    double ascentScale(double launchAltitude) const;
    // The measured shape: speed relative to the average at an altitude.
    static double ascentShape(double altitude);

    // Empty when valid, otherwise a human-readable reason.
    const char *validationError() const;
};

} // namespace sp
