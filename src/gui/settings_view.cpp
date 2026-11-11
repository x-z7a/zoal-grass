#include "gui/settings_view.h"

#include "biome.h"
#include "style.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

namespace zg::gui {

namespace {

using namespace zg::ui;

// --- the palette and sizes: zoal-atc's tokens (src/ui/panel_view.cpp), per theme ------------------------------

struct Palette {
    ImU32 glass, surface, raised, glass_card, glass_well;
    ImU32 border, border_strong;
    ImU32 text, text_dim, text_feed;
    ImU32 accent, on_accent, accent_container, on_accent_container;
    ImU32 tonal, on_tonal, outline;
    ImU32 active, warn, on_warn, warn_surface, danger;
};

Palette graphite() {
    Palette p{};
    p.glass = rgba(0x19191b, 0.90f);
    p.surface = rgba(0x222224);
    p.raised = rgba(0x2d2d30);
    p.glass_card = rgba(0xfff8f0, 0.045f);
    p.glass_well = rgba(0x000000, 0.22f);
    p.border = rgba(0xfff8f0, 0.08f);
    p.border_strong = rgba(0xfff8f0, 0.15f);
    p.text = rgba(0xefebe5);
    p.text_dim = rgba(0xa6a099);
    p.text_feed = rgba(0xd8d2ca);
    p.accent = rgba(0xefb45c);
    p.on_accent = rgba(0x2e1f06);
    p.accent_container = rgba(0x3d3121);
    p.on_accent_container = rgba(0xf8dcb2);
    p.tonal = rgba(0x2c3529);
    p.on_tonal = rgba(0xd3ecca);
    p.outline = rgba(0x4a4845);
    p.active = rgba(0xa9d39f);
    p.warn = rgba(0xff9b6a);
    p.on_warn = rgba(0x2b1206);
    p.warn_surface = rgba(0x3a2419);
    p.danger = rgba(0xff8a7a);
    return p;
}

Palette daylight() {
    Palette p{};
    p.glass = rgba(0xf2eee7, 0.92f);
    p.surface = rgba(0xfbf8f3);
    p.raised = rgba(0xe8e2d8);
    p.glass_card = rgba(0xffffff, 0.55f);
    p.glass_well = rgba(0x463219, 0.06f);
    p.border = rgba(0x463219, 0.12f);
    p.border_strong = rgba(0x463219, 0.21f);
    p.text = rgba(0x27231e);
    p.text_dim = rgba(0x6b6358);
    p.text_feed = rgba(0x3d3731);
    p.accent = rgba(0xa35d08);
    p.on_accent = rgba(0xffffff);
    p.accent_container = rgba(0xf2dab4);
    p.on_accent_container = rgba(0x48290a);
    p.tonal = rgba(0xd6e9cf);
    p.on_tonal = rgba(0x12300f);
    p.outline = rgba(0xc6bdb0);
    p.active = rgba(0x3b7738);
    p.warn = rgba(0xc0561d);
    p.on_warn = rgba(0xffffff);
    p.warn_surface = rgba(0xfae2d3);
    p.danger = rgba(0xb93a2a);
    return p;
}

Palette night() {
    Palette p{};
    p.glass = rgba(0x0e0c0b, 0.92f);
    p.surface = rgba(0x161312);
    p.raised = rgba(0x1f1b18);
    p.glass_card = rgba(0xffebd7, 0.035f);
    p.glass_well = rgba(0x000000, 0.30f);
    p.border = rgba(0xffebd7, 0.07f);
    p.border_strong = rgba(0xffebd7, 0.12f);
    p.text = rgba(0xc8b8a7);
    p.text_dim = rgba(0x85776a);
    p.text_feed = rgba(0xb4a493);
    p.accent = rgba(0xcc8748);
    p.on_accent = rgba(0x1e1004);
    p.accent_container = rgba(0x33241a);
    p.on_accent_container = rgba(0xe4c09d);
    p.tonal = rgba(0x1f271c);
    p.on_tonal = rgba(0xbfd4b6);
    p.outline = rgba(0x3b332d);
    p.active = rgba(0x8cab81);
    p.warn = rgba(0xd8844f);
    p.on_warn = rgba(0x1f0c03);
    p.warn_surface = rgba(0x2a1a10);
    p.danger = rgba(0xd4705f);
    return p;
}

const Palette& palette(const std::string& theme) {
    static const Palette kGraphite = graphite(), kDaylight = daylight(), kNight = night();
    if (theme == "daylight") return kDaylight;
    if (theme == "night") return kNight;
    return kGraphite;
}

struct ThemeOption {
    const char* id;
    const char* name;
    const char* description;
    unsigned bg, text, accent, active;
};
constexpr ThemeOption kThemes[] = {
    {"graphite", "Graphite", "Warm dark glass. The default.", 0x19191b, 0xefebe5, 0xefb45c, 0xa9d39f},
    {"daylight", "Daylight", "Light glass for a bright cockpit.", 0xf2eee7, 0x27231e, 0xa35d08, 0x3b7738},
    {"night", "Night", "Dim and low-glare, for flying in the dark.", 0x0e0c0b, 0xc8b8a7, 0xcc8748, 0x8cab81},
};

constexpr float kPadTop = 12.0f;
constexpr float kPadX = 14.0f;
constexpr float kPadBottom = 12.0f;
constexpr float kGap = 10.0f;
constexpr float kRadiusLg = 14.0f;
constexpr float kButtonHeight = 34.0f;
constexpr float kInputHeight = 36.0f;
constexpr float kLabelTracking = 0.07f;
constexpr float kWindowRadius = 16.0f;
constexpr float kMastHeight = 44.0f;
constexpr float kTabsHeight = 36.0f;
constexpr float kFooterHeight = 34.0f;


// One frame's drawing context; `later` runs presses from the flight loop, outside the draw callback.
struct Ui {
    const Typeface& face;
    const Palette& p;
    ImDrawList* draw;
    std::vector<std::function<void()>>* later;

