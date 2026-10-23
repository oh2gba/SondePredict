#pragma once

#include "geo.h"

#include <QtGlobal>

#include <compare>
#include <map>
#include <memory>
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

// A grid node at a whole UTC hour (hour = unix time / 3600). `layer` says which field of a
// BlendedWind the node belongs to (0 for a single field).
struct NodeKey {
    NodeIndex node;
    qint64 hour = 0;
    int layer = 0;
    auto operator<=>(const NodeKey &) const = default;
};

struct LevelWind {
    double altitude = 0.0; // geometric metres above mean sea level
    double u = 0.0;        // eastward wind, m/s
    double v = 0.0;        // northward wind, m/s
};

// How grid node indices relate to positions. NodeIndex.lon counts along the grid's x axis,
// NodeIndex.lat along its y axis.
class GridGeometry {
public:
    virtual ~GridGeometry() = default;
    // Fractional grid coordinates of a point: x along NodeIndex.lon, y along NodeIndex.lat.
    virtual void toGrid(GeoPoint p, double &x, double &y) const = 0;
    virtual GeoPoint position(NodeIndex n) const = 0;
    // Nodes once around the globe along x (a global latitude/longitude grid), or 0.
    virtual int wrapCount() const { return 0; }
};

// A regular latitude/longitude grid: node (lat, lon) is at (lat · step, lon · step).
class LatLonGrid : public GridGeometry {
public:
    explicit LatLonGrid(double step);
    double step() const { return m_step; }
    void toGrid(GeoPoint p, double &x, double &y) const override;
    GeoPoint position(NodeIndex n) const override;
    int wrapCount() const override { return m_lonCount; }

private:
    double m_step;
    int m_lonCount;
};

struct WindSample {
    double u = 0.0;
    double v = 0.0;
    double groundElevation = 0.0;
    bool aboveTopLevel = false; // altitude above the highest level; top wind held constant
};

// Something the simulator can fly through.
class WindSampler {
public:
    virtual ~WindSampler() = default;
    // Wind at a point, altitude and unix time. Nodes that are not loaded are added to
    // `missing` and replaced by the nearest loaded node, so a rough track can still be
    // flown to find out which data is needed. Returns nothing if there is no data at all.
    virtual std::optional<WindSample> sample(GeoPoint p, double altitude, double unixTime,
                                             std::set<NodeKey> *missing = nullptr) const = 0;
};

// Wind data on a grid (latitude/longitude or a model's own projected grid), stored per node
// and hour, interpolated bilinearly in space, linearly in altitude and linearly in time.
// Winds are always eastward/northward, whatever the grid.
class WindField : public WindSampler {
public:
    explicit WindField(double gridStep = 0.25);
    explicit WindField(std::shared_ptr<const GridGeometry> geometry);

    // The step of a latitude/longitude grid, degrees; 0 for a projected grid.
    double gridStep() const;
    const GridGeometry &geometry() const { return *m_geometry; }

    // Grid node containing the point in its lower-left corner.
    NodeIndex cellOf(GeoPoint p) const;
    // Brings the longitude index of a global grid into [-180°, 180°).
    NodeIndex wrap(NodeIndex n) const;
    GeoPoint position(NodeIndex n) const;

    // Stores one node/hour profile. Levels may be given in any order.
    void insert(NodeKey key, double groundElevation, std::vector<LevelWind> levels);
    bool contains(NodeKey key) const;
    bool isEmpty() const { return m_nodes.empty(); }
    std::size_t size() const { return m_nodes.size(); }
    void clear();

    std::optional<WindSample> sample(GeoPoint p, double altitude, double unixTime,
                                     std::set<NodeKey> *missing = nullptr) const override;

private:
    struct Entry {
        double groundElevation = 0.0;
        std::vector<LevelWind> levels; // ascending altitude
    };

    const Entry &lookup(NodeKey key, std::set<NodeKey> *missing) const;

    std::shared_ptr<const GridGeometry> m_geometry;
    int m_lonCount; // 0: no wrapping
    std::map<NodeKey, Entry> m_nodes;
    mutable std::map<NodeKey, NodeKey> m_substitutes;
};

// The average of several fields' winds: a small ensemble of models, whose errors partly
// cancel. Missing nodes of field i are reported with layer i. The ground elevation comes
// from the first field (put the finest grid first).
class BlendedWind : public WindSampler {
public:
    explicit BlendedWind(std::vector<std::shared_ptr<const WindField>> fields);

    std::optional<WindSample> sample(GeoPoint p, double altitude, double unixTime,
                                     std::set<NodeKey> *missing = nullptr) const override;

private:
    std::vector<std::shared_ptr<const WindField>> m_fields;
};

} // namespace sp
