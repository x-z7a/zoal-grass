// Offline check of the X-Plane-free parts: index, airport, draped polygons, mask, clump objects.
#include <chrono>
#include <cstdio>
#include <fstream>
#include "apt.h"
#include "assets.h"
#include "biome.h"
#include "dsf.h"
#include "mask.h"
#include "osm.h"
#include "style.h"
using namespace zg;
int main(int argc, char** argv) {
    if (argc < 5) { std::printf("usage: offline <xp_root> <ident> <out_dir> <resources> [osm]\n"); return 2; }
    auto log = [](const std::string& m) { std::printf("  log: %s\n", m.c_str()); };
    auto t0 = std::chrono::steady_clock::now();
    AirportIndex idx;
    idx.build(argv[1], std::string(argv[3]) + "/index.txt", log);
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("index: %zu airports, %zu files, %.2f s\n", idx.entries.size(), idx.files.size(), s);
    const AirportEntry* e = idx.find(argv[2]);
    if (!e) { std::printf("no %s\n", argv[2]); return 1; }
    std::printf("%s in %s r=%.0f\n", e->ident.c_str(), idx.files[e->file].c_str(), e->radius);
    auto ap = idx.load(*e);
    if (!ap) { std::printf("load failed\n"); return 1; }
    std::printf("%s: %zu runways %zu pavements %zu boundaries\n", ap->name.c_str(), ap->runways.size(), ap->pavements.size(), ap->boundaries.size());
    GroundPolygons gp(argv[1], &idx, "", "", log);
    t0 = std::chrono::steady_clock::now();
    ap->draped = gp.collect(*ap, *e, 450);
    s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("draped: %zu (%.2f s)\n", ap->draped.size(), s);
    if (argc > 5 && std::string(argv[5]) == "osm") {   // OpenStreetMap roads (network: sends the airport's box only)
        double bs = 90, bw = 180, bn = -90, be = -180;
        ap->for_each_point([&](const LatLon& q) {
            bs = std::min(bs, q.lat); bn = std::max(bn, q.lat); bw = std::min(bw, q.lon); be = std::max(be, q.lon);
        });
        LocalProjection pj((bs + bn) / 2, (bw + be) / 2);
        double dlat = 233.5 / pj.ky, dlon = 233.5 / pj.kx;
        t0 = std::chrono::steady_clock::now();
        OsmFetch of = osm_fetch("https://overpass-api.de/api/interpreter", std::string(argv[3]) + "/osm_cache", e->ident,
                                bs - dlat, bw - dlon, bn + dlat, be + dlon, 30, true);
        s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (of.ok) {
            OsmStats os;
            auto hard = osm_hard_ground(of.xml, &os);
            std::printf("osm: %s in %.2f s, %zu bytes: %d ways, %d roads/paths/rail, %d areas, %d ignored\n",
                        of.cached ? "cache" : "download", s, of.xml.size(), os.ways, os.bands, os.areas, os.skipped);
            ap->draped.insert(ap->draped.end(), hard.begin(), hard.end());
        } else {
            std::printf("osm: failed: %s\n", of.error.c_str());
        }
    }
    MaskSettings ms; ms.cell_m = 1.0; ms.margin_m = 3.5; ms.open_pad_m = 203.5;
    t0 = std::chrono::steady_clock::now();
    auto m = build_mask(*ap, ms);
    s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!m) { std::printf("no mask\n"); return 1; }
    size_t blocked = 0; for (auto b : m->blocked) blocked += b;
    std::printf("mask %dx%d cell %.2f, blocked %.1f%% (%.2f s)\n", m->w, m->h, m->cell, 100.0 * blocked / m->blocked.size(), s);
    // downsampled PGM of the mask
    int step = std::max(1, std::max(m->w, m->h) / 1400);
    int W = m->w / step, H = m->h / step;
    std::ofstream f(std::string(argv[3]) + "/mask.pgm", std::ios::binary);
    f << "P5\n" << W << " " << H << "\n255\n";
    for (int j = H - 1; j >= 0; --j) for (int i = 0; i < W; ++i) {
        double x = m->x0 + (i * step + 0.5) * m->cell, y = m->y0 + (j * step + 0.5) * m->cell;
        f.put(m->grass_ok(x, y) ? (char)90 : (char)230);
    }
    GrassAssets a;
    if (!a.load(argv[4], log)) return 1;
    ClimateMap climate;
    climate.load(std::string(argv[4]) + "/climate_kg.bin");
    int koppen = climate.koppen(e->lat, e->lon), biome = biome_for_koppen(koppen, e->lat);
    std::printf("%s climate: Koppen %s, %s plants\n", e->ident.c_str(), koppen_symbol(koppen), kBiomes[biome].name);
    for (const Style& st : kStyles) {
        LookParams p; p.footprint_m = 4.5; p.lod_far = 390; p.lod_near = 40; p.lod_mid = 140;
        p.mix = species_mix(biome, st.name);
        p.height_m = 0.25 * st.height_factor; p.dry_mix = st.dry_mix >= 0 ? st.dry_mix : 0.15;
        p.texture = st.texture;
        BuiltObjects bo;
        if (!a.build(argv[4], std::string(argv[3]) + "/objs_" + st.name, p, bo, log)) return 1;
        std::printf("%-12s objects %zu+%zu, tufts %d cards %d, tris %d/%d/%d/%d\n", st.name, bo.normal.size(),
                    bo.mowed.size(), bo.tufts, bo.cards, bo.tris[0], bo.tris[1], bo.tris[2], bo.tris[3]);
    }
    const char* when[] = {"Jan 15", "Apr 15", "Jul 1", "Sep 1", "Oct 20", "Dec 20"};
    int days[] = {14, 104, 181, 243, 292, 353};
    for (int k = 0; k < 6; ++k)
        std::printf("%s: KBTV(44N) %s  Faro(37N) %s  Sydney(34S) %s  St Barth(18N) %s\n", when[k], season_for(days[k], 44.5),
                    season_for(days[k], 37.0), season_for(days[k], -33.9), season_for(days[k], 17.9));
}
