#include "biome.h"

#include "platform.h"
#include "style.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace zg {

namespace {

const char* const kKoppen[31] = {"?",   "Af",  "Am",  "Aw",  "BWh", "BWk", "BSh", "BSk", "Csa", "Csb", "Csc",
                                 "Cwa", "Cwb", "Cwc", "Cfa", "Cfb", "Cfc", "Dsa", "Dsb", "Dsc", "Dsd", "Dwa",
                                 "Dwb", "Dwc", "Dwd", "Dfa", "Dfb", "Dfc", "Dfd", "ET",  "EF"};

// day of year shifted half a year in the southern hemisphere
int local_day(int day_of_year, double lat) {
    return ((lat < 0 ? day_of_year + 182 : day_of_year) % 365 + 365) % 365;
}

}  // namespace

int find_biome(const std::string& name) {
    for (int b = 0; b < kBiomeCount; ++b)
        if (name == kBiomes[b].name) return b;
    return -1;
}

const char* koppen_symbol(int code) { return code >= 1 && code <= 30 ? kKoppen[code] : kKoppen[0]; }

int biome_for_koppen(int code, double lat) {
    if (code == 1 || code == 2) return kTropical;
    if (code == 3) return kSavanna;
    if (code == 4 || code == 5) return kDesert;
    if (code == 6 || code == 7) return kSteppe;
    if (code >= 8 && code <= 10) return kMediterranean;
    if (code >= 11 && code <= 16) return kTemperate;
    if (code >= 17 && code <= 28) return kContinental;
    if (code == 29 || code == 30) return kTundra;
    double a = std::fabs(lat);   // no climate data: a rough guess by latitude
    return a < 23 ? kTropical : a < 50 ? kTemperate : a < 66 ? kContinental : kTundra;
}

int typical_koppen(int biome) {
    static const int k[kBiomeCount] = {15, 26, 9, 7, 4, 1, 3, 29};   // Cfb Dfb Csb BSk BWh Af Aw ET
    return biome >= 0 && biome < kBiomeCount ? k[biome] : 15;
}

const char* season_for_climate(int koppen, int day_of_year, double lat) {
    int d = local_day(day_of_year, lat);
    const char* base = season_for(day_of_year, lat);
    bool winter = std::strcmp(base, "winter") == 0;
    switch (koppen) {
        case 1: case 2:                                         // rainforest / monsoon: always green
            return "summer";
        case 3:                                                 // savanna: dry season Nov .. Apr (shifted south)
            return (d >= 305 || d < 120) ? "late_summer" : "summer";
        case 4:                                                 // hot desert
            return "late_summer";
        case 5:                                                 // cold desert
            return winter ? "winter" : "late_summer";
        case 6:                                                 // hot steppe: a short green flush in spring
            return std::strcmp(base, "spring") == 0 ? "summer" : "late_summer";
        case 7:                                                 // cold steppe
            if (winter) return "winter";
            if (std::strcmp(base, "spring") == 0) return "summer";
            return std::strcmp(base, "summer") == 0 ? "late_summer" : "autumn";
        case 8: case 9: case 10:                                // Mediterranean: green winter, golden summer
            if (d < 120 || d >= 330) return "spring";
            if (d < 165) return "summer";
            if (d < 300) return "late_summer";
            return "autumn";
        case 29:                                                // tundra: a short summer
            if (d >= 152 && d < 243) return "summer";
            if (d >= 243 && d < 280) return "autumn";
            if (d >= 135 && d < 152) return "spring";
            return "winter";
        case 30:
            return "winter";
        default:
            return base;
    }
}

