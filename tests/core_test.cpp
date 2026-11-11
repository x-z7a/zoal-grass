// X-Plane-free checks of the plugin core, run by CI on every platform:
//  - an apt.dat record parses, and its pavement mask keeps grass off the runway but not off the grass beside it
//  - the airport index finds the airport from a tiny scenery layout
//  - the grass objects build for every style and biome, are well-formed OBJ8 files and stay in the triangle budget
//  - the season mapping follows the hemisphere and the climate
//  - the Koppen climate grid gives the right climate for known airports (and for coastal ones in a water cell)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "apt.h"
#include "assets.h"
#include "biome.h"
#include "mask.h"
#include "platform.h"
#include "style.h"

using namespace zg;

static int failures = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

static const char* kApt =
    "I\n1200 Version\n\n"
    "1 100 0 0 ZTST Test Field\n"
    "1302 datum_lat 47.0\n1302 datum_lon 8.0\n"
    // a 1 km asphalt runway, 30 m wide, running north
    "100 30.00 1 0 0.25 0 0 0 18 47.0000000 8.0000000 0 0 0 0 0 0 36 47.0090000 8.0000000 0 0 0 0 0 0\n"
    // a square concrete apron east of it, with a node row to close the loop
    "110 2 0.25 0.00 Apron\n"
    "111 47.0040000 8.0010000\n111 47.0040000 8.0020000\n111 47.0050000 8.0020000\n113 47.0050000 8.0010000\n"
    "99\n";

