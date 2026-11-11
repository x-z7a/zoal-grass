// zoal-grass: native X-Plane 12 plugin that grows 3D grass (DINKIssTyle lawn art) on unpaved airport ground.
// Placement logic ported from XPGrass by Tor Jakobsen (MIT). Needs X-Plane 12.4.4 (SDK 4.4: the settings window is
// drawn by its panel graphics).
#include <XPLMDataAccess.h>
#include <XPLMMenus.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "biome.h"
#include "config.h"
#include "gui/settings_window.h"
#include "manager.h"

using namespace zg;

namespace
{

    std::unique_ptr<GrassManager> g_mgr;
    std::string g_plugin_dir, g_ini;
    XPLMFlightLoopID g_loop = nullptr;
    XPLMMenuID g_menu = nullptr;
    XPLMCommandRef g_cmd_toggle = nullptr, g_cmd_reload = nullptr, g_cmd_settings = nullptr;

    void log_line(const std::string &m) { XPLMDebugString(("zoal-grass: " + m + "\n").c_str()); }

    void apply(Settings s)
    {
        if (!g_mgr)
            return;
        s.clamp();
        g_mgr->apply_settings(s);
    }

    Settings reload_settings()
    {
        std::vector<std::string> warnings;
        Settings s = load_settings(g_ini, warnings);
        for (auto &w : warnings)
            log_line(w);
        if (g_mgr)
            g_mgr->apply_settings(s, true);
        log_line("settings reloaded from " + g_ini);
        return s;
    }

    // Plugins > zoal-grass: one item, the settings window. Everything else, the on / off switch included, is in it.
    void menu_handler(void *, void *) { gui::toggle_settings_window(); }

    void build_menu()
    {
        int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "zoal-grass", nullptr, 0);
        g_menu = XPLMCreateMenu("zoal-grass", XPLMFindPluginsMenu(), item, menu_handler, nullptr);
        XPLMAppendMenuItemWithCommand(g_menu, "Settings", g_cmd_settings);
    }

    bool start_gui(const std::string &xp_root)
    {
        gui::SettingsHost host;
        host.plugin_dir = g_plugin_dir;
        host.xp_root = xp_root;
        host.version = ZOAL_GRASS_VERSION;
        host.get = []
        { return g_mgr ? g_mgr->settings() : Settings(); };
        host.apply = apply;
        host.save = [](const Settings &s)
        {
            bool ok = save_settings(g_ini, s);
            log_line(ok ? "settings saved to " + g_ini : "could not save " + g_ini);
            return ok;
        };
        host.reload = []
        {
            reload_settings();
            return g_mgr ? g_mgr->settings() : Settings();
        };
        host.status = []
        {
            gui::LiveStatus st;
            if (!g_mgr)
                return st;
            st.enabled = g_mgr->settings().enabled;
            st.style = g_mgr->active_style();
            st.biome = g_mgr->active_biome();
            st.koppen = koppen_symbol(g_mgr->active_koppen());
            st.instances = g_mgr->instance_count();
            st.airports = int(g_mgr->active_airports());
            return st;
        };
        host.log_status = []
        {
            if (g_mgr)
                log_line(g_mgr->status());
        };
        host.log = [](const std::string &m)
        { log_line(m); };
        return gui::start_settings_window(std::move(host));
    }

    float flight_loop(float since, float, int, void *)
    {
        if (!g_mgr)
            return 0;
        return g_mgr->tick(since);
    }

    int cmd_handler(XPLMCommandRef cmd, XPLMCommandPhase phase, void *)
    {
        if (phase != xplm_CommandBegin || !g_mgr)
            return 1;
        if (cmd == g_cmd_settings)
        {
            gui::toggle_settings_window();
        }
        else if (cmd == g_cmd_toggle)
        {
            Settings s = g_mgr->settings();
            s.enabled = !s.enabled;
            apply(s);
        }
        else if (cmd == g_cmd_reload)
        {
            reload_settings();
        }
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
    g_cmd_settings = XPLMCreateCommand("zoal-grass/settings", "zoal-grass: show or hide the settings window");
    g_cmd_toggle = XPLMCreateCommand("zoal-grass/toggle", "zoal-grass: turn the grass on or off");
    g_cmd_reload = XPLMCreateCommand("zoal-grass/reload", "zoal-grass: reload settings from zoal-grass.ini");
    XPLMRegisterCommandHandler(g_cmd_settings, cmd_handler, 1, nullptr);
    XPLMRegisterCommandHandler(g_cmd_toggle, cmd_handler, 1, nullptr);
    XPLMRegisterCommandHandler(g_cmd_reload, cmd_handler, 1, nullptr);
    build_menu();
    if (!start_gui(xp_root))
        log_line("the settings window could not be created; settings stay in " + g_ini);
    log_line("enabled, plugin folder " + g_plugin_dir +
             "; settings: Plugins > zoal-grass > Settings, or the zoal-grass/settings command");
    return 1;
}

PLUGIN_API void XPluginDisable()
{
    for (XPLMCommandRef c : {g_cmd_settings, g_cmd_toggle, g_cmd_reload})
        if (c)
            XPLMUnregisterCommandHandler(c, cmd_handler, 1, nullptr);
    gui::stop_settings_window();
    if (g_loop)
        XPLMDestroyFlightLoop(g_loop);
    g_loop = nullptr;
    if (g_menu)
        XPLMDestroyMenu(g_menu);
    g_menu = nullptr;
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
