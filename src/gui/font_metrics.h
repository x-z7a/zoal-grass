// A typeface's line height in ems, read from its head and hhea tables, so ImGui (which sizes a font by its whole
// line) can draw the CSS pixel sizes the zoal UI is specified in. From zoal-atc (src/gui/font_metrics.cpp).
#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace zg::gui {

std::optional<float> font_line_to_em(const unsigned char* data, std::size_t size);
std::optional<float> font_line_to_em(const std::string& bytes);

}  // namespace zg::gui
