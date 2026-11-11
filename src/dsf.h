// Draped polygons and painted lines from a scenery pack's DSF that are hard ground but are not in apt.dat
// (turn-around pads, aprons, footpaths, service roads). Only uncompressed DSFs are read.
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

// Parse an uncompressed DSF. want(def) picks which polygon definitions are extracted; owns(airport id) tells whether
// a polygon placed under an airport filter belongs to the pack. Throws std::runtime_error on bad data.
std::vector<DsfPolygon> parse_dsf_polygons(const uint8_t* data, size_t size,
                                           const std::function<bool(const std::string&)>& want,
                                           const std::function<bool(const std::string&)>& owns);

class GroundPolygons {
public:
    using Log = std::function<void(const std::string&)>;
    GroundPolygons(std::string xp_root, const AirportIndex* index, const std::string& block_words,
                   const std::string& allow_words, Log log);

    // Pavement records (order 0 = above apt.dat) for draped hard ground around the airport.
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
    };
    struct FileInfo {
        bool resolved = false;
        std::string real;
        bool has_info = false;
        PolInfo info;
    };

    void load_library();
    std::string resolve(const std::string& path, const std::string& pack) const;
    const FileInfo& read(const std::string& path, const std::string& pack);
    int classify(const std::string& path, const std::string& pack);   // SurfaceClass or -1 (soft)
    int classify_line(const std::string& low, const FileInfo& fi);
    double half_width(const std::string& path, const std::string& pack);
    const std::vector<Found>& dsf_entries(const std::string& path, const std::string& pack);
    void once(const std::string& key, const std::string& msg);

    std::string xp_root_;
    const AirportIndex* index_;
    std::vector<std::string> block_, allow_;
    Log log_;
    bool library_ready_ = false;
    std::unordered_map<std::string, std::string> exports_;
    std::map<std::pair<std::string, std::string>, int> class_cache_;
    std::map<std::pair<std::string, std::string>, FileInfo> files_;
    std::map<std::string, std::pair<std::pair<long long, long long>, std::vector<Found>>> dsf_cache_;
    std::vector<std::string> dsf_order_;
    std::set<std::string> reported_;
    std::set<std::string> unresolved_;
};

}  // namespace zg
