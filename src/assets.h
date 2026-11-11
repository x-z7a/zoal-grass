// Builds the grass clump objects from one atlas per season (resources/textures/zg_atlas_<style>.png) described by
// resources/grass_art.txt:
//  - BLADE: the DINKIssTyle blade colour strip (tools/bake_art.py), the colour source of the real 3D blades
//  - FLOWER: the 165 flower plants of DINKIssTyle Lawn06_S_Flower01 (stem, leaves and one of four flower heads)
//  - CARD: plant sprites modelled and rendered in Blender (tools/blender_species.py, tools/compose_atlas.py): lawn
//    clumps, meadow panicles, seed-head grass, tussocks, moss, sedge, dandelions (upright cards) and clover / rosettes
//    (flat cards)
// Each clump is a square OBJ. By default the same Blender plants grow at every distance (levels: to lod_near with all
// flowers, to lod_mid with a few, beyond with fewer, larger clumps), so nothing changes type as the camera closes in;
// real_blades adds 3D blades close up (four levels). Which plants grow comes from the biome (biome.h).
// No .for forest is placed in the sim.
#pragma once

#include "biome.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace zg {

constexpr int kLods = 4;

struct LookParams {
    double height_m = 0.25;
    double density = 1.0;
    double dry_mix = 0.15;
    double width_scale = 1.0;
    double footprint_m = 5.0;
    double lod_near = 60, lod_mid = 160, lod_far = 300;   // detailed blades end at 40 % of lod_near
    std::string texture = "zg_atlas_summer.png";   // file in resources/textures
    double flowers_per_m2 = 0.0;
    unsigned flower_mask = 0xF;
    SpeciesMix mix;
    bool real_blades = false;   // the DINKIssTyle-coloured 3D blades close up (else Blender clumps at every distance)
    bool gpu = false;
    bool shadows = false;
    bool normal_map = true;
    int variants = 4;
};

struct BuiltObjects {
    std::vector<std::string> normal;   // absolute paths
    std::vector<std::string> mowed;
    int tufts = 0, cards = 0, flowers = 0;
    std::array<int, kLods> tris{};     // per level of detail, first normal object
};

class GrassAssets {
public:
    using Log = std::function<void(const std::string&)>;

    bool load(const std::string& resources_dir, const Log& log);
    bool loaded() const { return !blades_.empty() && !lawn_.empty(); }

    // Write the clump objects (and copy the textures) into out_dir.
    bool build(const std::string& resources_dir, const std::string& out_dir, const LookParams& p, BuiltObjects& out,
               const Log& log) const;

    struct Card {
        std::string name;
        bool top = false;                  // flat on the ground (else an upright card)
        float u0, v0, u1, v1;              // left / base, right / top
        float w, h;                        // metres as rendered (side: width x height, top: width x depth)
    };

private:
    struct Strip { bool dry; float u_base, u_tip, v_start, v_end; };
    struct Vert { float x, y, z, nx, ny, nz, u, v; };
    struct Flower {
        std::vector<Vert> verts;
        std::vector<int> idx;
        int type = 0;
    };
    std::vector<Strip> blades_;           // colour source of the 3D blades
    std::vector<Flower> flowers_;
    std::vector<Card> cards_;
    // card indices per kind (into cards_)
    std::vector<int> lawn_, lawn_dry_, meadow_, seed_tame_, seed_wild_, tussock_green_, tussock_dry_, moss_, sedge_,
        dandelion_, clover_, clover_fl_, rosette_;
};

}  // namespace zg
