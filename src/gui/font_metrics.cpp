#include "gui/font_metrics.h"

#include <cstdint>

namespace zg::gui {

namespace {

std::uint32_t u32(const unsigned char *p) {
  return (static_cast<std::uint32_t>(p[0]) << 24) |
         (static_cast<std::uint32_t>(p[1]) << 16) |
         (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
}

std::uint16_t u16(const unsigned char *p) {
  return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

std::int16_t i16(const unsigned char *p) {
  return static_cast<std::int16_t>(u16(p));
}

} // namespace

std::optional<float> font_line_to_em(const unsigned char *data,
                                     std::size_t size) {
  // The offset table: a 12-byte header, then 16 bytes per table record.
  if (data == nullptr || size < 12) {
    return std::nullopt;
  }
  const std::size_t tables = u16(data + 4);
  if (size < 12 + tables * 16) {
    return std::nullopt;
  }
  std::size_t head = 0;
  std::size_t hhea = 0;
  for (std::size_t i = 0; i < tables; ++i) {
    const unsigned char *record = data + 12 + i * 16;
    const std::uint32_t offset = u32(record + 8);
    if (record[0] == 'h' && record[1] == 'e' && record[2] == 'a' &&
        record[3] == 'd') {
      head = offset;
    } else if (record[0] == 'h' && record[1] == 'h' && record[2] == 'e' &&
               record[3] == 'a') {
      hhea = offset;
    }
  }
  // unitsPerEm is at 18 in head; ascender and descender at 4 and 6 in hhea.
  if (head == 0 || hhea == 0 || head + 20 > size || hhea + 8 > size) {
    return std::nullopt;
  }
  const int units = u16(data + head + 18);
  const int ascender = i16(data + hhea + 4);
  const int descender = i16(data + hhea + 6);
  const int line = ascender - descender;
  if (units < 16 || line <= 0) {
    return std::nullopt;
  }
  const float ratio = static_cast<float>(line) / static_cast<float>(units);
  // A line under 0.8 em or over 2 em is a broken table, not a typeface.
  if (ratio < 0.8f || ratio > 2.0f) {
    return std::nullopt;
  }
  return ratio;
}

std::optional<float> font_line_to_em(const std::string &bytes) {
  return font_line_to_em(reinterpret_cast<const unsigned char *>(bytes.data()),
                         bytes.size());
}

} // namespace zg::gui
