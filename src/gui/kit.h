#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "imgui.h"

// What the HUD and the panel are both drawn with: colour arithmetic, the
// typeface, text that behaves like the CSS it replaced, and the icons.
//
// Dear ImGui only -- no X-Plane -- so the panel can be rendered off-screen
// as well as in the sim. From zoal-atc (src/ui/kit.hpp).
namespace zg::ui {

// --- colour ------------------------------------------------------------------

ImU32 rgba(unsigned hex, float alpha = 1.0f);
// `colour` with its alpha scaled by `opacity`.
ImU32 faded(ImU32 colour, float opacity);
// The RGB mix of two hex colours, `t` of the way from `a` to `b`.
unsigned blend(unsigned a, unsigned b, float t);
// CSS color-mix(in srgb, a, b t): every channel, alpha included.
ImU32 mix(ImU32 a, ImU32 b, float t);

// --- the typeface --------------------------------------------------------------

// Inter in three weights, or X-Plane's Roboto without it.
struct Typeface {
  ImFont *regular = nullptr;
  ImFont *semibold = nullptr;
  ImFont *bold = nullptr;
  // CSS sizes a font by its em; ImGui (FreeType too) by its whole line,
  // ascender to descender. This is the face's line in ems -- Inter's is 1.21,
  // Roboto's 1.17 -- so an ImGui size of css(14) draws what CSS's 14px did.
  float em_to_line = 1.2f;

  float css(float px) const { return px * em_to_line; }
};

// Loads the typeface into the current context's atlas: Inter from
// `inter_dir` when it is there, else Roboto from `roboto_dir`, else ImGui's
// own font. `log` hears what was missing.
Typeface load_typeface(const std::filesystem::path &inter_dir,
                       const std::filesystem::path &roboto_dir,
                       const std::function<void(const std::string &)> &log);

// --- text ----------------------------------------------------------------------

ImVec2 measure(ImFont *font, float size, const std::string &text,
               float wrap = 0.0f);

// Draws `value`; with a `shadow` colour, a copy one pixel down first, for text
// with no glass behind it.
void text(ImDrawList *draw, ImFont *font, float size, ImVec2 pos, ImU32 colour,
          const std::string &value, ImU32 shadow = 0, float wrap = 0.0f);

// Cuts `value` to fit `width`, ending it with an ellipsis.
std::string fit(ImFont *font, float size, const std::string &value,
                float width);

// Text with CSS letter-spacing: `tracking` em added after every glyph.
float tracked_width(const Typeface &face, ImFont *font, float size,
                    const std::string &value, float tracking);
void tracked_text(ImDrawList *draw, const Typeface &face, ImFont *font,
                  float size, ImVec2 pos, ImU32 colour,
                  const std::string &value, float tracking, ImU32 shadow = 0);

// Word-wraps `value` to `width`, a line per entry, so each can be set on a
// CSS line height rather than the font's own.
std::vector<std::string> wrap_lines(ImFont *font, float size,
                                    const std::string &value, float width);

std::string upper(std::string text);

// --- shapes ----------------------------------------------------------------------

// A rounded rectangle's outline in dashes, as CSS's border-style: dashed.
void dashed_rect(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 colour,
                 float rounding, ImDrawFlags corners, float dash = 4.0f,
                 float gap = 3.0f);

// --- icons: Material Symbols' shapes, drawn in a box -------------------------

enum class Icon { menu, close, reload, plane, panel, check, warn, arrow_down };

void icon(ImDrawList *draw, Icon which, ImVec2 at, float size, ImU32 colour);

} // namespace zg::ui

