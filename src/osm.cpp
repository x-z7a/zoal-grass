#include "osm.h"

#include "geo.h"
#include "http.h"
#include "platform.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace zg {

namespace {

// value of attribute `name` inside one XML tag (between '<' and '>'); "" if absent
std::string attr(const std::string& xml, size_t from, size_t to, const char* name) {
    std::string key = std::string(" ") + name + "=\"";
    size_t p = xml.find(key, from);
    if (p == std::string::npos || p >= to) return "";
    p += key.size();
    size_t q = xml.find('"', p);
    if (q == std::string::npos || q > to) return "";
    std::string v = xml.substr(p, q - p);
    // the few entities that matter for tag values
    for (auto& e : std::map<std::string, std::string>{{"&amp;", "&"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"},
                                                    {"&lt;", "<"}, {"&gt;", ">"}}) {
        for (size_t k = v.find(e.first); k != std::string::npos; k = v.find(e.first, k + 1)) v.replace(k, e.first.size(), e.second);
    }
    return v;
}

double number(const std::string& s) {   // leading number of a tag such as "7.5", "7 m", "3;2"
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    return end && end != s.c_str() && std::isfinite(v) ? v : 0.0;
}

bool unpaved(const std::string& surface) {
    static const char* kSoft[] = {"unpaved", "dirt", "earth", "ground", "grass", "sand", "mud", "gravel", "fine_gravel",
                                  "compacted", "pebblestone", "woodchips", "grass_paver", "rock", "snow", "ice"};
    for (const char* s : kSoft)
        if (surface == s) return true;
    return false;
}

bool paved(const std::string& surface) {
    static const char* kHard[] = {"paved", "asphalt", "concrete", "concrete:plates", "concrete:lanes", "paving_stones",
                                  "sett", "cobblestone", "chipseal", "metal", "wood", "bricks"};
    for (const char* s : kHard)
        if (surface == s) return true;
    return false;
}

// half width in metres of a road / path class, 0 if it is not hard ground
double road_half_width(const std::string& hw, const std::string& surface) {
    struct C { const char* cls; double half; };
    static const C kRoads[] = {
        {"motorway", 6.0},     {"trunk", 5.5},         {"primary", 5.0},     {"secondary", 4.5},
        {"tertiary", 4.0},     {"motorway_link", 3.5}, {"trunk_link", 3.5},  {"primary_link", 3.5},
        {"secondary_link", 3.5}, {"tertiary_link", 3.5}, {"unclassified", 3.5}, {"residential", 3.5},
        {"road", 3.5},         {"busway", 3.5},        {"living_street", 3.0}, {"pedestrian", 3.0},
        {"service", 2.5},
    };
    for (const C& c : kRoads)
        if (hw == c.cls) return unpaved(surface) ? 0.0 : c.half;
    // sidewalks and cycleways are paved unless tagged otherwise; field tracks and paths only when tagged paved
    if (hw == "footway" || hw == "cycleway" || hw == "steps") return unpaved(surface) ? 0.0 : 1.2;
    if (hw == "track") return paved(surface) ? 2.0 : 0.0;
    if (hw == "path" || hw == "bridleway") return paved(surface) ? 1.0 : 0.0;
    return 0.0;
}

std::string fixed3(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}

}  // namespace

std::vector<Pavement> osm_hard_ground(const std::string& xml, OsmStats* stats) {
    OsmStats st;
    std::vector<Pavement> out;
    for (size_t p = xml.find("<way "); p != std::string::npos; p = xml.find("<way ", p + 1)) {
        size_t end = xml.find("</way>", p);
        if (end == std::string::npos) break;
        ++st.ways;
        std::vector<LatLon> pts;
        std::map<std::string, std::string> tags;
        for (size_t q = xml.find('<', p + 1); q != std::string::npos && q < end; q = xml.find('<', q + 1)) {
            size_t close = xml.find('>', q);
            if (close == std::string::npos || close > end) break;
            if (xml.compare(q, 4, "<nd ") == 0) {
                std::string la = attr(xml, q, close, "lat"), lo = attr(xml, q, close, "lon");
                if (!la.empty() && !lo.empty()) pts.push_back({std::atof(la.c_str()), std::atof(lo.c_str())});
            } else if (xml.compare(q, 5, "<tag ") == 0) {
                tags[attr(xml, q, close, "k")] = attr(xml, q, close, "v");
            }
            q = close;
        }
        p = end;
        auto tag = [&](const char* k) {
            auto it = tags.find(k);
            return it == tags.end() ? std::string() : it->second;
        };
        // not on the ground, or not built
        std::string tunnel = tag("tunnel"), layer = tag("layer");
        bool hidden = (!tunnel.empty() && tunnel != "no") || tag("location") == "underground" || number(layer) < 0 ||
                      tag("indoor") == "yes";
        if (hidden || !tag("proposed").empty() || tag("highway") == "construction" || tag("highway") == "proposed" ||
            pts.size() < 2) {
            ++st.skipped;
            continue;
        }
        std::string hw = tag("highway"), surface = tag("surface"), rail = tag("railway");
        bool closed = pts.size() >= 4 && pts.front() == pts.back();
        bool area = closed && (tag("amenity") == "parking" || tag("area") == "yes" || !tag("area:highway").empty());
        if (area) {
            if (unpaved(surface) || (tag("amenity") != "parking" && hw.empty() && tag("area:highway").empty())) {
                ++st.skipped;
                continue;
            }
            Ring ring(pts.begin(), pts.end() - 1);   // closed ring, first point not repeated
            out.push_back({1, {ring}, 0});
            ++st.areas;
            continue;
        }
        double half = 0;
        if (!hw.empty()) half = road_half_width(hw, surface);
        else if (rail == "rail" || rail == "light_rail" || rail == "tram" || rail == "narrow_gauge" || rail == "subway")
            half = 2.0;
        if (half <= 0) {
            ++st.skipped;
            continue;
        }
        double width = number(tag("width"));
        double lanes = number(tag("lanes"));
        if (width >= 1.0 && width <= 60.0) half = width / 2;
        else if (lanes >= 1 && lanes <= 12 && !hw.empty()) half = std::max(half, lanes * 1.65 + 0.5);
        LocalProjection proj(pts[0].lat, pts[0].lon);
        for (size_t k = 0; k + 1 < pts.size(); ++k) {
            XY a = proj.to_xy(pts[k]), b = proj.to_xy(pts[k + 1]);
            double len = std::hypot(b.x - a.x, b.y - a.y);
            if (len < 1e-3) continue;
            double ux = (b.x - a.x) / len, uy = (b.y - a.y) / len;
            double ax = a.x - ux * half, ay = a.y - uy * half, bx = b.x + ux * half, by = b.y + uy * half;
            double vx = -uy * half, vy = ux * half;
            out.push_back({1,
                           {{proj.to_latlon(ax - vx, ay - vy), proj.to_latlon(bx - vx, by - vy),
                             proj.to_latlon(bx + vx, by + vy), proj.to_latlon(ax + vx, ay + vy)}},
                           0});
        }
        ++st.bands;
    }
    if (stats) *stats = st;
    return out;
}

std::string overpass_query(double s, double w, double n, double e) {
    std::string box = "(" + fixed3(s) + "," + fixed3(w) + "," + fixed3(n) + "," + fixed3(e) + ")";
    return "[out:xml][timeout:25];(way[\"highway\"]" + box + ";way[\"amenity\"=\"parking\"]" + box +
           ";way[\"area:highway\"]" + box + ";way[\"railway\"~\"^(rail|light_rail|tram|narrow_gauge|subway)$\"]" + box +
           ";);out geom;";
}

OsmFetch osm_fetch(const std::string& server_url, const std::string& cache_dir, const std::string& ident, double s,
                   double w, double n, double e, double max_age_days, bool allow_network, int timeout_s) {
    OsmFetch r;
    s = std::floor(s * 1000) / 1000;
    w = std::floor(w * 1000) / 1000;
    n = std::ceil(n * 1000) / 1000;
    e = std::ceil(e * 1000) / 1000;
    std::string safe;
    for (char c : ident) safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    std::string path = cache_dir + "/" + safe + "_" + fixed3(s) + "_" + fixed3(w) + "_" + fixed3(n) + "_" + fixed3(e) + ".osm";
    namespace fs = std::filesystem;
    std::error_code ec;
    auto when = fs::last_write_time(fs_path(path), ec);
    if (!ec) {
        double age_days = std::chrono::duration<double>(fs::file_time_type::clock::now() - when).count() / 86400.0;
        if (age_days >= 0 && age_days < max_age_days) {
            std::ifstream in(fs_path(path), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            r.xml = ss.str();
            if (r.xml.find("<osm") != std::string::npos) {
                r.ok = r.cached = true;
                return r;
            }
        }
    }
    if (!allow_network) {
        r.error = "not cached";
        return r;
    }
    HttpResult h;
    for (int attempt = 0; attempt < 2; ++attempt) {   // busy public servers answer 429 / 502 / 504 now and then
        h = http_post_form(server_url, "data=" + url_encode(overpass_query(s, w, n, e)), timeout_s);
        bool busy = h.ok && h.body.find("<remark>") != std::string::npos && h.body.find("runtime error") != std::string::npos;
        if (h.ok && !busy && h.body.find("<osm") != std::string::npos) break;
        r.error = busy ? "server busy (Overpass runtime error)" : h.ok ? "not an OSM answer" : h.error;
        h.ok = false;   // a partial answer: do not trust or cache it
    }
    if (!h.ok) return r;
    r.error.clear();
    r.ok = true;
    r.xml = std::move(h.body);
    make_dir(cache_dir);
    std::string tmp = path + ".tmp";
    {
        std::ofstream out(fs_path(tmp), std::ios::binary | std::ios::trunc);
        out << r.xml;
    }
    rename_file(tmp, path);
    return r;
}

}  // namespace zg