    float css(float px) const { return face.css(px); }
    void defer(std::function<void()> action) const { later->push_back(std::move(action)); }
};

float centred(float top, float box, float size) { return top + (box - size) * 0.5f; }

void label_text(const Ui& ui, ImVec2 pos, const std::string& value, ImU32 colour, float px = 11.0f,
                float tracking = kLabelTracking, ImFont* font = nullptr) {
    tracked_text(ui.draw, ui.face, font ? font : ui.face.semibold, ui.css(px), pos, colour, upper(value), tracking);
}

// --- zoal-atc's controls --------------------------------------------------------------------------------------

enum class Variant { filled, tonal, outlined, text, warn };

float button_width(const Ui& ui, const std::string& label, float px = 14.0f, float pad = 16.0f) {
    return measure(ui.face.semibold, ui.css(px), label).x + pad * 2.0f;
}

bool button(const Ui& ui, const char* id, ImVec2 min, ImVec2 size, const std::string& label, Variant variant,
            bool enabled = true, float px = 14.0f) {
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("##b", size) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    ImGui::PopID();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 max(min.x + size.x, min.y + size.y);
    const float r = size.y * 0.5f;
    const Palette& p = ui.p;
    ImU32 bg = 0, fg = p.accent, edge = 0;
    switch (variant) {
        case Variant::filled: bg = p.accent; fg = p.on_accent; break;
        case Variant::tonal: bg = p.tonal; fg = p.on_tonal; break;
        case Variant::outlined: edge = p.outline; fg = p.accent; break;
        case Variant::text: fg = p.accent; break;
        case Variant::warn: bg = p.warn; fg = p.on_warn; break;
    }
    if (!enabled) {   // Material's disabled roles: 12% of the text colour behind 38% of it
        bg = (variant == Variant::text || variant == Variant::outlined) ? 0 : faded(p.text, 0.12f);
        edge = variant == Variant::outlined ? faded(p.text, 0.12f) : 0;
        fg = faded(p.text, 0.38f);
    }
    if (bg != 0) ui.draw->AddRectFilled(min, max, bg, r);
    if (hovered || held) ui.draw->AddRectFilled(min, max, faded(fg, held ? 0.16f : 0.08f), r);
    if (edge != 0) ui.draw->AddRect(min, max, edge, r, 1.0f);
    const float size_px = ui.css(px);
    const ImVec2 ts = measure(ui.face.semibold, size_px, label);
    ui.draw->AddText(ui.face.semibold, size_px, ImVec2(min.x + (size.x - ts.x) * 0.5f, centred(min.y, size.y, size_px)),
                     fg, label.c_str());
    return pressed;
}

// A sunken pill entry field (the FMS scratchpad look). True when Enter was pressed in it.
bool field(const Ui& ui, const char* id, ImVec2 min, ImVec2 size, char* buffer, size_t capacity, const char* hint) {
    const Palette& p = ui.p;
    const float px = ui.css(15.0f), pad_x = 14.0f, r = size.y * 0.5f;
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ImGui::PushID(id);
    const ImGuiID input_id = ImGui::GetID("##f");
    const bool focused = ImGui::GetActiveID() == input_id;
    ui.draw->AddRectFilled(min, max, p.glass_well, r);
    if (focused) {
        ui.draw->AddRect(ImVec2(min.x - 3.0f, min.y - 3.0f), ImVec2(max.x + 3.0f, max.y + 3.0f), faded(p.accent, 0.22f),
                         r + 3.0f, 3.0f);
        ui.draw->AddRect(min, max, p.accent, r, 1.0f);
    } else {
        ui.draw->AddRect(min, max, p.border_strong, r, 1.0f);
    }
    ImGui::PushFont(ui.face.regular, px);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, (size.y - px) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, p.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, faded(p.text_dim, 0.7f));
    ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, faded(p.accent, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_InputTextCursor, p.accent);
    ImGui::SetCursorScreenPos(ImVec2(min.x + pad_x, min.y));
    ImGui::SetNextItemWidth(size.x - pad_x * 2.0f);
    const bool entered =
        ImGui::InputTextWithHint("##f", hint, buffer, capacity, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ImGui::PopID();
    if (entered) ImGui::ClearActiveID();   // Enter applies and gives the keyboard back to the sim
    return entered;
}

float paragraph(const Ui& ui, ImVec2 pos, float width, const std::string& value, ImU32 colour, float px = 13.0f,
                float line_height = 1.45f) {
    const float size = ui.css(px), line = px * line_height;
    float y = pos.y;
    for (const std::string& part : wrap_lines(ui.face.regular, size, value, width)) {
        ui.draw->AddText(ui.face.regular, size, ImVec2(pos.x, centred(y, line, size)), colour, part.c_str());
        y += line;
    }
    return y - pos.y;
}

// A pill with a glowing dot: green when the grass is growing, the warning colour when it is off.
float status_chip(const Ui& ui, float right, float top, float box, bool ok, const std::string& value) {
    const Palette& p = ui.p;
    const float px = ui.css(12.0f), height = 26.0f;
    const float width = 10.0f + 6.0f + 7.0f + measure(ui.face.semibold, px, value).x + 12.0f;
    const ImVec2 min(right - width, centred(top, box, height)), max(right, min.y + height);
    ui.draw->AddRectFilled(min, max, p.glass_well, height * 0.5f);
    ui.draw->AddRect(min, max, ok ? p.border : faded(p.warn, 0.4f), height * 0.5f, 1.0f);
    const ImU32 light = ok ? p.active : p.warn;
    const ImVec2 dot(min.x + 13.0f, min.y + height * 0.5f);
    ui.draw->AddCircleFilled(dot, 6.0f, faded(light, 0.18f));
    ui.draw->AddCircleFilled(dot, 3.0f, light);
    ui.draw->AddText(ui.face.semibold, px, ImVec2(min.x + 23.0f, centred(min.y, height, px)), ok ? p.text_feed : p.warn,
                     value.c_str());
    return min.x;
}

bool round_button(const Ui& ui, const char* id, ImVec2 centre, float diameter, Icon which, ImU32 colour) {
    const ImVec2 min(centre.x - diameter * 0.5f, centre.y - diameter * 0.5f);
    ImGui::SetCursorScreenPos(min);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(diameter, diameter));
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ui.draw->AddCircleFilled(centre, diameter * 0.5f, faded(colour, 0.16f));
    }
    ui.draw->AddCircle(centre, diameter * 0.5f, ui.p.border_strong, 0, 1.0f);
    const float size = diameter * 0.5f;
    icon(ui.draw, which, ImVec2(centre.x - size * 0.5f, centre.y - size * 0.5f), size, colour);
    return pressed;
}

