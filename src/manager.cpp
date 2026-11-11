#include "manager.h"
#include "style.h"

#include <XPLMGraphics.h>
#include <XPLMPlugin.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace zg {

namespace {

constexpr double kDiscoverEveryS = 2.0;
constexpr double kActivateMarginM = 1000.0;
constexpr double kDeactivateExtraM = 1000.0;
constexpr size_t kMaxActiveAirports = 3;
constexpr double kJitter = 0.0;             // no jitter: square clumps exactly tile the placement grid
constexpr double kSnapRings[2] = {0.5, 1.0};
constexpr double kForwardBias = 0.7;
constexpr double kCatchupQueue = 300;
constexpr int kMaxInstances = 64000;
constexpr double kBudgetMs = 1.5;
constexpr double kCatchupBudgetMs = 4.0;
constexpr int kPruneEvery = 16;
constexpr double kSinkM = 0.03;
constexpr double kFootprintOverlap = 1.0;    // clump side = spacing (overlaps would show as denser lines)

int64_t key(int i, int j) { return (int64_t(i) << 32) ^ (int64_t(uint32_t(j))); }
int key_i(int64_t k) { return int(k >> 32); }
int key_j(int64_t k) { return int(int32_t(uint32_t(k & 0xffffffff))); }

double hash01(int i, int j, int k) {
    uint32_t h = uint32_t(i) * 0x9E3779B1u + uint32_t(j) * 0x85EBCA6Bu + uint32_t(k) * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h / 4294967296.0;
}

void row_span(double cx, double cy, int j, double s, double radius, int& first, int& last) {
    double dy = (j + 0.5) * s - cy;
    double r2 = radius * radius - dy * dy;
    if (r2 < 0) { first = 1; last = 0; return; }
    double half = std::sqrt(r2);
    first = int(std::ceil((cx - half) / s - 0.5));
    last = int(std::floor((cx + half) / s - 0.5));
}

bool snap(const PavementMask& mask, double px, double py, double spacing, double& ox, double& oy) {
    for (double ring : kSnapRings) {
        double reach = ring * spacing;
        for (int k = 0; k < 8; ++k) {
            double dx = std::cos(k * kPi / 4), dy = std::sin(k * kPi / 4);
            if (mask.grass_ok(px + dx * reach, py + dy * reach)) {
                double lo = 0, hi = reach;
                for (int n = 0; n < 4; ++n) {
                    double mid = (lo + hi) / 2;
                    if (mask.grass_ok(px + dx * mid, py + dy * mid)) hi = mid;
                    else lo = mid;
                }
                ox = px + dx * hi;
                oy = py + dy * hi;
                return true;
            }
        }
    }
    return false;
}

}  // namespace

GrassManager::GrassManager(std::string plugin_dir, std::string xp_root, Settings settings)
    : plugin_dir_(std::move(plugin_dir)), xp_root_(std::move(xp_root)), settings_(std::move(settings)) {
    cache_dir_ = plugin_dir_ + "/cache";
    resources_dir_ = plugin_dir_ + "/resources";
    ground_ = std::make_shared<std::unique_ptr<GroundPolygons>>();
    ground_key_ = std::make_shared<std::string>();
}

GrassManager::~GrassManager() {
    drop_all();
    unload_objects();
    if (probe_) XPLMDestroyProbe(probe_);
    probe_ = nullptr;
}