SpeciesMix species_mix(int biome, const std::string& style) {
    SpeciesMix m;
    switch (biome) {
        default:
        case kTemperate:
            m.meadow = 0.30; m.seed = 0.06; m.seed_wild = 0.2; m.tussock = 0.02; m.tussock_dry = 0.3; m.moss = 0.01;
            m.sedge = 0.02; m.clover = 0.35; m.rosette = 0.15;
            break;
        case kContinental:
            m.meadow = 0.30; m.seed = 0.08; m.seed_wild = 0.2; m.tussock = 0.04; m.tussock_dry = 0.3; m.moss = 0.05;
            m.sedge = 0.05; m.clover = 0.25; m.rosette = 0.12;
            break;
        case kMediterranean:
            m.blades = 0.85; m.height = 1.1; m.meadow = 0.12; m.seed = 0.35; m.seed_wild = 0.7; m.tussock = 0.12;
            m.tussock_dry = 0.5; m.sedge = 0.01; m.clover = 0.12; m.rosette = 0.10;
            break;
        case kSteppe:
            m.blades = 0.55; m.height = 1.1; m.meadow = 0.04; m.seed = 0.30; m.seed_wild = 0.6; m.tussock = 0.45;
            m.tussock_dry = 0.6; m.sedge = 0.01; m.clover = 0.02; m.rosette = 0.05;
            break;
        case kDesert:
            m.blades = 0.22; m.height = 0.9; m.seed = 0.05; m.seed_wild = 0.8; m.tussock = 0.25; m.tussock_dry = 0.8;
            m.rosette = 0.02;
            break;
        case kTropical:
            m.blades = 1.15; m.height = 1.3; m.meadow = 0.15; m.seed = 0.10; m.seed_wild = 0.3; m.tussock = 0.05;
            m.tussock_dry = 0.1; m.moss = 0.02; m.sedge = 0.10; m.clover = 0.08; m.rosette = 0.05;
            break;
        case kSavanna:
            m.blades = 0.9; m.height = 1.5; m.meadow = 0.08; m.seed = 0.50; m.seed_wild = 0.6; m.tussock = 0.25;
            m.tussock_dry = 0.5; m.sedge = 0.03; m.clover = 0.02; m.rosette = 0.03;
            break;
        case kTundra:
            m.blades = 0.7; m.height = 0.6; m.meadow = 0.04; m.tussock = 0.15; m.tussock_dry = 0.4; m.moss = 0.5;
            m.sedge = 0.15; m.rosette = 0.02;
            break;
    }
    // the season: panicles and seed heads come later in the year
    if (style == "spring") {
        m.meadow *= 0.5; m.seed *= 0.2;
    } else if (style == "late_summer") {
        m.meadow *= 0.8; m.seed *= 1.3; m.tussock_dry = std::fmin(1.0, m.tussock_dry + 0.2);
    } else if (style == "autumn") {
        m.meadow *= 0.5; m.tussock_dry = std::fmin(1.0, m.tussock_dry + 0.3);
    } else if (style == "winter") {
        m.meadow *= 0.2; m.seed *= 0.5; m.clover *= 0.5; m.tussock_dry = 1.0;
    } else {                                                    // summer, custom
        m.seed *= 0.6;
    }
    return m;
}

bool ClimateMap::load(const std::string& path) {
    std::ifstream in(fs_path(path), std::ios::binary);
    std::string header;
    if (!in || !std::getline(in, header)) return false;
    std::istringstream hs(header);
    std::string magic;
    int ver = 0, w = 0, h = 0;
    hs >> magic >> ver >> w >> h;
    if (magic != "ZGKG" || ver != 1 || w <= 0 || h <= 0 || w > 10000 || h > 5000) return false;
    std::vector<uint8_t> cells(size_t(w) * size_t(h));
    size_t k = 0;
    unsigned char rec[3];
    while (k < cells.size() && in.read(reinterpret_cast<char*>(rec), 3)) {
        size_t n = size_t(rec[0]) | size_t(rec[1]) << 8;
        if (n == 0 || k + n > cells.size()) return false;
        std::memset(&cells[k], rec[2] <= 30 ? rec[2] : 0, n);
        k += n;
    }
    if (k != cells.size()) return false;
    set(w, h, std::move(cells));
    return true;
}

int ClimateMap::at(int x, int y) const {
    x = ((x % w_) + w_) % w_;
    if (y < 0 || y >= h_) return 0;
    return cells_[size_t(y) * size_t(w_) + size_t(x)];
}

int ClimateMap::koppen(double lat, double lon) const {
    if (!loaded() || !std::isfinite(lat) || !std::isfinite(lon)) return 0;
    int x = int(std::floor((lon + 180.0) / 360.0 * w_));
    int y = int(std::floor((90.0 - lat) / 180.0 * h_));
    if (int c = at(x, y)) return c;
    // coast / island airports: the nearest land cell, ring by ring
    int reach = std::max(1, w_ / 360);
    for (int r = 1; r <= 4 * reach; ++r) {
        int best = 0;
        double best_d = 1e9;
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int c = at(x + dx, y + dy);
                double d = double(dx) * dx + double(dy) * dy;
                if (c && d < best_d) { best = c; best_d = d; }
            }
        if (best) return best;
    }
    return 0;
}

}  // namespace zg
