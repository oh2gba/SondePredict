#pragma once

// Synthetic wind fields and sources shared by the tests.

#include "windsource.h"

#include <QTimer>

#include <functional>

namespace sptest {

using WindFunction = std::function<sp::LevelWind(sp::NodeIndex node, qint64 hour, double altitude)>;

inline std::vector<sp::LevelWind> profile(const WindFunction &wind, sp::NodeIndex node, qint64 hour)
{
    std::vector<sp::LevelWind> levels;
    for (double altitude = 0.0; altitude <= 32000.0; altitude += 2000.0)
        levels.push_back(wind(node, hour, altitude));
    return levels;
}

inline WindFunction uniform(double u, double v)
{
    return [u, v](sp::NodeIndex, qint64, double altitude) { return sp::LevelWind{altitude, u, v}; };
}

// Fills every node within the lat/lon box for the given hours.
inline void fill(sp::WindField &field, sp::GeoPoint min, sp::GeoPoint max, qint64 firstHour, qint64 lastHour,
                 const WindFunction &wind, double ground = 0.0)
{
    const sp::NodeIndex a = field.cellOf(min);
    const sp::NodeIndex b = field.cellOf(max);
    for (int i = a.lat; i <= b.lat + 1; ++i) {
        for (int j = a.lon; j <= b.lon + 1; ++j) {
            for (qint64 h = firstHour; h <= lastHour; ++h)
                field.insert({{i, j}, h}, ground, profile(wind, {i, j}, h));
        }
    }
}

// A WindSource that answers asynchronously from a function, optionally only up to a
// last available hour (to imitate the end of the forecast range).
class FakeSource : public sp::WindSource {
public:
    explicit FakeSource(WindFunction wind, double ground = 0.0, qint64 lastAvailableHour = INT64_MAX)
        : m_wind(std::move(wind)), m_ground(ground), m_lastAvailable(lastAvailableHour) {}

    void fetch(const std::vector<sp::NodeIndex> &nodes, qint64 firstHour, qint64 lastHour,
               sp::WindField &field, Done done) override
    {
        ++fetchCount;
        nodesRequested += static_cast<int>(nodes.size());
        QTimer::singleShot(0, [this, nodes, firstHour, lastHour, &field, done]() {
            for (const sp::NodeIndex &n : nodes) {
                for (qint64 h = firstHour; h <= std::min(lastHour, m_lastAvailable); ++h)
                    field.insert({n, h}, m_ground, profile(m_wind, n, h));
            }
            done({});
        });
    }

    int fetchCount = 0;
    int nodesRequested = 0;

private:
    WindFunction m_wind;
    double m_ground;
    qint64 m_lastAvailable;
};

} // namespace sptest
