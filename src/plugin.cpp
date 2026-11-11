// zoal-grass: native X-Plane 12 plugin that grows 3D grass (DINKIssTyle lawn art) on unpaved airport ground.
// Placement logic ported from XPGrass by Tor Jakobsen (MIT).
#include <XPLMDataAccess.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "manager.h"
#include "biome.h"
#include "style.h"

using namespace zg;

namespace
{

    std::unique_ptr<GrassManager> g_mgr;
    std::string g_plugin_dir, g_ini;
    XPLMFlightLoopID g_loop = nullptr;
    XPLMMenuID g_menu = nullptr;
    XPLMCommandRef g_cmd_toggle = nullptr, g_cmd_reload = nullptr;

    const double kRadii[] = {200, 300, 400, 800, 1600};
    const double kSpacings[] = {3.0, 3.5, 4.5, 6.0, 8.0};
    const char *kSpacingNames[] = {"3.0 m (lush)", "3.5 m", "4.5 m (default)", "6 m", "8 m (light)"};
    const double kHeights[] = {10, 15, 25, 35, 50};
    const double kDensities[] = {0.5, 0.75, 1.0, 1.3};
    const double kDry[] = {0.0, 0.15, 0.35, 0.7};
    const char *kDryNames[] = {"Green", "Mostly green", "Half dry", "Dry"};

    enum Item : intptr_t
    {
        kEnabled = 1,
        kDirt,
        kGravel,
        kTurf,
        kDsf,
        kOutside,
        kGpu,
        kShadows,
        kNormal,
        kBlades,
        kWind,
        kOsm,
        kReload,
        kSave,
        kStatus,
        kRadiusBase = 100,
        kSpacingBase = 200,
        kHeightBase = 300,
        kDensityBase = 400,
        kDryBase = 500,
        kStyleBase = 600, // +0 = auto, +1.. = kStyles
        kBiomeBase = 800, // +0 = auto, +1.. = kBiomes
    };

    struct MenuRef
    {
        XPLMMenuID menu;
        int index;
        intptr_t id;
    };
    std::vector<MenuRef> g_items;

    void log_line(const std::string &m) { XPLMDebugString(("zoal-grass: " + m + "\n").c_str()); }

    void add_item(XPLMMenuID menu, const char *name, intptr_t id)
    {
        int idx = XPLMAppendMenuItem(menu, name, reinterpret_cast<void *>(id), 0);
        g_items.push_back({menu, idx, id});
    }

    void sync_menu()
    {
        if (!g_mgr)
            return;
        const Settings &s = g_mgr->settings();
        auto same = [](double a, double b)
        { return std::fabs(a - b) < 1e-6; };
        for (auto &it : g_items)
        {
            bool on = false;
            intptr_t id = it.id;
            if (id >= kBiomeBase)
                on = id == kBiomeBase ? s.biome == "auto" : s.biome == kBiomes[id - kBiomeBase - 1].name;
            else if (id >= kStyleBase)
                on = id == kStyleBase ? s.style == "auto" : s.style == kStyles[id - kStyleBase - 1].name;
            else if (id >= kDryBase)
                on = s.style == "custom" && same(s.dry_mix, kDry[id - kDryBase]);
            else if (id >= kDensityBase)
                on = same(s.density, kDensities[id - kDensityBase]);
            else if (id >= kHeightBase)
                on = same(s.grass_height_cm, kHeights[id - kHeightBase]);
            else if (id >= kSpacingBase)
                on = same(s.spacing_m, kSpacings[id - kSpacingBase]);
            else if (id >= kRadiusBase)
                on = same(s.radius_m, kRadii[id - kRadiusBase]);
            else if (id == kEnabled)
                on = s.enabled;
            else if (id == kDirt)
                on = s.grass_on_dirt;
            else if (id == kGravel)
                on = s.grass_on_gravel;
            else if (id == kTurf)
                on = s.grass_on_turf_runways;
            else if (id == kDsf)
                on = s.use_dsf_polygons;
            else if (id == kOutside)
                on = s.grass_outside_airport;
            else if (id == kGpu)
                on = s.optimize_for_gpu;
            else if (id == kShadows)
                on = s.cast_shadows;
            else if (id == kNormal)
                on = s.normal_map;
            else if (id == kBlades)
                on = s.real_blades;
            else if (id == kWind)
                on = s.wind_sway;
            else if (id == kOsm)
                on = s.use_osm_roads;
            else
                continue;
            XPLMCheckMenuItem(it.menu, it.index, on ? xplm_Menu_Checked : xplm_Menu_Unchecked);
        }
    }

    void reload_settings()
    {
        std::vector<std::string> warnings;
        Settings s = load_settings(g_ini, warnings);
        for (auto &w : warnings)
            log_line(w);
        if (g_mgr)
            g_mgr->apply_settings(s, true);
        log_line("settings reloaded from " + g_ini);
    }

