// Builds the grass clump objects from the DINKIssTyle grass art, baked by tools/bake_art.py into one atlas
// (resources/textures/zg_atlas_<style>.png) and a description (resources/grass_art.txt):
//  - STRIP: the dense blade strip of 3D_Grass_7, used for long, gently curving fringe cards (the grass mass)
//  - SPRITE: the four Lawn07 tuft sprites (two green, two dry), used for upright tufts of three crossed bent cards
//  - FLOWER: the 165 flower plants of Lawn06_S_Flower01 (stem, leaves and one of four flower heads)
// Each clump is a square OBJ with three levels of detail. No .for forest is placed in the sim.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace zg {

struct LookParams {
    double height_m = 0.25;
    double density = 1.0;
    double dry_mix = 0.15;
    double width_scale = 1.0;
    double footprint_m = 5.0;
    double lod_near = 60, lod_mid = 160, lod_far = 300;
    std::string texture = "zg_atlas_summer.png";   // file in resources/textures
    double flowers_per_m2 = 0.0;
    unsigned flower_mask = 0xF;
    bool gpu = false;
    bool shadows = false;
    bool normal_map = true;
    int variants = 4;
};

struct BuiltObjects {
    std::vector<std::string> normal;   // absolute paths
    std::vector<std::string> mowed;
    int strips = 0, tufts = 0, flowers = 0, tris_near = 0, tris_mid = 0, tris_far = 0;
};

class GrassAssets {
public:
    using Log = std::function<void(const std::string&)>;

    bool load(const std::string& resources_dir, const Log& log);
    bool loaded() const { return !sprites_.empty() && !strips_.empty() && !blades_.empty(); }

    // Write the clump objects (and copy the textures) into out_dir.
    bool build(const std::string& resources_dir, const std::string& out_dir, const LookParams& p, BuiltObjects& out,
               const Log& log) const;

private:
    struct Sprite { bool dry; float u_left, u_right, v_base, v_tip; };
    struct Strip { bool dry; float u_base, u_tip, v_start, v_end; };
    struct Vert { float x, y, z, nx, ny, nz, u, v; };
    struct Flower {
        std::vector<Vert> verts;
        std::vector<int> idx;
        int type = 0;
    };
    std::vector<Sprite> sprites_;
    std::vector<Strip> strips_;
    std::vector<Strip> blades_;           // colour source of the 3D blades (same layout as a strip)
    std::vector<Flower> flowers_;
};

}  // namespace zg