double GrassManager::now() const {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void GrassManager::log(const std::string& msg) const {
    std::string line = "zoal-grass: " + msg + "\n";
    XPLMDebugString(line.c_str());
}

void GrassManager::flush_logs() {
    for (auto& m : worker_.drain_logs()) log(m);
}

void GrassManager::setup() {
    lat_ref_ = XPLMFindDataRef("sim/flightmodel/position/latitude");
    lon_ref_ = XPLMFindDataRef("sim/flightmodel/position/longitude");
    y_ref_ = XPLMFindDataRef("sim/flightmodel/position/local_y");
    latr_ref_ = XPLMFindDataRef("sim/flightmodel/position/lat_ref");
    lonr_ref_ = XPLMFindDataRef("sim/flightmodel/position/lon_ref");
    fps_ref_ = XPLMFindDataRef("sim/time/framerate_period");
    day_ref_ = XPLMFindDataRef("sim/time/local_date_days");          // the sim's date, not the computer's
    snow_ref_ = XPLMFindDataRef("sim/weather/region/snow_cover");
    if (!probe_) probe_ = XPLMCreateProbe(xplm_ProbeY);
    autoshift_ = reinterpret_cast<void (*)(XPLMInstanceRef)>(XPLMFindSymbol("XPLMInstanceSetAutoShift"));
    if (!climate_.load(resources_dir_ + "/climate_kg.bin")) log("climate map missing: plants chosen by latitude only");
    if (!assets_.load(resources_dir_, [this](const std::string& m) { log(m); })) {
        log("grass meshes missing; grass is disabled");
        settings_.enabled = false;
    }
    // airport index on the worker thread
    auto index = std::make_shared<AirportIndex>();
    auto state = std::make_shared<std::atomic<int>>(0);
    index_state_ = state;
    std::string root = xp_root_, cache = plugin_dir_ + "/airport_index.txt";
    Worker* w = &worker_;
    worker_.post([index, state, root, cache, w] {
        auto t0 = std::chrono::steady_clock::now();
        bool ok = index->build(root, cache, [w](const std::string& m) { w->log(m); });
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        char buf[160];
        std::snprintf(buf, sizeof buf, "%zu airports known from %zu apt.dat files (%.1f s)", index->entries.size(),
                      index->files.size(), s);
        w->log(buf);
        state->store(ok ? 1 : -1);
    });
    index_ = index;
    ready_ = true;
    log(std::string("ready, X-Plane folder ") + xp_root_ + ", autoshift " + (autoshift_ ? "yes" : "no"));
}

void GrassManager::unload_objects() {
    for (auto o : normal_objs_) XPLMUnloadObject(o);
    for (auto o : mowed_objs_) XPLMUnloadObject(o);
    normal_objs_.clear();
    mowed_objs_.clear();
}

bool GrassManager::rebuild_objects() {
    for (auto& kv : airports_) {
        destroy_cells(*kv.second);
        kv.second->has_scan = false;
    }
    unload_objects();
    objects_dirty_ = false;
    if (!assets_.loaded()) return false;
    int biome = kTemperate;
    active_style_ = resolve_look(last_lat_, last_lon_, biome, active_koppen_);
    active_biome_ = kBiomes[biome].name;
    const Style* st = find_style(active_style_);
    if (!st) st = find_style("summer");
    LookParams p;
    p.mix = species_mix(biome, active_style_);
    p.height_m = settings_.grass_height_cm / 100.0 * st->height_factor;
    p.density = settings_.density;
    p.dry_mix = st->dry_mix >= 0 ? st->dry_mix : settings_.dry_mix;
    p.texture = st->texture;
    p.flowers_per_m2 = st->flowers_per_m2 * settings_.flowers;
    p.flower_mask = st->flower_mask;
    p.width_scale = settings_.width_scale;
    p.footprint_m = settings_.spacing_m * kFootprintOverlap;
    p.lod_near = settings_.lod_near_m;
    p.lod_mid = settings_.lod_mid_m;
    p.lod_far = std::max(p.lod_mid + 50.0, settings_.radius_m + settings_.hysteresis_m + 150.0);
    p.gpu = settings_.optimize_for_gpu;
    p.real_blades = settings_.real_blades;
    p.shadows = settings_.cast_shadows;
    p.normal_map = settings_.normal_map;
    BuiltObjects built;
    auto lg = [this](const std::string& m) { log(m); };
    if (!assets_.build(resources_dir_, cache_dir_, p, built, lg)) {
        log("could not build the grass objects");
        return false;
    }
    auto rel = [this](const std::string& path) {
        // XPLMLoadObject wants paths relative to the X-Plane folder
        std::string root = xp_root_;
        if (!root.empty() && root.back() != '/') root += '/';
        return path.rfind(root, 0) == 0 ? path.substr(root.size()) : path;
    };
    for (auto& path : built.normal) {
        XPLMObjectRef o = XPLMLoadObject(rel(path).c_str());
        if (o) normal_objs_.push_back(o);
        else log("could not load " + path);
    }
    for (auto& path : built.mowed) {
        XPLMObjectRef o = XPLMLoadObject(rel(path).c_str());
        if (o) mowed_objs_.push_back(o);
        else log("could not load " + path);
    }
    built_ = built;
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "built %zu grass objects, style %s%s, %s plants (Koppen %s): %.0f cm, %d blade tufts + %d plant cards + "
                  "%d flowers per %.1f m clump, triangles per level %d/%d/%d/%d (%s)",
                  normal_objs_.size() + mowed_objs_.size(), active_style_.c_str(),
                  settings_.style == "auto" ? " (auto)" : "", active_biome_.c_str(), koppen_symbol(active_koppen_),
                  p.height_m * p.mix.height * 100, built.tufts, built.cards, built.flowers, p.footprint_m, built.tris[0],
                  built.tris[1], built.tris[2], built.tris[3], p.gpu ? "GPU mode" : "CPU mode");
    log(buf);
    return !normal_objs_.empty();
}

