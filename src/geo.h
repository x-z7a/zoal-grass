// Geometry helpers shared by the apt.dat parser, the DSF reader and the pavement mask.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace zg {

constexpr double kPi = 3.14159265358979323846;
inline double deg2rad(double d) { return d * kPi / 180.0; }

struct LatLon {
    double lat = 0, lon = 0;
    bool operator==(const LatLon& o) const { return lat == o.lat && lon == o.lon; }
    bool operator!=(const LatLon& o) const { return !(*this == o); }
};
struct XY {
    double x = 0, y = 0;
};

using Ring = std::vector<LatLon>;   // closed ring, first point not repeated
using XYRing = std::vector<XY>;

// Tangent-plane approximation around (lat0, lon0): x east, y north, metres.
struct LocalProjection {
    double lat0 = 0, lon0 = 0, kx = 1, ky = 1;
    LocalProjection() = default;
    LocalProjection(double lat, double lon) : lat0(lat), lon0(lon) {
        double phi = deg2rad(lat);
        ky = 111132.92 - 559.82 * std::cos(2 * phi) + 1.175 * std::cos(4 * phi) - 0.0023 * std::cos(6 * phi);
        kx = 111412.84 * std::cos(phi) - 93.5 * std::cos(3 * phi) + 0.118 * std::cos(5 * phi);
    }
    XY to_xy(double lat, double lon) const {
        double dlon = std::fmod(lon - lon0 + 540.0, 360.0) - 180.0;
        return {dlon * kx, (lat - lat0) * ky};
    }
    XY to_xy(const LatLon& p) const { return to_xy(p.lat, p.lon); }
    LatLon to_latlon(double x, double y) const {
        double lon = lon0 + x / kx;
        if (lon > 180.0) lon -= 360.0;
        else if (lon < -180.0) lon += 360.0;
        return {lat0 + y / ky, lon};
    }
};

inline double approx_metres(const LatLon& a, const LatLon& b) {
    const double KY = 111132.0;
    double kx = KY * std::cos(deg2rad((a.lat + b.lat) / 2));
    double dlon = std::fmod(b.lon - a.lon + 540.0, 360.0) - 180.0;
    return std::hypot((b.lat - a.lat) * KY, dlon * kx);
}

inline double flat_metres(double lat, double lon, double lat2, double lon2) {
    return std::hypot((lat2 - lat) * 111132.0, (lon2 - lon) * 111320.0 * std::cos(deg2rad(lat)));
}

// Points strictly between p0 and p3 on a cubic Bezier, spaced roughly seg_len metres apart.
inline void cubic_interior(const LatLon& p0, const LatLon& p1, const LatLon& p2, const LatLon& p3,
                           std::vector<LatLon>& out, double seg_len = 3.0) {
    double length = approx_metres(p0, p1) + approx_metres(p1, p2) + approx_metres(p2, p3);
    int n = std::max(3, std::min(40, int(length / seg_len) + 1));
    for (int k = 1; k < n; ++k) {
        double t = double(k) / n, m = 1.0 - t;
        double a = m * m * m, b = 3 * m * m * t, c = 3 * m * t * t, d = t * t * t;
        out.push_back({a * p0.lat + b * p1.lat + c * p2.lat + d * p3.lat,
                       a * p0.lon + b * p1.lon + c * p2.lon + d * p3.lon});
    }
}

struct Bounds {
    double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
    void add(double x, double y) {
        minx = std::min(minx, x); miny = std::min(miny, y);
        maxx = std::max(maxx, x); maxy = std::max(maxy, y);
    }
    void add(const Bounds& b) { add(b.minx, b.miny); add(b.maxx, b.maxy); }
    bool valid() const { return minx <= maxx; }
};

inline Bounds loops_bounds(const std::vector<XYRing>& loops) {
    Bounds b;
    for (auto& l : loops)
        for (auto& p : l) b.add(p.x, p.y);
    return b;
}

