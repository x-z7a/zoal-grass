#include "gui/kit.h"

#include "gui/font_metrics.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iterator>

#include "imgui_freetype.h"

namespace zg::ui {

// --- colour ------------------------------------------------------------------

ImU32 rgba(unsigned hex, float alpha) {
  return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF,
                  static_cast<int>(std::lround(alpha * 255.0f)));
}

ImU32 faded(ImU32 colour, float opacity) {
  const auto alpha = static_cast<float>((colour >> IM_COL32_A_SHIFT) & 0xFF);
  const auto scaled = static_cast<ImU32>(std::lround(alpha * opacity));
  return (colour & ~IM_COL32_A_MASK) | (scaled << IM_COL32_A_SHIFT);
}

unsigned blend(unsigned a, unsigned b, float t) {
  auto channel = [&](int shift) {
    const float x = static_cast<float>((a >> shift) & 0xFF);
    const float y = static_cast<float>((b >> shift) & 0xFF);
    return static_cast<unsigned>(std::lround(x + (y - x) * t)) << shift;
  };
  return channel(16) | channel(8) | channel(0);
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
  auto channel = [&](unsigned shift) {
    const float x = static_cast<float>((a >> shift) & 0xFF);
    const float y = static_cast<float>((b >> shift) & 0xFF);
    return static_cast<ImU32>(std::lround(x + (y - x) * t)) << shift;
  };
  return channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) |
         channel(IM_COL32_B_SHIFT) | channel(IM_COL32_A_SHIFT);
}

// --- the typeface --------------------------------------------------------------

