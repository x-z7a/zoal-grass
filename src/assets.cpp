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
const char* kBuildVersion = "blades-3";   // part of the object file names: bump when the geometry code changes
constexpr double kPi2 = 6.283185307179586;
constexpr double kUpBias = 0.45;          // vertex normals lean up this much (soft lighting, no glowing flat cards)
constexpr double kStripRowSpacing = 0.30; // metres between fringe rows at 25 cm grass (scales with height)
constexpr double kTuftsPerM2 = 5.0;       // upright tufts per m2 at 25 cm grass
constexpr int kMaxStrips = 260, kMaxTufts = 420, kMaxFlowers = 120;
constexpr double kFlowerScale = 2.3;
constexpr double kBladeTuftsPerM2 = 27.0; // near level: tufts of real blades per m2 at 25 cm grass
constexpr int kMaxBladeTufts = 900;      // flower plant scale at 25 cm grass (the plants are ~16 cm at scale 1)

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

using P3 = std::array<double, 3>;
using N2 = std::array<double, 2>;

// Accumulates OBJ8 vertices and per-LOD indices.
struct MeshOut {
    bool two_sided = true;
    std::string vt;
    int nverts = 0;
    std::vector<int> idx[3];
    char line[160];

    int vert(double x, double y, double z, float nx, float ny, float nz, double u, double v) {
        std::snprintf(line, sizeof line, "VT %.4f %.4f %.4f %.3f %.3f %.3f %.5f %.5f\n", x, y, z, nx, ny, nz, u, v);
        vt += line;
        return nverts++;
    }
    // Quads between a bottom and a top row of points. Front faces use the DINKIssTyle winding (clockwise seen from
    // the normal side); the back is a mirrored copy with flipped normals.
    void ribbon(int L, const std::vector<P3>& bottom, const std::vector<P3>& top, const std::vector<double>& ub,
                const std::vector<double>& ut, const std::vector<double>& vb, const std::vector<double>& vt_,
                const std::vector<N2>& normals) {
        size_t n = bottom.size();
        for (int side = 0; side < (two_sided ? 2 : 1); ++side) {
            double flip = side == 0 ? 1.0 : -1.0;
            int base = nverts;
            for (size_t k = 0; k < n; ++k) {
                float nx = float(flip * normals[k][0] * (1 - kUpBias)), nz = float(flip * normals[k][1] * (1 - kUpBias));
                float ny = float(kUpBias);
                norm3(nx, ny, nz);
                vert(bottom[k][0], bottom[k][1], bottom[k][2], nx, ny, nz, ub[k], vb[k]);
                vert(top[k][0], top[k][1], top[k][2], nx, ny, nz, ut[k], vt_[k]);
            }
            for (size_t k = 0; k + 1 < n; ++k) {
                int b0 = base + int(2 * k), t0 = b0 + 1, b1 = b0 + 2, t1 = b0 + 3;
                if (side == 0) idx[L].insert(idx[L].end(), {b0, t1, b1, b0, t0, t1});
                else idx[L].insert(idx[L].end(), {b0, b1, t1, b0, t1, t0});
            }
        }
    }
};

}  // namespace

