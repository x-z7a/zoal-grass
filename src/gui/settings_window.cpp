#include "gui/settings_window.h"

#include "gui/imgui_window.h"
#include "gui/settings_view.h"

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

#include "imgui.h"

namespace zg::gui {

namespace {

constexpr int kOpenWidth = 780, kOpenHeight = 680, kMinWidth = 600, kMinHeight = 460;

struct Window {
    SettingsView view;
    std::unique_ptr<ImguiWindow> window;
    bool dragging = false;
    int drag_mouse_x = 0, drag_mouse_y = 0, drag_left = 0, drag_top = 0;
    bool placed = false;
    XPLMDataRef ui_scale = nullptr;
    XPLMFlightLoopID loop = nullptr;
};

std::unique_ptr<Window> w;

void build() { draw_settings(w->view); }

void place_initially() {
    int left = 0, top = 0, right = 0, bottom = 0;
    XPLMGetScreenBoundsGlobal(&left, &top, &right, &bottom);
    const int sw = right - left, sh = top - bottom;
    const int width = std::max(kMinWidth, std::min(kOpenWidth, sw - 80));
    const int height = std::max(kMinHeight, std::min(kOpenHeight, sh - 120));
    const int x = left + (sw - width) / 2, y = top - (sh - height) / 2;
    w->window->set_geometry(x, y, x + width, y - height);
    w->placed = true;
}

float tick(float, float, int, void*) {
    if (!w) return 0;
    SettingsView& v = w->view;
    v.now = XPLMGetElapsedTime();
    std::vector<std::function<void()>> later;
    later.swap(v.later);
    for (auto& action : later) action();
    v.shown = v.host.get();
    v.live = v.host.status();
    if (v.close) {
        v.close = false;
        w->window->set_visible(false);
    }
    // dragging follows the mouse in global boxels
    const bool drag = w->window->visible() && (v.drag_started || v.moving);
    if (drag) {
        int mx = 0, my = 0;
        XPLMGetMouseLocationGlobal(&mx, &my);
        int l = 0, t = 0, r = 0, b = 0;
        w->window->geometry(&l, &t, &r, &b);
        if (v.drag_started || !w->dragging) {
            w->dragging = true;
            w->drag_mouse_x = mx;
            w->drag_mouse_y = my;
            w->drag_left = l;
            w->drag_top = t;
        } else {
            const int x = w->drag_left + (mx - w->drag_mouse_x), y = w->drag_top + (my - w->drag_mouse_y);
            w->window->set_geometry(x, y, x + (r - l), y - (t - b));
        }
    } else {
        w->dragging = false;
    }
    v.drag_started = false;
    // text is rasterised for the screen's density: twice X-Plane's UI scale, sharp on Retina and scaled UIs alike
    float scale = w->ui_scale ? std::max(1.0f, XPLMGetDataf(w->ui_scale)) : 1.0f;
    w->window->set_framebuffer_scale(2.0f * scale);
    return w->window->visible() ? -1.0f : 0.25f;
}

}  // namespace

bool start_settings_window(SettingsHost host) {
    if (w) return false;
    w = std::make_unique<Window>();
    w->view.host = std::move(host);
    ImguiWindow::Options options;
    options.keyboard = true;
    options.resizable = true;
    options.min_width = kMinWidth;
    options.min_height = kMinHeight;
    w->window = std::make_unique<ImguiWindow>(0, kOpenHeight, kOpenWidth, 0, build, options);
    if (!w->window->valid()) {
        if (w->view.host.log) w->view.host.log("X-Plane did not create the settings window (it needs X-Plane 12.4.4 or newer)");
        w.reset();
        return false;
    }
    {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(w->window->context());
        w->view.face = ui::load_typeface(std::filesystem::path(w->view.host.plugin_dir) / "fonts",
                                     std::filesystem::path(w->view.host.xp_root) / "Resources" / "fonts",
                                     w->view.host.log);
        settings_setup_style();
        ImGui::SetCurrentContext(previous);
    }
    w->ui_scale = XPLMFindDataRef("sim/graphics/misc/user_interface_scale");
    w->view.shown = w->view.saved = w->view.host.get();
    w->view.live = w->view.host.status();
    XPLMCreateFlightLoop_t fl{sizeof(XPLMCreateFlightLoop_t), xplm_FlightLoop_Phase_AfterFlightModel, tick, nullptr};
    w->loop = XPLMCreateFlightLoop(&fl);
    XPLMScheduleFlightLoop(w->loop, 0.25f, 1);
    return true;
}

void stop_settings_window() {
    if (!w) return;
    if (w->loop) XPLMDestroyFlightLoop(w->loop);
    w->view.later.clear();
    w->window.reset();
    w.reset();
}

void show_settings_window(bool show) {
    if (!w) return;
    if (show && !w->placed) place_initially();
    w->window->set_visible(show);
    if (show) {
        w->window->bring_to_front();
        XPLMScheduleFlightLoop(w->loop, -1.0f, 1);
    }
}

void toggle_settings_window() {
    if (w) show_settings_window(!w->window->visible());
}

bool settings_window_visible() { return w && w->window->visible(); }

}  // namespace zg::gui

