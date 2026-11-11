#include "mask.h"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace zg {

namespace {

struct Element {
    int order, layer;
    SurfaceClass cls;
    std::vector<XYRing> loops;
    bool core;
};

SurfaceClass operational_class(int code) {
    SurfaceClass c = surface_class(code);
    return c == TURF ? MOWED : c;
}

XYRing rect(double cx, double cy, double ux, double uy, double half_len, double half_wid) {
    double vx = -uy, vy = ux;
    return {{cx - ux * half_len - vx * half_wid, cy - uy * half_len - vy * half_wid},
            {cx + ux * half_len - vx * half_wid, cy + uy * half_len - vy * half_wid},
            {cx + ux * half_len + vx * half_wid, cy + uy * half_len + vy * half_wid},
            {cx - ux * half_len + vx * half_wid, cy - uy * half_len + vy * half_wid}};
}

// Even-odd scanline fill of loops into grid rows, sampling cell centres.
void fill(std::vector<uint8_t>& grid, int w, int h, double x0, double y0, double cell,
          const std::vector<XYRing>& loops, uint8_t value) {
    struct Edge { double ya, yb, xa, slope; };
    std::vector<Edge> edges;
    for (auto& loop : loops) {
        if (loop.size() < 3) continue;
        XY a = loop.back();
        for (const XY& b : loop) {
            if (a.y < b.y) edges.push_back({a.y, b.y, a.x, (b.x - a.x) / (b.y - a.y)});
            else if (b.y < a.y) edges.push_back({b.y, a.y, b.x, (a.x - b.x) / (a.y - b.y)});
            a = b;
        }
    }
    if (edges.empty()) return;
    std::sort(edges.begin(), edges.end(), [](const Edge& p, const Edge& q) { return p.ya < q.ya; });
    double ymax = -1e300;
    for (auto& e : edges) ymax = std::max(ymax, e.yb);
    int j0 = std::max(0, int(std::ceil((edges[0].ya - y0) / cell - 0.5)));
    int j1 = std::min(h - 1, int(std::floor((ymax - y0) / cell - 0.5)));
    std::vector<const Edge*> active;
    std::vector<double> xs;
    size_t nxt = 0;
    for (int j = j0; j <= j1; ++j) {
        double yc = y0 + (j + 0.5) * cell;
        while (nxt < edges.size() && edges[nxt].ya <= yc) active.push_back(&edges[nxt++]);
        active.erase(std::remove_if(active.begin(), active.end(), [yc](const Edge* e) { return e->yb <= yc; }),
                     active.end());
        if (active.size() < 2) continue;
        xs.clear();
        for (auto* e : active) xs.push_back(e->xa + (yc - e->ya) * e->slope);
        std::sort(xs.begin(), xs.end());
        uint8_t* row = &grid[size_t(j) * size_t(w)];
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int i0 = std::max(0, int(std::ceil((xs[k] - x0) / cell - 0.5)));
            int i1 = std::min(w, int(std::ceil((xs[k + 1] - x0) / cell - 0.5)));
            if (i1 > i0) std::fill(row + i0, row + i1, value);
        }
    }
}

// Grow non-zero cells by a disc of `radius` cells (offsets with dx^2 + dy^2 <= r^2 + r).
std::vector<uint8_t> dilate(const std::vector<uint8_t>& src, int w, int h, double radius) {
    if (radius <= 0) return src;
    double limit = radius * radius + radius;
    int rows_up = int(std::sqrt(limit) + 1e-9);
    std::vector<int> reach(size_t(rows_up) + 1);
    for (int dy = 0; dy <= rows_up; ++dy) reach[size_t(dy)] = int(std::sqrt(limit - double(dy) * dy) + 1e-9);
    // horizontal dilation for each distinct reach via prefix sums per row
    std::vector<int> distinct(reach.begin(), reach.end());
    std::sort(distinct.begin(), distinct.end());
    distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
    std::vector<std::vector<uint8_t>> hor(distinct.size(), std::vector<uint8_t>(src.size()));
    std::vector<int> prefix(size_t(w) + 1);
    for (int j = 0; j < h; ++j) {
        const uint8_t* row = &src[size_t(j) * size_t(w)];
        prefix[0] = 0;
        for (int i = 0; i < w; ++i) prefix[size_t(i) + 1] = prefix[size_t(i)] + (row[i] ? 1 : 0);
        if (prefix[size_t(w)] == 0) continue;
        for (size_t d = 0; d < distinct.size(); ++d) {
            int r = distinct[d];
            uint8_t* out = &hor[d][size_t(j) * size_t(w)];
            for (int i = 0; i < w; ++i) {
                int a = std::max(0, i - r), b = std::min(w, i + r + 1);
                out[i] = prefix[size_t(b)] - prefix[size_t(a)] > 0;
            }
        }
    }
    auto idx = [&](int r) { return size_t(std::lower_bound(distinct.begin(), distinct.end(), r) - distinct.begin()); };
    std::vector<uint8_t> out(src.size(), 0);
    for (int j = 0; j < h; ++j) {
        uint8_t* o = &out[size_t(j) * size_t(w)];
        for (int dy = -rows_up; dy <= rows_up; ++dy) {
            int jj = j + dy;
            if (jj < 0 || jj >= h) continue;
            const uint8_t* s = &hor[idx(reach[size_t(std::abs(dy))])][size_t(jj) * size_t(w)];
            for (int i = 0; i < w; ++i) o[i] |= s[i];
        }
    }
    return out;
}