// Raw Bezier node row: position plus optional outgoing control point.
struct BezNode {
    double lat, lon;
    bool curved;
    double clat, clon;
};

// Join colocated rows that encode one "split" Bezier node (as WED writes them) and flatten edges.
struct MergedNode {
    LatLon p;
    bool has_lo, has_hi;
    LatLon lo, hi;
};

inline std::vector<MergedNode> merge_split_nodes(const std::vector<BezNode>& nodes) {
    std::vector<MergedNode> out;
    size_t n = nodes.size(), i = 0;
    while (i < n) {
        const BezNode& a = nodes[i];
        MergedNode m;
        m.p = {a.lat, a.lon};
        m.has_lo = a.curved;
        if (a.curved) m.lo = {2 * a.lat - a.clat, 2 * a.lon - a.clon};
        bool prev_curved = a.curved;
        size_t j = i;
        while (j + 1 < n) {
            const BezNode& nx = nodes[j + 1];
            if (nx.lat == a.lat && nx.lon == a.lon && (prev_curved || nx.curved) && !(prev_curved && nx.curved)) {
                ++j;
                prev_curved = nx.curved;
            } else {
                break;
            }
        }
        const BezNode& last = nodes[j];
        m.has_hi = last.curved;
        if (last.curved) m.hi = {last.clat, last.clon};
        out.push_back(m);
        i = j + 1;
    }
    return out;
}

// Closed ring (closed=true) or open chain of points from node rows, Bezier edges flattened.
inline std::vector<LatLon> flatten_nodes(const std::vector<BezNode>& nodes, bool closed, double seg_len = 3.0) {
    auto merged = merge_split_nodes(nodes);
    size_t n = merged.size();
    std::vector<LatLon> pts;
    if (n == 0) return pts;
    size_t edges = closed ? n : n - 1;
    for (size_t k = 0; k < edges; ++k) {
        const MergedNode& a = merged[k];
        const MergedNode& b = merged[(k + 1) % n];
        pts.push_back(a.p);
        if (a.has_hi || b.has_lo)
            cubic_interior(a.p, a.has_hi ? a.hi : a.p, b.has_lo ? b.lo : b.p, b.p, pts, seg_len);
    }
    if (!closed) pts.push_back(merged.back().p);
    std::vector<LatLon> dedup;
    dedup.reserve(pts.size());
    for (size_t k = 0; k < pts.size(); ++k) {
        if (closed) {
            const LatLon& prev = pts[(k + pts.size() - 1) % pts.size()];
            if (pts[k] == prev && pts.size() > 1) continue;
        } else if (k > 0 && pts[k] == pts[k - 1]) {
            continue;
        }
        dedup.push_back(pts[k]);
    }
    return dedup;
}

// Pitch and roll (degrees) that stand an object with the given heading upright on terrain with unit normal
// (nx, ny, nz) in local OpenGL coordinates. X-Plane orients an object as Ry(-heading) Rx(pitch) Rz(-roll), so its up
// axis becomes (sin roll, cos roll cos pitch, cos roll sin pitch) in the heading frame.
inline void terrain_tilt(double nx, double ny, double nz, double heading_deg, double& pitch_deg, double& roll_deg) {
    double h = heading_deg * 3.14159265358979 / 180.0;
    double hx = nx * std::cos(h) + nz * std::sin(h), hz = -nx * std::sin(h) + nz * std::cos(h);
    double m = std::sqrt(hx * hx + ny * ny + hz * hz);
    if (m < 1e-9 || ny <= 0) {
        pitch_deg = roll_deg = 0;
        return;
    }
    hx /= m;
    double hy = ny / m;
    hz /= m;
    roll_deg = std::asin(std::fmax(-1.0, std::fmin(1.0, hx))) * 180.0 / 3.14159265358979;
    pitch_deg = std::atan2(hz, hy) * 180.0 / 3.14159265358979;
}

}  // namespace zg
