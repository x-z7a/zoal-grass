// Grass sway in the wind. The clump objects (assets.cpp, wind_sway on) tilt their plant cards in groups about the
// group's foot on the ground, driven by per-instance datarefs; this computes those values from the sim's wind.
#pragma once

#include <cmath>

namespace zg {

constexpr int kSwayGroups = 4;   // phase groups per clump; each has a tilt about the clump's X and Z axes
constexpr const char* kSwayDatarefs[2 * kSwayGroups + 1] = {
    "zoal/grass/sway_x0", "zoal/grass/sway_x1", "zoal/grass/sway_x2", "zoal/grass/sway_x3",
    "zoal/grass/sway_z0", "zoal/grass/sway_z1", "zoal/grass/sway_z2", "zoal/grass/sway_z3", nullptr};

// Tilt angles (degrees) for one clump: out[g] = rotation about the clump's Z axis, out[kSwayGroups + g] about its X
// axis, for group g. wind_x / wind_z: the wind (air motion) in local OpenGL metres per second (x east, z south);
// t: seconds; (x, z): where the clump is; heading: its heading in degrees; max_deg: the lean in a strong wind; seed:
// a per-clump number in [0, 1). The grass leans downwind, plus gusts that travel across the field.
inline void sway_values(double wind_x, double wind_z, double t, double x, double z, double heading, double max_deg,
                        double seed, float out[2 * kSwayGroups]) {
    double speed = std::hypot(wind_x, wind_z);
    if (speed < 0.3 || max_deg <= 0) {
        for (int k = 0; k < 2 * kSwayGroups; ++k) out[k] = 0.0f;
        return;
    }
    double dx = wind_x / speed, dz = wind_z / speed;
    double strength = std::pow(std::fmin(1.0, speed / 14.0), 0.8);   // full lean from ~27 kt
    double lean = max_deg * strength * 0.55, gust = max_deg * strength * 0.45;
    double along = x * dx + z * dz;                                     // gust waves run downwind
    double wave_speed = 2.0 + 0.6 * speed;                              // m/s
    // world -> clump frame (heading turns the clump clockwise seen from above)
    double h = heading * 3.14159265358979 / 180.0;
    double lx = dx * std::cos(h) + dz * std::sin(h), lz = -dx * std::sin(h) + dz * std::cos(h);
    for (int g = 0; g < kSwayGroups; ++g) {
        double phase = (along - wave_speed * t) / 9.0 + 0.23 * g + seed;
        double flutter = std::sin(t * (2.7 + 0.4 * g) + 6.283 * seed + 1.7 * g);
        double tilt = lean + gust * (0.6 * std::sin(6.283 * phase) + 0.4 * std::sin(6.283 * 2.3 * phase + 1.1)) +
                      0.12 * max_deg * strength * flutter;
        // rotating +y about Z by a tilts it toward -x; about X by b tilts it toward +z
        out[g] = float(-tilt * lx);
        out[kSwayGroups + g] = float(tilt * lz);
    }
}

}  // namespace zg
