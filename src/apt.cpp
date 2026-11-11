#include "apt.h"

#include "platform.h"


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace zg {

namespace {

// Split a line into whitespace separated tokens (views into the line).
struct Tok {
    const char* p;
    size_t n;
    std::string str() const { return std::string(p, n); }
    bool is(const char* s) const { return std::strlen(s) == n && std::memcmp(p, s, n) == 0; }
    double num(bool& ok) const {
        char buf[64];
        size_t k = std::min(n, sizeof buf - 1);
        std::memcpy(buf, p, k);
        buf[k] = 0;
        char* end;
        double d = std::strtod(buf, &end);
        if (end == buf) ok = false;
        return d;
    }
};

size_t tokenize(const char* a, const char* b, Tok* out, size_t max) {
    size_t n = 0;
    while (a < b && n < max) {
        while (a < b && (*a == ' ' || *a == '\t' || *a == '\r')) ++a;
        if (a >= b) break;
        const char* s = a;
        while (a < b && *a != ' ' && *a != '\t' && *a != '\r') ++a;
        out[n++] = {s, size_t(a - s)};
    }
    return n;
}

bool all_digits(const Tok& t, bool allow_dot_sign) {
    if (t.n == 0) return false;
    for (size_t i = 0; i < t.n; ++i) {
        char c = t.p[i];
        if (c >= '0' && c <= '9') continue;
        if (allow_dot_sign && (c == '.' || (c == '-' && i == 0))) continue;
        return false;
    }
    return true;
}

bool is_header(const Tok* t, size_t n) {
    if (n < 5) return false;
    if (!(t[0].is("1") || t[0].is("16") || t[0].is("17"))) return false;
    return all_digits(t[1], true) && all_digits(t[2], false) && all_digits(t[3], false);
}

class Parser {
public:
    std::unique_ptr<Airport> ap;

    bool feed(const char* a, const char* b) {
        Tok t[32];
        size_t n = tokenize(a, b, t, 32);
        if (n == 0) return true;
        const Tok& code = t[0];
        int node_kind = node_row(code);
        if (node_kind >= 0) {
            if (kind_ != 0) {
                bool ok = true;
                if (n < 3) return true;
                BezNode node{t[1].num(ok), t[2].num(ok), false, 0, 0};
                if (node_kind & 1) {
                    if (n < 5) return true;
                    node.curved = true;
                    node.clat = t[3].num(ok);
                    node.clon = t[4].num(ok);
                }
                if (!ok) return true;
                nodes_.push_back(node);
                if (code.is("113") || code.is("114") || code.is("115") || code.is("116")) close_loop();
            }
            return true;
        }
        if (code.p[0] == '#') return true;
        if (code.is("1") || code.is("16") || code.is("17")) {
            if (ap) return false;
            ap.reset(new Airport());
            bool ok = true;
            ap->kind = std::atoi(code.str().c_str());
            if (n >= 5) {
                ap->elevation_ft = t[1].num(ok);
                ap->ident = t[4].str();
                std::string name;
                for (size_t i = 5; i < n; ++i) {
                    if (!name.empty()) name += ' ';
                    name += t[i].str();
                }
                ap->name = name;
            } else {
                ap->ident = "?";
            }
            return true;
        }
        if (code.is("99")) return false;
        if (!ap) return true;
        flush();
        row(t, n);
        return true;
    }

    std::unique_ptr<Airport> finish() {
        if (ap) flush();
        return std::move(ap);
    }

private:
    int kind_ = 0;          // open polygon header: 110, 120 or 130
    int surface_ = 0;
    int order_ = 0;
    std::vector<Ring> loops_;
    std::vector<BezNode> nodes_;
    bool have_dlat_ = false, have_dlon_ = false;
    double dlat_ = 0, dlon_ = 0;

    static int node_row(const Tok& c) {
        // returns bit0 = has control point; -1 if not a node row
        if (c.n != 3 || c.p[0] != '1' || c.p[1] != '1') return -1;
        switch (c.p[2]) {
            case '1': case '3': case '5': return 0;
            case '2': case '4': case '6': return 1;
            default: return -1;
        }
    }

    void close_loop() {
        if ((kind_ == 110 || kind_ == 130) && nodes_.size() >= 3) {
            Ring ring = flatten_nodes(nodes_, true);
            if (ring.size() >= 3) loops_.push_back(std::move(ring));
        }
        nodes_.clear();
    }

    void flush() {
        if (kind_ == 0) return;
        close_loop();
        if (!loops_.empty()) {
            if (kind_ == 110) ap->pavements.push_back({surface_, std::move(loops_), order_});
            else if (kind_ == 130) ap->boundaries.push_back(std::move(loops_));
        }
        kind_ = 0;
        loops_.clear();
    }

    void row(const Tok* t, size_t n) {
        bool ok = true;
        const Tok& c = t[0];
        if (c.is("100")) {
            if (n < 26) return;
            Runway r;
            r.width = t[1].num(ok);
            r.surface = int(t[2].num(ok));
            r.shoulder = int(t[3].num(ok));
            r.ends[0] = {t[9].num(ok), t[10].num(ok)};
            r.ends[1] = {t[18].num(ok), t[19].num(ok)};
            r.overrun[0] = t[12].num(ok);
            r.overrun[1] = t[21].num(ok);
            if (!ok) return;
            r.order = ++order_;
            ap->runways.push_back(r);
        } else if (c.is("102")) {
            if (n < 8) return;
            Helipad h;
            h.lat = t[2].num(ok);
            h.lon = t[3].num(ok);
            h.heading = t[4].num(ok);
            h.length = t[5].num(ok);
            h.width = t[6].num(ok);
            h.surface = int(t[7].num(ok));
            h.shoulder = n > 9 ? int(t[9].num(ok)) : 0;
            if (!ok) return;
            h.order = ++order_;
            ap->helipads.push_back(h);
        } else if (c.is("101")) {
            if (n < 9) return;
            LatLon a{t[4].num(ok), t[5].num(ok)}, b{t[7].num(ok), t[8].num(ok)};
            if (ok) ap->sealanes.push_back({a, b});
        } else if (c.is("110")) {
            if (n < 2) return;
            ++order_;
            kind_ = 110;
            surface_ = int(t[1].num(ok));
            loops_.clear();
            nodes_.clear();
        } else if (c.is("120") || c.is("130")) {
            kind_ = c.is("120") ? 120 : 130;
            loops_.clear();
            nodes_.clear();
        } else if (c.is("1302") && n >= 3) {
            if (t[1].is("datum_lat")) { dlat_ = t[2].num(ok); have_dlat_ = ok; }
            else if (t[1].is("datum_lon")) { dlon_ = t[2].num(ok); have_dlon_ = ok; }
            if (have_dlat_ && have_dlon_) {
                ap->has_datum = true;
                ap->datum = {dlat_, dlon_};
            }
        }
    }
};

// Summarise one airport record for the index.
bool summarise(const char* a, const char* b, const std::string& ident, uint64_t offset, AirportEntry& out) {
    std::vector<LatLon> pts;
    bool have_dlat = false, have_dlon = false;
    double dlat = 0, dlon = 0;
    Tok t[32];
    const char* line = a;
    while (line < b) {
        const char* eol = static_cast<const char*>(std::memchr(line, '\n', size_t(b - line)));
        if (!eol) eol = b;
        if (line[0] == '1') {
            size_t n = tokenize(line, eol, t, 32);
            bool ok = true;
            if (n >= 20 && t[0].is("100")) {
                LatLon p1{t[9].num(ok), t[10].num(ok)}, p2{t[18].num(ok), t[19].num(ok)};
                if (ok) { pts.push_back(p1); pts.push_back(p2); }
            } else if (n >= 4 && t[0].is("102")) {
                LatLon p{t[2].num(ok), t[3].num(ok)};
                if (ok) pts.push_back(p);
            } else if (n >= 9 && t[0].is("101")) {
                LatLon p1{t[4].num(ok), t[5].num(ok)}, p2{t[7].num(ok), t[8].num(ok)};
                if (ok) { pts.push_back(p1); pts.push_back(p2); }
            } else if (n >= 3 && t[0].is("1302")) {
                if (t[1].is("datum_lat")) { dlat = t[2].num(ok); have_dlat = ok; }
                else if (t[1].is("datum_lon")) { dlon = t[2].num(ok); have_dlon = ok; }
            }
        }
        line = eol + 1;
    }
    double lat, lon;
    if (have_dlat && have_dlon) {
        lat = dlat;
        lon = dlon;
        pts.push_back({lat, lon});
    } else if (!pts.empty()) {
        double mnla = 1e9, mxla = -1e9, mnlo = 1e9, mxlo = -1e9;
        for (auto& p : pts) {
            mnla = std::min(mnla, p.lat); mxla = std::max(mxla, p.lat);
            mnlo = std::min(mnlo, p.lon); mxlo = std::max(mxlo, p.lon);
        }
        lat = (mnla + mxla) / 2;
        lon = (mnlo + mxlo) / 2;
    } else {
        return false;
    }
    double r = 0;
    for (auto& p : pts) r = std::max(r, flat_metres(lat, lon, p.lat, p.lon));
    out.ident = ident;
    out.lat = lat;
    out.lon = lon;
    out.radius = std::min(20000.0, r + 250.0);
    out.offset = offset;
    out.length = uint64_t(b - a);
    return true;
}

std::vector<AirportEntry> scan_file(const std::string& path) {
    std::vector<AirportEntry> entries;
    MappedFile mf(path);
    if (!mf.data()) return entries;
    const char* base = reinterpret_cast<const char*>(mf.data());
    const char* end = base + mf.size();
    const char* line = base;
    const char* rec_start = nullptr;
    std::string rec_ident;
    Tok t[8];
    auto finish = [&](const char* rec_end) {
        if (!rec_start) return;
        AirportEntry e;
        if (summarise(rec_start, rec_end, rec_ident, uint64_t(rec_start - base), e)) entries.push_back(std::move(e));
    };
    while (line < end) {
        const char* eol = static_cast<const char*>(std::memchr(line, '\n', size_t(end - line)));
        if (!eol) eol = end;
        if (line[0] == '1' && (line[1] == ' ' || line[1] == '\t' || ((line[1] == '6' || line[1] == '7') &&
                                                                    (line[2] == ' ' || line[2] == '\t')))) {
            size_t n = tokenize(line, eol, t, 8);
            if (is_header(t, n)) {
                finish(line);
                rec_start = line;
                rec_ident = t[4].str();
            }
        }
        line = eol + 1;
    }
    finish(end);
    return entries;
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (!b.empty() && b[0] == '/') return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}

std::string normalize(std::string p) {
    // collapse duplicate slashes and strip a trailing slash
    std::string out;
    for (char c : p) {
        if (c == '\\') c = '/';
        if (c == '/' && !out.empty() && out.back() == '/') continue;
        out += c;
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

int64_t grid_key(double lat, double lon) {
    int64_t a = (int64_t)std::floor(lat * 4), b = (int64_t)std::floor(lon * 4);
    return (a << 32) ^ (b & 0xffffffff);
}

}  // namespace

void Airport::for_each_point(const std::function<void(const LatLon&)>& fn) const {
    if (has_datum) fn(datum);
    for (auto& r : runways) { fn(r.ends[0]); fn(r.ends[1]); }
    for (auto& h : helipads) fn({h.lat, h.lon});
    for (auto& s : sealanes) { fn(s.first); fn(s.second); }
    for (auto& p : pavements)
        for (auto& l : p.loops)
            for (auto& q : l) fn(q);
    for (auto& b : boundaries)
        for (auto& l : b)
            for (auto& q : l) fn(q);
}

bool Airport::center(LatLon& out) const {
    double mnla = 1e9, mxla = -1e9, mnlo = 1e9, mxlo = -1e9;
    bool any = false;
    for_each_point([&](const LatLon& p) {
        any = true;
        mnla = std::min(mnla, p.lat); mxla = std::max(mxla, p.lat);
        mnlo = std::min(mnlo, p.lon); mxlo = std::max(mxlo, p.lon);
    });
    if (!any) return false;
    out = {(mnla + mxla) / 2, (mnlo + mxlo) / 2};
    return true;
}

std::unique_ptr<Airport> parse_airport(const char* data, size_t len) {
    Parser p;
    const char* line = data;
    const char* end = data + len;
    while (line < end) {
        const char* eol = static_cast<const char*>(std::memchr(line, '\n', size_t(end - line)));
        if (!eol) eol = end;
        if (!p.feed(line, eol)) break;
        line = eol + 1;
    }
    return p.finish();
}

std::vector<std::string> pack_dirs(const std::string& xp_root, std::vector<std::string>* skipped) {
    std::vector<std::string> found;
    std::string global_dir = normalize(join(xp_root, "Global Scenery/Global Airports"));
    std::ifstream in(fs_path(join(xp_root, "Custom Scenery/scenery_packs.ini")));
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.rfind("SCENERY_PACK_DISABLED", 0) == 0 || line.rfind("SCENERY_PACK", 0) != 0) continue;
        std::string pack = line.substr(12);
        size_t a = pack.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        pack = pack.substr(a);
        std::string folder = pack == "*GLOBAL_AIRPORTS*" ? global_dir : normalize(join(xp_root, pack));
        if (std::find(found.begin(), found.end(), folder) != found.end()) continue;
        if (folder != global_dir && on_virtual_fs(folder)) {
            if (skipped) skipped->push_back(folder);
            continue;
        }
        found.push_back(folder);
    }
    return found;
}

bool AirportIndex::build(const std::string& root, const std::string& cache_path,
                         const std::function<void(const std::string&)>& log) {
    xp_root = root;
    std::vector<std::string> paths;
    auto add = [&](const std::string& p) {
        std::string n = normalize(p);
        if (is_file(n) && std::find(paths.begin(), paths.end(), n) == paths.end()) paths.push_back(n);
    };
    std::vector<std::string> skipped;
    for (auto& d : pack_dirs(root, &skipped)) add(join(d, "Earth nav data/apt.dat"));
    for (auto& d : skipped) log("not reading " + d + " (virtual file system, e.g. streaming ortho)");
    add(join(root, "Global Scenery/Global Airports/Earth nav data/apt.dat"));
    add(join(root, "Resources/default scenery/default apt dat/Earth nav data/apt.dat"));

    // cache: path -> (size, mtime, entries)
    struct Cached {
        long long size, mtime;
        std::vector<AirportEntry> entries;
    };
    std::map<std::string, Cached> cache;
    {
        std::ifstream in(fs_path(cache_path));
        std::string line;
        if (std::getline(in, line) && line == "ZGIDX 1") {
            Cached* cur = nullptr;
            while (std::getline(in, line)) {
                if (line.rfind("F ", 0) == 0) {
                    std::istringstream ss(line.substr(2));
                    long long size, mtime;
                    size_t count;
                    ss >> size >> mtime >> count;
                    std::string path;
                    std::getline(ss, path);
                    if (!path.empty() && path[0] == ' ') path.erase(0, 1);
                    cur = &cache[path];
                    cur->size = size;
                    cur->mtime = mtime;
                    cur->entries.reserve(count);
                } else if (cur) {
                    std::istringstream ss(line);
                    AirportEntry e;
                    ss >> e.ident >> e.lat >> e.lon >> e.radius >> e.offset >> e.length;
                    if (ss) cur->entries.push_back(std::move(e));
                }
            }
        }
    }
    bool changed = false;
    std::vector<std::vector<AirportEntry>> per_file;
    files.clear();
    for (auto& p : paths) {
        long long size, mtime;
        if (!file_key(p, size, mtime)) continue;
        auto it = cache.find(p);
        if (it != cache.end() && it->second.size == size && it->second.mtime == mtime) {
            per_file.push_back(it->second.entries);
        } else {
            log("indexing " + p);
            auto entries = scan_file(p);
            cache[p] = {size, mtime, entries};
            per_file.push_back(std::move(entries));
            changed = true;
        }
        files.push_back(p);
    }
    if (changed) {
        std::ofstream out(fs_path(cache_path + ".tmp"), std::ios::trunc);
        out << "ZGIDX 1\n";
        out.precision(9);
        for (size_t i = 0; i < files.size(); ++i) {
            const Cached& c = cache[files[i]];
            out << "F " << c.size << " " << c.mtime << " " << c.entries.size() << " " << files[i] << "\n";
            for (auto& e : c.entries)
                out << e.ident << " " << e.lat << " " << e.lon << " " << e.radius << " " << e.offset << " " << e.length
                    << "\n";
        }
        out.close();
        if (out) {
            remove_file(cache_path);              // Windows will not rename over an existing file
            rename_file(cache_path + ".tmp", cache_path);
        }
    }
    entries.clear();
    by_ident_.clear();
    grid_.clear();
    max_radius_ = 0;
    for (size_t f = 0; f < per_file.size(); ++f) {
        for (auto& e : per_file[f]) {
            if (by_ident_.count(e.ident)) continue;   // a higher-priority file already defines it
            AirportEntry copy = e;
            copy.file = int(f);
            by_ident_[copy.ident] = entries.size();
            entries.push_back(std::move(copy));
        }
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        max_radius_ = std::max(max_radius_, entries[i].radius);
        grid_[grid_key(entries[i].lat, entries[i].lon)].push_back(i);
    }
    return !entries.empty();
}

std::vector<std::pair<double, const AirportEntry*>> AirportIndex::nearby(double lat, double lon, double radius_m) const {
    std::vector<std::pair<double, const AirportEntry*>> found;
    double span_lat = (radius_m + max_radius_) / 111000.0;
    double span_lon = span_lat / std::max(0.05, std::cos(deg2rad(lat)));
    for (long a = (long)std::floor((lat - span_lat) * 4); a <= (long)std::floor((lat + span_lat) * 4); ++a) {
        for (long b = (long)std::floor((lon - span_lon) * 4); b <= (long)std::floor((lon + span_lon) * 4); ++b) {
            int64_t key = ((int64_t)a << 32) ^ ((int64_t)b & 0xffffffff);
            auto it = grid_.find(key);
            if (it == grid_.end()) continue;
            for (size_t i : it->second) {
                const AirportEntry& e = entries[i];
                double d = flat_metres(lat, lon, e.lat, e.lon);
                if (d <= e.radius + radius_m) found.push_back({d, &e});
            }
        }
    }
    std::sort(found.begin(), found.end(), [](auto& x, auto& y) { return x.first < y.first; });
    return found;
}

const AirportEntry* AirportIndex::find(const std::string& ident) const {
    auto it = by_ident_.find(ident);
    return it == by_ident_.end() ? nullptr : &entries[it->second];
}

std::unique_ptr<Airport> AirportIndex::load(const AirportEntry& e) const {
    std::ifstream in(fs_path(files[size_t(e.file)]), std::ios::binary);
    if (!in) return nullptr;
    in.seekg(std::streamoff(e.offset));
    std::string buf(size_t(e.length), '\0');
    in.read(&buf[0], std::streamsize(e.length));
    if (size_t(in.gcount()) != e.length) return nullptr;
    auto ap = parse_airport(buf.data(), buf.size());
    if (!ap || ap->ident != e.ident) return nullptr;
    return ap;
}

}  // namespace zg
