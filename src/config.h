// User settings, read from zoal-grass.ini next to the plugin (written with defaults when absent).
#pragma once

#include <string>
#include <vector>

namespace zg {

struct Settings {
    bool enabled = true;
    // placement
    double radius_m = 200.0;           // grass appears within this many metres of the aircraft
    double hysteresis_m = 40.0;        // removed only once this much further away
    double spacing_m = 4.5;            // clump grid spacing; plant cards can extend beyond it
    double max_slope_deg = 12.0;
    double pavement_margin_m = 3.5;    // extra clearance beyond the full clump footprint
    double boundary_margin_m = 20.0;
    double fallback_margin_m = 120.0;
    double mask_cell_m = 1.0;
    bool grass_outside_airport = true;
    bool grass_on_turf_runways = false;
    bool grass_on_dirt = false;
    bool grass_on_gravel = false;
    bool use_dsf_polygons = true;
    std::string dsf_block_words;
    std::string dsf_allow_words;
    bool use_osm_roads = true;         // keep grass off OpenStreetMap roads, car parks and rail (downloads, cached)
    std::string osm_overpass_url = "https://overpass-api.de/api/interpreter";
    double osm_cache_days = 30.0;
    // look (changing any of these rebuilds the clump objects)
    std::string style = "auto";        // auto | spring | summer | late_summer | autumn | winter | custom
    std::string biome = "auto";        // auto (Koppen climate at the aircraft position) | temperate | ... (biome.h)
    double grass_height_cm = 25.0;     // average tuft height on normal ground
    double density = 1.0;              // tuft density factor (1 = as dense as the DINKIssTyle lawn at that height)
    double dry_mix = 0.15;             // custom style only: fraction of tufts that use the dry (yellow) texture
    double width_scale = 1.0;          // extra horizontal tuft scale
    double lod_near_m = 40.0;          // real_blades only: 3D blades closer than this
    double lod_mid_m = 140.0;          // real_blades only: full plant cards closer than this, fewer beyond
    bool real_blades = false;          // 3D blades close up (more triangles; the grass changes look near the camera)
    bool optimize_for_gpu = false;     // single-sided ATTR_no_cull meshes instead of two-sided instanced ones
    bool cast_shadows = false;
    bool normal_map = true;
    // diagnostics
    bool log_stats = false;
    // the settings window
    std::string ui_theme = "graphite"; // graphite | daylight | night

    void clamp();
    double open_pad_m() const { return grass_outside_airport ? radius_m + pavement_margin_m : 0.0; }
};

// Read settings from path, creating the file with defaults if absent. Warnings are appended to `warnings`.
Settings load_settings(const std::string& path, std::vector<std::string>& warnings);
bool save_settings(const std::string& path, const Settings& s);
bool settings_equal(const Settings& a, const Settings& b);   // every setting the ini holds

// Settings that change the pavement mask / the clump objects / the layout.
bool mask_changed(const Settings& a, const Settings& b);
bool look_changed(const Settings& a, const Settings& b);
bool layout_changed(const Settings& a, const Settings& b);

}  // namespace zg
