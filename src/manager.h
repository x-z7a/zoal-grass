// Places grass clump instances around the aircraft. The only module (besides the plugin entry) that talks to X-Plane.
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <XPLMDataAccess.h>
#include <XPLMInstance.h>
#include <XPLMScenery.h>

#include "apt.h"
#include "assets.h"
#include "config.h"
#include "dsf.h"
#include "mask.h"
#include "worker.h"

namespace zg {

class GrassManager {
public:
    GrassManager(std::string plugin_dir, std::string xp_root, Settings settings);
    ~GrassManager();

    float tick(float since_last);            // flight loop body; returns the next interval
    void apply_settings(const Settings& s, bool force = false);
    const Settings& settings() const { return settings_; }
    void set_enabled(bool on);
    void reset();                            // new flight / airport: forget everything placed
    void refresh_terrain();                  // scenery tiles (re)loaded: keep the grass, re-try the empty cells
    std::string status() const;
    const std::string& active_style() const { return active_style_; }
    const std::string& active_biome() const { return active_biome_; }
    void log(const std::string& msg) const;

private:
    struct Cell {
        XPLMInstanceRef inst = nullptr;      // nullptr: no grass here
        double lat = 0, lon = 0, alt = 0;
        float heading = 0;
    };
    struct Pending {
        int i, j;
        double x, y;
    };
    struct MaskJob {
        std::atomic<bool> done{false};
        std::unique_ptr<PavementMask> mask;
        std::string message;
    };
    struct AirportState {
        AirportEntry entry;
        std::shared_ptr<MaskJob> job;
        std::unique_ptr<PavementMask> mask;
        std::unordered_map<int64_t, Cell> cells;
        std::unordered_set<int64_t> live;
        std::vector<Pending> pending;
        std::vector<Pending> retry;          // terrain not loaded yet (streaming ortho): try again later
        double retry_at = 0;
        bool has_scan = false;
        double sx = 0, sy = 0;
        bool has_heading = false;
        double hx = 0, hy = 0;
        int scans = 0;
        bool alt_known = false, alt_ok = false;
        double alt = 0;
    };

    void setup();
    bool rebuild_objects();
    void unload_objects();
    void drop_all();
    void destroy_cells(AirportState& st);
    void discover(double lat, double lon);
    void activate(const AirportEntry& e);
    bool work(AirportState& st, double lat, double lon, double ref_y, double until);
    void scan(AirportState& st, double ax, double ay);
    int place(AirportState& st, double ax, double ay, double ref_y, double until);
    enum class Made { Placed, Rejected, Missed };
    Made create(AirportState& st, int i, int j, double px, double py, double ref_y, double min_ny, Cell& out);
    bool ground_altitude(AirportState& st, double lat, double lon, double ref_y);
    void follow_origin();
    // season style and biome for a position: "auto" -> Koppen climate there, the sim date and snow cover;
    // keep_climate: the climate of the current look instead of the one at (lat, lon)
    std::string resolve_look(double lat, double lon, int& biome, int& koppen, bool keep_climate = false) const;
    void flush_logs();
    double now() const;

    std::string plugin_dir_, xp_root_, cache_dir_, resources_dir_;
    Settings settings_;
    Worker worker_;
    GrassAssets assets_;
    bool ready_ = false;
    bool objects_dirty_ = true;
    std::vector<XPLMObjectRef> normal_objs_, mowed_objs_;
    XPLMProbeRef probe_ = nullptr;
    XPLMDataRef lat_ref_ = nullptr, lon_ref_ = nullptr, y_ref_ = nullptr, latr_ref_ = nullptr, lonr_ref_ = nullptr;
    XPLMDataRef fps_ref_ = nullptr, day_ref_ = nullptr, snow_ref_ = nullptr;
    bool snow_hidden_ = false;
    std::string active_style_, active_biome_;
    int active_koppen_ = 0;
    ClimateMap climate_;
    double next_style_check_ = 0, last_lat_ = 0, last_lon_ = 0;
    void (*autoshift_)(XPLMInstanceRef) = nullptr;
    bool origin_known_ = false;
    float origin_lat_ = 0, origin_lon_ = 0;

    std::shared_ptr<AirportIndex> index_;          // set once the worker finished it
    std::shared_ptr<std::atomic<int>> index_state_; // 0 building, 1 ready, -1 failed
    std::map<std::string, std::unique_ptr<AirportState>> airports_;
    std::unordered_set<std::string> failed_;
    double next_discover_ = 0;
    double next_stats_ = 0;
    int count_ = 0;
    long created_ = 0, rejected_ = 0, missed_ = 0;
    double tick_ms_ = 0, tick_max_ = 0;
    BuiltObjects built_;

    // worker-thread state: the draped-polygon reader (recreated when its settings change)
    std::shared_ptr<std::unique_ptr<GroundPolygons>> ground_;
    std::shared_ptr<std::string> ground_key_;
};

}  // namespace zg