// A segmented control. Returns the tab picked this frame, or -1.
int tabs(const Ui& ui, ImVec2 min, const std::vector<const char*>& labels, int selected) {
    const Palette& p = ui.p;
    const float px = ui.css(13.0f);
    std::vector<float> widths;
    float width = 3.0f * 2.0f + 2.0f * float(labels.size() - 1);
    for (const char* l : labels) {
        widths.push_back(measure(ui.face.semibold, px, l).x + 32.0f);
        width += widths.back();
    }
    const ImVec2 max(min.x + width, min.y + kTabsHeight);
    ui.draw->AddRectFilled(min, max, p.glass_well, kTabsHeight * 0.5f);
    ui.draw->AddRect(min, max, p.border, kTabsHeight * 0.5f, 1.0f);
    float x = min.x + 4.0f;
    int picked = -1;
    for (size_t i = 0; i < labels.size(); ++i) {
        const ImVec2 bmin(x, min.y + 4.0f), bsize(widths[i], kTabsHeight - 8.0f);
        const ImVec2 bmax(bmin.x + bsize.x, bmin.y + bsize.y);
        ImGui::SetCursorScreenPos(bmin);
        ImGui::PushID(int(i));
        if (ImGui::InvisibleButton("tab", bsize)) picked = int(i);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImU32 fg = p.text_dim;
        if (int(i) == selected) {
            ui.draw->AddRectFilled(bmin, bmax, p.accent_container, bsize.y * 0.5f);
            fg = p.on_accent_container;
        } else if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ui.draw->AddRectFilled(bmin, bmax, faded(p.accent, 0.08f), bsize.y * 0.5f);
            fg = p.text;
        }
        const float tw = measure(ui.face.semibold, px, labels[i]).x;
        ui.draw->AddText(ui.face.semibold, px, ImVec2(bmin.x + (bsize.x - tw) * 0.5f, centred(bmin.y, bsize.y, px)), fg,
                         labels[i]);
        x += widths[i] + 2.0f;
    }
    return picked;
}

// --- new controls, drawn the same way -------------------------------------------------------------------------

constexpr float kSwitchW = 42.0f, kSwitchH = 24.0f;

// A Material switch. True when flipped this frame.
bool toggle(const Ui& ui, const char* id, ImVec2 min, bool on, bool enabled = true) {
    const Palette& p = ui.p;
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton("##t", ImVec2(kSwitchW, kSwitchH)) && enabled;
    const bool hovered = enabled && ImGui::IsItemHovered();
    ImGui::PopID();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const ImVec2 max(min.x + kSwitchW, min.y + kSwitchH);
    const float r = kSwitchH * 0.5f, alpha = enabled ? 1.0f : 0.38f;
    if (on) {
        ui.draw->AddRectFilled(min, max, faded(p.accent, alpha), r);
    } else {
        ui.draw->AddRectFilled(min, max, p.glass_well, r);
        ui.draw->AddRect(min, max, faded(p.outline, alpha), r, 1.5f);
    }
    const float knob = on ? 9.0f : 6.5f;
    const ImVec2 c(on ? max.x - r : min.x + r, min.y + r);
    if (hovered) ui.draw->AddCircleFilled(c, r + 2.0f, faded(on ? p.accent : p.text_dim, 0.16f));
    ui.draw->AddCircleFilled(c, knob, faded(on ? p.on_accent : p.text_dim, alpha));
    if (on) icon(ui.draw, Icon::check, ImVec2(c.x - 5.0f, c.y - 5.0f), 10.0f, faded(p.accent, alpha));
    return pressed;
}

