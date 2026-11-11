// Where on Earth the aircraft is decides which plants grow: a Koppen-Geiger climate grid (Beck et al. 2018, CC BY 4.0,
// baked by tools/bake_climate.py into resources/climate_kg.bin) maps the position to a biome; the biome decides the
// species mix and how the seasons look (Mediterranean summers are golden, savannas brown in the dry season, ...).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace zg {

enum Biome : int {
    kTemperate = 0,     // Cf, Cw: lush lawn and meadow, clover, rosettes
    kContinental,       // D: meadow, more moss and sedge, real winters
    kMediterranean,     // Cs: green in winter and spring, golden seed grass in summer
    kSteppe,            // BS: bunch-grass tussocks and dry seed grass
    kDesert,            // BW: sparse tussocks on bare ground
    kTropical,          // Af, Am: tall lush green grass all year, sedges
    kSavanna,           // Aw: tall seed grass, green wet season, straw dry season
    kTundra,            // ET, EF: moss cushions, sedge, short grass
    kBiomeCount
};

struct BiomeInfo {
    const char* name;    // as in zoal-grass.ini
    const char* label;   // menu text
};

inline const BiomeInfo kBiomes[kBiomeCount] = {
    {"temperate", "Temperate (lawn, meadow, clover)"},
    {"continental", "Continental (meadow, moss, sedge)"},
    {"mediterranean", "Mediterranean (golden summer grass)"},
    {"steppe", "Steppe (tussocks, dry seed grass)"},
    {"desert", "Desert (sparse tussocks)"},
    {"tropical", "Tropical (tall lush grass)"},
    {"savanna", "Savanna (tall seed grass)"},
    {"tundra", "Tundra (moss, sedge)"},
};

int find_biome(const std::string& name);              // -1 if unknown
const char* koppen_symbol(int code);                  // "Cfb", "?" for 0 / out of range
int biome_for_koppen(int code, double lat);           // code 0 (unknown): by latitude
int typical_koppen(int biome);                        // a representative class (for a biome chosen by hand)

// Season style ("spring" .. "winter") for a Koppen class, day of year (0 = 1 Jan) and latitude.
const char* season_for_climate(int koppen, int day_of_year, double lat);

// How many of each plant grow per m2 (before the user's density), for a biome in a season style.
struct SpeciesMix {
    double blades = 1.0;         // factor on the 3D blade tufts (near) and the lawn cards (mid / far)
    double height = 1.0;         // factor on the grass height
    double meadow = 0;           // grass with feathery panicles
    double seed = 0;             // tall seed-head grass (timothy / wild awned)
    double seed_wild = 0;        //   share of the straw-coloured awned kind
    double tussock = 0;          // bunch-grass mounds
    double tussock_dry = 0;      //   share of the dry ones
    double moss = 0;             // moss cushions
    double sedge = 0;            // dark spiky sedge / rush
    double clover = 0;           // flat clover patches
    double rosette = 0;          // flat dandelion / plantain rosettes
};
SpeciesMix species_mix(int biome, const std::string& style);

// The climate grid (0.25 degree; row 0 = 90N, column 0 = 180W).
class ClimateMap {
public:
    bool load(const std::string& path);
    bool loaded() const { return w_ > 0; }
    // Koppen class at a position; water cells take the nearest land class within ~1 degree; 0 if none.
    int koppen(double lat, double lon) const;
    // test hook: a w x h grid of classes
    void set(int w, int h, std::vector<uint8_t> cells) { w_ = w; h_ = h; cells_ = std::move(cells); }

private:
    int at(int x, int y) const;
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> cells_;
};

}  // namespace zg
