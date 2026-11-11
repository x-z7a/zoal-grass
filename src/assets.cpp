#include "assets.h"

#include "platform.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <sstream>

namespace zg {

namespace {

const char* kNormal = "zg_atlas_NRM.png";
const char* kBuildVersion = "species-3";  // part of the object file names: bump when the geometry code changes
constexpr double kPi2 = 6.283185307179586;
constexpr double kUpBias = 0.45;          // blade normals lean up this much (soft lighting, no glowing flat cards)
constexpr double kCardUpBias = 0.78;      // card normals: lit almost like the ground they stand on
constexpr double kDetailShare = 0.4;      // curved blades end at this share of lod_near
constexpr double kBladeTuftsPerM2 = 27.0; // tufts of real blades per m2 at 25 cm grass
constexpr int kMaxBladeTufts = 900;
constexpr double kLawnCardsPerM2 = 2.0;   // Blender lawn clumps per m2 (mid level) at 25 cm grass
constexpr int kMaxCards = 360;            // per kind
constexpr int kMaxFlowers = 120;
constexpr double kFlowerScale = 2.3;      // flower plant scale at 25 cm grass (the plants are ~16 cm at scale 1)

bool copy_file(const std::string& from, const std::string& to) {
    long long as, am, bs, bm;
    if (!file_key(from, as, am)) return false;
    if (file_key(to, bs, bm) && as == bs && am <= bm) return true;
    std::ifstream in(fs_path(from), std::ios::binary);
    std::ofstream out(fs_path(to), std::ios::binary | std::ios::trunc);
    out << in.rdbuf();
    return bool(out);
}

void norm3(float& x, float& y, float& z) {
    float m = std::sqrt(x * x + y * y + z * z);
    if (m < 1e-9f) { x = 0; y = 1; z = 0; return; }
    x /= m; y /= m; z /= m;
}

// Accumulates OBJ8 vertices and per-LOD indices. Front faces use the DINKIssTyle winding (clockwise seen from the
// normal side); two-sided meshes add a mirrored copy with flipped normals.
struct MeshOut {
    bool two_sided = true;
    std::string vt;
    int nverts = 0;
    std::vector<int> idx[kLods];
    char line[160];