// A slider: a track, the filled part in the accent and a knob. The value follows the mouse while dragging and is
// returned (true) once, when the mouse is let go, so the grass is rebuilt once, not every frame.
bool slider(const Ui& ui, const char* id, ImVec2 min, float width, double& value, double lo, double hi, double step,
            const std::function<std::string(double)>& format, bool enabled = true) {
    static std::unordered_map<ImGuiID, double> dragging;
    const Palette& p = ui.p;
    const float h = 28.0f, readout = 76.0f, track_w = width - readout;
    ImGui::SetCursorScreenPos(min);
    ImGui::PushID(id);
    ImGui::InvisibleButton("##s", ImVec2(track_w, h));
    const ImGuiID gid = ImGui::GetItemID();
    const bool active = enabled && ImGui::IsItemActive();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool released = ImGui::IsItemDeactivated();
    ImGui::PopID();
    if (hovered || active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const float knob_r = 8.0f, x0 = min.x + knob_r, x1 = min.x + track_w - knob_r;
    double v = value;
    auto it = dragging.find(gid);
    if (active) {
        double t = std::clamp(double((ImGui::GetIO().MousePos.x - x0) / std::max(1.0f, x1 - x0)), 0.0, 1.0);
        v = std::clamp(lo + std::round(t * (hi - lo) / step) * step, lo, hi);
        dragging[gid] = v;
    } else if (it != dragging.end()) {
        v = it->second;
    }
    bool committed = false;
    if (released && it != dragging.end()) {
        committed = std::fabs(v - value) > step * 0.25;
        value = v;
        dragging.erase(gid);
    }
    const float alpha = enabled ? 1.0f : 0.38f;
    const float t = float(std::clamp((v - lo) / (hi - lo), 0.0, 1.0));
    const float cy = min.y + h * 0.5f, kx = x0 + (x1 - x0) * t;
    ui.draw->AddRectFilled(ImVec2(x0, cy - 2.5f), ImVec2(x1, cy + 2.5f), faded(p.border_strong, alpha), 2.5f);
    ui.draw->AddRectFilled(ImVec2(x0, cy - 2.5f), ImVec2(kx, cy + 2.5f), faded(p.accent, alpha), 2.5f);
    if (hovered || active) ui.draw->AddCircleFilled(ImVec2(kx, cy), knob_r + 6.0f, faded(p.accent, active ? 0.22f : 0.12f));
    ui.draw->AddCircleFilled(ImVec2(kx, cy), knob_r, faded(p.accent, alpha));
    ui.draw->AddCircleFilled(ImVec2(kx, cy), 3.0f, faded(p.on_accent, alpha));
    // the readout: a small well with the value, tabular
    const ImVec2 rmin(min.x + track_w + 8.0f, centred(min.y, h, 26.0f)), rmax(min.x + width, rmin.y + 26.0f);
    ui.draw->AddRectFilled(rmin, rmax, p.glass_well, 13.0f);
    ui.draw->AddRect(rmin, rmax, active ? p.accent : p.border, 13.0f, 1.0f);
    const std::string text = format(v);
    const float px = ui.css(13.0f);
    const float tw = measure(ui.face.semibold, px, text).x;
    ui.draw->AddText(ui.face.semibold, px, ImVec2(rmin.x + (rmax.x - rmin.x - tw) * 0.5f, centred(rmin.y, 26.0f, px)),
                     faded(active ? p.accent : p.text, alpha), text.c_str());
    return committed;
}

// Choice chips, wrapped over the width. Returns the chip picked this frame, or -1. Height in *height.
int chips(const Ui& ui, const char* id, ImVec2 min, float width, const std::vector<std::string>& labels, int selected,
          float* height) {
    const Palette& p = ui.p;
    const float px = ui.css(13.0f), h = 32.0f, gap = 8.0f;
    float x = min.x, y = min.y;
    int picked = -1;
    ImGui::PushID(id);
    for (size_t i = 0; i < labels.size(); ++i) {
        const bool sel = int(i) == selected;
        const float w = measure(ui.face.semibold, px, labels[i]).x + (sel ? 46.0f : 32.0f);
        if (x > min.x && x + w > min.x + width) {
            x = min.x;
            y += h + gap;
        }
        const ImVec2 cmin(x, y), cmax(x + w, y + h);
        ImGui::SetCursorScreenPos(cmin);
        ImGui::PushID(int(i));
        if (ImGui::InvisibleButton("chip", ImVec2(w, h)) && !sel) picked = int(i);
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        ImU32 fg = p.text;
        if (sel) {
            ui.draw->AddRectFilled(cmin, cmax, p.accent_container, 8.0f);
            fg = p.on_accent_container;
            icon(ui.draw, Icon::check, ImVec2(x + 10.0f, centred(y, h, 14.0f)), 14.0f, fg);
        } else {
            ui.draw->AddRect(cmin, cmax, hovered ? p.border_strong : p.outline, 8.0f, 1.0f);
            if (hovered) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ui.draw->AddRectFilled(cmin, cmax, faded(p.accent, 0.08f), 8.0f);
            }
        }
        ui.draw->AddText(ui.face.semibold, px, ImVec2(x + (sel ? 30.0f : 16.0f), centred(y, h, px)), fg,
                         labels[i].c_str());
        x += w + gap;
    }
    ImGui::PopID();
    *height = y + h - min.y;
    return picked;
}

// --- the settings flow: sections, then a setting per row (title and help left, the control right) -------------

struct Flow {
    const Ui& ui;
    float x, width, y;
    bool first_section = true;
    bool first_row = true;

    void section(const std::string& title) {
        if (!first_section) y += 18.0f;
        first_section = false;
        first_row = true;
        const float box = 12.0f * 1.4f;
        label_text(ui, ImVec2(x, centred(y, box, ui.css(12.0f))), title, ui.p.accent, 12.0f, 0.08f, ui.face.bold);
        y += box + 4.0f;
        ui.draw->AddLine(ImVec2(x, y), ImVec2(x + width, y), ui.p.border_strong, 1.0f);
        y += 1.0f;
    }

    // Title and help in the text column; returns where the control goes (right column, at the title).
    float text_column(float control_w) const { return std::max(160.0f, width - control_w - 24.0f); }

    ImVec2 row(const std::string& title, const std::string& help, float control_w, float control_h, bool dim = false) {
        if (!first_row) {
            ui.draw->AddLine(ImVec2(x, y), ImVec2(x + width, y), ui.p.border, 1.0f);
            y += 1.0f;
        }
        first_row = false;
        y += 12.0f;
        const float col = text_column(control_w);
        const float tpx = ui.css(14.0f), tbox = 14.0f * 1.4f;
        ui.draw->AddText(ui.face.semibold, tpx, ImVec2(x + 2.0f, centred(y, tbox, tpx)),
                         dim ? faded(ui.p.text, 0.45f) : ui.p.text, fit(ui.face.semibold, tpx, title, col).c_str());
        float text_h = tbox;
        if (!help.empty()) text_h += 2.0f + paragraph(ui, ImVec2(x + 2.0f, y + tbox + 2.0f), col, help, ui.p.text_dim);
        const ImVec2 at(x + width - control_w, y + std::max(0.0f, (std::min(text_h, 44.0f) - control_h) * 0.5f));
        pending_h = std::max(text_h, at.y - y + control_h);
        return at;
    }
    void end_row() {
        y += pending_h + 12.0f;
        pending_h = 0;
    }
    // A block control under the title / help (chips, cards): returns its top-left; call end_block with its height.
    ImVec2 block(const std::string& title, const std::string& help) {
        ImVec2 at = row(title, help, 0.0f, 0.0f);
        (void)at;
        y += pending_h + 10.0f;
        pending_h = 0;
        return ImVec2(x + 2.0f, y);
    }
    void end_block(float h) { y += h + 12.0f; }

    float pending_h = 0;
};