namespace {

std::string read_file(const std::filesystem::path &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

} // namespace

// Inter is drawn for screens at small sizes, and evenly spaced without
// kerning, which ImGui does not do. Roboto, X-Plane's own UI face, is on every
// install; it has no semibold, so bold stands in.
Typeface load_typeface(const std::filesystem::path &inter_dir,
                       const std::filesystem::path &roboto_dir,
                       const std::function<void(const std::string &)> &log) {
  Typeface face;
  ImGuiIO &io = ImGui::GetIO();
  // Light hinting snaps glyphs to the pixel grid vertically only: stems and
  // baselines land on whole pixels, so small text is crisp rather than thin and
  // soft, while letter spacing keeps the font's own widths.
  io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;

  struct Slot {
    std::filesystem::path path;
    ImFont **font;
  };
  std::vector<Slot> slots;
  if (std::filesystem::exists(inter_dir / "Inter-Regular.ttf")) {
    slots = {{inter_dir / "Inter-Regular.ttf", &face.regular},
             {inter_dir / "Inter-SemiBold.ttf", &face.semibold},
             {inter_dir / "Inter-Bold.ttf", &face.bold}};
  } else {
    if (log) {
      log("no Inter in " + inter_dir.string() + "; using X-Plane's Roboto");
    }
    slots = {{roboto_dir / "Roboto-Regular.ttf", &face.regular},
             {roboto_dir / "Roboto-Bold.ttf", &face.semibold},
             {roboto_dir / "Roboto-Bold.ttf", &face.bold}};
  }

  if (const auto ratio = gui::font_line_to_em(read_file(slots.front().path))) {
    face.em_to_line = *ratio;
  }
  for (const Slot &slot : slots) {
    if (std::filesystem::exists(slot.path)) {
      // The size is only the default: ImGui 1.92 rasterizes each size asked.
      *slot.font =
          io.Fonts->AddFontFromFileTTF(slot.path.string().c_str(), face.css(14));
    }
    if (*slot.font == nullptr) {
      if (log) {
        log("could not load " + slot.path.string() +
            "; using ImGui's built-in font");
      }
      *slot.font = io.Fonts->AddFontDefault();
    }
  }
  return face;
}

// --- text ----------------------------------------------------------------------

ImVec2 measure(ImFont *font, float size, const std::string &text, float wrap) {
  return font->CalcTextSizeA(size, FLT_MAX, wrap, text.c_str());
}

void text(ImDrawList *draw, ImFont *font, float size, ImVec2 pos, ImU32 colour,
          const std::string &value, ImU32 shadow, float wrap) {
  if (shadow != 0) {
    draw->AddText(font, size, ImVec2(pos.x, pos.y + 1.0f), shadow,
                  value.c_str(), nullptr, wrap);
  }
  draw->AddText(font, size, pos, colour, value.c_str(), nullptr, wrap);
}

namespace {

// Drops the last UTF-8 character of `text`.
void pop_character(std::string &text) {
  while (!text.empty() &&
         (static_cast<unsigned char>(text.back()) & 0xC0) == 0x80) {
    text.pop_back();
  }
  if (!text.empty()) {
    text.pop_back();
  }
}

const char *next_character(const char *at, const char *end) {
  const char *next = at + 1;
  while (next < end && (static_cast<unsigned char>(*next) & 0xC0) == 0x80) {
    ++next;
  }
  return next;
}

} // namespace

std::string fit(ImFont *font, float size, const std::string &value,
                float width) {
  if (measure(font, size, value).x <= width) {
    return value;
  }
  const std::string ellipsis = "\xE2\x80\xA6";
  std::string cut = value;
  while (!cut.empty() && measure(font, size, cut + ellipsis).x > width) {
    pop_character(cut);
  }
  return cut + ellipsis;
}

float tracked_width(const Typeface &face, ImFont *font, float size,
                    const std::string &value, float tracking) {
  float width = 0.0f;
  const char *at = value.c_str();
  const char *end = at + value.size();
  while (at < end) {
    const char *next = next_character(at, end);
    width += font->CalcTextSizeA(size, FLT_MAX, 0.0f, at, next).x +
             tracking * size / face.em_to_line;
    at = next;
  }
  return width;
}

void tracked_text(ImDrawList *draw, const Typeface &face, ImFont *font,
                  float size, ImVec2 pos, ImU32 colour,
                  const std::string &value, float tracking, ImU32 shadow) {
  const char *at = value.c_str();
  const char *end = at + value.size();
  while (at < end) {
    const char *next = next_character(at, end);
    if (shadow != 0) {
      draw->AddText(font, size, ImVec2(pos.x, pos.y + 1.0f), shadow, at, next);
    }
    draw->AddText(font, size, pos, colour, at, next);
    pos.x += font->CalcTextSizeA(size, FLT_MAX, 0.0f, at, next).x +
             tracking * size / face.em_to_line;
    at = next;
  }
}

std::vector<std::string> wrap_lines(ImFont *font, float size,
                                    const std::string &value, float width) {
  std::vector<std::string> lines;
  const char *at = value.c_str();
  const char *end = at + value.size();
  while (at < end) {
    const char *newline = std::find(at, end, '\n');
    if (newline == at) {
      lines.emplace_back();
      ++at;
      continue;
    }
    const char *cut = font->CalcWordWrapPosition(size, at, newline, width);
    if (cut == at) {
      // A word wider than the box: break it rather than loop.
      cut = next_character(at, end);
    }
    lines.emplace_back(at, cut);
    at = cut;
    while (at < end && (*at == ' ' || *at == '\n')) {
      ++at;
    }
  }
  if (lines.empty()) {
    lines.emplace_back();
  }
  // Trailing spaces left at a wrap would make a line measure wider than it is.
  for (std::string &line : lines) {
    while (!line.empty() && line.back() == ' ') {
      line.pop_back();
    }
  }
  return lines;
}

std::string upper(std::string text) {
  for (char &c : text) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text;
}

// --- shapes ----------------------------------------------------------------------

void dashed_rect(ImDrawList *draw, ImVec2 min, ImVec2 max, ImU32 colour,
                 float rounding, ImDrawFlags corners, float dash, float gap) {
  // Inset by half a pixel, as AddRect does, so the line sits on the edge.
  draw->PathRect(ImVec2(min.x + 0.5f, min.y + 0.5f),
                 ImVec2(max.x - 0.5f, max.y - 0.5f), rounding, corners);
  std::vector<ImVec2> points(draw->_Path.begin(), draw->_Path.end());
  draw->PathClear();
  if (points.size() < 2) {
    return;
  }
  points.push_back(points.front());
  bool on = true;
  float left = dash;
  for (std::size_t i = 0; i + 1 < points.size(); ++i) {
    ImVec2 a = points[i];
    const ImVec2 b = points[i + 1];
    float length = std::hypot(b.x - a.x, b.y - a.y);
    while (length > 0.0f) {
      const float step = std::min(left, length);
      const float t = step / length;
      const ImVec2 c(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
      if (on) {
        draw->AddLine(a, c, colour, 1.0f);
      }
      a = c;
      length -= step;
      left -= step;
      if (left <= 0.0f) {
        on = !on;
        left = on ? dash : gap;
      }
    }
  }
}

// --- icons -------------------------------------------------------------------------

void icon(ImDrawList *draw, Icon which, ImVec2 at, float size, ImU32 colour) {
  const float s = size;
  const float w = std::max(1.5f, s / 9.0f);
  auto P = [&](float x, float y) { return ImVec2(at.x + x * s, at.y + y * s); };
  switch (which) {
  case Icon::menu:
    for (const float y : {0.25f, 0.5f, 0.75f}) {
      draw->AddLine(P(0.15f, y), P(0.85f, y), colour, w);
    }
    break;
  case Icon::close:
    draw->AddLine(P(0.22f, 0.22f), P(0.78f, 0.78f), colour, w);
    draw->AddLine(P(0.78f, 0.22f), P(0.22f, 0.78f), colour, w);
    break;
  case Icon::reload: {
    const ImVec2 centre = P(0.5f, 0.5f);
    draw->PathArcTo(centre, s * 0.32f, -0.6f, 4.4f, 20);
    draw->PathStroke(colour, w);
    // The arrowhead at the arc's open end.
    const ImVec2 tip = P(0.80f, 0.20f);
    draw->AddTriangleFilled(tip, P(0.84f, 0.48f), P(0.56f, 0.30f), colour);
    break;
  }
  case Icon::plane: {
    // Material Symbols' "flight", traced from its path (viewBox 960): the nose
    // is two quadratic curves, everything else straight edges.
    const float k = 1.0f / 960.0f;
    auto V = [&](float x, float y) { return P(x * k, y * k); };
    draw->PathLineTo(V(280, 880));
    draw->PathLineTo(V(280, 780));
    draw->PathLineTo(V(400, 696));
    draw->PathLineTo(V(400, 552));
    draw->PathLineTo(V(80, 680));
    draw->PathLineTo(V(80, 560));
    draw->PathLineTo(V(400, 336));
    draw->PathLineTo(V(400, 160));
    draw->PathBezierQuadraticCurveTo(V(400, 127), V(423.5f, 103.5f), 4);
    draw->PathBezierQuadraticCurveTo(V(447, 80), V(480, 80), 4);
    draw->PathBezierQuadraticCurveTo(V(513, 80), V(536.5f, 103.5f), 4);
    draw->PathBezierQuadraticCurveTo(V(560, 127), V(560, 160), 4);
    draw->PathLineTo(V(560, 336));
    draw->PathLineTo(V(880, 560));
    draw->PathLineTo(V(880, 680));
    draw->PathLineTo(V(560, 552));
    draw->PathLineTo(V(560, 696));
    draw->PathLineTo(V(680, 780));
    draw->PathLineTo(V(680, 880));
    draw->PathLineTo(V(480, 820));
    draw->PathFillConcave(colour);
    break;
  }
  case Icon::panel: {
    const ImVec2 points[] = {P(0.45f, 0.15f), P(0.15f, 0.15f), P(0.15f, 0.85f),
                             P(0.85f, 0.85f), P(0.85f, 0.55f)};
    draw->AddPolyline(points, 5, colour, w);
    draw->AddLine(P(0.42f, 0.58f), P(0.85f, 0.15f), colour, w);
    draw->AddLine(P(0.60f, 0.15f), P(0.85f, 0.15f), colour, w);
    draw->AddLine(P(0.85f, 0.15f), P(0.85f, 0.40f), colour, w);
    break;
  }
  case Icon::check: {
    const ImVec2 points[] = {P(0.16f, 0.52f), P(0.40f, 0.76f), P(0.86f, 0.28f)};
    draw->AddPolyline(points, 3, colour, w * 1.2f);
    break;
  }
  case Icon::warn: {
    draw->AddTriangle(P(0.5f, 0.10f), P(0.06f, 0.88f), P(0.94f, 0.88f), colour,
                      w);
    draw->AddLine(P(0.5f, 0.38f), P(0.5f, 0.62f), colour, w);
    draw->AddCircleFilled(P(0.5f, 0.74f), w * 0.7f, colour);
    break;
  }
  case Icon::arrow_down: {
    draw->AddLine(P(0.5f, 0.15f), P(0.5f, 0.80f), colour, w);
    const ImVec2 points[] = {P(0.22f, 0.54f), P(0.5f, 0.82f), P(0.78f, 0.54f)};
    draw->AddPolyline(points, 3, colour, w);
    break;
  }
  }
}

} // namespace zg::ui