    void menu_handler(void *, void *ref)
    {
        if (!g_mgr)
            return;
        intptr_t id = reinterpret_cast<intptr_t>(ref);
        Settings s = g_mgr->settings();
        if (id >= kBiomeBase)
            s.biome = id == kBiomeBase ? "auto" : kBiomes[id - kBiomeBase - 1].name;
        else if (id >= kStyleBase)
            s.style = id == kStyleBase ? "auto" : kStyles[id - kStyleBase - 1].name;
        else if (id >= kDryBase)
        {
            s.dry_mix = kDry[id - kDryBase];
            s.style = "custom";
        }
        else if (id >= kDensityBase)
            s.density = kDensities[id - kDensityBase];
        else if (id >= kHeightBase)
            s.grass_height_cm = kHeights[id - kHeightBase];
        else if (id >= kSpacingBase)
            s.spacing_m = kSpacings[id - kSpacingBase];
        else if (id >= kRadiusBase)
            s.radius_m = kRadii[id - kRadiusBase];
        else
            switch (id)
            {
            case kEnabled:
                s.enabled = !s.enabled;
                break;
            case kDirt:
                s.grass_on_dirt = !s.grass_on_dirt;
                break;
            case kGravel:
                s.grass_on_gravel = !s.grass_on_gravel;
                break;
            case kTurf:
                s.grass_on_turf_runways = !s.grass_on_turf_runways;
                break;
            case kDsf:
                s.use_dsf_polygons = !s.use_dsf_polygons;
                break;
            case kOutside:
                s.grass_outside_airport = !s.grass_outside_airport;
                break;
            case kGpu:
                s.optimize_for_gpu = !s.optimize_for_gpu;
                break;
            case kShadows:
                s.cast_shadows = !s.cast_shadows;
                break;
            case kNormal:
                s.normal_map = !s.normal_map;
                break;
            case kBlades:
                s.real_blades = !s.real_blades;
                break;
            case kWind:
                s.wind_sway = !s.wind_sway;
                break;
            case kOsm:
                s.use_osm_roads = !s.use_osm_roads;
                break;
            case kReload:
                reload_settings();
                sync_menu();
                return;
            case kSave:
                log_line(save_settings(g_ini, s) ? "settings saved to " + g_ini : "could not save " + g_ini);
                return;
            case kStatus:
                log_line(g_mgr->status());
                return;
            default:
                return;
            }
        s.clamp();
        g_mgr->apply_settings(s);
        sync_menu();
    }

    void build_menu()
    {
        int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "zoal-grass", nullptr, 0);
        g_menu = XPLMCreateMenu("zoal-grass", XPLMFindPluginsMenu(), item, menu_handler, nullptr);
        add_item(g_menu, "Grass enabled", kEnabled);
        XPLMAppendMenuSeparator(g_menu);
        auto sub = [](const char *title)
        {
            int i = XPLMAppendMenuItem(g_menu, title, nullptr, 0);
            return XPLMCreateMenu(title, g_menu, i, menu_handler, nullptr);
        };
        XPLMMenuID m = sub("Radius");
        for (int i = 0; i < 5; ++i)
            add_item(m, (std::to_string(int(kRadii[i])) + " m").c_str(), kRadiusBase + i);
        m = sub("Clump spacing");
        for (int i = 0; i < 5; ++i)
            add_item(m, kSpacingNames[i], kSpacingBase + i);
        m = sub("Grass height");
        for (int i = 0; i < 5; ++i)
            add_item(m, (std::to_string(int(kHeights[i])) + " cm").c_str(), kHeightBase + i);
        m = sub("Tuft density");
        const char *dn[] = {"Sparse", "Medium", "Full (default)", "Extra"};
        for (int i = 0; i < 4; ++i)
            add_item(m, dn[i], kDensityBase + i);
        m = sub("Season / style");
        add_item(m, "Auto (from date, position and climate)", kStyleBase);
        for (int i = 0; i < kStyleCount; ++i)
            add_item(m, kStyles[i].label, kStyleBase + 1 + i);
        m = sub("Plants / biome");
        add_item(m, "Auto (from the climate at your GPS position)", kBiomeBase);
        for (int i = 0; i < kBiomeCount; ++i)
            add_item(m, kBiomes[i].label, kBiomeBase + 1 + i);
        m = sub("Custom grass colour");
        for (int i = 0; i < 4; ++i)
            add_item(m, kDryNames[i], kDryBase + i);
        XPLMAppendMenuSeparator(g_menu);
        add_item(g_menu, "Grass on dirt", kDirt);
        add_item(g_menu, "Grass on gravel", kGravel);
        add_item(g_menu, "Grass on turf runways", kTurf);
        add_item(g_menu, "Keep grass off scenery pavement polygons", kDsf);
        add_item(g_menu, "Keep grass off OpenStreetMap roads (online)", kOsm);
        add_item(g_menu, "Grass outside the airport boundary", kOutside);
        XPLMAppendMenuSeparator(g_menu);
        add_item(g_menu, "Grass sways in the wind (experimental)", kWind);
        add_item(g_menu, "Real 3D blades close up", kBlades);
        add_item(g_menu, "Optimize for GPU (single-sided)", kGpu);
        add_item(g_menu, "Grass casts shadows", kShadows);
        add_item(g_menu, "Normal map", kNormal);
        XPLMAppendMenuSeparator(g_menu);
        add_item(g_menu, "Reload settings from zoal-grass.ini", kReload);
        add_item(g_menu, "Save current settings to zoal-grass.ini", kSave);
        add_item(g_menu, "Log status", kStatus);
        sync_menu();
    }

    float flight_loop(float since, float, int, void *)
    {
        if (!g_mgr)
            return 0;
        return g_mgr->tick(since);
    }

    int cmd_handler(XPLMCommandRef cmd, XPLMCommandPhase phase, void *)
    {
        if (phase != xplm_CommandBegin)
            return 1;
        if (cmd == g_cmd_toggle)
            menu_handler(nullptr, reinterpret_cast<void *>(intptr_t(kEnabled)));
        else if (cmd == g_cmd_reload)
            menu_handler(nullptr, reinterpret_cast<void *>(intptr_t(kReload)));
        return 1;
    }

    std::string slashes(std::string p)
    {
        for (auto &c : p)
            if (c == '\\')
                c = '/';
        return p;
    }

    std::string parent(const std::string &p)
    {
        size_t s = p.find_last_of('/');
        return s == std::string::npos ? p : p.substr(0, s);
    }

} // namespace