std::string fmt(const char* f, double v) {
    char b[48];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

SettingsView* g = nullptr;   // the view being drawn (and whose deferred actions run next tick)


// Apply a changed copy of the settings from the flight loop.
void change(const Ui& ui, std::function<void(Settings&)> edit) {
    ui.defer([edit] {
        Settings s = g->host.get();
        edit(s);
        g->host.apply(s);
    });
}

void sync_field(char* buf, size_t cap, std::string& seen, const std::string& value) {
    if (seen != value) {
        seen = value;
        std::snprintf(buf, cap, "%s", value.c_str());
    }
}

std::string title_case(const std::string& id) {
    std::string out = id;
    for (char& c : out)
        if (c == '_') c = ' ';
    if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
    return out;
}

// One setting row with a switch.
void switch_row(Flow& f, const char* id, const std::string& title, const std::string& help, bool on,
                std::function<void(Settings&, bool)> set, bool enabled = true) {
    ImVec2 at = f.row(title, help, kSwitchW, kSwitchH, !enabled);
    if (toggle(f.ui, id, at, on, enabled)) change(f.ui, [set, on](Settings& s) { set(s, !on); });
    f.end_row();
}

// One setting row with a slider.
void slider_row(Flow& f, const char* id, const std::string& title, const std::string& help, double value, double lo,
                double hi, double step, const std::function<std::string(double)>& format,
                std::function<void(Settings&, double)> set, bool enabled = true) {
    const float w = std::min(330.0f, std::max(230.0f, f.width * 0.42f));
    ImVec2 at = f.row(title, help, w, 28.0f, !enabled);
    double v = value;
    if (slider(f.ui, id, at, w, v, lo, hi, step, format, enabled)) change(f.ui, [set, v](Settings& s) { set(s, v); });
    f.end_row();
}

// One setting row with a text field and an Apply button.
void text_row(Flow& f, const char* id, const std::string& title, const std::string& help, char* buf, size_t cap,
              const char* hint, std::function<void(Settings&, const std::string&)> set) {
    const float bw = button_width(f.ui, "Apply");
    const float w = std::min(420.0f, std::max(260.0f, f.width * 0.5f));
    ImVec2 at = f.row(title, help, w, kInputHeight);
    bool apply = field(f.ui, id, at, ImVec2(w - bw - 8.0f, kInputHeight), buf, cap, hint);
    ImGui::PushID(id);
    apply |= button(f.ui, "apply", ImVec2(at.x + w - bw, at.y + 1.0f), ImVec2(bw, kButtonHeight), "Apply",
                    Variant::tonal);
    ImGui::PopID();
    if (apply) {
        std::string value(buf);
        change(f.ui, [set, value](Settings& s) { set(s, value); });
    }
    f.end_row();
}

void theme_picker(Flow& f, const Settings& s) {
    const Ui& ui = f.ui;
    const Palette& p = ui.p;
    const float gap = 8.0f, avail = std::min(f.width, 780.0f);
    const int columns = std::clamp(int((avail + gap) / (180.0f + gap)), 1, 3);
    const float card_w = (avail - gap * float(columns - 1)) / float(columns);
    const float text_x = 12.0f + 47.0f + 10.0f, text_w = card_w - text_x - 12.0f;
    const float desc_px = ui.css(12.0f), desc_line = 12.0f * 1.35f;
    size_t most = 1;
    for (const ThemeOption& o : kThemes) most = std::max(most, wrap_lines(ui.face.regular, desc_px, o.description, text_w).size());
    const float block = 14.0f * 1.4f + 2.0f + desc_line * float(most), card_h = std::max(58.0f, block + 20.0f);
    int index = 0;
    for (const ThemeOption& o : kThemes) {
        const ImVec2 min(f.x + float(index % columns) * (card_w + gap), f.y + float(index / columns) * (card_h + gap));
        const ImVec2 max(min.x + card_w, min.y + card_h);
        ImGui::SetCursorScreenPos(min);
        ImGui::PushID(index);
        const bool pressed = ImGui::InvisibleButton("theme", ImVec2(card_w, card_h));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        const bool selected = s.ui_theme == o.id;
        if (pressed && !selected) {
            std::string id = o.id;
            change(ui, [id](Settings& st) { st.ui_theme = id; });
        }
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ui.draw->AddRectFilled(min, max, p.glass_card, kRadiusLg);
        ui.draw->AddRect(min, max, selected ? p.accent : hovered ? p.border_strong : p.border, kRadiusLg,
                         selected ? 2.0f : 1.0f);
        const ImVec2 smin(min.x + 12.0f, centred(min.y, card_h, 21.0f)), smax(smin.x + 47.0f, smin.y + 21.0f);
        ui.draw->AddRectFilled(smin, smax, rgba(o.bg), 10.5f);
        ui.draw->AddRect(smin, smax, IM_COL32(128, 128, 128, 77), 10.5f, 1.0f);
        const unsigned dots[] = {o.text, o.accent, o.active};
        for (int d = 0; d < 3; ++d)
            ui.draw->AddCircleFilled(ImVec2(smin.x + 11.5f + float(d) * 12.0f, smin.y + 10.5f), 4.5f, rgba(dots[d]));
        const float tx = min.x + text_x, name_px = ui.css(14.0f), top = centred(min.y, card_h, block);
        ui.draw->AddText(ui.face.semibold, name_px, ImVec2(tx, centred(top, 14.0f * 1.4f, name_px)), p.text, o.name);
        float dy = top + 14.0f * 1.4f + 2.0f;
        for (const std::string& line : wrap_lines(ui.face.regular, desc_px, o.description, text_w)) {
            ui.draw->AddText(ui.face.regular, desc_px, ImVec2(tx, centred(dy, desc_line, desc_px)), p.text_dim,
                             line.c_str());
            dy += desc_line;
        }
        ++index;
    }
    const int rows = (index + columns - 1) / columns;
    f.y += float(rows) * card_h + float(rows - 1) * gap;
}

// --- the tabs ---------------------------------------------------------------------------------------------------

void tab_look(Flow& f, const Settings& s) {
    const LiveStatus& live = g->live;
    f.section("Season and plants");
    {
        std::vector<std::string> labels = {"Auto"};
        int sel = s.style == "auto" ? 0 : -1;
        for (int i = 0; i < kStyleCount; ++i) {
            labels.push_back(title_case(kStyles[i].name));
            if (s.style == kStyles[i].name) sel = i + 1;
        }
        std::string now = live.style.empty() ? "" : " Now: " + title_case(live.style) + ".";
        ImVec2 at = f.block("Season", "How the grass is coloured and how much of it is dry. Auto follows the sim's "
                                      "date, your latitude, the climate and snow on the ground." + now);
        float h = 0;
        int picked = chips(f.ui, "season", at, f.width - 4.0f, labels, sel, &h);
        if (picked >= 0) {
            std::string id = picked == 0 ? "auto" : kStyles[picked - 1].name;
            change(f.ui, [id](Settings& st) { st.style = id; });
        }
        f.end_block(h);
    }
    {
        std::vector<std::string> labels = {"Auto"};
        int sel = s.biome == "auto" ? 0 : -1;
        for (int i = 0; i < kBiomeCount; ++i) {
            labels.push_back(title_case(kBiomes[i].name));
            if (s.biome == kBiomes[i].name) sel = i + 1;
        }
        std::string now = live.biome.empty() ? ""
                                             : " Now: " + title_case(live.biome) +
                                                   (live.koppen.empty() || live.koppen == "?" ? "" : " (Köppen " + live.koppen + ")") + ".";
        ImVec2 at = f.block("Plants", "Which plants grow: lawn, meadow grass, seed heads, tussocks, moss, sedge, clover "
                                      "and rosettes, mixed for the climate. Auto reads the climate at your GPS position." +
                                          now);
        float h = 0;
        int picked = chips(f.ui, "biome", at, f.width - 4.0f, labels, sel, &h);
        if (picked >= 0) {
            std::string id = picked == 0 ? "auto" : kBiomes[picked - 1].name;
            change(f.ui, [id](Settings& st) { st.biome = id; });
        }
        f.end_block(h);
    }
    slider_row(f, "dry", "Dry grass", "Custom season only: the share of dry, yellow tufts.", s.dry_mix, 0.0, 1.0, 0.05,
               [](double v) { return fmt("%.0f %%", v * 100); }, [](Settings& st, double v) { st.dry_mix = v; },
               s.style == "custom");

    f.section("Grass");
    slider_row(f, "height", "Height", "Average tuft height on normal ground (mown strips are half as tall).",
               s.grass_height_cm, 5, 80, 1, [](double v) { return fmt("%.0f cm", v); },
               [](Settings& st, double v) { st.grass_height_cm = v; });
    slider_row(f, "density", "Density", "How many clumps per square metre. Higher is lusher and costs more GPU.",
               s.density, 0.2, 2.0, 0.05, [](double v) { return fmt("%.2f\xC3\x97", v); },
               [](Settings& st, double v) { st.density = v; });
    slider_row(f, "width", "Width", "Extra horizontal scale of the plant clumps.", s.width_scale, 0.5, 2.0, 0.05,
               [](double v) { return fmt("%.2f\xC3\x97", v); }, [](Settings& st, double v) { st.width_scale = v; });
}

void tab_placement(Flow& f, const Settings& s) {
    f.section("Where grass grows");
    slider_row(f, "radius", "Radius", "Grass appears within this distance of the aircraft.", s.radius_m, 30, 1500, 10,
               [](double v) { return fmt("%.0f m", v); }, [](Settings& st, double v) { st.radius_m = v; });
    slider_row(f, "spacing", "Clump spacing", "Distance between the grass clumps' centres. Smaller is lusher; larger "
                                              "is lighter on the CPU and GPU.",
               s.spacing_m, 1.5, 20, 0.5, [](double v) { return fmt("%.1f m", v); },
               [](Settings& st, double v) { st.spacing_m = v; });
    switch_row(f, "outside", "Outside the airport boundary",
               "Grow grass up to the radius beyond the airport's boundary, not just inside it.",
               s.grass_outside_airport, [](Settings& st, bool v) { st.grass_outside_airport = v; });
    switch_row(f, "tilt", "Follow the terrain slope",
               "Tilt each clump to the ground's slope, so its ground cover stays just above the terrain.",
               s.terrain_tilt, [](Settings& st, bool v) { st.terrain_tilt = v; });
    slider_row(f, "slope", "Steepest slope", "No grass on terrain steeper than this.", s.max_slope_deg, 1, 60, 1,
               [](double v) { return fmt("%.0f\xC2\xB0", v); }, [](Settings& st, double v) { st.max_slope_deg = v; });

    f.section("Surfaces");
    switch_row(f, "dirt", "Grass on dirt", "Grow grass on dirt surfaces (taxiways, aprons, runways).", s.grass_on_dirt,
               [](Settings& st, bool v) { st.grass_on_dirt = v; });
    switch_row(f, "gravel", "Grass on gravel", "Grow grass on gravel surfaces.", s.grass_on_gravel,
               [](Settings& st, bool v) { st.grass_on_gravel = v; });
    switch_row(f, "turf", "Grass on turf runways", "Grow mown, half-height grass on turf runways and helipads.",
               s.grass_on_turf_runways, [](Settings& st, bool v) { st.grass_on_turf_runways = v; });
    slider_row(f, "margin", "Pavement clearance", "Extra space kept clear around pavement, beyond the clumps' own "
                                                  "footprint.",
               s.pavement_margin_m, 0, 30, 0.5, [](double v) { return fmt("%.1f m", v); },
               [](Settings& st, double v) { st.pavement_margin_m = v; });
}

void tab_clearance(Flow& f, const Settings& s) {
    f.section("Scenery");
    switch_row(f, "dsf", "Keep off scenery roads and buildings",
               "Keep grass off the airport scenery's draped pavement, painted roads and paths, and its buildings' "
               "footprints.",
               s.use_dsf_polygons, [](Settings& st, bool v) { st.use_dsf_polygons = v; });
    sync_field(g->block_words, sizeof g->block_words, g->block_seen, s.dsf_block_words);
    sync_field(g->allow_words, sizeof g->allow_words, g->allow_seen, s.dsf_allow_words);
    text_row(f, "block", "Always pavement", "Comma-separated words: a scenery .pol / .lin path containing one is "
                                            "always kept clear.",
             g->block_words, sizeof g->block_words, "e.g. asphalt,concrete",
             [](Settings& st, const std::string& v) { st.dsf_block_words = v; });
    text_row(f, "allow", "Never pavement", "Comma-separated words: a scenery .pol / .lin path containing one may "
                                           "have grass.",
             g->allow_words, sizeof g->allow_words, "e.g. grass,field",
             [](Settings& st, const std::string& v) { st.dsf_allow_words = v; });

    f.section("OpenStreetMap");
    switch_row(f, "osm", "Keep off OpenStreetMap roads",
               "Roads, car parks and rail lines from OpenStreetMap, downloaded once per airport and cached. Only the "
               "airport's bounding box is sent. Map data \xC2\xA9 OpenStreetMap contributors, ODbL.",
               s.use_osm_roads, [](Settings& st, bool v) { st.use_osm_roads = v; });
    sync_field(g->overpass, sizeof g->overpass, g->overpass_seen, s.osm_overpass_url);
    text_row(f, "overpass", "Overpass server", "Where the OpenStreetMap roads are downloaded from.", g->overpass,
             sizeof g->overpass, "https://overpass-api.de/api/interpreter",
             [](Settings& st, const std::string& v) { st.osm_overpass_url = v; });
    slider_row(f, "osmdays", "Keep downloads for", "Re-download an airport's roads after this many days.",
               s.osm_cache_days, 1, 365, 1, [](double v) { return fmt("%.0f days", v); },
               [](Settings& st, double v) { st.osm_cache_days = v; }, s.use_osm_roads);
}

void tab_rendering(Flow& f, const Settings& s) {
    f.section("Detail");
    switch_row(f, "blades", "Real 3D blades close up",
               "Adds individual 3D blades near the camera. About 20 times the triangles there, and the grass changes "
               "look as you come closer.",
               s.real_blades, [](Settings& st, bool v) { st.real_blades = v; });
    slider_row(f, "near", "Close-up distance", "How far from the camera real 3D blades are drawn, and grass sways in "
                                               "the wind.",
               s.lod_near_m, 10, 300, 5, [](double v) { return fmt("%.0f m", v); },
               [](Settings& st, double v) { st.lod_near_m = v; }, s.real_blades || s.wind_sway);
    slider_row(f, "mid", "Full detail within", "Real blades only: beyond this, fewer and larger clumps stand in.",
               s.lod_mid_m, std::min(1000.0, s.lod_near_m + 10), 1000, 10, [](double v) { return fmt("%.0f m", v); },
               [](Settings& st, double v) { st.lod_mid_m = v; }, s.real_blades);

    f.section("Wind");
    switch_row(f, "wind", "Grass sways in the wind",
               "Experimental: grass within the close-up distance leans downwind and moves in gusts, from the sim's "
               "wind. Each 0.75 m patch tilts as a whole. Watch your FPS with it on.",
               s.wind_sway, [](Settings& st, bool v) { st.wind_sway = v; });
    slider_row(f, "winddeg", "Lean in a strong wind", "How far the grass leans in about 27 kt of wind.", s.wind_sway_deg,
               1, 20, 1, [](double v) { return fmt("%.0f\xC2\xB0", v); },
               [](Settings& st, double v) { st.wind_sway_deg = v; }, s.wind_sway);

    f.section("Graphics");
    switch_row(f, "gpu", "Optimize for GPU", "Single-sided meshes with culling turned off: half the triangles, but "
                                             "X-Plane draws them one by one (more CPU).",
               s.optimize_for_gpu, [](Settings& st, bool v) { st.optimize_for_gpu = v; });
    switch_row(f, "shadows", "Grass casts shadows", "Costly with this much geometry.", s.cast_shadows,
               [](Settings& st, bool v) { st.cast_shadows = v; });
    switch_row(f, "normal", "Normal map", "Use the DINKIssTyle normal map for lighting detail.", s.normal_map,
               [](Settings& st, bool v) { st.normal_map = v; });
    slider_row(f, "hyst", "Remove beyond", "Grass is removed only this much further away than the radius, so it does "
                                           "not flicker at the edge.",
               s.hysteresis_m, 5, 500, 5, [](double v) { return fmt("+%.0f m", v); },
               [](Settings& st, double v) { st.hysteresis_m = v; });
}

void tab_advanced(Flow& f, const Settings& s) {
    f.section("Appearance");
    f.y += 10.0f;
    theme_picker(f, s);
    f.y += 14.0f;

    f.section("Airport area");
    slider_row(f, "boundary", "Boundary margin", "How far grass may reach beyond the airport's boundary polygon.",
               s.boundary_margin_m, 0, 500, 5, [](double v) { return fmt("%.0f m", v); },
               [](Settings& st, double v) { st.boundary_margin_m = v; });
    slider_row(f, "fallback", "Area without a boundary", "For airports without a boundary: how far around the "
                                                         "runways and pavement grass may grow.",
               s.fallback_margin_m, 0, 1000, 10, [](double v) { return fmt("%.0f m", v); },
               [](Settings& st, double v) { st.fallback_margin_m = v; });
    slider_row(f, "cell", "Mask resolution", "Cell size of the pavement mask. Smaller follows pavement edges more "
                                             "closely and takes more memory.",
               s.mask_cell_m, 0.5, 10, 0.5, [](double v) { return fmt("%.1f m", v); },
               [](Settings& st, double v) { st.mask_cell_m = v; });

    f.section("Diagnostics");
    switch_row(f, "stats", "Log status every 30 s", "Writes a status line to X-Plane's Log.txt.", s.log_stats,
               [](Settings& st, bool v) { st.log_stats = v; });
    {
        const float lw = button_width(f.ui, "Log status now"), rw = button_width(f.ui, "Reset to defaults");
        ImVec2 at = f.row("Tools", "Reset puts every setting back to its default (the theme stays).", lw + rw + 8.0f,
                          kButtonHeight);
        if (button(f.ui, "logstatus", at, ImVec2(lw, kButtonHeight), "Log status now", Variant::outlined))
            f.ui.defer([] { g->host.log_status(); });
        if (button(f.ui, "reset", ImVec2(at.x + lw + 8.0f, at.y), ImVec2(rw, kButtonHeight), "Reset to defaults",
                   Variant::warn))
            change(f.ui, [](Settings& st) {
                std::string theme = st.ui_theme;
                st = Settings();
                st.ui_theme = theme;
            });
        f.end_row();
    }
}

// --- the frame --------------------------------------------------------------------------------------------------

void setup_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.ItemSpacing = ImVec2(0.0f, 0.0f);
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.ScrollbarSize = 8.0f;
    style.ScrollbarRounding = 4.0f;
    style.ScrollbarPadding = 1.0f;
    style.DisabledAlpha = 0.5f;
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
}

}  // namespace

