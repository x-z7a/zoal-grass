// Synthetic DSFs exercise real binary pools/commands and a priority-ordered scenery stack without X-Plane.
#include "dsf.h"
#include "platform.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace zg;
using Bytes = std::vector<uint8_t>;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; ++failures; } } while (0)
static void u8(Bytes& b, unsigned v) { b.push_back(uint8_t(v)); }
static void u16(Bytes& b, unsigned v) { u8(b, v); u8(b, v >> 8); }
static void u32(Bytes& b, uint32_t v) { u16(b, v); u16(b, v >> 16); }
static void f32(Bytes& b, float f) { uint32_t v; std::memcpy(&v, &f, 4); u32(b, v); }
static void append(Bytes& a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); }
static Bytes atom(std::string id, const Bytes& body) {
    Bytes b(id.rbegin(), id.rend()); u32(b, unsigned(body.size() + 8)); append(b, body); return b;
}
static Bytes strings(const std::vector<std::string>& v) {
    Bytes b;
    for (auto& s : v) { b.insert(b.end(), s.begin(), s.end()); b.push_back(0); }
    return b;
}
static void write(const std::string& path, const Bytes& bytes) {
    make_dir(to_utf8(fs_path(path).parent_path()));
    std::ofstream out(fs_path(path), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}
static void write(const std::string& path, const std::string& text) { write(path, Bytes(text.begin(), text.end())); }
static Bytes read(const std::string& path) {
    std::ifstream in(fs_path(path), std::ios::binary);
    return Bytes(std::istreambuf_iterator<char>(in), {});
}

struct Fixture {
    Bytes geo, commands;
    std::vector<std::string> polygons, objects, networks, properties{"sim/overlay", "1"};
    unsigned pools = 0, road_pools = 0;
    // First two planes: lon/lat within 8..8.01 / 47..47.01. Other planes are raw integers (zero scale).
    unsigned pool(const std::vector<std::vector<double>>& points, bool wide = false, int encoding = 0) {
        Bytes data, scale;
        u32(data, unsigned(points.size())); u8(data, unsigned(points.at(0).size()));
        for (size_t plane = 0; plane < points[0].size(); ++plane) {
            float extent = plane < 2 ? 0.01f : 0, offset = plane == 0 ? 8 : plane == 1 ? 47 : 0;
            f32(scale, extent); f32(scale, offset);
            u8(data, encoding);
            uint32_t prev = 0;
            if (encoding >= 2) u8(data, unsigned(points.size())); // literal RLE run
            for (const auto& p : points) {
                uint32_t v = plane < 2 ? uint32_t(std::llround((p[plane] - offset) / extent * (wide ? 4294967295.0 : 65535.0))) : uint32_t(p[plane]);
                uint32_t raw = encoding == 1 || encoding == 3 ? v - prev : v;
                if (wide) u32(data, raw); else u16(data, raw);
                prev = v;
            }
        }
        append(geo, atom(wide ? "PO32" : "POOL", data));
        append(geo, atom(wide ? "SC32" : "SCAL", scale));
        return wide ? road_pools++ : pools++;
    }
    void select(unsigned pool, unsigned def) { u8(commands, 1); u16(commands, pool); u8(commands, 3); u8(commands, def); }
    void polygon(const std::string& name, double lon, double lat, double size = 0.0003) {
        unsigned id = unsigned(polygons.size()); polygons.push_back(name);
        select(pool({{lon, lat}, {lon + size, lat}, {lon + size, lat + size}, {lon, lat + size}}), id);
        u8(commands, 13); u16(commands, 0); u16(commands, 0); u16(commands, 4);
    }
    void object(const std::string& name, double lon, double lat, bool range = false) {
        unsigned id = unsigned(objects.size()); objects.push_back(name);
        select(pool({{lon, lat, 90}}), id);
        u8(commands, range ? 8 : 7); u16(commands, 0); if (range) u16(commands, 1);
    }
    void road(const std::vector<std::vector<double>>& points, int cmd = 9, int encoding = 0, int subtype = 0,
              unsigned offset = 0) {
        if (networks.empty()) networks.push_back("lib/test.net");
        select(pool(points, true, encoding), 0);
        u8(commands, 6); u8(commands, subtype);
        u8(commands, 2); u32(commands, offset);
        u8(commands, cmd);
        if (cmd == 10) { u16(commands, 0); u16(commands, unsigned(points.size()) - offset); }
        else {
            u8(commands, unsigned(points.size()) - offset);
            for (unsigned i = offset; i < points.size(); ++i) {
                if (cmd == 11) u32(commands, i); else u16(commands, i - offset);
            }
        }
    }
    void filter(int index) { u8(commands, 32); u8(commands, 6); u16(commands, 1); u32(commands, uint32_t(index)); }
    Bytes bytes() const {
        Bytes b{'X','P','L','N','E','D','S','F'}; u32(b, 1);
        append(b, atom("HEAD", atom("PROP", strings(properties))));
        Bytes defs;
        append(defs, atom("POLY", strings(polygons))); append(defs, atom("OBJT", strings(objects)));
        append(defs, atom("NETW", strings(networks)));
        append(b, atom("DEFN", defs)); append(b, atom("GEOD", geo)); append(b, atom("CMDS", commands));
        b.resize(b.size() + 16); // hash isn't consulted by the reader
        return b;
    }
};
static DsfData parse(const Fixture& f, bool owned = true) {
    auto b = f.bytes();
    return parse_dsf(b.data(), b.size(), [](const std::string&) { return true; }, [owned](const std::string&) { return owned; });
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::string scratch = argv[1], fixtures = argv[2];
    // Keep test runs independent, including old index caches and disabled pack fixtures.
    std::filesystem::remove_all(fs_path(scratch));
    make_dir(scratch);

    for (int encoding = 0; encoding < 4; ++encoding) {
        for (int cmd = 9; cmd <= 11; ++cmd) {
            Fixture f;
            f.road({{8.004,47.002,0,1}, {8.004,47.003,0,0}, {8.004,47.004,0,2}, {8.004,47.005,0,3}}, cmd, encoding);
            f.polygon("block.agb", 8.005, 47.003);
            f.object("point.agp", 8.006, 47.003, true);
            auto d = parse(f);
            CHECK(d.roads.size() == 2); // the middle junction belongs to both chains
            CHECK(d.roads[0].points.size() == 3 && d.roads[1].points.size() == 2);
            CHECK(std::abs(d.roads[1].points[0][1] - 47.004) < 1e-7);
            CHECK(d.polygons.size() == 1 && d.objects.size() == 1);
            CHECK(d.objects[0].heading == 90); // zero-scale plane is raw data
        }
    }
    {
        Fixture f;
        f.road({{8.001,47.001,0,99}, {8.004,47.002,0,1}, {8.004,47.003,0,2}}, 9, 3, 0, 1);
        CHECK(std::abs(parse(f).roads[0].points[0][0] - 8.004) < 1e-7);
        f.properties.insert(f.properties.end(), {"sim/filter/aptid", "OTHER"});
        f.filter(0);
        f.polygon("filtered.agb", 8.005, 47.003);
        f.object("filtered.agp", 8.006, 47.003);
        f.road({{8.004,47.006,0,1}, {8.004,47.007,0,2}});
        auto d = parse(f, false);
        CHECK(d.polygons.empty() && d.objects.empty() && d.roads.size() == 1);
        f.filter(-1); f.polygon("unfiltered.fac", 8.005, 47.005);
        CHECK(parse(f, false).polygons.size() == 1);
        auto bad = f.bytes(); bad.resize(20);
        bool threw = false;
        try { parse_dsf(bad.data(), bad.size(), {}, {}); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }

    {   // malformed compressed pool runs and truncated commands must fail promptly, without crossing atoms
        Fixture f;
        f.road({{8.004,47.002,0,1}, {8.004,47.003,0,2}}, 9, 2);
        f.geo[14] = 0x80; // zero-length repeated RLE run in the first PO32 plane
        bool threw = false;
        try { parse(f); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
        Fixture truncated;
        u8(truncated.commands, 4); // SET_DEFINITION_16 without its operand
        threw = false;
        try { parse(truncated); } catch (const std::runtime_error&) { threw = true; }
        CHECK(threw);
    }

    std::string root = scratch + "/xp";
    const std::string tile = "/Earth nav data/+40+000/+47+008.dsf";
    auto pack = [&](const std::string& name) { return root + "/Custom Scenery/" + name; };
    write(root + "/Custom Scenery/scenery_packs.ini",
          "SCENERY_PACK Custom Scenery/Airport/\nSCENERY_PACK Custom Scenery/Overlay/\n"
          "SCENERY_PACK_DISABLED Custom Scenery/Disabled/\nSCENERY_PACK Custom Scenery/Base/\n");
    const std::string apt = "I\n1200 Version\n1 100 0 0 TEST Test\n"
        "100 30 1 0 0.25 0 0 0 18 47.001 8.001 0 0 0 0 0 0 36 47.009 8.001 0 0 0 0 0 0\n99\n";
    write(pack("Airport") + "/Earth nav data/apt.dat", apt);
    write(pack("Overlay") + "/library.txt", "A\n800\nLIBRARY\nEXPORT lib/test.net roads.net\nEXPORT lib/house.obj house.obj\n");
    write(pack("Overlay") + "/roads.net", "A\n800\nROADS\nROAD_TYPE 20 12 10 0 1 1 1\nROAD_CENTER 3\n"
        "ROAD_DRAPED 0 0 0 0\nROAD_DRAPE_CHOICE 0 20\n"); // asymmetric: envelope is 9 m
    write(pack("Overlay") + "/house.obj", "A\n800\nOBJ\nVT -2 0 -4 0 1 0 0 0\nVT 2 10 4 0 1 0 1 1\n");
    write(pack("Overlay") + "/point.agp", "A\n1000\nAG_POINT\nTEXTURE_SCALE 100 100\nTEXTURE_WIDTH 20\n"
        "OBJECT lib/house.obj\nTILE 0 0 100 100\nANCHOR_PT 0 0\nOBJ_DRAPED 100 100 0 0\n");
    write(pack("Overlay") + "/paved.pol", "A\n850\nDRAPED_POLYGON\nSURFACE concrete\n");
    write(pack("Overlay") + "/dirt.pol", "A\n850\nDRAPED_POLYGON\nSURFACE dirt\n");
    // Regional/random library variants can be wider than the first export; use their combined envelope.
    std::ofstream(fs_path(pack("Overlay") + "/library.txt"), std::ios::app)
        << "EXPORT lib/test.net roads-wide.net\nEXPORT lib/house.obj house-wide.obj\n";
    write(pack("Overlay") + "/roads-wide.net", "ROAD_TYPE 20 24 10 0 1 1 1\n"
        "ROAD_DRAPED 0 0 0 0\nROAD_DRAPE_CHOICE 0 20\n");
    write(pack("Overlay") + "/house-wide.obj", "A\n800\nOBJ\nVT 12 0 0 0 1 0 0 0\nVT -12 10 0 0 1 0 1 1\n");
    Fixture upper;
    upper.properties.insert(upper.properties.end(), {"sim/exclude_obj", "8.0058/47.0048/8.0066/47.0056"});
    upper.polygon("own.agb",8.006,47.005); // own exclusion must not remove own feature
    write(pack("Airport") + tile, upper.bytes());
    Fixture overlay;
    overlay.polygon("block.agb",8.005,47.003);
    overlay.polygon("string.ags",8.005,47.004);
    overlay.polygon("house.fac",8.005,47.006);
    overlay.polygon("excluded.agb",8.006,47.005,0.0005); // removed by higher pack
    overlay.polygon("paved.pol",8.007,47.003);
    overlay.polygon("dirt.pol",8.007,47.003); // must not erase the pavement
    overlay.object("lib/house.obj",8.006,47.007);
    overlay.object("point.agp",8.007,47.007);
    overlay.road({{8.004,47.002,0,1}, {8.004,47.004,0,2}},9,3);
    // Cubic curve bulges east: x=8.0065, y=47.0025 is on it, not on its endpoint chord.
    overlay.road({{8.005,47.002,0,1}, {8.007,47.002,1,0}, {8.007,47.003,1,0}, {8.005,47.003,0,2}},11);
    write(pack("Overlay") + tile, overlay.bytes());
    write(scratch + "/compressed-source.dsf", overlay.bytes());
    Fixture disabled; disabled.polygon("disabled.agb",8.008,47.008);
    write(pack("Disabled") + tile, disabled.bytes());
    Fixture base; base.properties = {}; base.polygon("base.agb",8.008,47.003);
    write(pack("Base") + tile, base.bytes());
    Fixture global; global.properties = {}; global.polygon("inactive.agb",8.008,47.005);
    write(root + "/Global Scenery/X-Plane 12 Global Scenery" + tile, global.bytes());

    AirportIndex index;
    CHECK(index.build(root, scratch + "/index.txt", [](const std::string&) {}));
    const auto* entry = index.find("TEST");
    CHECK(entry != nullptr);
    if (!entry) return 1;
    auto airport = index.load(*entry);
    GroundPolygons reader(root, &index, "", "", [](const std::string& s) { std::cout << s << '\n'; });
    airport->draped = reader.collect(*airport, *entry, 1000);
    MaskSettings settings; settings.open_pad_m = 1000; settings.margin_m = 0; settings.grass_on_dirt = true;
    auto mask = build_mask(*airport, settings);
    CHECK(mask != nullptr);
    auto ok = [&](double lon, double lat, double radius = 0) {
        XY q = mask->proj.to_xy(lat, lon);
        return mask->footprint_ok(q.x,q.y,radius);
    };
    CHECK(!ok(8.0051,47.0032)); // AGB
    CHECK(!ok(8.0051,47.0041)); // AGS
    CHECK(!ok(8.0051,47.0061)); // facade
    CHECK(!ok(8.0061,47.0051)); // higher pack's own block
    CHECK(ok(8.00645,47.00545)); // only excluded lower block occupied this spot
    CHECK(!ok(8.0071,47.0031)); // paved + allowed dirt still blocked
    CHECK(!ok(8.006,47.007));   // OBJ
    CHECK(!ok(8.00612,47.007)); // wider alternative OBJ export
    CHECK(!ok(8.0073,47.007));  // offset AGP anchor + child's bounds
    CHECK(!ok(8.004,47.003));   // road network
    CHECK(!ok(8.00410,47.003)); // asymmetric road extends > 6 m from center
    CHECK(!ok(8.004155,47.003)); // wider alternative road export
    CHECK(ok(8.0042,47.003));
    CHECK(!ok(8.0042,47.003,10)); // center clear, clump overlaps road
    CHECK(!ok(8.0065,47.0025)); // curve, well away from endpoint chord
    CHECK(!ok(8.0081,47.0031)); // active base
    CHECK(ok(8.0081,47.0051));  // lower base suppressed
    CHECK(ok(8.0081,47.0081));  // disabled pack ignored

    // Polygonal exclusions, including a concave boundary, must not use their bounding rectangle alone.
    Fixture exclude;
    exclude.properties.insert(exclude.properties.end(), {"sim/exclude_obj",
        "8.004/47.002/8.009/47.009;8.004,47.002,8.009,47.002,8.009,47.009,8.0075,47.009,8.0075,47.005,8.004,47.005"});
    write(pack("Airport") + tile, exclude.bytes());
    GroundPolygons reader2(root, &index, "", "", [](const std::string&) {});
    airport->draped = reader2.collect(*airport, *entry, 1000);
    mask = build_mask(*airport, settings);
    CHECK(ok(8.0051,47.0032));
    CHECK(!ok(8.006,47.007));
    CHECK(!ok(8.0051,47.0061)); // object exclusions do not remove facades

    // Compressed global DSFs use exactly the same parser. The fixture is a 7z of compressed-source.dsf above.
    auto compressed = read(fixtures + "/scenery.dsf.7z");
    CHECK(!compressed.empty());
    if (!compressed.empty()) {
        auto d = parse_dsf(compressed.data(), compressed.size(), [](const std::string&) { return true; }, {});
        CHECK(d.roads.size() == 2 && d.polygons.size() == 6 && d.objects.size() == 2);
        write(pack("Overlay") + tile, compressed);
        GroundPolygons reader3(root, &index, "", "", [](const std::string&) {});
        airport->draped = reader3.collect(*airport, *entry, 1000);
        mask = build_mask(*airport, settings);
        CHECK(!ok(8.004,47.003));
    }
    // A corrupt existing tile withholds grass, rather than silently allowing it over unknown scenery.
    write(pack("Overlay") + tile, "bad DSF");
    GroundPolygons reader4(root, &index, "", "", [](const std::string&) {});
    airport->draped = reader4.collect(*airport, *entry, 1000);
    mask = build_mask(*airport, settings);
    CHECK(!ok(8.0042,47.003));

    // If the custom mesh is disabled, the global base contributes its own autogen.
    write(pack("Airport") + tile, Fixture{}.bytes());
    write(pack("Overlay") + tile, overlay.bytes());
    write(root + "/Custom Scenery/scenery_packs.ini",
          "SCENERY_PACK Custom Scenery/Airport/\nSCENERY_PACK Custom Scenery/Overlay/\n"
          "SCENERY_PACK_DISABLED Custom Scenery/Base/\n");
    GroundPolygons reader5(root, &index, "", "", [](const std::string&) {});
    airport->draped = reader5.collect(*airport, *entry, 1000);
    mask = build_mask(*airport, settings);
    CHECK(!ok(8.0081,47.0051));
    CHECK(ok(8.0081,47.0031));
    // Explicitly disabled global packs must not be reintroduced by fallback discovery.
    std::ofstream(fs_path(root + "/Custom Scenery/scenery_packs.ini"), std::ios::app)
        << "SCENERY_PACK_DISABLED Global Scenery/X-Plane 12 Global Scenery/\n";
    GroundPolygons reader6(root, &index, "", "", [](const std::string&) {});
    airport->draped = reader6.collect(*airport, *entry, 1000);
    mask = build_mask(*airport, settings);
    CHECK(ok(8.0081,47.0051));

    // All covered cells, including interior blockers and the mask edge, participate in footprint clearance.
    PavementMask tiny;
    tiny.w = tiny.h = tiny.cw = tiny.ch = 20;
    tiny.blocked.assign(400,0); tiny.area.assign(400,1);
    tiny.blocked[10*20+12] = 1;
    CHECK(tiny.grass_ok(10,10));
    CHECK(!tiny.footprint_ok(10,10,4));
    CHECK(tiny.footprint_ok(5,5,2));
    CHECK(!tiny.footprint_ok(1,1,2));
    tiny.area[5*20+6] = 0;
    CHECK(!tiny.footprint_ok(5,5,2));
    // A 10 cm wide strip between raster centers must still block grass.
    airport->draped = {{1, {{{47.003,8.004}, {47.004,8.004}, {47.004,8.004001}, {47.003,8.004001}}}, 0}};
    mask = build_mask(*airport, settings);
    CHECK(!ok(8.004,47.0035,1));
    std::cout << failures << " failed checks\n";
    return failures ? 1 : 0;
}
