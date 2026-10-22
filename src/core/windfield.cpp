#include "windfield.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace sp {

namespace {

struct ProfileWind {
    double u;
    double v;
    bool aboveTop;
};

ProfileWind interpolateProfile(const std::vector<LevelWind> &levels, double altitude)
{
    if (levels.empty())
        return {0.0, 0.0, true};
    if (altitude <= levels.front().altitude)
        return {levels.front().u, levels.front().v, false};
    if (altitude >= levels.back().altitude)
        return {levels.back().u, levels.back().v, altitude > levels.back().altitude};

    const auto upper = std::upper_bound(levels.begin(), levels.end(), altitude,
                                        [](double a, const LevelWind &l) { return a < l.altitude; });
    const auto lower = upper - 1;
    const double f = (altitude - lower->altitude) / (upper->altitude - lower->altitude);
    return {lower->u + f * (upper->u - lower->u), lower->v + f * (upper->v - lower->v), false};
}

} // namespace

WindField::WindField(double gridStep)
    : m_step(gridStep)
    , m_lonCount(static_cast<int>(std::lround(360.0 / gridStep)))
{
}

NodeIndex WindField::cellOf(GeoPoint p) const
{
    return wrap({static_cast<int>(std::floor(p.lat / m_step)), static_cast<int>(std::floor(p.lon / m_step))});
}

NodeIndex WindField::wrap(NodeIndex n) const
{
    const int half = m_lonCount / 2;
    int lon = (n.lon + half) % m_lonCount;
    if (lon < 0)
        lon += m_lonCount;
    return {n.lat, lon - half};
}

GeoPoint WindField::position(NodeIndex n) const
{
    return {n.lat * m_step, normalizeLon(n.lon * m_step)};
}

void WindField::insert(NodeKey key, double groundElevation, std::vector<LevelWind> levels)
{
    std::sort(levels.begin(), levels.end(),
              [](const LevelWind &a, const LevelWind &b) { return a.altitude < b.altitude; });
    key.node = wrap(key.node);
    m_nodes[key] = Entry{groundElevation, std::move(levels)};
    m_substitutes.clear();
}

bool WindField::contains(NodeKey key) const
{
    key.node = wrap(key.node);
    return m_nodes.contains(key);
}

void WindField::clear()
{
    m_nodes.clear();
    m_substitutes.clear();
}

const WindField::Entry &WindField::lookup(NodeKey key, std::set<NodeKey> *missing) const
{
    key.node = wrap(key.node);
    if (const auto it = m_nodes.find(key); it != m_nodes.end())
        return it->second;

    if (missing)
        missing->insert(key);

    if (const auto it = m_substitutes.find(key); it != m_substitutes.end())
        return m_nodes.at(it->second);

    // Nearest loaded node, preferring the same hour: an hour counts like ~4 cells.
    const NodeKey *best = nullptr;
    double bestScore = std::numeric_limits<double>::max();
    for (const auto &[candidate, entry] : m_nodes) {
        const double dLat = candidate.node.lat - key.node.lat;
        double dLon = std::abs(candidate.node.lon - key.node.lon);
        dLon = std::min(dLon, m_lonCount - dLon);
        const double dHour = static_cast<double>(candidate.hour - key.hour) * 4.0;
        const double score = dLat * dLat + dLon * dLon + dHour * dHour;
        if (score < bestScore) {
            bestScore = score;
            best = &candidate;
        }
    }
    m_substitutes[key] = *best;
    return m_nodes.at(*best);
}

std::optional<WindSample> WindField::sample(GeoPoint p, double altitude, double unixTime,
                                            std::set<NodeKey> *missing) const
{
    if (m_nodes.empty())
        return std::nullopt;

    const double y = p.lat / m_step;
    const double x = p.lon / m_step;
    const int i0 = static_cast<int>(std::floor(y));
    const int j0 = static_cast<int>(std::floor(x));
    const double fy = y - i0;
    const double fx = x - j0;

    const double hours = unixTime / 3600.0;
    const qint64 h0 = static_cast<qint64>(std::floor(hours));
    const double ft = hours - static_cast<double>(h0);

    WindSample out;
    for (int di = 0; di <= 1; ++di) {
        for (int dj = 0; dj <= 1; ++dj) {
            const double wSpace = (di ? fy : 1.0 - fy) * (dj ? fx : 1.0 - fx);
            for (int dh = 0; dh <= 1; ++dh) {
                const double w = wSpace * (dh ? ft : 1.0 - ft);
                // Skip zero-weight corners so exact grid/hour hits don't request neighbours.
                if (w == 0.0)
                    continue;
                const Entry &e = lookup({{i0 + di, j0 + dj}, h0 + dh}, missing);
                const ProfileWind wind = interpolateProfile(e.levels, altitude);
                out.u += w * wind.u;
                out.v += w * wind.v;
                out.groundElevation += w * e.groundElevation;
                out.aboveTopLevel = out.aboveTopLevel || wind.aboveTop;
            }
        }
    }
    return out;
}

} // namespace sp
