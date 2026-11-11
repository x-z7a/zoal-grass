#include "config.h"

#include "biome.h"
#include "platform.h"
#include "style.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace zg {

namespace {

template <class T>
T clampv(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

enum class Kind { Bool, Num, Str };
struct Field {
    const char* name;
    Kind kind;
    size_t offset;
    const char* comment;
};

#define F_BOOL(n, c) {#n, Kind::Bool, offsetof(Settings, n), c}
#define F_NUM(n, c) {#n, Kind::Num, offsetof(Settings, n), c}
#define F_STR(n, c) {#n, Kind::Str, offsetof(Settings, n), c}

const Field kFields[] = {
    F_BOOL(enabled, "master switch (also in Plugins > zoal-grass)"),
    F_NUM(radius_m, "grass appears within this many metres of the aircraft"),
    F_NUM(hysteresis_m, "grass is removed only once it is this much further away (stops flicker at the edge)"),
    F_NUM(spacing_m, "distance between clumps on the placement grid, metres (each clump covers slightly more)"),
    F_NUM(max_slope_deg, "no grass on ground steeper than this"),
    F_NUM(pavement_margin_m, "extra keep-out distance beyond the full grass footprint, metres"),
    F_NUM(boundary_margin_m, "how far outside the airport boundary grass may extend (when grass_outside_airport = false)"),
    F_NUM(fallback_margin_m, "grass area around the pavement for airports without a boundary"),
    F_NUM(mask_cell_m, "resolution of the pavement mask, metres"),
    F_BOOL(grass_outside_airport, "true: grass anywhere within radius_m that is not pavement; false: only inside the airport boundary"),
    F_BOOL(grass_on_turf_runways, "grow (half-height) grass on turf runways and helipads"),
    F_BOOL(grass_on_dirt, "grow grass on dirt surfaces"),
    F_BOOL(grass_on_gravel, "grow grass on gravel surfaces"),
    F_BOOL(use_dsf_polygons, "keep grass off pavement, roads, autogen and object footprints in the active scenery stack"),
    F_STR(dsf_block_words, "comma separated words: a .pol/.lin library path containing one is always pavement"),
    F_STR(dsf_allow_words, "comma separated words: a .pol/.lin library path containing one is never pavement"),
    F_BOOL(use_osm_roads, "keep grass off OpenStreetMap roads, car parks and rail lines (downloaded once per airport from the Overpass server below, then cached; only the airport's bounding box is sent)"),
    F_STR(osm_overpass_url, "Overpass API server for the OpenStreetMap roads"),
    F_NUM(osm_cache_days, "re-download an airport's OpenStreetMap roads after this many days (1 .. 3650)"),
    F_STR(style, "season / style: auto (from date, position and climate), spring, summer, late_summer, autumn, winter, custom"),
    F_STR(biome, "plants: auto (from the Koppen climate at the aircraft position), temperate, continental, mediterranean, steppe, desert, tropical, savanna, tundra"),
    F_NUM(grass_height_cm, "average grass height, centimetres (5 .. 80)"),
    F_NUM(density, "tuft density factor (0.2 .. 2.0); higher is lusher and costs more GPU"),
    F_NUM(dry_mix, "custom style only: fraction of tufts drawn with the dry/yellow texture (0 .. 1)"),
    F_NUM(width_scale, "extra horizontal tuft scale (0.5 .. 2.0)"),
    F_NUM(lod_near_m, "real_blades only: 3D blades within this camera distance, metres"),
    F_NUM(lod_mid_m, "real_blades only: full plant cards within this camera distance, fewer beyond"),
    F_BOOL(wind_sway, "grass near the camera sways with the sim's wind (experimental: animated objects; check your FPS)"),
    F_NUM(wind_sway_deg, "how far the grass leans in a strong wind, degrees (1 .. 20)"),
    F_BOOL(terrain_tilt, "tilt each grass clump to the terrain slope, so its ground cover stays just above the ground"),
    F_BOOL(real_blades, "add the DINKIssTyle-coloured 3D blades close up (more triangles; else the same Blender grass at every distance)"),
    F_BOOL(optimize_for_gpu, "false: two-sided meshes that X-Plane hardware-instances (CPU friendly); true: ATTR_no_cull meshes with half the triangles"),
    F_BOOL(cast_shadows, "let the grass cast shadows (costly)"),
    F_BOOL(normal_map, "use the DINKIssTyle normal map"),
    F_BOOL(log_stats, "write a status line to Log.txt every 30 s"),
};

}  // namespace

void Settings::clamp() {
    radius_m = clampv(radius_m, 30.0, 1500.0);
    hysteresis_m = clampv(hysteresis_m, 5.0, 500.0);
    spacing_m = clampv(spacing_m, 1.5, 50.0);
    max_slope_deg = clampv(max_slope_deg, 1.0, 60.0);
    pavement_margin_m = clampv(pavement_margin_m, 0.0, 30.0);
    boundary_margin_m = clampv(boundary_margin_m, 0.0, 500.0);
    fallback_margin_m = clampv(fallback_margin_m, 0.0, 1000.0);
    mask_cell_m = clampv(mask_cell_m, 0.5, 10.0);
    grass_height_cm = clampv(grass_height_cm, 5.0, 80.0);
    density = clampv(density, 0.2, 2.0);
    dry_mix = clampv(dry_mix, 0.0, 1.0);
    width_scale = clampv(width_scale, 0.5, 2.0);
    lod_near_m = clampv(lod_near_m, 10.0, 1000.0);
    lod_mid_m = clampv(lod_mid_m, lod_near_m + 10.0, 3000.0);
    style = lower(trim(style));
    if (style != "auto" && !find_style(style)) style = "auto";
    wind_sway_deg = clampv(wind_sway_deg, 1.0, 20.0);
    osm_cache_days = clampv(osm_cache_days, 1.0, 3650.0);
    osm_overpass_url = trim(osm_overpass_url);
    if (osm_overpass_url.rfind("http", 0) != 0) osm_overpass_url = "https://overpass-api.de/api/interpreter";
    biome = lower(trim(biome));
    if (biome != "auto" && find_biome(biome) < 0) biome = "auto";
}

Settings load_settings(const std::string& path, std::vector<std::string>& warnings) {
    Settings s;
    std::ifstream in(fs_path(path));
    if (!in) {
        if (!save_settings(path, s)) warnings.push_back("could not write default settings to " + path);
        s.clamp();
        return s;
    }
    std::string line;
    while (std::getline(in, line)) {
        auto semi = line.find(';');
        if (semi != std::string::npos) line = line.substr(0, semi);
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty() || line[0] == '[') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        bool known = false;
        for (const Field& f : kFields) {
            if (key != f.name) continue;
            known = true;
            char* base = reinterpret_cast<char*>(&s) + f.offset;
            if (f.kind == Kind::Bool) {
                std::string v = lower(val);
                if (v == "true" || v == "yes" || v == "on" || v == "1") *reinterpret_cast<bool*>(base) = true;
                else if (v == "false" || v == "no" || v == "off" || v == "0") *reinterpret_cast<bool*>(base) = false;
                else warnings.push_back("ignoring invalid value for " + key + ": " + val);
            } else if (f.kind == Kind::Num) {
                char* end = nullptr;
                double d = std::strtod(val.c_str(), &end);
                if (end && end != val.c_str() && *end == 0) *reinterpret_cast<double*>(base) = d;
                else warnings.push_back("ignoring invalid value for " + key + ": " + val);
            } else {
                *reinterpret_cast<std::string*>(base) = val;
            }
        }
        if (!known && key != "flowers") warnings.push_back("unknown setting " + key);   // flowers: retired
    }
    s.clamp();
    return s;
}