    int vert(double x, double y, double z, float nx, float ny, float nz, double u, double v) {
        std::snprintf(line, sizeof line, "VT %.4f %.4f %.4f %.3f %.3f %.3f %.5f %.5f\n", x, y, z, nx, ny, nz, u, v);
        vt += line;
        return nverts++;
    }
    // An upright card: base corners (x0,z0)-(x1,z1), top shifted by (lx,lz); horizontal normal (nx,nz) tilted up.
    void card(int L, double x0, double z0, double x1, double z1, double y0, double h, double lx, double lz,
              double nx, double nz, double u0, double u1, double vb, double vt_) {
        for (int side = 0; side < (two_sided ? 2 : 1); ++side) {
            double flip = side == 0 ? 1.0 : -1.0;
            float fx = float(flip * nx * (1 - kCardUpBias)), fz = float(flip * nz * (1 - kCardUpBias));
            float fy = float(kCardUpBias);
            norm3(fx, fy, fz);
            int b0 = vert(x0, y0, z0, fx, fy, fz, u0, vb);
            int t0 = vert(x0 + lx, y0 + h, z0 + lz, fx, fy, fz, u0, vt_);
            int b1 = vert(x1, y0, z1, fx, fy, fz, u1, vb);
            int t1 = vert(x1 + lx, y0 + h, z1 + lz, fx, fy, fz, u1, vt_);
            if (side == 0) idx[L].insert(idx[L].end(), {b0, t1, b1, b0, t0, t1});
            else idx[L].insert(idx[L].end(), {b0, b1, t1, b0, t1, t0});
        }
    }
};

}  // namespace

bool GrassAssets::load(const std::string& resources_dir, const Log& log) {
    std::string path = resources_dir + "/grass_art.txt";
    std::ifstream in(fs_path(path));
    std::string line;
    if (!in || !std::getline(in, line) || line.rfind("ZGART 3", 0) != 0) {
        log("could not read " + path + " (want ZGART 3)");
        return false;
    }
    blades_.clear();
    flowers_.clear();
    cards_.clear();
    Flower* cur = nullptr;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        if (key == "BLADE") {
            int dry;
            Strip s;
            ss >> dry >> s.u_base >> s.u_tip >> s.v_start >> s.v_end;
            s.dry = dry != 0;
            if (ss) blades_.push_back(s);
        } else if (key == "CARD") {
            Card c;
            std::string kind;
            ss >> c.name >> kind >> c.u0 >> c.v0 >> c.u1 >> c.v1 >> c.w >> c.h;
            c.top = kind == "top";
            if (ss && c.w > 0 && c.h > 0) cards_.push_back(c);
        } else if (key == "FLOWER") {
            int nv, nt, type;
            ss >> nv >> nt >> type;
            flowers_.emplace_back();
            cur = &flowers_.back();
            cur->type = std::max(0, std::min(3, type));
        } else if (key == "V" && cur) {
            Vert v;
            ss >> v.x >> v.y >> v.z >> v.nx >> v.ny >> v.nz >> v.u >> v.v;
            if (ss) cur->verts.push_back(v);
        } else if (key == "I" && cur) {
            int a, b, c;
            ss >> a >> b >> c;
            int n = int(cur->verts.size());
            if (ss && a >= 0 && b >= 0 && c >= 0 && a < n && b < n && c < n) cur->idx.insert(cur->idx.end(), {a, b, c});
        }
    }
    for (auto* v : {&lawn_, &lawn_dry_, &meadow_, &seed_tame_, &seed_wild_, &tussock_green_, &tussock_dry_, &moss_,
                    &sedge_, &dandelion_, &clover_, &clover_fl_, &rosette_})
        v->clear();
    for (int k = 0; k < int(cards_.size()); ++k) {
        const std::string& n = cards_[size_t(k)].name;
        auto starts = [&](const char* p) { return n.rfind(p, 0) == 0; };
        if (n == "lawn_dry") lawn_dry_.push_back(k);
        else if (starts("lawn_")) lawn_.push_back(k);
        else if (starts("meadow")) meadow_.push_back(k);
        else if (n == "seed_wild") seed_wild_.push_back(k);
        else if (starts("seed_")) seed_tame_.push_back(k);
        else if (n == "tussock_dry") tussock_dry_.push_back(k);
        else if (starts("tussock")) tussock_green_.push_back(k);
        else if (starts("moss")) moss_.push_back(k);
        else if (starts("sedge")) sedge_.push_back(k);
        else if (starts("dandelion")) dandelion_.push_back(k);
        else if (n == "clover_fl") clover_fl_.push_back(k);
        else if (starts("clover")) clover_.push_back(k);
        else if (starts("rosette")) rosette_.push_back(k);
    }
    if (lawn_dry_.empty()) lawn_dry_ = lawn_;
    char buf[200];
    std::snprintf(buf, sizeof buf, "grass art: %zu blade colours, %zu flower plants, %zu plant cards (%zu lawn)",
                  blades_.size(), flowers_.size(), cards_.size(), lawn_.size());
    log(buf);
    return loaded();
}

