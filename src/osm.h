// OpenStreetMap roads around an airport, so grass is kept off roads, car parks and rail lines the scenery does not
// describe. Data comes from an Overpass API server (default https://overpass-api.de) on the worker thread and is
// cached on disk; only the bounding box of the airport is sent. OSM data: (c) OpenStreetMap contributors, ODbL.
#pragma once

#include <string>
#include <vector>

#include "apt.h"

namespace zg {

struct OsmStats {
    int ways = 0;       // ways in the answer
    int bands = 0;      // roads / paths / rail turned into paved bands (ways)
    int areas = 0;      // closed areas (car parks, paved squares)
    int skipped = 0;    // unpaved, tunnels, proposed, ...
};

// Hard ground from an Overpass XML answer (`out geom`): Pavement records with surface 1 (paved), order 0, like the
// draped polygons of a scenery pack. Line ways become one band per segment.
std::vector<Pavement> osm_hard_ground(const std::string& xml, OsmStats* stats = nullptr);

// The Overpass QL query for a bounding box (degrees).
std::string overpass_query(double s, double w, double n, double e);

struct OsmFetch {
    bool ok = false;
    bool cached = false;   // answered from the disk cache
    std::string xml;
    std::string error;
};

// The answer for the box: from cache_dir when a file for it is younger than max_age_days, else (allow_network) from
// the server, tried twice, and then cached. The box is rounded outward to 0.001 degrees, which is also the cache key
// with `ident`. Public Overpass servers can take a minute when busy: call it with allow_network only off the worker
// thread that builds the masks.
OsmFetch osm_fetch(const std::string& server_url, const std::string& cache_dir, const std::string& ident, double s,
                   double w, double n, double e, double max_age_days, bool allow_network, int timeout_s = 90);

}  // namespace zg