bool near_anchor(const Bounds& b, const Bounds& anchor, double gap) {
    double dx = std::max({b.minx - anchor.maxx, anchor.minx - b.maxx, 0.0});
    double dy = std::max({b.miny - anchor.maxy, anchor.miny - b.maxy, 0.0});
    return std::hypot(dx, dy) <= gap;
}

std::vector<XYRing> project(const std::vector<Ring>& loops, const LocalProjection& proj) {
    std::vector<XYRing> out;
    out.reserve(loops.size());
    for (auto& l : loops) {
        XYRing r;
        r.reserve(l.size());
        for (auto& p : l) r.push_back(proj.to_xy(p));
        out.push_back(std::move(r));
    }
    return out;
}

}  // namespace

SurfaceClass surface_class(int code) {
    if (code == 3) return TURF;
    if (code == 4) return DIRT;
    if (code == 5) return GRAVEL;
    return PAVED;
}

std::unique_ptr<PavementMask> build_mask(const Airport& ap, const MaskSettings& s) {
    LatLon center;
    if (!ap.center(center)) return nullptr;
    LocalProjection proj(center.lat, center.lon);

    std::vector<Element> polys;
    for (auto& r : ap.runways) {
        XY p1 = proj.to_xy(r.ends[0]), p2 = proj.to_xy(r.ends[1]);
        double length = std::hypot(p2.x - p1.x, p2.y - p1.y);
        if (length < 1.0) continue;
        double ux = (p2.x - p1.x) / length, uy = (p2.y - p1.y) / length;
        double o1 = std::max(0.0, r.overrun[0]), o2 = std::max(0.0, r.overrun[1]);
        double cx = (p1.x + p2.x) / 2 + ux * (o2 - o1) / 2, cy = (p1.y + p2.y) / 2 + uy * (o2 - o1) / 2;
        polys.push_back({r.order, 0, operational_class(r.surface),
                         {rect(cx, cy, ux, uy, length / 2 + (o1 + o2) / 2, r.width / 2)}, true});
        if (r.shoulder > 0) {
            int surface = r.shoulder % 100, metres = r.shoulder / 100;
            if (surface != 0) {
                double sw = metres ? double(metres) : std::min(5.0, std::max(3.0, r.width * 0.12));
                double mx = (p1.x + p2.x) / 2, my = (p1.y + p2.y) / 2;
                polys.push_back({r.order, 1, operational_class(surface),
                                 {rect(mx, my, ux, uy, length / 2, r.width / 2 + sw)}, true});
            }
        }
    }
    for (auto& hp : ap.helipads) {
        XY c = proj.to_xy(hp.lat, hp.lon);
        double a = deg2rad(hp.heading);
        double ux = std::sin(a), uy = std::cos(a);
        polys.push_back({hp.order, 0, operational_class(hp.surface), {rect(c.x, c.y, ux, uy, hp.length / 2, hp.width / 2)}, true});
        if (hp.shoulder > 0)
            polys.push_back({hp.order, 1, PAVED, {rect(c.x, c.y, ux, uy, hp.length / 2 + 1.5, hp.width / 2 + 1.5)}, true});
    }
    for (auto& p : ap.pavements) polys.push_back({p.order, 0, surface_class(p.surface), project(p.loops, proj), false});
    std::vector<std::vector<XYRing>> boundary;
    for (auto& b : ap.boundaries) boundary.push_back(project(b, proj));
    if (polys.empty()) return nullptr;

    Bounds anchor;
    for (auto& p : polys)
        if (p.core) anchor.add(loops_bounds(p.loops));
    if (!anchor.valid())
        for (auto& p : polys) anchor.add(loops_bounds(p.loops));
    polys.erase(std::remove_if(polys.begin(), polys.end(),
                               [&](const Element& e) { return !near_anchor(loops_bounds(e.loops), anchor, kMaxGapM); }),
                polys.end());
    boundary.erase(std::remove_if(boundary.begin(), boundary.end(),
                                  [&](const std::vector<XYRing>& b) { return !near_anchor(loops_bounds(b), anchor, kMaxGapM); }),
                   boundary.end());
    Bounds all;
    for (auto& p : polys) all.add(loops_bounds(p.loops));
    for (auto& b : boundary) all.add(loops_bounds(b));
    if (!all.valid()) return nullptr;
    double pad = std::max(s.margin_m, boundary.empty() ? s.fallback_margin_m : s.boundary_margin_m) + 4 * s.cell_m;
    if (s.open_pad_m > 0) pad = std::max(pad, s.open_pad_m + 4 * s.cell_m);
    double minx = all.minx - pad, miny = all.miny - pad, maxx = all.maxx + pad, maxy = all.maxy + pad;

    double cell = s.cell_m;
    if ((maxx - minx) * (maxy - miny) / (cell * cell) > s.max_cells)
        cell = std::sqrt((maxx - minx) * (maxy - miny) / s.max_cells);
    int f = std::max(1, int(std::lround(s.area_cell_m / cell)));
    int w = int(std::ceil((maxx - minx) / cell / f)) * f;
    int h = int(std::ceil((maxy - miny) / cell / f)) * f;

    std::vector<uint8_t> grid(size_t(w) * size_t(h), 0);
    // draped DSF polygons go on top of everything (order 0, painted last), clipped to the grid
    for (auto& p : ap.draped) polys.push_back({0, 0, surface_class(p.surface), project(p.loops, proj), false});
    std::stable_sort(polys.begin(), polys.end(), [](const Element& a, const Element& b) {
        return std::tie(a.order, a.layer) > std::tie(b.order, b.layer);
    });
    for (auto& p : polys) fill(grid, w, h, minx, miny, cell, p.loops, p.cls);

    uint8_t table[256] = {0};
    table[PAVED] = 1;
    table[DIRT] = s.grass_on_dirt ? 0 : 1;
    table[GRAVEL] = s.grass_on_gravel ? 0 : 1;
    table[MOWED] = s.grass_on_turf_runways ? 0 : 1;
    std::vector<uint8_t> blocked(grid.size());
    for (size_t k = 0; k < grid.size(); ++k) blocked[k] = table[grid[k]];

    auto mask = std::make_unique<PavementMask>();
    if (s.grass_on_turf_runways) {
        mask->mowed.resize(grid.size());
        for (size_t k = 0; k < grid.size(); ++k) mask->mowed[k] = grid[k] == MOWED;
    }

    int cw = w / f, ch = h / f;
    std::vector<uint8_t> area(size_t(cw) * size_t(ch), 0);
    if (s.open_pad_m > 0) {
        std::fill(area.begin(), area.end(), 1);
    } else {
        double acell = cell * f;
        int radius;
        if (!boundary.empty()) {
            for (auto& b : boundary) fill(area, cw, ch, minx, miny, acell, b, 1);
            radius = int(std::ceil(s.boundary_margin_m / acell));
        } else {
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i)
                    if (grid[size_t(j) * size_t(w) + size_t(i)]) area[size_t(j / f) * size_t(cw) + size_t(i / f)] = 1;
            radius = int(std::ceil(s.fallback_margin_m / acell));
        }
        area = dilate(area, cw, ch, radius);
    }

    mask->proj = proj;
    mask->x0 = minx;
    mask->y0 = miny;
    mask->cell = cell;
    mask->inv = 1.0 / cell;
    mask->w = w;
    mask->h = h;
    mask->f = f;
    mask->cw = cw;
    mask->ch = ch;
    mask->blocked = dilate(blocked, w, h, s.margin_m / cell);
    mask->area = std::move(area);
    return mask;
}

}  // namespace zg