void GrassManager::destroy_cells(AirportState& st) {
    for (auto& kv : st.cells) {
        if (kv.second.inst) {
            XPLMDestroyInstance(kv.second.inst);
            --count_;
        }
    }
    st.cells.clear();
    st.live.clear();
    st.pending.clear();
    st.retry.clear();
}

void GrassManager::drop_all() {
    for (auto& kv : airports_) destroy_cells(*kv.second);
    airports_.clear();
    failed_.clear();
    next_discover_ = 0;
}

void GrassManager::reset() {
    drop_all();
    auto ground = ground_;
    worker_.post([ground] { ground->reset(); });   // scenery may have changed: library and DSF caches go too
}

void GrassManager::refresh_terrain() {
    for (auto& kv : airports_) {
        AirportState& st = *kv.second;
        for (auto it = st.cells.begin(); it != st.cells.end();) {
            if (!it->second.inst) it = st.cells.erase(it);
            else ++it;
        }
        st.has_scan = false;
        st.retry_at = 0;
        if (!st.alt_ok) st.alt_known = false;
    }
}

void GrassManager::set_enabled(bool on) {
    settings_.enabled = on;
    if (!on) drop_all();
}

void GrassManager::apply_settings(const Settings& s, bool force) {
    Settings old = settings_;
    settings_ = s;
    if (force) {
        objects_dirty_ = true;
        reset();
        return;
    }
    if (look_changed(old, s)) objects_dirty_ = true;
    if (mask_changed(old, s)) {
        drop_all();
    } else if (layout_changed(old, s)) {
        for (auto& kv : airports_) {
            destroy_cells(*kv.second);
            kv.second->has_scan = false;
        }
    } else if (old.radius_m != s.radius_m || old.hysteresis_m != s.hysteresis_m) {
        for (auto& kv : airports_) kv.second->has_scan = false;
    }
    if (!s.enabled) drop_all();
}

