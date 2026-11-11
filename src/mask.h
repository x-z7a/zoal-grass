// Rasterise an airport's pavement into a grid, so "is there grass here?" is an array lookup.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "apt.h"
#include "geo.h"

namespace zg {

enum SurfaceClass : uint8_t { NONE = 0, PAVED = 1, DIRT = 2, GRAVEL = 3, TURF = 4, MOWED = 5 };

SurfaceClass surface_class(int code);
constexpr double kMaxGapM = 3000.0;   // elements further than this from the runways are stray data

struct MaskSettings {
    double cell_m = 1.0;
    double margin_m = 3.5;
    double boundary_margin_m = 20.0;
    double fallback_margin_m = 120.0;
    bool grass_on_dirt = false, grass_on_gravel = false, grass_on_turf_runways = false;
    double max_cells = 16e6;
    double area_cell_m = 8.0;
    double open_pad_m = 0.0;
};

class PavementMask {
public:
    LocalProjection proj;
    double x0 = 0, y0 = 0, cell = 1, inv = 1;
    int w = 0, h = 0, f = 1, cw = 0, ch = 0;
    std::vector<uint8_t> blocked;   // w*h
    std::vector<uint8_t> area;      // cw*ch
    std::vector<uint8_t> mowed;     // w*h or empty

    bool grass_ok(double x, double y) const {
        double fx = (x - x0) * inv, fy = (y - y0) * inv;
        if (fx < 0 || fy < 0) return false;
        int i = int(fx), j = int(fy);
        if (i >= w || j >= h || blocked[size_t(j) * size_t(w) + size_t(i)]) return false;
        return area[size_t(j / f) * size_t(cw) + size_t(i / f)] == 1;
    }
    bool is_mowed(double x, double y) const {
        if (mowed.empty()) return false;
        double fx = (x - x0) * inv, fy = (y - y0) * inv;
        if (fx < 0 || fy < 0) return false;
        int i = int(fx), j = int(fy);
        return i < w && j < h && mowed[size_t(j) * size_t(w) + size_t(i)];
    }
};

std::unique_ptr<PavementMask> build_mask(const Airport& ap, const MaskSettings& s);

}  // namespace zg