void settings_setup_style() { setup_style(); }

void draw_settings(SettingsView& view) {
    g = &view;
    const Settings& s = g->shown;
    const Palette& p = palette(s.ui_theme);
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 display = io.DisplaySize;
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(p.border_strong);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(faded(p.text_dim, 0.5f));
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(faded(p.text_dim, 0.7f));

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(display);
    ImGui::Begin("zoal-grass settings", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const Ui ui{g->face, p, draw, &g->later};
    draw->AddRectFilled(ImVec2(0.0f, 0.0f), display, p.glass, kWindowRadius);
    draw->AddRect(ImVec2(0.0f, 0.0f), display, p.border_strong, kWindowRadius, 1.0f);

    const float width = display.x - kPadX * 2.0f;
    float y = kPadTop;

    // ---- the mast: title left; close, the on / off switch and the status right
    {
        const float right = kPadX + width, close_d = 30.0f;
        if (round_button(ui, "close", ImVec2(right - close_d * 0.5f, y + kMastHeight * 0.5f), close_d, Icon::close,
                         p.text_dim))
            g->close = true;
        const ImVec2 sw(right - close_d - 12.0f - kSwitchW, centred(y, kMastHeight, kSwitchH));
        if (toggle(ui, "enabled", sw, s.enabled)) change(ui, [on = s.enabled](Settings& st) { st.enabled = !on; });
        const LiveStatus& live = g->live;
        std::string status;
        if (!s.enabled) {
            status = "Grass off";
        } else {
            status = title_case(live.style.empty() ? "loading" : live.style);
            if (!live.biome.empty()) status += " \xC2\xB7 " + title_case(live.biome);
            if (!live.koppen.empty() && live.koppen != "?") status += " \xC2\xB7 " + live.koppen;
            status += live.airports > 0 ? " \xC2\xB7 " + std::to_string(live.instances) + " clumps" : " \xC2\xB7 no airport near";
        }
        status_chip(ui, sw.x - 12.0f, y, kMastHeight, s.enabled, status);

        const float eyebrow_px = 11.0f, title_px = 20.0f, eyebrow_box = eyebrow_px * 1.4f, title_box = title_px * 1.2f;
        const float top = centred(y, kMastHeight, eyebrow_box + 1.0f + title_box);
        label_text(ui, ImVec2(kPadX + 4.0f, centred(top, eyebrow_box, ui.css(eyebrow_px))), "zoal-grass " + g->host.version,
                   p.text_dim);
        const float ty = top + eyebrow_box + 1.0f;
        tracked_text(draw, g->face, g->face.bold, ui.css(title_px),
                     ImVec2(kPadX + 4.0f, centred(ty, title_box, ui.css(title_px))), p.text, "Grass Settings", -0.01f);
        y += kMastHeight + kGap;
    }

    // ---- tabs
    static const std::vector<const char*> kTabs = {"Look", "Placement", "Clearance", "Rendering", "Advanced"};
    int picked = tabs(ui, ImVec2(kPadX, y), kTabs, g->tab);
    if (picked >= 0) g->tab = picked;
    y += kTabsHeight + kGap;

    // ---- the page, scrolling
    const float footer_top = display.y - kPadBottom - kFooterHeight;
    {
        const ImVec2 cmin(kPadX, y), cmax(display.x - kPadX, footer_top - kGap);
        ImGui::SetCursorScreenPos(cmin);
        ImGui::BeginChild("page", ImVec2(cmax.x - cmin.x, cmax.y - cmin.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground);
        const Ui inner{g->face, p, ImGui::GetWindowDrawList(), &g->later};
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float scrollbar = ImGui::GetScrollMaxY() > 0.0f ? 14.0f : 0.0f;
        Flow flow{inner, origin.x, ImGui::GetContentRegionAvail().x - scrollbar, origin.y};
        switch (g->tab) {
            case 0: tab_look(flow, s); break;
            case 1: tab_placement(flow, s); break;
            case 2: tab_clearance(flow, s); break;
            case 3: tab_rendering(flow, s); break;
            default: tab_advanced(flow, s); break;
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(1.0f, flow.y - origin.y + 4.0f));
        ImGui::EndChild();
    }

    // ---- the footer: saved or not, and Save / Reload
    {
        draw->AddLine(ImVec2(kPadX, footer_top - kGap * 0.5f), ImVec2(kPadX + width, footer_top - kGap * 0.5f), p.border,
                      1.0f);
        const bool dirty = !settings_equal(s, g->saved);
        const float sw = button_width(ui, "Save"), rw = button_width(ui, "Reload");
        const float right = kPadX + width;
        if (button(ui, "save", ImVec2(right - sw, footer_top), ImVec2(sw, kButtonHeight), "Save", Variant::filled, dirty))
            ui.defer([] {
                Settings now = g->host.get();
                if (g->host.save(now)) {
                    g->saved = now;
                    g->toast = "Saved to zoal-grass.ini";
                } else {
                    g->toast = "Could not write zoal-grass.ini";
                }
                g->toast_until = g->now + 4.0f;
            });
        if (button(ui, "reload", ImVec2(right - sw - 8.0f - rw, footer_top), ImVec2(rw, kButtonHeight), "Reload",
                   Variant::outlined))
            ui.defer([] {
                g->saved = g->host.reload();
                g->toast = "Reloaded zoal-grass.ini";
                g->toast_until = g->now + 4.0f;
            });
        std::string note = g->now < g->toast_until
                               ? g->toast
                               : dirty ? "Unsaved changes \xE2\x80\x94 they apply now; Save keeps them for next time"
                                       : "Changes apply as you make them";
        const float px = ui.css(12.0f);
        if (dirty) draw->AddCircleFilled(ImVec2(kPadX + 6.0f, footer_top + kButtonHeight * 0.5f), 3.5f, p.accent);
        draw->AddText(g->face.regular, px,
                      ImVec2(kPadX + (dirty ? 16.0f : 2.0f), centred(footer_top, kButtonHeight, px)), p.text_dim,
                      fit(g->face.regular, px, note, width - sw - rw - 40.0f).c_str());
    }

    // the whole panel is its grip: a press on anything that is not a control moves the window
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive()) {
        g->moving = true;
        g->drag_started = true;
    }
    if (g->moving && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) g->moving = false;
    if (g->moving) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ImGui::End();
}

}  // namespace zg::gui
