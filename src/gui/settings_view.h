// The settings window's contents: Dear ImGui only, no X-Plane, so tools/settings_preview can render them off-screen
// as well as the sim (settings_window.cpp hosts them in an X-Plane window).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "config.h"
#include "gui/kit.h"
#include "gui/settings_window.h"

namespace zg::gui {

struct SettingsView {
    SettingsHost host;
    ui::Typeface face;
    std::vector<std::function<void()>> later;   // presses, run by the host outside the draw callback
    Settings shown;                             // the settings in force (refreshed by the host every tick)
    Settings saved;                             // as in zoal-grass.ini
    LiveStatus live;
    double now = 0;                             // seconds, for the footer's notes
    int tab = 0;
    bool close = false, moving = false, drag_started = false;
    std::string toast;
    double toast_until = 0;
    char block_words[512] = {}, allow_words[512] = {}, overpass[512] = {};
    std::string block_seen, allow_seen, overpass_seen;
};

// Once per ImGui context, after the fonts are loaded.
void settings_setup_style();
// One frame, between NewFrame and Render, filling the display.
void draw_settings(SettingsView& view);

}  // namespace zg::gui
