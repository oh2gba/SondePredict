#pragma once

#include "geo.h"

#include <QtGlobal>

#include <compare>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace sp {

// A grid node: latitude = lat · step, longitude = lon · step.
struct NodeIndex {
    int lat = 0;
    int lon = 0;
    auto operator<=>(const NodeIndex &) const = default;
};

// A grid node at a whole UTC hour (hour = unix time / 3600).
struct NodeKey {
    NodeIndex node;
    qint64 hour = 0;
    auto operator<=>(const NodeKey &) const = default;
};

struct LevelWind {
    double altitude = 0.0; // geometric metres above mean sea level
    double u = 0.0;        // eastward wind, m/s
    double v = 0.0;        // northward wind, m/s
};

struct WindSample {
    double u = 0.0;
    double v = 0.0;
    double groundElevation = 0.0;
    bool aboveTopLevel = false; // altitude above the highest level; top wind held constant
};

// Wind data on a regular lat/lon grid, stored per node and hour, interpolated bilinearly
// in space, linearly in altitude and linearly in time.
class WindField {
public:
    explicit WindField(double gridStep = 0.25);

    double gridStep() const { return m_step; }

    // Grid node containing the point in its lower-left corner.
    NodeIndex cellOf(GeoPoint p) const;
    // Brings the longitude index into [-180°, 180°).
    NodeIndex wrap(NodeIndex n) const;
    GeoPoint position(NodeIndex n) const;

    // Stores one node/hour profile. Levels may be given in any order.
    void insert(NodeKey key, double groundElevation, std::vector<LevelWind> levels);
    bool contains(NodeKey key) const;
    bool isEmpty() const { return m_nodes.empty(); }
    std::size_t size() const { return m_nodes.size(); }
    void clear();

    // Wind at a point, altitude and unix time. Nodes that are not loaded are added to
    // `missing` and replaced by the nearest loaded node, so a rough track can still be
    // flown to find out which data is needed. Returns nothing if the field is empty.
    std::optional<WindSample> sample(GeoPoint p, double altitude, double unixTime,
                                     std::set<NodeKey> *missing = nullptr) const;

private:
    struct Entry {
        double groundElevation = 0.0;
        std::vector<LevelWind> levels; // ascending altitude
    };

    const Entry &lookup(NodeKey key, std::set<NodeKey> *missing) const;

    double m_step;
    int m_lonCount;
    std::map<NodeKey, Entry> m_nodes;
    mutable std::map<NodeKey, NodeKey> m_substitutes;
};

} // namespace sp