PLUGIN_API int XPluginStart(char *name, char *sig, char *desc)
{
    std::strcpy(name, "zoal-grass");
    std::strcpy(sig, "com.github.x-z7a.zoal-grass");
    std::strcpy(desc, "3D grass (DINKIssTyle lawn art) on unpaved airport ground, as instanced objects.");
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);
    char path[1024];
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path, nullptr, nullptr);
    g_plugin_dir = parent(parent(slashes(path))); // <plugin>/<platform>/zoal-grass.xpl -> <plugin>
    g_ini = g_plugin_dir + "/zoal-grass.ini";
    return 1;
}

PLUGIN_API void XPluginStop() {}

PLUGIN_API int XPluginEnable()
{
    char root[1024];
    XPLMGetSystemPath(root);
    std::string xp_root = slashes(root);
    while (xp_root.size() > 1 && xp_root.back() == '/')
        xp_root.pop_back();
    std::vector<std::string> warnings;
    Settings s = load_settings(g_ini, warnings);
    for (auto &w : warnings)
        log_line(w);
    g_mgr.reset(new GrassManager(g_plugin_dir, xp_root, s));
    XPLMCreateFlightLoop_t fl{sizeof(XPLMCreateFlightLoop_t), xplm_FlightLoop_Phase_BeforeFlightModel, flight_loop, nullptr};
    g_loop = XPLMCreateFlightLoop(&fl);
    XPLMScheduleFlightLoop(g_loop, 1.0f, 1);
    build_menu();
    g_cmd_toggle = XPLMCreateCommand("zoal-grass/toggle", "zoal-grass: toggle grass");
    g_cmd_reload = XPLMCreateCommand("zoal-grass/reload", "zoal-grass: reload settings");
    XPLMRegisterCommandHandler(g_cmd_toggle, cmd_handler, 1, nullptr);
    XPLMRegisterCommandHandler(g_cmd_reload, cmd_handler, 1, nullptr);
    log_line("enabled, plugin folder " + g_plugin_dir);
    return 1;
}

PLUGIN_API void XPluginDisable()
{
    if (g_cmd_toggle)
        XPLMUnregisterCommandHandler(g_cmd_toggle, cmd_handler, 1, nullptr);
    if (g_cmd_reload)
        XPLMUnregisterCommandHandler(g_cmd_reload, cmd_handler, 1, nullptr);
    if (g_loop)
        XPLMDestroyFlightLoop(g_loop);
    g_loop = nullptr;
    if (g_menu)
        XPLMDestroyMenu(g_menu);
    g_menu = nullptr;
    g_items.clear();
    g_mgr.reset();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int msg, void *)
{
    if (!g_mgr)
        return;
    if (msg == XPLM_MSG_AIRPORT_LOADED)
        g_mgr->reset();
    else if (msg == XPLM_MSG_SCENERY_LOADED)
        g_mgr->refresh_terrain();
}