static std::string read_text(const std::string& path) {
    std::ifstream in(fs_path(path), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: core_test <resources dir> <scratch dir>\n");
        return 2;
    }
    std::string resources = argv[1], scratch = argv[2];
    make_dir(scratch);

    // ---- apt.dat parsing and the pavement mask
    auto ap = parse_airport(kApt, std::char_traits<char>::length(kApt));
    CHECK(ap && ap->ident == "ZTST");
    CHECK(ap && ap->runways.size() == 1);
    CHECK(ap && ap->pavements.size() == 1);
    if (ap) {
        MaskSettings ms;
        ms.cell_m = 1.0;
        ms.margin_m = 3.0;
        ms.open_pad_m = 200.0;
        auto mask = build_mask(*ap, ms);
        CHECK(mask != nullptr);
        if (mask) {
            XY mid_runway = mask->proj.to_xy(47.0045, 8.0);
            XY beside = mask->proj.to_xy(47.0045, 8.0 - 60.0 / 75800.0);    // ~60 m west of the centreline
            XY apron = mask->proj.to_xy(47.0045, 8.0015);
            XY edge = mask->proj.to_xy(47.0045, 8.0 + 16.5 / 75800.0);      // 1.5 m off the runway edge: keep-out
            CHECK(!mask->grass_ok(mid_runway.x, mid_runway.y));
            CHECK(mask->grass_ok(beside.x, beside.y));
            CHECK(!mask->grass_ok(apron.x, apron.y));
            CHECK(!mask->grass_ok(edge.x, edge.y));
        }
    }

    // ---- the airport index over a tiny X-Plane folder
    std::string root = scratch + "/xp";
    make_dir(root + "/Custom Scenery/Test Pack/Earth nav data");
    make_dir(root + "/Global Scenery/Global Airports/Earth nav data");
    {
        std::ofstream(fs_path(root + "/Custom Scenery/scenery_packs.ini"))
            << "I\n1000 Version\nSCENERY\n\nSCENERY_PACK Custom Scenery/Test Pack/\nSCENERY_PACK *GLOBAL_AIRPORTS*\n";
        std::ofstream(fs_path(root + "/Custom Scenery/Test Pack/Earth nav data/apt.dat"), std::ios::binary) << kApt;
    }
    AirportIndex index;
    index.build(root, scratch + "/index.txt", [](const std::string&) {});
    CHECK(index.find("ZTST") != nullptr);
    CHECK(!index.nearby(47.004, 8.001, 500).empty());
    if (const AirportEntry* e = index.find("ZTST")) {
        auto loaded = index.load(*e);
        CHECK(loaded && loaded->runways.size() == 1);
    }
    CHECK(!on_virtual_fs(root));

    // ---- grass objects for every style
    GrassAssets assets;
    CHECK(assets.load(resources, [](const std::string& m) { std::printf("  %s\n", m.c_str()); }));
    for (int run = 0; run < kStyleCount + kBiomeCount; ++run) {
        const Style& st = kStyles[run < kStyleCount ? run : 1];               // the biomes in summer
        int biome = run < kStyleCount ? kTemperate : run - kStyleCount;
        LookParams p;
        p.mix = species_mix(biome, st.name);
        p.footprint_m = 4.5;
        p.height_m = 0.25 * st.height_factor;
        p.dry_mix = st.dry_mix >= 0 ? st.dry_mix : 0.15;
        p.texture = st.texture;
        p.lod_near = 40;
        p.lod_mid = 140;
        p.lod_far = 390;
        BuiltObjects out;
        std::string dir = scratch + "/objs_" + st.name + "_" + kBiomes[biome].name;
        CHECK(assets.build(resources, dir, p, out, [](const std::string& m) { std::printf("  %s\n", m.c_str()); }));
        CHECK(out.normal.size() == 4 && out.mowed.size() == 4);
        CHECK(out.tris[0] > 0 && out.tris[1] == 0 && out.tris[2] == 0 && out.tris[3] == 0);
        CHECK(out.tris[0] < 1800);                    // one level for every clump: keep it cheap
        std::printf("%-12s %-13s triangles per level %d/%d/%d/%d, %d plant cards\n", st.name, kBiomes[biome].name,
                    out.tris[0], out.tris[1], out.tris[2], out.tris[3], out.cards);
        if (!out.normal.empty()) {
            std::string obj = read_text(out.normal[0]);
            CHECK(obj.rfind("A\n800\nOBJ\n", 0) == 0);
            CHECK(obj.find("TEXTURE " + std::string(st.texture)) != std::string::npos);
            CHECK(obj.find("ATTR_LOD 0 390") != std::string::npos);   // one level out to the grass edge
            CHECK(obj.find("ATTR_LOD", obj.find("ATTR_LOD") + 1) == std::string::npos);
            CHECK(obj.find('\r') == std::string::npos);
            CHECK(is_file(dir + "/" + st.texture));
        }
    }

    {   // real 3D blades close up: four levels
        LookParams p;
        p.footprint_m = 4.5;
        p.lod_near = 40;
        p.lod_mid = 140;
        p.lod_far = 390;
        p.real_blades = true;
        p.mix = species_mix(kTemperate, "summer");
        BuiltObjects out;
        CHECK(assets.build(resources, scratch + "/objs_blades", p, out, [](const std::string&) {}));
        CHECK(out.tris[0] > out.tris[1] && out.tris[1] > out.tris[2] && out.tris[2] > out.tris[3] && out.tris[0] < 40000);
        std::string obj = read_text(out.normal[0]);
        CHECK(obj.find("ATTR_LOD 0 16") != std::string::npos && obj.find("ATTR_LOD 16 40") != std::string::npos);
        std::printf("real blades  temperate     triangles per level %d/%d/%d/%d\n", out.tris[0], out.tris[1], out.tris[2],
                    out.tris[3]);
    }

    // ---- seasons
    CHECK(std::string(season_for(181, 47.0)) == "summer");
    CHECK(std::string(season_for(181, -45.0)) == "winter");
    CHECK(std::string(season_for(14, 10.0)) == "summer");
    CHECK(std::string(season_for(14, 37.0)) == "autumn");
    CHECK(std::string(season_for_climate(8, 181, 41.8)) == "late_summer");   // Rome in July: golden
    CHECK(std::string(season_for_climate(8, 14, 41.8)) == "spring");         // ... and green in January
    CHECK(std::string(season_for_climate(3, 181, -12.4)) == "late_summer");  // Darwin: July is the dry season
    CHECK(std::string(season_for_climate(3, 14, -12.4)) == "summer");
    CHECK(std::string(season_for_climate(1, 14, 1.4)) == "summer");          // Singapore
    CHECK(std::string(season_for_climate(26, 181, 44.5)) == "summer");       // Burlington
    CHECK(std::string(season_for_climate(29, 100, 71.3)) == "winter");       // Utqiagvik in April

    // ---- climate grid
    ClimateMap climate;
    CHECK(climate.load(resources + "/climate_kg.bin"));
    struct Place { const char* name; double lat, lon; int biome; };
    const Place places[] = {
        {"London", 51.47, -0.45, kTemperate},     {"Burlington", 44.47, -73.15, kContinental},
        {"Rome", 41.80, 12.25, kMediterranean},   {"Denver", 39.86, -104.67, kSteppe},
        {"Phoenix", 33.43, -112.01, kDesert},     {"Singapore", 1.36, 103.99, kTropical},
        {"Darwin", -12.41, 130.88, kSavanna},     {"Utqiagvik", 71.29, -156.77, kTundra},
        {"Dover strait", 51.0, 1.5, kTemperate},  // a water cell: the nearest land
    };
    for (const Place& pl : places) {
        int k = climate.koppen(pl.lat, pl.lon);
        std::printf("%-13s Koppen %-3s -> %s\n", pl.name, koppen_symbol(k), kBiomes[biome_for_koppen(k, pl.lat)].name);
        CHECK(biome_for_koppen(k, pl.lat) == pl.biome);
    }
    CHECK(climate.koppen(-40.0, -100.0) == 0);              // South Pacific: no land near
    CHECK(biome_for_koppen(0, 10.0) == kTropical && biome_for_koppen(0, 45.0) == kTemperate);

    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
