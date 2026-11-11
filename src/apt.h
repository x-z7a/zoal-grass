// apt.dat: an index of every airport (where it is, where its record sits in which file) and a parser for one record.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "geo.h"

namespace zg {

struct Runway {
    double width = 0;
    int surface = 0;
    int shoulder = 0;               // surface code + 100 * shoulder width in metres (0 = none)
    LatLon ends[2];
    double overrun[2] = {0, 0};
    int order = 0;                  // position in the file; lower is drawn on top
};

struct Helipad {
    double lat = 0, lon = 0, heading = 0, length = 0, width = 0;
    int surface = 0, shoulder = 0, order = 0;
};

struct Pavement {
    int surface = 0;
    std::vector<Ring> loops;        // outer plus holes
    int order = 0;
};

struct Airport {
    std::string ident, name;
    int kind = 1;
    double elevation_ft = 0;
    bool has_datum = false;
    LatLon datum;
    std::vector<Runway> runways;
    std::vector<Helipad> helipads;
    std::vector<Pavement> pavements;
    std::vector<std::vector<Ring>> boundaries;
    std::vector<Pavement> draped;   // hard ground / occupied footprints from active scenery and OSM
    std::vector<std::pair<LatLon, LatLon>> sealanes;

    void for_each_point(const std::function<void(const LatLon&)>& fn) const;
    bool center(LatLon& out) const;
};

// Parse the first airport record in [data, data + len). Returns nullptr if there is none.
std::unique_ptr<Airport> parse_airport(const char* data, size_t len);

struct AirportEntry {
    std::string ident;
    double lat = 0, lon = 0, radius = 0;
    uint64_t offset = 0, length = 0;
    int file = 0;                   // index into AirportIndex::files
};

// Enabled scenery pack folders in scenery_packs.ini order (Global Airports resolved), absolute paths.
// Packs on a virtual file system are left out (and listed in *skipped): reading them can block while tiles stream.
std::vector<std::string> pack_dirs(const std::string& xp_root, std::vector<std::string>* skipped = nullptr);

class AirportIndex {
public:
    // Build (or load from cache) the index. Runs on a worker thread; log is thread-safe.
    bool build(const std::string& xp_root, const std::string& cache_path,
               const std::function<void(const std::string&)>& log);

    // Airports whose extent lies within radius_m of the position, nearest first.
    std::vector<std::pair<double, const AirportEntry*>> nearby(double lat, double lon, double radius_m) const;
    const AirportEntry* find(const std::string& ident) const;
    std::unique_ptr<Airport> load(const AirportEntry& e) const;

    std::vector<std::string> files;
    std::vector<AirportEntry> entries;
    std::string xp_root;

private:
    std::unordered_map<std::string, size_t> by_ident_;
    std::unordered_map<int64_t, std::vector<size_t>> grid_;
    double max_radius_ = 0;
};

}  // namespace zg
