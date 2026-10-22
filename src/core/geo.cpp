#include "geo.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace sp {

namespace {

constexpr double kA = 6378137.0;           // WGS84 semi-major axis
constexpr double kF = 1.0 / 298.257223563; // WGS84 flattening
constexpr double kB = kA * (1.0 - kF);
constexpr double kE2 = kF * (2.0 - kF);
constexpr double kDeg = std::numbers::pi / 180.0;

double wrapBearing(double deg)
{
    deg = std::fmod(deg, 360.0);
    return deg < 0.0 ? deg + 360.0 : deg;
}

Geodesic sphericalInverse(GeoPoint from, GeoPoint to)
{
    constexpr double kMeanRadius = 6371008.8;
    const double phi1 = from.lat * kDeg, phi2 = to.lat * kDeg;
    const double dPhi = phi2 - phi1, dLambda = (to.lon - from.lon) * kDeg;
    const double h = std::sin(dPhi / 2) * std::sin(dPhi / 2)
                   + std::cos(phi1) * std::cos(phi2) * std::sin(dLambda / 2) * std::sin(dLambda / 2);
    const double distance = 2.0 * kMeanRadius * std::asin(std::min(1.0, std::sqrt(h)));
    const double y = std::sin(dLambda) * std::cos(phi2);
    const double x = std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dLambda);
    return {distance, wrapBearing(std::atan2(y, x) / kDeg)};
}

} // namespace

double normalizeLon(double lon)
{
    lon = std::fmod(lon + 180.0, 360.0);
    if (lon < 0.0)
        lon += 360.0;
    return lon - 180.0;
}

GeoPoint offset(GeoPoint p, double northMetres, double eastMetres)
{
    const double phi = p.lat * kDeg;
    const double s = std::sin(phi);
    const double w = std::sqrt(1.0 - kE2 * s * s);
    const double meridianRadius = kA * (1.0 - kE2) / (w * w * w);
    const double normalRadius = kA / w;

    GeoPoint out;
    out.lat = p.lat + northMetres / meridianRadius / kDeg;
    const double cosPhi = std::max(std::cos(phi), 1e-9);
    out.lon = normalizeLon(p.lon + eastMetres / (normalRadius * cosPhi) / kDeg);
    return out;
}

Geodesic inverse(GeoPoint from, GeoPoint to)
{
    const double L = normalizeLon(to.lon - from.lon) * kDeg;
    const double U1 = std::atan((1.0 - kF) * std::tan(from.lat * kDeg));
    const double U2 = std::atan((1.0 - kF) * std::tan(to.lat * kDeg));
    const double sinU1 = std::sin(U1), cosU1 = std::cos(U1);
    const double sinU2 = std::sin(U2), cosU2 = std::cos(U2);

    double lambda = L;
    double sinSigma = 0, cosSigma = 0, sigma = 0, cosSqAlpha = 0, cos2SigmaM = 0;
    double sinLambda = 0, cosLambda = 0;

    for (int i = 0; i < 200; ++i) {
        sinLambda = std::sin(lambda);
        cosLambda = std::cos(lambda);
        const double t1 = cosU2 * sinLambda;
        const double t2 = cosU1 * sinU2 - sinU1 * cosU2 * cosLambda;
        sinSigma = std::sqrt(t1 * t1 + t2 * t2);
        if (sinSigma == 0.0)
            return {0.0, 0.0}; // coincident points
        cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        sigma = std::atan2(sinSigma, cosSigma);
        const double sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        cosSqAlpha = 1.0 - sinAlpha * sinAlpha;
        cos2SigmaM = cosSqAlpha != 0.0 ? cosSigma - 2.0 * sinU1 * sinU2 / cosSqAlpha : 0.0;
        const double C = kF / 16.0 * cosSqAlpha * (4.0 + kF * (4.0 - 3.0 * cosSqAlpha));
        const double previous = lambda;
        lambda = L + (1.0 - C) * kF * sinAlpha
                         * (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)));
        if (std::abs(lambda - previous) < 1e-12) {
            const double uSq = cosSqAlpha * (kA * kA - kB * kB) / (kB * kB);
            const double A = 1.0 + uSq / 16384.0 * (4096.0 + uSq * (-768.0 + uSq * (320.0 - 175.0 * uSq)));
            const double B = uSq / 1024.0 * (256.0 + uSq * (-128.0 + uSq * (74.0 - 47.0 * uSq)));
            const double deltaSigma = B * sinSigma
                * (cos2SigmaM + B / 4.0
                    * (cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)
                       - B / 6.0 * cos2SigmaM * (-3.0 + 4.0 * sinSigma * sinSigma)
                           * (-3.0 + 4.0 * cos2SigmaM * cos2SigmaM)));
            const double distance = kB * A * (sigma - deltaSigma);
            const double bearing = std::atan2(cosU2 * sinLambda, cosU1 * sinU2 - sinU1 * cosU2 * cosLambda);
            return {distance, wrapBearing(bearing / kDeg)};
        }
    }
    return sphericalInverse(from, to);
}

} // namespace sp