bool GrassAssets::build(const std::string& resources_dir, const std::string& out_dir, const LookParams& p,
                        BuiltObjects& out, const Log& log) const {
    if (!loaded()) return false;
    make_dir(out_dir);
    std::string texture = p.texture;
    if (!copy_file(resources_dir + "/textures/" + texture, out_dir + "/" + texture)) {
        log("could not copy texture " + texture + "; using summer");
        texture = "zg_atlas_summer.png";
        if (!copy_file(resources_dir + "/textures/" + texture, out_dir + "/" + texture)) return false;
    }
    bool normal_map = p.normal_map && copy_file(resources_dir + "/textures/" + kNormal, out_dir + "/" + kNormal);

    for (auto& n : list_dir(out_dir))                // remove objects from earlier builds
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".obj") == 0) remove_file(out_dir + "/" + n);
    // a stamp in the file names makes X-Plane load the new objects instead of reusing cached ones
    const SpeciesMix& mx = p.mix;
    char mixkey[400];
    std::snprintf(mixkey, sizeof mixkey, "%g %g %g %g %g %g %g %g %g %g %g %g %g %g", mx.blades, mx.height, mx.meadow,
                  mx.seed, mx.seed_wild, mx.tussock, mx.tussock_dry, mx.moss, mx.sedge, mx.clover, mx.clover_flowers,
                  mx.rosette, mx.dandelion, mx.flowers);
    unsigned stamp = unsigned(std::hash<std::string>()(
        std::string(kBuildVersion) + std::to_string(p.height_m) + std::to_string(p.density) + std::to_string(p.dry_mix) + std::to_string(p.width_scale) +
        std::to_string(p.footprint_m) + std::to_string(p.lod_near) + std::to_string(p.lod_mid) + std::to_string(p.lod_far) +
        texture + std::to_string(p.flowers_per_m2) + std::to_string(p.flower_mask) + mixkey + (p.gpu ? "g" : "c") +
        (p.shadows ? "s" : "n") + (normal_map ? "m" : "x"))) & 0xffffff;

    const Strip* green_blade = &blades_[0];
    const Strip* dry_blade = &blades_.back();
    for (auto& s : blades_) (s.dry ? dry_blade : green_blade) = &s;
    std::vector<const Flower*> flower_pool;
    for (auto& f : flowers_)
        if (p.flower_mask & (1u << f.type)) flower_pool.push_back(&f);

    const double lod_lo[kLods] = {0, p.lod_near * kDetailShare, p.lod_near, p.lod_mid};
    const double lod_hi[kLods] = {p.lod_near * kDetailShare, p.lod_near, p.lod_mid, p.lod_far};

    out = BuiltObjects();
    // mowed: short turf with blades, lawn clumps and the flat weeds only
    auto write_set = [&](double height, int seed, const char* kind, bool mowed, std::vector<std::string>& paths) -> bool {
        const double F = p.footprint_m;
        const double hs = height / 0.25;                       // relative to the 25 cm the densities are tuned for
        const double area = F * F;
        int nf = 0;
        if (!mowed && !flower_pool.empty())
            nf = std::min(kMaxFlowers, int(std::lround(area * p.flowers_per_m2 * mx.flowers)));

        for (int var = 0; var < p.variants; ++var) {
            std::mt19937 rng(unsigned(seed + var * 7919));
            std::uniform_real_distribution<double> U(0.0, 1.0);
            auto pick = [&](const std::vector<int>& pool) -> const Card& {
                return cards_[size_t(pool[size_t(U(rng) * double(pool.size())) % pool.size()])];
            };
            auto count = [&](double per_m2) {                  // a random rounding keeps small densities alive
                double n = area * per_m2 * p.density;
                return std::min(kMaxCards, int(n) + (U(rng) < n - std::floor(n) ? 1 : 0));
            };
            MeshOut m;
            m.two_sided = !p.gpu;

            // ---- upright card: random yaw, a little lean; `planes` 2 = crossed (plants that are round from above)
            auto side_card = [&](int L, double cx, double cz, double yaw, double sw, double sh, double lean_dir,
                                 double lean, const Card& c, int planes, bool mirror) {
                double w = c.w * sw, h = c.h * sh;
                double lx = std::cos(lean_dir) * lean * h, lz = std::sin(lean_dir) * lean * h;
                for (int k = 0; k < planes; ++k) {
                    double a = yaw + k * kPi2 / 4;
                    double ux = std::cos(a), uz = std::sin(a);
                    double u0 = mirror ? c.u1 : c.u0, u1 = mirror ? c.u0 : c.u1;
                    m.card(L, cx - ux * w / 2, cz - uz * w / 2, cx + ux * w / 2, cz + uz * w / 2, -0.02, h, lx, lz,
                           -uz, ux, u0, u1, c.v0, c.v1);
                }
            };
            // ---- flat card lying on the ground, facing up (one-sided: never seen from below)
            auto top_card = [&](int L, double cx, double cz, double yaw, double s, double y, const Card& c) {
                double hw = c.w * s / 2, hd = c.h * s / 2;
                double cs = std::cos(yaw), sn = std::sin(yaw);
                int base = m.nverts;
                const double lx[4] = {-hw, hw, hw, -hw}, lz[4] = {-hd, -hd, hd, hd};
                const double us[4] = {c.u0, c.u1, c.u1, c.u0}, vs[4] = {c.v1, c.v1, c.v0, c.v0};
                for (int k = 0; k < 4; ++k)
                    m.vert(cx + lx[k] * cs - lz[k] * sn, y, cz + lx[k] * sn + lz[k] * cs, 0.f, 1.f, 0.f, us[k], vs[k]);
                m.idx[L].insert(m.idx[L].end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            };

            auto flower = [&](int L, double cx, double cz, double yaw, double s, const Flower& f) {
                double c = std::cos(yaw), sn = std::sin(yaw);
                for (int side = 0; side < (p.gpu ? 1 : 2); ++side) {
                    int base = m.nverts;
                    double flip = side == 0 ? 1.0 : -1.0;
                    for (const Vert& v : f.verts) {
                        double px = v.x * s, pz = v.z * s;
                        float nx = float(flip * (v.nx * c + v.nz * sn) * (1 - kUpBias));
                        float nz = float(flip * (-v.nx * sn + v.nz * c) * (1 - kUpBias));
                        float ny = float(flip * v.ny * (1 - kUpBias) + kUpBias);
                        norm3(nx, ny, nz);
                        m.vert(cx + px * c + pz * sn, v.y * s - 0.01, cz - px * sn + pz * c, nx, ny, nz, v.u, v.v);
                    }
                    for (size_t q = 0; q + 2 < f.idx.size(); q += 3) {
                        if (side == 0) m.idx[L].insert(m.idx[L].end(), {base + f.idx[q], base + f.idx[q + 1], base + f.idx[q + 2]});
                        else m.idx[L].insert(m.idx[L].end(), {base + f.idx[q], base + f.idx[q + 2], base + f.idx[q + 1]});
                    }
                }
            };

            // ---- tufts of real blades: each blade a tapered, leaning, drooping strip of three triangles (or one
            // straight triangle) coloured by a thin slice of the DINKIssTyle blade strip (dark base, streaky green)
            auto blade_tuft = [&](int L, double cx, double cz, double h0, bool dry, double widen, int blades_cap,
                                  double curved_share) {
                const Strip& st = dry ? *dry_blade : *green_blade;
                double u_tip = st.u_base + (st.u_tip - st.u_base) * 0.82;   // stay inside the opaque part of the strip
                int nb = std::min(blades_cap, 5 + int(U(rng) * 5));
                double tuft_lean = (U(rng) - 0.5) * 0.3;                     // the whole tuft leans a little
                double tl_dir = U(rng) * kPi2;
                for (int b = 0; b < nb; ++b) {
                    double ra = U(rng) * kPi2, rr = U(rng) * 0.03;
                    double bx = cx + std::cos(ra) * rr, bz = cz + std::sin(ra) * rr;
                    double phi = U(rng) * kPi2;                              // the way the blade leans
                    double dx = std::cos(phi), dz = std::sin(phi);
                    double h = h0 * (0.55 + 0.7 * U(rng));
                    double lean = 0.12 + 0.45 * U(rng), droop = 0.1 + 0.5 * U(rng) * U(rng);
                    double w = (0.010 + 0.010 * U(rng)) * std::sqrt(std::max(0.4, hs)) * p.width_scale * widen;
                    double sx = -dz, sz = dx;                                // blade width axis
                    double tw = (U(rng) - 0.5) * 0.6;                        // a little twist
                    double tx = sx * std::cos(tw) + dx * std::sin(tw), tz = sz * std::cos(tw) + dz * std::sin(tw);
                    auto at = [&](double t, double& x, double& y, double& z) {
                        double o = lean * h * t + droop * h * t * t;
                        x = bx + dx * o + std::cos(tl_dir) * tuft_lean * h * t;
                        z = bz + dz * o + std::sin(tl_dir) * tuft_lean * h * t;
                        y = h * t * (1.0 - 0.35 * droop * t) - 0.01;
                    };
                    double x0, y0, z0, x1, y1, z1, x2, y2, z2;
                    at(0, x0, y0, z0);
                    at(0.55, x1, y1, z1);
                    at(1, x2, y2, z2);
                    double v = st.v_start + (st.v_end - st.v_start) * (0.05 + 0.9 * U(rng));
                    double dv = 0.001;
                    double um = st.u_base + (u_tip - st.u_base) * 0.55;
                    bool simple = U(rng) >= curved_share;
                    for (int side = 0; side < (p.gpu ? 1 : 2); ++side) {
                        double flip = side == 0 ? 1.0 : -1.0;
                        // face normal ~ the lean direction, tilted up
                        float nx = float(flip * dx * (1 - kUpBias)), nz = float(flip * dz * (1 - kUpBias)), ny = float(kUpBias);
                        norm3(nx, ny, nz);
                        int a0 = m.vert(x0 - tx * w, y0, z0 - tz * w, nx, ny, nz, st.u_base, v - dv);
                        int a1 = m.vert(x0 + tx * w, y0, z0 + tz * w, nx, ny, nz, st.u_base, v + dv);
                        if (simple) {
                            int c0 = m.vert((x1 + x2) / 2, (y1 + y2) / 2 * 0.95, (z1 + z2) / 2, nx, ny, nz, u_tip, v);
                            if (side == 0) m.idx[L].insert(m.idx[L].end(), {a0, c0, a1});
                            else m.idx[L].insert(m.idx[L].end(), {a0, a1, c0});
                            continue;
                        }
                        int b0 = m.vert(x1 - tx * w * 0.75, y1, z1 - tz * w * 0.75, nx, ny, nz, um, v - dv);
                        int b1 = m.vert(x1 + tx * w * 0.75, y1, z1 + tz * w * 0.75, nx, ny, nz, um, v + dv);
                        int c0 = m.vert(x2, y2, z2, nx, ny, nz, u_tip, v);
                        if (side == 0) m.idx[L].insert(m.idx[L].end(), {a0, b1, a1, a0, b0, b1, b0, c0, b1});
                        else m.idx[L].insert(m.idx[L].end(), {a0, a1, b1, a0, b1, b0, b0, b1, c0});
                    }
                }
            };

            // ---- evenly spread points (jittered grid, shuffled so every prefix is spread too)
            auto spread = [&](int n) {
                std::vector<std::pair<double, double>> pts;
                int g = std::max(1, int(std::ceil(std::sqrt(double(n)))));
                double cell = F / g;
                std::vector<int> slots(static_cast<size_t>(g * g));
                std::iota(slots.begin(), slots.end(), 0);
                std::shuffle(slots.begin(), slots.end(), rng);
                for (int k = 0; k < n; ++k) {
                    int sl = slots[size_t(k)];
                    pts.push_back({((sl % g) + U(rng)) * cell - F / 2, ((sl / g) + U(rng)) * cell - F / 2});
                }
                return pts;
            };
            // ---- patchy points: plants of a kind grow in drifts, different in every clump variant
            auto patchy = [&](int n, double spread_m) {
                std::vector<std::pair<double, double>> pts;
                int patches = 1 + int(U(rng) * 3);
                std::vector<std::pair<double, double>> centres;
                for (int c = 0; c < patches; ++c) centres.push_back({(U(rng) - 0.5) * F, (U(rng) - 0.5) * F});
                std::normal_distribution<double> N(0.0, spread_m);
                for (int k = 0; k < n; ++k) {
                    double x, z;
                    if (U(rng) < 0.3) {
                        x = (U(rng) - 0.5) * F;
                        z = (U(rng) - 0.5) * F;
                    } else {
                        auto& c = centres[size_t(k) % centres.size()];
                        x = std::max(-F / 2, std::min(F / 2, c.first + N(rng)));
                        z = std::max(-F / 2, std::min(F / 2, c.second + N(rng)));
                    }
                    pts.push_back({x, z});
                }
                return pts;
            };

            // ---- blades: level 0 every tuft, half of them curved; level 1 a little over half, straight and wider
            const double bh = height;
            int nbt = int(std::lround(area * kBladeTuftsPerM2 * p.density * mx.blades / std::max(0.4, hs)));
            nbt = std::max(mx.blades > 0.05 ? 12 : 0, std::min(kMaxBladeTufts, nbt));
            {
                auto pts = spread(nbt);
                std::vector<double> hts(static_cast<size_t>(nbt));
                std::vector<bool> dry(static_cast<size_t>(nbt));
                for (int k = 0; k < nbt; ++k) {
                    hts[size_t(k)] = bh * (0.8 + 0.4 * U(rng));
                    dry[size_t(k)] = U(rng) < p.dry_mix;
                }
                struct BL { double share, widen; int cap; double curved; };
                const BL bl[2] = {{1.0, 1.0, 99, 0.5}, {0.55, 1.5, 7, 0.0}};
                for (int L = 0; L < 2; ++L) {
                    int n = int(nbt * bl[L].share);
                    for (int k = 0; k < n; ++k)
                        blade_tuft(L, pts[size_t(k)].first, pts[size_t(k)].second, hts[size_t(k)], dry[size_t(k)],
                                   bl[L].widen, bl[L].cap, bl[L].curved);
                }
            }

            // ---- lawn clumps (Blender cards): mixed into the blades close up, carry the lawn beyond 40 m
            {
                int n = std::min(kMaxCards, int(std::lround(area * kLawnCardsPerM2 * p.density * mx.blades)));
                auto pts = spread(n);
                const std::vector<int>& dom = lawn_;
                int dominant = dom[size_t(var) % dom.size()];        // each variant leans to one colour: patchy fields
                struct LC { double yaw, sw, sh, ld, lean; int card; bool mirror; };
                std::vector<LC> lcs;
                for (int k = 0; k < n; ++k) {
                    int ci = U(rng) < p.dry_mix ? lawn_dry_[size_t(U(rng) * double(lawn_dry_.size())) % lawn_dry_.size()]
                             : U(rng) < 0.55 ? dominant
                                             : dom[size_t(U(rng) * double(dom.size())) % dom.size()];
                    double s = hs * (0.8 + 0.4 * U(rng));
                    lcs.push_back({U(rng) * kPi2, s * p.width_scale * (0.75 + 0.5 * U(rng)), s, U(rng) * kPi2,
                                   (U(rng) - 0.5) * 0.25, ci, U(rng) < 0.5});
                }
                // the same clumps close up as at 16 .. 40 m: zooming in must not change which plants grow
                const double share[kLods] = {0.6, 0.6, 1.0, 0.5};
                const double grow[kLods] = {1.0, 1.0, 1.0, 1.3};      // the far level: fewer, larger clumps
                for (int L = 0; L < kLods; ++L) {
                    int nl = int(n * share[L]);
                    for (int k = 0; k < nl; ++k) {
                        const LC& c = lcs[size_t(k)];
                        side_card(L, pts[size_t(k)].first, pts[size_t(k)].second, c.yaw, c.sw * grow[L],
                                  c.sh * (L == 3 ? 1.1 : 1.0), c.ld, c.lean, cards_[size_t(c.card)], L < 3 ? 2 : 1,
                                  c.mirror);   // crossed: they cover the ground seen from above too
                    }
                }
            }

            // ---- species cards
            struct Kind {
                double per_m2;
                const std::vector<int>* pool;
                const std::vector<int>* alt;    // drawn with probability alt_share
                double alt_share;
                double scale_lo, scale_hi;      // times sqrt(grass height / 25 cm)
                int planes_near;                // crossed planes at levels 0 and 1
                double share[kLods];
                double patch_m;
            };
            const double ss = std::sqrt(hs);                     // hs already carries the biome height
            std::vector<Kind> kinds;
            if (!mowed) {
                kinds.push_back({mx.meadow, &meadow_, nullptr, 0, 0.8, 1.15, 1, {1, 1, 0.7, 0.35}, 0.9});
                kinds.push_back({mx.seed, &seed_tame_, &seed_wild_, mx.seed_wild, 0.8, 1.15, 1, {1, 1, 0.8, 0.5}, 0.8});
                kinds.push_back({mx.tussock, &tussock_green_, &tussock_dry_, mx.tussock_dry, 0.75, 1.25, 2, {1, 1, 1, 0.6}, 1.2});
                kinds.push_back({mx.moss, &moss_, nullptr, 0, 0.7, 1.3, 2, {1, 1, 0.6, 0}, 0.7});
                kinds.push_back({mx.sedge, &sedge_, nullptr, 0, 0.8, 1.2, 2, {1, 1, 0.6, 0.3}, 0.6});
                kinds.push_back({mx.dandelion, &dandelion_, nullptr, 0, 0.8, 1.2, 1, {1, 1, 0.3, 0}, 0.5});
            }
            const double clover_k = mowed ? 0.8 : 1.0, rosette_k = mowed ? 1.2 : 1.0;
            kinds.push_back({mx.clover * clover_k, &clover_, &clover_fl_, mx.clover_flowers, 0.8, 1.3, 0, {1, 1, 0.4, 0}, 0.4});
            kinds.push_back({mx.rosette * rosette_k, &rosette_, nullptr, 0, 0.7, 1.3, 0, {1, 1, 0, 0}, 0.8});
            int species_cards = 0;
            for (const Kind& kd : kinds) {
                if (kd.pool->empty() || kd.per_m2 <= 0) continue;
                int n = count(kd.per_m2);
                auto pts = patchy(n, kd.patch_m);
                species_cards += n;
                struct SC { double yaw, s, ld, lean; const Card* c; bool mirror; };
                std::vector<SC> scs;
                for (int k = 0; k < n; ++k) {
                    const Card& c = (kd.alt && !kd.alt->empty() && U(rng) < kd.alt_share) ? pick(*kd.alt) : pick(*kd.pool);
                    double s = (c.top ? 1.0 : ss) * (kd.scale_lo + (kd.scale_hi - kd.scale_lo) * U(rng));
                    scs.push_back({U(rng) * kPi2, s, U(rng) * kPi2, (U(rng) - 0.5) * 0.2, &c, U(rng) < 0.5});
                }
                for (int L = 0; L < kLods; ++L) {
                    int nl = int(n * kd.share[L]);
                    for (int k = 0; k < nl; ++k) {
                        const SC& c = scs[size_t(k)];
                        double x = pts[size_t(k)].first, z = pts[size_t(k)].second;
                        if (c.c->top) top_card(L, x, z, c.yaw, c.s, 0.025 + 0.004 * (k % 4), *c.c);
                        else side_card(L, x, z, c.yaw, c.s, c.s, c.ld, c.lean, *c.c, L < 2 ? std::max(1, kd.planes_near) : 1,
                                       c.mirror);
                    }
                }
            }

            // ---- DINKIssTyle flower plants, in small patches of mostly one kind
            if (nf > 0) {
                struct FP { double x, z, yaw, s; const Flower* f; };
                std::vector<FP> fps;
                int patches = std::max(1, nf / 6);
                std::vector<std::pair<double, double>> centres;
                std::vector<int> kinds_f;
                for (int c = 0; c < patches; ++c) {
                    centres.push_back({(U(rng) - 0.5) * F * 0.9, (U(rng) - 0.5) * F * 0.9});
                    kinds_f.push_back(flower_pool[size_t(U(rng) * double(flower_pool.size())) % flower_pool.size()]->type);
                }
                for (int k = 0; k < nf; ++k) {
                    int pc = k % patches;
                    int want = U(rng) < 0.8 ? kinds_f[size_t(pc)]
                                            : flower_pool[size_t(U(rng) * double(flower_pool.size())) % flower_pool.size()]->type;
                    const Flower* fl = flower_pool[0];
                    for (int tries = 0; tries < 16; ++tries) {
                        const Flower* cand = flower_pool[size_t(U(rng) * double(flower_pool.size())) % flower_pool.size()];
                        fl = cand;
                        if (cand->type == want) break;
                    }
                    double ang = U(rng) * kPi2, rr = std::sqrt(U(rng)) * 0.6;
                    fps.push_back({std::max(-F / 2, std::min(F / 2, centres[size_t(pc)].first + std::cos(ang) * rr)),
                                   std::max(-F / 2, std::min(F / 2, centres[size_t(pc)].second + std::sin(ang) * rr)),
                                   U(rng) * kPi2, kFlowerScale * hs * (0.8 + 0.4 * U(rng)), fl});
                }
                const double share[kLods] = {1.0, 1.0, 0.15, 0.0};   // dots beyond 40 m: a few suffice
                for (int L = 0; L < kLods; ++L)
                    for (int k = 0; k < int(nf * share[L]); ++k)
                        flower(L, fps[size_t(k)].x, fps[size_t(k)].z, fps[size_t(k)].yaw, fps[size_t(k)].s, *fps[size_t(k)].f);
            }

            size_t total = 0;
            for (auto& v : m.idx) total += v.size();
            char name[96];
            std::snprintf(name, sizeof name, "zg_%06x_%s_%d.obj", stamp, kind, var + 1);
            std::string path = out_dir + "/" + name;
            std::ofstream f(fs_path(path), std::ios::trunc | std::ios::binary);   // binary: \n line ends everywhere
            if (!f) {
                log("could not write " + path);
                return false;
            }
            f << "A\n800\nOBJ\n\n";
            f << "TEXTURE " << texture << "\n";
            if (normal_map) f << "TEXTURE_NORMAL " << kNormal << "\n";
            f << "GLOBAL_no_blend 0.5\nGLOBAL_specular 0.0\n";
            if (!p.shadows) f << "GLOBAL_no_shadow\n";
            f << "\nPOINT_COUNTS " << m.nverts << " 0 0 " << total << "\n\n";
            f << m.vt << "\n";
            std::vector<int> all;
            all.reserve(total);
            for (auto& v : m.idx) all.insert(all.end(), v.begin(), v.end());
            size_t k = 0;
            for (; k + 10 <= all.size(); k += 10) {
                f << "IDX10";
                for (size_t q = 0; q < 10; ++q) f << ' ' << all[k + q];
                f << '\n';
            }
            for (; k < all.size(); ++k) f << "IDX " << all[k] << '\n';
            f << '\n';
            size_t offset = 0;
            for (int L = 0; L < kLods; ++L) {
                f << "ATTR_LOD " << int(lod_lo[L]) << ' ' << int(lod_hi[L]) << '\n';
                if (p.gpu) f << "ATTR_no_cull\n";
                if (!m.idx[L].empty()) f << "TRIS " << offset << ' ' << m.idx[L].size() << '\n';
                offset += m.idx[L].size();
            }
            f.close();
            if (!f) {
                log("could not write " + path);
                return false;
            }
            paths.push_back(path);
            if (!mowed && var == 0) {
                out.tufts = nbt;
                out.cards = species_cards;
                out.flowers = nf;
                for (int L = 0; L < kLods; ++L) out.tris[size_t(L)] = int(m.idx[L].size() / 3);
            }
        }
        return true;
    };
    if (!write_set(p.height_m * mx.height, 12, "n", false, out.normal)) return false;
    if (!write_set(p.height_m * 0.5 * std::min(1.0, mx.height), 112, "m", true, out.mowed)) return false;
    return true;
}

}  // namespace zg