float GrassManager::tick(float /*since_last*/) {
    double t0 = now();
    float interval = 1.0f;
    flush_logs();
    if (!settings_.enabled) {
        if (!airports_.empty()) drop_all();
        return 1.0f;
    }
    if (!ready_) setup();
    if (!settings_.enabled) return 1.0f;
    int state = index_state_ ? index_state_->load() : -1;
    if (state == 0) return 0.5f;
    if (state < 0) {
        log("the airport index could not be built; grass is disabled");
        settings_.enabled = false;
        return 1.0f;
    }
    double lat = XPLMGetDatad(lat_ref_), lon = XPLMGetDatad(lon_ref_);
    if (lat == 0.0 && lon == 0.0) return 1.0f;
    double ref_y = XPLMGetDatad(y_ref_);
    last_lat_ = lat;
    last_lon_ = lon;
    if (t0 >= next_style_check_) {
        next_style_check_ = t0 + 5.0;
        // auto: deep snow on the ground buries the grass
        float snow = snow_ref_ ? XPLMGetDataf(snow_ref_) : 0.0f;
        bool hide = settings_.style == "auto" && snow >= 0.6f;
        if (hide != snow_hidden_) {
            snow_hidden_ = hide;
            char buf[96];
            std::snprintf(buf, sizeof buf, "snow cover %.0f%%: grass %s", snow * 100.0, hide ? "hidden" : "shown again");
            log(buf);
            if (hide) drop_all();
        }
        // a new climate (flying elsewhere) is only taken up while no airport has grass: rebuilding takes a moment,
        // and an airport on a climate border must not flip between looks. Date and snow apply at once.
        int biome, koppen;
        std::string style = resolve_look(lat, lon, biome, koppen, !airports_.empty());
        if (!objects_dirty_ && !active_style_.empty() && (style != active_style_ || active_biome_ != kBiomes[biome].name)) {
            log("look changed to " + style + ", " + kBiomes[biome].name + " plants (Koppen " + koppen_symbol(koppen) +
                "; sim date / snow cover / position)");
            objects_dirty_ = true;
        }
    }
    if (snow_hidden_) return 1.0f;
    if (objects_dirty_) rebuild_objects();   // after the position is known, so "auto" picks the right season
    if (normal_objs_.empty()) return 2.0f;
    if (t0 >= next_discover_) {
        next_discover_ = t0 + kDiscoverEveryS;
        discover(lat, lon);
    }
    follow_origin();
    size_t backlog = 0;
    for (auto& kv : airports_) backlog += kv.second->pending.size();
    double spend = kBudgetMs + (kCatchupBudgetMs - kBudgetMs) * std::min(1.0, double(backlog) / kCatchupQueue);
    double until = t0 + spend / 1000.0;
    bool busy = false;
    for (auto& kv : airports_) {
        AirportState& st = *kv.second;
        if (st.job) {
            if (!st.job->done.load()) { busy = true; continue; }
            st.mask = std::move(st.job->mask);
            if (!st.job->message.empty()) log(st.job->message);
            st.job.reset();
            if (!st.mask) failed_.insert(st.entry.ident);
            busy = true;
            continue;
        }
        busy |= work(st, lat, lon, ref_y, until);
    }
    if (settings_.log_stats && t0 >= next_stats_) {
        next_stats_ = t0 + 30.0;
        log(status());
    }
    double ms = (now() - t0) * 1000.0;
    tick_ms_ += (ms - tick_ms_) * 0.05;
    tick_max_ = std::max(tick_max_ * 0.995, ms);
    interval = busy ? -1.0f : (airports_.empty() ? 1.0f : 0.25f);
    return interval;
}

void GrassManager::discover(double lat, double lon) {
    double reach = settings_.radius_m + kActivateMarginM;
    auto near = index_->nearby(lat, lon, reach);
    size_t n = 0;
    for (auto& d : near) {
        if (n++ >= kMaxActiveAirports) break;
        const AirportEntry& e = *d.second;
        if (!airports_.count(e.ident) && !failed_.count(e.ident)) activate(e);
    }
    std::unordered_set<std::string> keep;
    for (auto& d : index_->nearby(lat, lon, reach + kDeactivateExtraM)) keep.insert(d.second->ident);
    for (auto it = airports_.begin(); it != airports_.end();) {
        if (!keep.count(it->first)) {
            destroy_cells(*it->second);
            log(it->first + " left behind");
            it = airports_.erase(it);
        } else {
            ++it;
        }
    }
}

void GrassManager::activate(const AirportEntry& e) {
    auto st = std::make_unique<AirportState>();
    st->entry = e;
    auto job = std::make_shared<MaskJob>();
    st->job = job;
    std::shared_ptr<AirportIndex> index = index_;
    Settings cfg = settings_;
    auto ground = ground_;
    auto ground_key = ground_key_;
    Worker* w = &worker_;
    AirportEntry entry = e;
    worker_.post([job, index, cfg, ground, ground_key, w, entry] {
        auto t0 = std::chrono::steady_clock::now();
        auto ap = index->load(entry);
        if (!ap) {
            job->message = "could not read " + entry.ident + " from " + index->files[size_t(entry.file)];
            job->done.store(true);
            return;
        }
        char buf[256];
        std::snprintf(buf, sizeof buf, "%s %s: %zu runways, %zu pavements", ap->ident.c_str(), ap->name.c_str(),
                      ap->runways.size(), ap->pavements.size());
        w->log(buf);
        if (cfg.use_dsf_polygons) {
            std::string gk = cfg.dsf_block_words + "|" + cfg.dsf_allow_words;
            if (!*ground || *ground_key != gk) {
                ground->reset(new GroundPolygons(index->xp_root, index.get(), cfg.dsf_block_words, cfg.dsf_allow_words,
                                                 [w](const std::string& m) { w->log(m); }));
                *ground_key = gk;
            }
            try {
                double pad = std::max({cfg.boundary_margin_m, cfg.fallback_margin_m, cfg.open_pad_m()}) + 30.0;
                ap->draped = (*ground)->collect(*ap, entry, pad);
            } catch (const std::exception& ex) {
                w->log(entry.ident + ": draped polygons skipped after an error: " + ex.what());
            }
        }
        MaskSettings ms;
        ms.cell_m = cfg.mask_cell_m;
        ms.margin_m = cfg.pavement_margin_m;
        ms.boundary_margin_m = cfg.boundary_margin_m;
        ms.fallback_margin_m = cfg.fallback_margin_m;
        ms.grass_on_dirt = cfg.grass_on_dirt;
        ms.grass_on_gravel = cfg.grass_on_gravel;
        ms.grass_on_turf_runways = cfg.grass_on_turf_runways;
        ms.open_pad_m = cfg.open_pad_m();
        job->mask = build_mask(*ap, ms);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (job->mask) {
            std::snprintf(buf, sizeof buf, "%s: pavement mask %dx%d cells of %.1f m, %zu draped polygons (%.2f s)",
                          entry.ident.c_str(), job->mask->w, job->mask->h, job->mask->cell, ap->draped.size(), s);
            job->message = buf;
        } else {
            job->message = entry.ident + " has no usable geometry";
        }
        job->done.store(true);
    });
    airports_[e.ident] = std::move(st);
}