bool save_settings(const std::string& path, const Settings& s) {
    std::ofstream out(fs_path(path), std::ios::trunc);
    if (!out) return false;
    out << "; zoal-grass settings. Edit, save, then use Plugins > zoal-grass > Reload settings (or restart X-Plane).\n"
           "; Plugins > zoal-grass > Save settings writes the values currently in use back to this file.\n\n[grass]\n";
    for (const Field& f : kFields) {
        const char* base = reinterpret_cast<const char*>(&s) + f.offset;
        out << "; " << f.comment << "\n" << f.name << " = ";
        if (f.kind == Kind::Bool) out << (*reinterpret_cast<const bool*>(base) ? "true" : "false");
        else if (f.kind == Kind::Num) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%g", *reinterpret_cast<const double*>(base));
            out << buf;
        } else out << *reinterpret_cast<const std::string*>(base);
        out << "\n\n";
    }
    return bool(out);
}

bool mask_changed(const Settings& a, const Settings& b) {
    return a.pavement_margin_m != b.pavement_margin_m || a.boundary_margin_m != b.boundary_margin_m ||
           a.fallback_margin_m != b.fallback_margin_m || a.mask_cell_m != b.mask_cell_m ||
           a.grass_outside_airport != b.grass_outside_airport || a.grass_on_turf_runways != b.grass_on_turf_runways ||
           a.grass_on_dirt != b.grass_on_dirt || a.grass_on_gravel != b.grass_on_gravel ||
           a.use_dsf_polygons != b.use_dsf_polygons || a.dsf_block_words != b.dsf_block_words ||
           a.dsf_allow_words != b.dsf_allow_words || a.use_osm_roads != b.use_osm_roads ||
           a.osm_overpass_url != b.osm_overpass_url ||
           (b.grass_outside_airport && b.radius_m > a.radius_m);
}

bool look_changed(const Settings& a, const Settings& b) {
    return a.style != b.style || a.biome != b.biome || a.grass_height_cm != b.grass_height_cm || a.density != b.density || a.dry_mix != b.dry_mix ||
           a.width_scale != b.width_scale || a.lod_near_m != b.lod_near_m || a.lod_mid_m != b.lod_mid_m ||
           a.optimize_for_gpu != b.optimize_for_gpu || a.real_blades != b.real_blades || a.wind_sway != b.wind_sway || a.cast_shadows != b.cast_shadows ||
           a.normal_map != b.normal_map || a.spacing_m != b.spacing_m || a.radius_m != b.radius_m;
}

bool layout_changed(const Settings& a, const Settings& b) {
    return a.terrain_tilt != b.terrain_tilt || a.spacing_m != b.spacing_m || a.max_slope_deg != b.max_slope_deg || b.radius_m < a.radius_m;
}

}  // namespace zg