bool GrassAssets::load(const std::string& resources_dir, const Log& log) {
    std::string path = resources_dir + "/grass_art.txt";
    std::ifstream in(fs_path(path));
    std::string line;
    if (!in || !std::getline(in, line) || line.rfind("ZGART 2", 0) != 0) {
        log("could not read " + path);
        return false;
    }
    sprites_.clear();
    strips_.clear();
    blades_.clear();
    flowers_.clear();
    Flower* cur = nullptr;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string key;
        if (!(ss >> key) || key[0] == '#') continue;
        if (key == "SPRITE") {
            int dry;
            Sprite s;
            ss >> dry >> s.u_left >> s.u_right >> s.v_base >> s.v_tip;
            s.dry = dry != 0;
            if (ss) sprites_.push_back(s);
        } else if (key == "BLADE") {
            int dry;
            Strip s;
            ss >> dry >> s.u_base >> s.u_tip >> s.v_start >> s.v_end;
            s.dry = dry != 0;
            if (ss) blades_.push_back(s);
        } else if (key == "STRIP") {
            int dry;
            Strip s;
            ss >> dry >> s.u_base >> s.u_tip >> s.v_start >> s.v_end;
            s.dry = dry != 0;
            if (ss) strips_.push_back(s);
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
    char buf[200];
    std::snprintf(buf, sizeof buf, "grass art: %zu tuft sprites, %zu blade strips, %zu blade colours, %zu flower plants",
                  sprites_.size(), strips_.size(), blades_.size(), flowers_.size());
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
    unsigned stamp = unsigned(std::hash<std::string>()(
        std::string(kBuildVersion) + std::to_string(p.height_m) + std::to_string(p.density) + std::to_string(p.dry_mix) + std::to_string(p.width_scale) +
        std::to_string(p.footprint_m) + std::to_string(p.lod_near) + std::to_string(p.lod_mid) + std::to_string(p.lod_far) +
        texture + std::to_string(p.flowers_per_m2) + std::to_string(p.flower_mask) + (p.gpu ? "g" : "c") +
        (p.shadows ? "s" : "n") + (normal_map ? "m" : "x"))) & 0xffffff;

    std::vector<const Sprite*> green_sprites, dry_sprites;
    for (auto& s : sprites_) (s.dry ? dry_sprites : green_sprites).push_back(&s);
    if (green_sprites.empty()) green_sprites = dry_sprites;
    if (dry_sprites.empty()) dry_sprites = green_sprites;
    const Strip* green_strip = &strips_[0];
    const Strip* dry_strip = &strips_.back();
    for (auto& s : strips_) (s.dry ? dry_strip : green_strip) = &s;
    const Strip* green_blade = &blades_[0];
    const Strip* dry_blade = &blades_.back();
    for (auto& s : blades_) (s.dry ? dry_blade : green_blade) = &s;
    std::vector<const Flower*> flower_pool;
    for (auto& f : flowers_)
        if (p.flower_mask & (1u << f.type)) flower_pool.push_back(&f);

    out = BuiltObjects();
    auto write_set = [&](double height, int seed, const char* kind, bool with_flowers,
                         std::vector<std::string>& paths) -> bool {
        const double F = p.footprint_m;
        const double hs = height / 0.25;                       // relative to the 25 cm the densities are tuned for
        const double row = kStripRowSpacing * std::max(0.5, std::min(1.6, hs)) / std::sqrt(p.density);
        const double seg_len = std::max(0.12, height * 1.4 * p.width_scale);
        const int segs = 3;
        int nstrips = int(std::lround(F * F / row / (segs * seg_len)));
        nstrips = std::max(6, std::min(kMaxStrips, nstrips));
        int ntufts = int(std::lround(F * F * kTuftsPerM2 * p.density / std::max(0.4, hs * hs)));
        ntufts = std::max(6, std::min(kMaxTufts, ntufts));
        int nf = 0;
        if (with_flowers && !flower_pool.empty())
            nf = std::min(kMaxFlowers, int(std::lround(F * F * p.flowers_per_m2)));

        for (int var = 0; var < p.variants; ++var) {
            std::mt19937 rng(unsigned(seed + var * 7919));
            std::uniform_real_distribution<double> U(0.0, 1.0);
            MeshOut m;
            m.two_sided = !p.gpu;

            // ---- fringe strips: three segments, gently curving and leaning, blades along the card height
            auto strip = [&](int L, double cx, double cz, double dir, double h, bool dry, double curve, double lean) {
                const Strip& st = dry ? *dry_strip : *green_strip;
                std::vector<P3> bot, top;
                std::vector<double> ub, ut, vb, vt;
                std::vector<N2> nrm;
                double a = dir - curve * segs / 2;
                double x = cx - std::cos(dir) * seg_len * segs / 2, z = cz - std::sin(dir) * seg_len * segs / 2;
                for (int k = 0; k <= segs; ++k) {
                    double nx = -std::sin(a), nz = std::cos(a);
                    double hk = h * (0.9 + 0.2 * U(rng));
                    bot.push_back({x, -0.01, z});
                    top.push_back({x + nx * lean * hk, hk * (1 - 0.15 * std::fabs(lean)), z + nz * lean * hk});
                    ub.push_back(st.u_base);
                    ut.push_back(st.u_tip);
                    double v = (k % 2 == 0) ? st.v_start : st.v_end;   // mirror every other segment: seamless joints
                    vb.push_back(v);
                    vt.push_back(v);
                    nrm.push_back({nx, nz});
                    if (k < segs) {
                        a += curve;
                        x += std::cos(a) * seg_len;
                        z += std::sin(a) * seg_len;
                    }
                }
                m.ribbon(L, bot, top, ub, ut, vb, vt, nrm);
            };

            // ---- upright tufts: crossed cards, narrow at the base, each bending outward above mid height
            auto tuft = [&](int L, double cx, double cz, double yaw, double h, const Sprite& sp, int planes, bool bent) {
                double w = h * 1.05 * p.width_scale;
                for (int c = 0; c < planes; ++c) {
                    double a = yaw + 3.14159265 * c / planes + (U(rng) - 0.5) * 0.3;
                    double ux = std::cos(a), uz = std::sin(a), nx = -uz, nz = ux;
                    double lean = (U(rng) - 0.5) * 0.5 * h;
                    bool mirror = U(rng) < 0.5;
                    double ul = mirror ? sp.u_right : sp.u_left, ur = mirror ? sp.u_left : sp.u_right;
                    std::vector<P3> bot, mid, top;
                    std::vector<double> us, vb, vm, vt;
                    std::vector<N2> nrm;
                    for (int e = 0; e < 2; ++e) {
                        double sgn = e == 0 ? -0.5 : 0.5;
                        bot.push_back({cx + ux * w * sgn, -0.01, cz + uz * w * sgn});
                        mid.push_back({cx + ux * w * sgn + nx * lean * 0.3, h * 0.55, cz + uz * w * sgn + nz * lean * 0.3});
                        top.push_back({cx + ux * w * 1.1 * sgn + nx * lean, h, cz + uz * w * 1.1 * sgn + nz * lean});
                        us.push_back(e == 0 ? ul : ur);
                        vb.push_back(sp.v_base);
                        vm.push_back(sp.v_base + (sp.v_tip - sp.v_base) * 0.55);
                        vt.push_back(sp.v_tip);
                        nrm.push_back({nx, nz});
                    }
                    // the sprite itself is narrow at the base, so the card can stay rectangular
                    if (bent) {
                        m.ribbon(L, bot, mid, us, us, vb, vm, nrm);
                        m.ribbon(L, mid, top, us, us, vm, vt, nrm);
                    } else {
                        m.ribbon(L, bot, top, us, us, vb, vt, nrm);
                    }
                }
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

            // placements are drawn once; each level takes a prefix, so mid and far are subsets of near
            struct SP { double x, z, dir, h, curve, lean; bool dry; };
            std::vector<SP> sps(static_cast<size_t>(nstrips));
            for (auto& s : sps) {
                s.x = (U(rng) - 0.5) * F;
                s.z = (U(rng) - 0.5) * F;
                s.dir = U(rng) * kPi2;
                s.h = height * (0.75 + 0.5 * U(rng));
                s.curve = (U(rng) - 0.5) * 0.7;
                s.lean = (U(rng) - 0.5) * 0.6;
                s.dry = U(rng) < p.dry_mix;
            }
            struct TP { double x, z, yaw, h; const Sprite* sp; };
            std::vector<TP> tps(static_cast<size_t>(ntufts));
            for (auto& t : tps) {
                t.x = (U(rng) - 0.5) * F;
                t.z = (U(rng) - 0.5) * F;
                t.yaw = U(rng) * kPi2;
                t.h = height * (0.85 + 0.6 * U(rng));
                const auto& pool = U(rng) < p.dry_mix ? dry_sprites : green_sprites;
                t.sp = pool[size_t(U(rng) * double(pool.size())) % pool.size()];
            }
            struct FP { double x, z, yaw, s; const Flower* f; };
            std::vector<FP> fps;
            if (nf > 0) {
                // flowers grow in small patches, mostly one kind per patch
                int patches = std::max(1, nf / 6);
                std::vector<std::pair<double, double>> centres;
                std::vector<int> kinds;
                for (int c = 0; c < patches; ++c) {
                    centres.push_back({(U(rng) - 0.5) * F * 0.9, (U(rng) - 0.5) * F * 0.9});
                    kinds.push_back(flower_pool[size_t(U(rng) * double(flower_pool.size())) % flower_pool.size()]->type);
                }
                for (int k = 0; k < nf; ++k) {
                    int pc = k % patches;
                    int want = U(rng) < 0.8 ? kinds[size_t(pc)]
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
            }

            // ---- near level: tufts of real blades. Each blade is a tapered, leaning, drooping strip of three
            // triangles coloured by a thin slice of the DINKIssTyle blade strip (dark base, streaky green/olive).
            // L = level of detail; widen > 1 and all-simple blades for the distant levels (same look, fewer triangles)
            auto blade_tuft = [&](int L, double cx, double cz, double h0, bool dry, double widen, int blades_cap) {
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
                        double out = lean * h * t + droop * h * t * t;
                        x = bx + dx * out + std::cos(tl_dir) * tuft_lean * h * t;
                        z = bz + dz * out + std::sin(tl_dir) * tuft_lean * h * t;
                        y = h * t * (1.0 - 0.35 * droop * t) - 0.01;
                    };
                    double x0, y0, z0, x1, y1, z1, x2, y2, z2;
                    at(0, x0, y0, z0);
                    at(0.55, x1, y1, z1);
                    at(1, x2, y2, z2);
                    double v = st.v_start + (st.v_end - st.v_start) * (0.05 + 0.9 * U(rng));
                    double dv = 0.002;
                    double um = st.u_base + (u_tip - st.u_base) * 0.55;
                    bool simple = L > 0 || U(rng) < 0.5;                    // half the near blades: one straight triangle
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
            int nbt = int(std::lround(F * F * kBladeTuftsPerM2 * p.density / std::max(0.4, hs)));
            nbt = std::max(20, std::min(kMaxBladeTufts, nbt));
            {
                struct BT { double x, z, h; bool dry; };
                std::vector<BT> bts;
                int g = int(std::ceil(std::sqrt(double(nbt))));
                double cell = F / g;
                std::vector<int> slots(static_cast<size_t>(g * g));
                std::iota(slots.begin(), slots.end(), 0);
                std::shuffle(slots.begin(), slots.end(), rng);
                for (int k = 0; k < nbt; ++k) {
                    int sl = slots[size_t(k)];
                    bts.push_back({((sl % g) + U(rng)) * cell - F / 2, ((sl / g) + U(rng)) * cell - F / 2,
                                   height * (0.8 + 0.4 * U(rng)), U(rng) < p.dry_mix});
                }
                // near: every tuft; mid: 50 % with wider single-triangle blades; far: 22 %, wider still
                struct BL { double share, widen; int cap; };
                const BL bl[3] = {{1.0, 1.0, 99}, {0.5, 2.0, 6}, {0.22, 3.6, 5}};
                for (int L = 0; L < 3; ++L) {
                    int n = int(nbt * bl[L].share);
                    for (int k = 0; k < n; ++k)
                        blade_tuft(L, bts[size_t(k)].x, bts[size_t(k)].z, bts[size_t(k)].h * (L == 2 ? 1.1 : 1.0),
                                   bts[size_t(k)].dry, bl[L].widen, bl[L].cap);
                }
            }

            // blades at every distance (cards and strips looked like dark bricks in X-Plane); only flowers are added here
            struct Level { double strips, tufts, flowers; bool bent; int planes; double lo, hi; };
            Level levels[3] = {{0.0, 0.0, 1.0, true, 3, 0, p.lod_near},
                               {0.0, 0.0, 0.5, false, 2, p.lod_near, p.lod_mid},
                               {0.0, 0.0, 0.0, false, 2, p.lod_mid, p.lod_far}};
            for (int L = 0; L < 3; ++L) {
                int ns = int(nstrips * levels[L].strips), nt = int(ntufts * levels[L].tufts);
                int nfl = int(nf * levels[L].flowers);
                for (int k = 0; k < ns; ++k) {
                    const SP& s = sps[size_t(k)];
                    strip(L, s.x, s.z, s.dir, s.h * (L == 2 ? 1.15 : 1.0), s.dry, s.curve, s.lean);
                }
                for (int k = 0; k < nt; ++k) {
                    const TP& t = tps[size_t(k)];
                    tuft(L, t.x, t.z, t.yaw, t.h * (L == 2 ? 1.2 : 1.0), *t.sp, levels[L].planes, levels[L].bent);
                }
                for (int k = 0; k < nfl; ++k) {
                    const FP& q = fps[size_t(k)];
                    flower(L, q.x, q.z, q.yaw, q.s, *q.f);
                }
            }

            size_t total = m.idx[0].size() + m.idx[1].size() + m.idx[2].size();
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
            for (int L = 0; L < 3; ++L) {
                f << "ATTR_LOD " << int(levels[L].lo) << ' ' << int(levels[L].hi) << '\n';
                if (p.gpu) f << "ATTR_no_cull\n";
                f << "TRIS " << offset << ' ' << m.idx[L].size() << '\n';
                offset += m.idx[L].size();
            }
            f.close();
            if (!f) {
                log("could not write " + path);
                return false;
            }
            paths.push_back(path);
            if (std::strcmp(kind, "n") == 0 && var == 0) {
                out.strips = nstrips;
                out.tufts = nbt;
                out.flowers = nf;
                out.tris_near = int(m.idx[0].size() / 3);
                out.tris_mid = int(m.idx[1].size() / 3);
                out.tris_far = int(m.idx[2].size() / 3);
            }
        }
        return true;
    };
    if (!write_set(p.height_m, 12, "n", true, out.normal)) return false;
    if (!write_set(p.height_m * 0.5, 112, "m", false, out.mowed)) return false;   // mown turf: no flowers
    return true;
}

}  // namespace zg
