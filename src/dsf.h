// Hard ground and occupied scenery from the enabled DSF stack (including 7z global scenery).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "apt.h"
#include "mask.h"

namespace zg {

struct DsfPolygon {
    std::string definition;
    int param = 0;
    // each winding: points as (lon, lat) or, for curved, (lon, lat, ctrl lon, ctrl lat)
    std::vector<std::vector<std::array<double, 4>>> windings;
    bool curved = false;
};

struct DsfRoad {
    std::string definition;
    int subtype = 0;
    // lon, lat, elevation/stacking level (or Bezier control flag for shape points), junction ID
    std::vector<std::array<double, 4>> points;
    double curve_padding_m = 0; // conservative envelope for legacy seven-plane road controls
};

struct DsfObject {
    std::string definition;
    double lon = 0, lat = 0, heading = 0;
};

struct DsfData {
    std::vector<DsfPolygon> polygons;
    std::vector<DsfRoad> roads;
    std::vector<DsfObject> objects;
    std::vector<std::pair<std::string, std::string>> properties;
};

DsfData parse_dsf(const uint8_t* data, size_t size,
                  const std::function<bool(const std::string&)>& want,
                  const std::function<bool(const std::string&)>& owns);

// Parse an uncompressed or 7z DSF. want(def) picks which polygon definitions are extracted; owns(airport id) tells whether
// a polygon placed under an airport filter belongs to the pack. Throws std::runtime_error on bad data.
std::vector<DsfPolygon> parse_dsf_polygons(const uint8_t* data, size_t size,
                                           const std::function<bool(const std::string&)>& want,
                                           const std::function<bool(const std::string&)>& owns);

class GroundPolygons {
public:
    using Log = std::function<void(const std::string&)>;
    GroundPolygons(std::string xp_root, const AirportIndex* index, const std::string& block_words,
                   const std::string& allow_words, Log log);

    // Hard-ground and occupied-footprint keep-outs around the airport.
    std::vector<Pavement> collect(const Airport& ap, const AirportEntry& entry, double pad_m);

private:
    struct PolInfo {
        std::string layer, surface, texture, decal;
        bool ortho = false;
        double half_width = 0;
    };
    struct Found {
        SurfaceClass cls;
        std::vector<Ring> rings;
        double s, w, n, e;
        bool line;
        std::string category;       // pol, lin, obj, fac, net (DSF exclusion category)
        Ring anchors;              // original placement vertices, for exclusion tests
    };
    struct Tile {
        std::vector<Found> entries;
        std::vector<std::pair<std::string, Ring>> exclusions;
        bool overlay = false;
        bool failed = false;
    };
    struct FileInfo {
        bool resolved = false;
        std::string real;
        bool has_info = false;
        PolInfo info;
    };

    void load_library();
    std::string resolve(const std::string& path, const std::string& pack) const;
    std::vector<std::string> resolve_all(const std::string& path, const std::string& pack) const;
    const FileInfo& read(const std::string& path, const std::string& pack);
    int classify(const std::string& path, const std::string& pack);   // SurfaceClass or -1 (soft)
    int classify_line(const std::string& low, const FileInfo& fi);
    double half_width(const std::string& path, const std::string& pack);
    const Tile& dsf_entries(const std::string& path, const std::string& pack);
    double object_radius(const std::string& path, const std::string& pack);
    std::pair<double, bool> road_width(const std::string& path, const std::string& pack, int subtype);
    void once(const std::string& key, const std::string& msg);

    std::string xp_root_;
    const AirportIndex* index_;
    std::vector<std::string> block_, allow_;
    Log log_;
    bool library_ready_ = false;
    std::unordered_map<std::string, std::vector<std::string>> exports_;
    std::vector<std::string> scenery_packs_;
    std::map<std::pair<std::string, std::string>, double> object_radii_;
    std::map<std::pair<std::string, std::string>, std::map<int, std::pair<double, bool>>> road_widths_;
    std::map<std::pair<std::string, std::string>, int> class_cache_;
    std::map<std::pair<std::string, std::string>, FileInfo> files_;
    std::map<std::string, std::pair<std::pair<long long, long long>, Tile>> dsf_cache_;
    std::vector<std::string> dsf_order_;
    std::set<std::string> reported_;
    std::set<std::string> unresolved_;
};

}  // namespace zg
