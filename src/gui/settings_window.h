// The settings window: every zoal-grass setting, organised in tabs, drawn with zoal-atc's look (Inter, the
// Graphite / Daylight / Night palettes, glass, pills) through X-Plane 12.4.4's panel graphics. Plugins > zoal-grass >
// Settings (or the zoal-grass/settings command) shows and hides it.
#pragma once

#include <functional>
#include <string>

#include "config.h"

namespace zg::gui {

struct LiveStatus {
    bool enabled = false;
    std::string style, biome, koppen;   // what is growing now (auto picks)
    int instances = 0;
    int airports = 0;
};

// What the window needs from the plugin. Called from the flight loop, never from the draw callback.
struct SettingsHost {
    std::string plugin_dir, xp_root, version;
    std::function<Settings()> get;                    // the settings in force
    std::function<void(Settings)> apply;              // clamp and apply
    std::function<bool(const Settings&)> save;        // write zoal-grass.ini
    std::function<Settings()> reload;                 // read zoal-grass.ini, apply it, return it
    std::function<LiveStatus()> status;
    std::function<void()> log_status;
    std::function<void(const std::string&)> log;
};

bool start_settings_window(SettingsHost host);   // false: X-Plane did not create the window
void stop_settings_window();
void show_settings_window(bool show);
void toggle_settings_window();
bool settings_window_visible();

}  // namespace zg::gui
