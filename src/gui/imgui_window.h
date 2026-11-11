// ImguiWindow: an X-Plane window whose contents are a Dear ImGui frame, rendered natively by X-Plane's panel-graphics
// ImGui helpers (SDK 4.4, X-Plane 12.4.4): textures through XPLMCreateTexture, draw lists through XPLMDrawCalls -- no
// OpenGL. From zoal-atc (src/xplane/imgui_window.*).
#pragma once

#include <XPLMDisplay.h>

#include <functional>
#include <utility>

struct ImGuiContext;
struct ImTextureData;

namespace zg::gui {

class ImguiWindow {
public:
    using BuildFn = std::function<void()>;

    struct Options {
        bool keyboard = false;   // take X-Plane's keyboard only while ImGui edits text
        bool resizable = false;
        int min_width = 0;
        int min_height = 0;
    };

    // Created hidden, at the given global boxel rectangle (y up).
    ImguiWindow(int left, int top, int right, int bottom, BuildFn build, Options options);
    ~ImguiWindow();
    ImguiWindow(const ImguiWindow&) = delete;
    ImguiWindow& operator=(const ImguiWindow&) = delete;

    bool valid() const { return window_ != nullptr; }
    bool visible() const;
    void set_visible(bool visible);
    void bring_to_front();
    void set_geometry(int left, int top, int right, int bottom);
    void geometry(int* left, int* top, int* right, int* bottom) const;
    ImGuiContext* context() const { return context_; }   // make it current before loading fonts / style
    void set_framebuffer_scale(float scale);              // physical pixels per boxel

private:
    static void draw_cb(XPLMWindowID, void* refcon);
    static int click_cb(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon);
    static int right_click_cb(XPLMWindowID, int x, int y, XPLMMouseStatus status, void* refcon);
    static void key_cb(XPLMWindowID, char, XPLMKeyFlags, char, void*, int);
    static XPLMCursorStatus cursor_cb(XPLMWindowID, int x, int y, void* refcon);
    static int wheel_cb(XPLMWindowID, int x, int y, int wheel, int clicks, void* refcon);

    void draw();
    int click(int x, int y, XPLMMouseStatus status, int button);
    void key(char key, XPLMKeyFlags flags, char virtual_key, bool losing_focus);
    void sync_keyboard_focus();
    void mouse_to_local(int x, int y, float* out_x, float* out_y) const;
    void update_texture(ImTextureData* texture);
    void destroy_textures();

    XPLMWindowID window_ = nullptr;
    ImGuiContext* context_ = nullptr;
    BuildFn build_;
    Options options_;
    int cursor_ = 0;
    bool releasing_focus_ = false;
    float last_time_ = 0.0f;
    float framebuffer_scale_ = 1.0f;
};

}  // namespace zg::gui