bool GrassManager::work(AirportState& st, double lat, double lon, double ref_y, double until) {
    if (!st.mask) return false;
    XY a = st.mask->proj.to_xy(lat, lon);
    if (!st.retry.empty() && now() >= st.retry_at) {
        // put missed cells back at the low-priority end of the queue
        st.pending.insert(st.pending.begin(), st.retry.begin(), st.retry.end());
        st.retry.clear();
    }
    if (!st.has_scan || std::hypot(a.x - st.sx, a.y - st.sy) > std::max(10.0, settings_.spacing_m)) scan(st, a.x, a.y);
    if (!st.pending.empty() && now() < until) place(st, a.x, a.y, ref_y, until);
    return !st.pending.empty() && count_ < kMaxInstances;
}

void GrassManager::scan(AirportState& st, double ax, double ay) {
    const double s = settings_.spacing_m, radius = settings_.radius_m;
    const double far_sq = (radius + settings_.hysteresis_m) * (radius + settings_.hysteresis_m);
    const PavementMask& mask = *st.mask;
    // drop clumps that fell out of range
    for (auto it = st.live.begin(); it != st.live.end();) {
        int i = key_i(*it), j = key_j(*it);
        double dx = (i + 0.5) * s - ax, dy = (j + 0.5) * s - ay;
        if (dx * dx + dy * dy > far_sq) {
            auto c = st.cells.find(*it);
            if (c != st.cells.end()) {
                if (c->second.inst) {
                    XPLMDestroyInstance(c->second.inst);
                    --count_;
                }
                st.cells.erase(c);
            }
            it = st.live.erase(it);
        } else {
            ++it;
        }
    }
    if (++st.scans % kPruneEvery == 0) {
        for (auto it = st.cells.begin(); it != st.cells.end();) {
            int i = key_i(it->first), j = key_j(it->first);
            double dx = (i + 0.5) * s - ax, dy = (j + 0.5) * s - ay;
            if (!it->second.inst && dx * dx + dy * dy > far_sq) it = st.cells.erase(it);
            else ++it;
        }
    }
    bool had_prev = st.has_scan;
    double px0 = st.sx, py0 = st.sy;
    if (had_prev) {
        double vx = ax - px0, vy = ay - py0, moved = std::hypot(vx, vy);
        if (moved >= 2.0) {
            st.has_heading = true;
            st.hx = vx / moved;
            st.hy = vy / moved;
        }
    }
    double bias = st.has_heading ? kForwardBias : 0.0;
    struct Cand { double score; int i, j; double x, y; };
    std::vector<Cand> found;
    int j0 = int(std::ceil((ay - radius) / s - 0.5)), j1 = int(std::floor((ay + radius) / s - 0.5));
    for (int j = j0; j <= j1; ++j) {
        int lo, hi;
        row_span(ax, ay, j, s, radius, lo, hi);
        int spans[2][2] = {{lo, hi}, {1, 0}};
        if (had_prev) {
            int lo0, hi0;
            row_span(px0, py0, j, s, radius, lo0, hi0);
            if (hi0 >= lo0) {
                spans[0][0] = lo; spans[0][1] = std::min(hi, lo0 - 1);
                spans[1][0] = std::max(lo, hi0 + 1); spans[1][1] = hi;
            }
        }
        for (auto& sp : spans) {
            for (int i = sp[0]; i <= sp[1]; ++i) {
                int64_t k = key(i, j);
                if (st.cells.count(k)) continue;
                double cx = (i + 0.5) * s, cy = (j + 0.5) * s;
                double x = cx + (hash01(i, j, 1) - 0.5) * kJitter * s;
                double y = cy + (hash01(i, j, 2) - 0.5) * kJitter * s;
                double ox, oy;
                bool ok = mask.grass_ok(x, y);
                if (!ok && snap(mask, x, y, s, ox, oy)) { x = ox; y = oy; ok = true; }
                if (ok) {
                    double rx = cx - ax, ry = cy - ay;
                    found.push_back({std::hypot(rx, ry) - bias * (rx * st.hx + ry * st.hy), i, j, x, y});
                } else {
                    st.cells[k] = Cell();
                }
            }
        }
    }
    const double near_sq = radius * radius;
    for (auto& p : st.pending) {
        if (st.cells.count(key(p.i, p.j))) continue;
        double rx = (p.i + 0.5) * s - ax, ry = (p.j + 0.5) * s - ay;
        if (rx * rx + ry * ry <= near_sq)
            found.push_back({std::hypot(rx, ry) - bias * (rx * st.hx + ry * st.hy), p.i, p.j, p.x, p.y});
    }
    std::sort(found.begin(), found.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    // drop duplicates (a cell queued again by this scan and from the old queue)
    st.pending.clear();
    std::unordered_set<int64_t> seen;
    for (auto it = found.rbegin(); it != found.rend(); ++it)
        if (seen.insert(key(it->i, it->j)).second) st.pending.push_back({it->i, it->j, it->x, it->y});
    std::reverse(st.pending.begin(), st.pending.end());   // best candidate last
    st.has_scan = true;
    st.sx = ax;
    st.sy = ay;
}

int GrassManager::place(AirportState& st, double ax, double ay, double ref_y, double until) {
    const double reach = settings_.radius_m + settings_.hysteresis_m;
    const double min_ny = std::cos(deg2rad(settings_.max_slope_deg));
    int placed = 0;
    while (!st.pending.empty() && count_ < kMaxInstances) {
        if (placed && now() >= until) break;
        Pending p = st.pending.back();
        st.pending.pop_back();
        int64_t k = key(p.i, p.j);
        if (st.cells.count(k) || (p.x - ax) * (p.x - ax) + (p.y - ay) * (p.y - ay) > reach * reach) continue;
        Cell c;
        Made made = create(st, p.i, p.j, p.x, p.y, ref_y, min_ny, c);
        if (made == Made::Missed) {
            if (st.retry.empty()) st.retry_at = now() + 3.0;
            st.retry.push_back(p);
        } else {
            if (made == Made::Placed) st.live.insert(k);
            st.cells[k] = c;
        }
        ++placed;
    }
    return placed;
}

bool GrassManager::ground_altitude(AirportState& st, double lat, double lon, double ref_y) {
    double x, y, z;
    XPLMWorldToLocal(lat, lon, 0.0, &x, &y, &z);
    XPLMProbeInfo_t info;
    info.structSize = sizeof(info);
    if (XPLMProbeTerrainXYZ(probe_, float(x), float(ref_y), float(z), &info) != xplm_ProbeHitTerrain) return false;
    double wlat, wlon, walt;
    XPLMLocalToWorld(x, info.locationY, z, &wlat, &wlon, &walt);
    st.alt = walt;
    st.alt_ok = true;
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s: ground %.0f m above sea level", st.entry.ident.c_str(), walt);
    log(buf);
    return true;
}

GrassManager::Made GrassManager::create(AirportState& st, int i, int j, double px, double py, double ref_y, double min_ny, Cell& out) {
    LatLon ll = st.mask->proj.to_latlon(px, py);
    if (!st.alt_ok) st.alt_known = ground_altitude(st, ll.lat, ll.lon, ref_y);
    double x, y, z;
    XPLMWorldToLocal(ll.lat, ll.lon, st.alt_ok ? st.alt : 0.0, &x, &y, &z);
    XPLMProbeInfo_t info;
    info.structSize = sizeof(info);
    if (XPLMProbeTerrainXYZ(probe_, float(x), float(ref_y), float(z), &info) != xplm_ProbeHitTerrain) {
        ++missed_;
        return Made::Missed;                 // no terrain there yet (tile still loading or streaming)
    }
    if (info.is_wet || info.normalY < min_ny) {
        ++rejected_;
        return Made::Rejected;
    }
    // quarter turns only: the square clumps then tile the placement grid
    float heading = float(int(hash01(i, j, 4) * 4.0) * 90);
    bool mowed = st.mask->is_mowed(px, py) && !mowed_objs_.empty();
    const auto& pool = mowed ? mowed_objs_ : normal_objs_;
    XPLMObjectRef obj = pool[std::min(pool.size() - 1, size_t(hash01(i, j, 3) * double(pool.size())))];
    const char* drefs[] = {nullptr};
    XPLMInstanceRef inst = XPLMCreateInstance(obj, drefs);
    if (!inst) return Made::Rejected;
    XPLMDrawInfo_t d;
    d.structSize = sizeof(d);
    d.x = info.locationX;
    d.y = info.locationY - float(kSinkM);
    d.z = info.locationZ;
    d.pitch = 0;
    d.heading = heading;
    d.roll = 0;
    float dummy = 0;
    XPLMInstanceSetPosition(inst, &d, &dummy);
    if (autoshift_) autoshift_(inst);
    out.inst = inst;
    out.heading = heading;
    if (!autoshift_) XPLMLocalToWorld(d.x, d.y, d.z, &out.lat, &out.lon, &out.alt);
    ++count_;
    ++created_;
    return Made::Placed;
}

void GrassManager::follow_origin() {
    if (!latr_ref_ || !lonr_ref_) return;
    float la = XPLMGetDataf(latr_ref_), lo = XPLMGetDataf(lonr_ref_);
    if (origin_known_ && (la != origin_lat_ || lo != origin_lon_) && !autoshift_) {
        for (auto& kv : airports_)
            for (auto& c : kv.second->cells) {
                if (!c.second.inst) continue;
                double x, y, z;
                XPLMWorldToLocal(c.second.lat, c.second.lon, c.second.alt, &x, &y, &z);
                XPLMDrawInfo_t d{sizeof(XPLMDrawInfo_t), float(x), float(y), float(z), 0, c.second.heading, 0};
                float dummy = 0;
                XPLMInstanceSetPosition(c.second.inst, &d, &dummy);
            }
    }
    origin_known_ = true;
    origin_lat_ = la;
    origin_lon_ = lo;
}

std::string GrassManager::resolve_look(double lat, double lon, int& biome, int& koppen, bool keep_climate) const {
    koppen = keep_climate && !active_style_.empty() ? active_koppen_ : climate_.koppen(lat, lon);
    bool auto_biome = settings_.biome == "auto";
    biome = auto_biome ? biome_for_koppen(koppen, lat) : find_biome(settings_.biome);
    if (biome < 0) biome = kTemperate;
    if (settings_.style != "auto") return settings_.style;
    if (snow_ref_ && XPLMGetDataf(snow_ref_) >= 0.15f) return "winter";   // snow on the ground: dormant grass
    int day = day_ref_ ? XPLMGetDatai(day_ref_) : 180;
    return season_for_climate(auto_biome ? koppen : typical_koppen(biome), day, lat);
}

std::string GrassManager::status() const {
    size_t pending = 0;
    std::string names;
    for (auto& kv : airports_) {
        pending += kv.second->pending.size();
        names += (names.empty() ? "" : ",") + kv.first + (kv.second->job ? "(loading)" : "");
    }
    char fps[32] = "";
    if (fps_ref_) {
        float period = XPLMGetDataf(fps_ref_);
        if (period > 0) std::snprintf(fps, sizeof fps, ", %.0f fps", 1.0 / period);
    }
    char buf[512];
    std::snprintf(buf, sizeof buf,
                  "style %s, %s plants (Koppen %s), %d instances, %zu queued, airports [%s], created %ld rejected %ld probe-missed %ld, plugin %.2f ms/tick (max %.1f)%s",
                  active_style_.c_str(), active_biome_.c_str(), koppen_symbol(active_koppen_), count_, pending, names.c_str(), created_, rejected_, missed_, tick_ms_, tick_max_, fps);
    return buf;
}

}  // namespace zg
