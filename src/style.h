// Seasonal grass styles: which baked texture, how many dry tufts, how tall.
#pragma once

#include <cmath>
#include <cstring>
#include <string>

namespace zg {

struct Style {
    const char* name;          // as in zoal-grass.ini
    const char* label;         // menu text
    const char* texture;       // file in resources/textures
    double dry_mix;            // fraction of tufts drawn from the dry texture rows
    double height_factor;      // times grass_height_cm
};

// "custom" uses dry_mix from the ini and the original texture.
inline const Style kStyles[] = {
    {"spring", "Spring (fresh green)", "zg_atlas_spring.png", 0.03, 0.90},
    {"summer", "Summer", "zg_atlas_summer.png", 0.12, 1.00},
    {"late_summer", "Late summer (drying)", "zg_atlas_late_summer.png", 0.45, 1.05},
    {"autumn", "Autumn (olive / brown)", "zg_atlas_autumn.png", 0.55, 0.95},
    {"winter", "Winter (dormant, flattened)", "zg_atlas_winter.png", 0.80, 0.70},
    {"custom", "Custom (grass colour / ini)", "zg_atlas_summer.png", -1.0, 1.00},
};
constexpr int kStyleCount = int(sizeof(kStyles) / sizeof(kStyles[0]));

inline const Style* find_style(const std::string& name) {
    for (const Style& s : kStyles)
        if (name == s.name) return &s;
    return nullptr;
}

// Season from the day of year (0 = 1 Jan) and latitude: tropics stay summer, subtropical winters stay autumnal.
inline const char* season_for(int day_of_year, double lat) {
    double alat = std::fabs(lat);
    if (alat < 23.0) return "summer";
    int d = ((lat < 0 ? day_of_year + 182 : day_of_year) % 365 + 365) % 365;
    const char* s;
    if (d < 59) s = "winter";            // Jan, Feb
    else if (d < 141) s = "spring";      // Mar .. 20 May
    else if (d < 223) s = "summer";      // .. 10 Aug
    else if (d < 264) s = "late_summer"; // .. 20 Sep
    else if (d < 330) s = "autumn";      // .. 25 Nov
    else s = "winter";
    if (alat < 40.0 && std::strcmp(s, "winter") == 0) s = "autumn";   // mild winters stay green-brown
    return s;
}

}  // namespace zg
