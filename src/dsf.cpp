#include "dsf.h"

#include "platform.h"


#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace zg {

namespace {

constexpr int kUvParam = 65535;
constexpr double kLineSegM = 6.0;
constexpr double kDefaultLineHalfM = 1.5;
constexpr double kMinLineHalfM = 1.0;
constexpr double kMaxLineHalfM = 20.0;
constexpr size_t kMaxCachedDsfs = 8;

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string slashes(std::string s) {
    for (auto& c : s)
        if (c == '\\') c = '/';
    return s;
}
bool ends_with(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}
bool contains_any(const std::string& s, const std::vector<std::string>& words) {
    for (auto& w : words)
        if (!w.empty() && s.find(w) != std::string::npos) return true;
    return false;
}
bool contains_any(const std::string& s, std::initializer_list<const char*> words) {
    for (auto* w : words)
        if (s.find(w) != std::string::npos) return true;
    return false;
}
std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string w;
    while (std::getline(ss, w, ',')) {
        size_t a = w.find_first_not_of(" \t"), b = w.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(lower(w.substr(a, b - a + 1)));
    }
    return out;
}
std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return a.back() == '/' ? a + b : a + "/" + b;
}
std::string normpath(const std::string& p) {
    // resolve "." and ".." segments, collapse slashes
    std::vector<std::string> parts;
    std::stringstream ss(slashes(p));
    std::string seg;
    while (std::getline(ss, seg, '/')) {
        if (seg.empty() || seg == ".") continue;
        if (seg == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else parts.push_back(seg);
    }
    std::string out = (!p.empty() && (p[0] == '/' || p[0] == '\\')) ? "/" : "";
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
    return out;
}
std::string basename_noext(const std::string& p) {
    size_t s = p.find_last_of('/');
    std::string b = s == std::string::npos ? p : p.substr(s + 1);
    size_t d = b.find_last_of('.');
    return d == std::string::npos ? b : b.substr(0, d);
}
std::string dirname(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? "" : p.substr(0, s);
}

int surface_keyword(const std::string& s) {   // SurfaceClass, -1 soft, -2 unknown
    if (s == "asphalt" || s == "concrete" || s == "shoulder" || s == "blastpad" || s == "snow" || s == "lakebed" ||
        s == "water")
        return PAVED;
    if (s == "dirt") return DIRT;
    if (s == "gravel") return GRAVEL;
    if (s == "grass") return -1;
    return -2;
}

const std::initializer_list<const char*> kPavedWords = {
    "pavement", "asphalt", "concrete", "tarmac", "apron", "taxiway", "runway", "parking", "paved", "cobble",
    "brick", "plaza", "road", "street", "sidewalk", "curb", "helipad", "marking", "ramp", "blast"};
const std::initializer_list<const char*> kDirtWords = {"dirt", "sand", "mud", "soil"};
const std::initializer_list<const char*> kGravelWords = {"gravel", "pebble", "crushed", "rubble"};
const std::initializer_list<const char*> kLineSkipWords = {"pavement_fx", "airport/lines/", "grass", "lawn", "shore",
                                                           "water"};

// ---------------------------------------------------------------------------------------------- DSF reading

struct Reader {
    const uint8_t* d;
    size_t size;
    void need(size_t pos, size_t n) const {
        if (pos + n > size) throw std::runtime_error("truncated DSF");
    }
    uint8_t u8(size_t pos) const { need(pos, 1); return d[pos]; }
    uint16_t u16(size_t pos) const { need(pos, 2); return uint16_t(d[pos] | (d[pos + 1] << 8)); }
    uint32_t u32(size_t pos) const {
        need(pos, 4);
        return uint32_t(d[pos]) | (uint32_t(d[pos + 1]) << 8) | (uint32_t(d[pos + 2]) << 16) | (uint32_t(d[pos + 3]) << 24);
    }
    int32_t i32(size_t pos) const { return int32_t(u32(pos)); }
    float f32(size_t pos) const {
        uint32_t v = u32(pos);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
};

struct Atom {
    std::string id;
    size_t start, stop;
};

std::vector<Atom> atoms(const Reader& r, size_t pos, size_t end) {
    std::vector<Atom> out;
    while (pos + 8 <= end) {
        uint32_t raw = r.u32(pos), size = r.u32(pos + 4);
        if (size < 8 || pos + size > end) throw std::runtime_error("corrupt atom");
        std::string id(4, ' ');
        id[0] = char((raw >> 24) & 0xff);
        id[1] = char((raw >> 16) & 0xff);
        id[2] = char((raw >> 8) & 0xff);
        id[3] = char(raw & 0xff);
        out.push_back({id, pos + 8, pos + size});
        pos += size;
    }
    return out;
}

std::vector<std::string> strings(const Reader& r, size_t start, size_t end) {
    std::vector<std::string> out;
    size_t a = start;
    for (size_t i = start; i < end; ++i) {
        if (r.d[i] == 0) {
            out.emplace_back(reinterpret_cast<const char*>(r.d + a), i - a);
            a = i + 1;
        }
    }
    return out;
}

struct Pool {
    size_t start;
    std::vector<float> scale;
    bool loaded = false;
    int nplanes = 0;
    std::vector<std::vector<uint32_t>> raw;
    std::vector<double> k, o;

    void load(const Reader& r) {
        if (loaded) return;
        uint32_t count = r.u32(start);
        int planes = r.u8(start + 4);
        size_t pos = start + 5;
        for (int p = 0; p < planes && p < 4; ++p) {
            int enc = r.u8(pos++);
            std::vector<uint32_t> values;
            values.reserve(count);
            if (enc == 0 || enc == 1) {
                r.need(pos, size_t(count) * 2);
                for (uint32_t i = 0; i < count; ++i) values.push_back(r.u16(pos + 2 * i));
                pos += size_t(count) * 2;
            } else if (enc == 2 || enc == 3) {
                while (values.size() < count) {
                    int n = r.u8(pos++);
                    if (n & 0x80) {
                        uint16_t v = r.u16(pos);
                        pos += 2;
                        for (int i = 0; i < (n & 0x7f); ++i) values.push_back(v);
                    } else {
                        for (int i = 0; i < n; ++i) values.push_back(r.u16(pos + 2 * size_t(i)));
                        pos += 2 * size_t(n);
                    }
                }
                values.resize(count);
            } else {
                throw std::runtime_error("unknown plane encoding");
            }
            if (enc == 1 || enc == 3) {
                uint32_t acc = 0;
                for (auto& v : values) {
                    acc = (acc + v) & 0xffff;
                    v = acc;
                }
            }
            raw.push_back(std::move(values));
        }
        if (raw.size() < 2 || scale.size() < 2 * raw.size()) throw std::runtime_error("point pool without coordinates");
        for (size_t p = 0; p < raw.size(); ++p) {
            k.push_back(scale[2 * p] / 65535.0);
            o.push_back(scale[2 * p + 1]);
        }
        nplanes = planes;
        loaded = true;
    }
    std::array<double, 4> point(uint32_t i, bool curved) const {
        if (i >= raw[0].size()) throw std::runtime_error("pool index out of range");
        std::array<double, 4> p{o[0] + raw[0][i] * k[0], o[1] + raw[1][i] * k[1], 0, 0};
        if (curved) {
            p[2] = o[2] + raw[2][i] * k[2];
            p[3] = o[3] + raw[3][i] * k[3];
        }
        return p;
    }
};

}  // namespace

std::vector<DsfPolygon> parse_dsf_polygons(const uint8_t* data, size_t size,
                                           const std::function<bool(const std::string&)>& want,
                                           const std::function<bool(const std::string&)>& owns) {
    std::vector<DsfPolygon> out;
    if (size >= 6 && std::memcmp(data, "7z\xbc\xaf\x27\x1c", 6) == 0) throw std::runtime_error("7z-compressed DSF");
    if (size < 28 || std::memcmp(data, "XPLNEDSF", 8) != 0) throw std::runtime_error("not a DSF file");
    Reader r{data, size};
    size_t end = size - 16;   // MD5 footer
    std::vector<std::pair<std::string, std::string>> props;
    std::vector<std::string> poly_defs;
    std::vector<size_t> raw_pools;
    std::vector<std::vector<float>> scales;
    size_t cmd_start = 0, cmd_stop = 0;
    for (auto& a : atoms(r, 12, end)) {
        if (a.id == "HEAD") {
            for (auto& s : atoms(r, a.start, a.stop))
                if (s.id == "PROP") {
                    auto items = strings(r, s.start, s.stop);
                    for (size_t i = 0; i + 1 < items.size(); i += 2) props.push_back({items[i], items[i + 1]});
                }
        } else if (a.id == "DEFN") {
            for (auto& s : atoms(r, a.start, a.stop))
                if (s.id == "POLY") {
                    auto items = strings(r, s.start, s.stop);
                    poly_defs.insert(poly_defs.end(), items.begin(), items.end());
                }
        } else if (a.id == "GEOD") {
            for (auto& s : atoms(r, a.start, a.stop)) {
                if (s.id == "POOL") raw_pools.push_back(s.start);
                else if (s.id == "SCAL") {
                    std::vector<float> f;
                    for (size_t p = s.start; p + 4 <= s.stop; p += 4) f.push_back(r.f32(p));
                    scales.push_back(std::move(f));
                }
            }
        } else if (a.id == "CMDS") {
            cmd_start = a.start;
            cmd_stop = a.stop;
        }
    }
    if (cmd_stop == 0) return out;
    std::vector<Pool> pools;
    for (size_t i = 0; i < raw_pools.size() && i < scales.size(); ++i) { Pool pl; pl.start = raw_pools[i]; pl.scale = scales[i]; pools.push_back(std::move(pl)); }
    std::vector<std::string> filter_ids;
    for (auto& p : props)
        if (p.first == "sim/filter/aptid") filter_ids.push_back(p.second);
    std::vector<char> keep(poly_defs.size());
    for (size_t i = 0; i < poly_defs.size(); ++i) keep[i] = want && want(poly_defs[i]);

    size_t pos = cmd_start, stop = cmd_stop;
    Pool* pool = nullptr;
    uint32_t definition = 0;
    int filter_index = -1;
    while (pos < stop) {
        int cmd = r.u8(pos++);
        if (cmd >= 12 && cmd <= 15) {
            bool keep_it = definition < keep.size() && keep[definition];
            int param = r.u16(pos);
            std::vector<std::vector<uint32_t>> windings;
            if (cmd == 12) {
                int m = r.u8(pos + 2);
                pos += 3;
                std::vector<uint32_t> w;
                for (int k = 0; k < m; ++k) w.push_back(r.u16(pos + 2 * size_t(k)));
                pos += 2 * size_t(m);
                windings.push_back(std::move(w));
            } else if (cmd == 13) {
                uint32_t a = r.u16(pos + 2), b = r.u16(pos + 4);
                pos += 6;
                std::vector<uint32_t> w;
                for (uint32_t k = a; k < b; ++k) w.push_back(k);
                windings.push_back(std::move(w));
            } else if (cmd == 14) {
                int count = r.u8(pos + 2);
                pos += 3;
                for (int c = 0; c < count; ++c) {
                    int m = r.u8(pos++);
                    std::vector<uint32_t> w;
                    for (int k = 0; k < m; ++k) w.push_back(r.u16(pos + 2 * size_t(k)));
                    pos += 2 * size_t(m);
                    windings.push_back(std::move(w));
                }
            } else {
                int count = r.u8(pos + 2);
                pos += 3;
                std::vector<uint32_t> idx;
                for (int k = 0; k <= count; ++k) idx.push_back(r.u16(pos + 2 * size_t(k)));
                pos += 2 * size_t(count + 1);
                for (int c = 0; c < count; ++c) {
                    std::vector<uint32_t> w;
                    for (uint32_t k = idx[size_t(c)]; k < idx[size_t(c) + 1]; ++k) w.push_back(k);
                    windings.push_back(std::move(w));
                }
            }
            if (!keep_it || !pool) continue;
            if (filter_index >= 0 && owns) {
                if (size_t(filter_index) >= filter_ids.size() || !owns(filter_ids[size_t(filter_index)])) continue;
            }
            pool->load(r);
            bool curved = (pool->nplanes == 4 && param != kUvParam) || pool->nplanes == 8;
            if (curved && pool->raw.size() < 4) curved = false;
            DsfPolygon poly;
            poly.definition = poly_defs[definition];
            poly.param = param;
            poly.curved = curved;
            for (auto& w : windings) {
                std::vector<std::array<double, 4>> ring;
                ring.reserve(w.size());
                for (uint32_t i : w) ring.push_back(pool->point(i, curved));
                poly.windings.push_back(std::move(ring));
            }
            out.push_back(std::move(poly));
        } else if (cmd == 1) {
            uint16_t pi = r.u16(pos);
            pos += 2;
            pool = pi < pools.size() ? &pools[pi] : nullptr;
        } else if (cmd == 3) {
            definition = r.u8(pos);
            pos += 1;
        } else if (cmd == 4) {
            definition = r.u16(pos);
            pos += 2;
        } else if (cmd == 5) {
            definition = r.u32(pos);
            pos += 4;
        } else if (cmd == 7) {
            pos += 2;
        } else if (cmd == 8 || cmd == 10 || cmd == 25 || cmd == 28 || cmd == 31 || cmd == 2) {
            pos += 4;
        } else if (cmd == 9 || cmd == 23 || cmd == 26 || cmd == 29) {
            pos += 1 + 2 * size_t(r.u8(pos));
        } else if (cmd == 24 || cmd == 27 || cmd == 30 || cmd == 11) {
            pos += 1 + 4 * size_t(r.u8(pos));
        } else if (cmd == 6 || cmd == 17) {
            pos += 1;
        } else if (cmd == 16) {
        } else if (cmd == 18) {
            pos += 9;
        } else if (cmd == 32 || cmd == 33 || cmd == 34) {
            size_t sz = cmd == 32 ? 1 : cmd == 33 ? 2 : 4;
            size_t length = sz == 1 ? r.u8(pos) : sz == 2 ? r.u16(pos) : r.u32(pos);
            size_t body = pos + sz;
            if (length >= 6 && r.u16(body) == 1) filter_index = r.i32(body + 2);
            pos = body + length;
        } else {
            throw std::runtime_error("unknown DSF command " + std::to_string(cmd));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- ground polygons

GroundPolygons::GroundPolygons(std::string xp_root, const AirportIndex* index, const std::string& block_words,
                               const std::string& allow_words, Log log)
    : xp_root_(std::move(xp_root)), index_(index), block_(words(block_words)), allow_(words(allow_words)),
      log_(std::move(log)) {}

void GroundPolygons::once(const std::string& key, const std::string& msg) {
    if (reported_.insert(key).second) log_(msg);
}

void GroundPolygons::load_library() {
    std::vector<std::string> folders = pack_dirs(xp_root_);
    std::string base = join(xp_root_, "Resources/default scenery");
    std::vector<std::string> defaults;
    for (auto& n : list_dir(base)) defaults.push_back(join(base, n));
    std::sort(defaults.begin(), defaults.end());
    folders.insert(folders.end(), defaults.begin(), defaults.end());
    for (auto& folder : folders) {
        std::ifstream in(fs_path(join(folder, "library.txt")));
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 8) continue;
            std::string low = lower(line);
            if (low.find(".pol") == std::string::npos && low.find(".lin") == std::string::npos) continue;
            std::istringstream ss(line);
            std::string first;
            ss >> first;
            std::string ufirst = first;
            for (auto& c : ufirst) c = (char)std::toupper((unsigned char)c);
            if (ufirst.rfind("EXPORT", 0) != 0) continue;
            std::string tok;
            while (ss >> tok) {
                std::string lt = lower(tok);
                if (ends_with(lt, ".pol") || ends_with(lt, ".lin")) {
                    std::string rest;
                    std::getline(ss, rest);
                    size_t a = rest.find_first_not_of(" \t"), b = rest.find_last_not_of(" \t\r");
                    if (a != std::string::npos) {
                        std::string virt = lower(slashes(tok));
                        if (!exports_.count(virt)) exports_[virt] = normpath(join(folder, slashes(rest.substr(a, b - a + 1))));
                    }
                    break;
                }
            }
        }
    }
    library_ready_ = true;
}

std::string GroundPolygons::resolve(const std::string& path, const std::string& pack) const {
    auto it = exports_.find(lower(slashes(path)));
    if (it != exports_.end() && is_file(it->second)) return it->second;
    std::string local = normpath(join(pack, slashes(path)));
    return is_file(local) ? local : std::string();
}

const GroundPolygons::FileInfo& GroundPolygons::read(const std::string& path, const std::string& pack) {
    auto key = std::make_pair(lower(path), pack);
    auto it = files_.find(key);
    if (it != files_.end()) return it->second;
    FileInfo fi;
    fi.real = resolve(path, pack);
    fi.resolved = !fi.real.empty();
    if (fi.resolved) {
        std::ifstream in(fs_path(fi.real));
        if (in) {
            fi.has_info = true;
            PolInfo& info = fi.info;
            std::string line;
            std::vector<std::array<double, 3>> offsets;
            double scale = 0, tex_width = 0;
            size_t read_bytes = 0;
            while (std::getline(in, line) && read_bytes < 65536) {
                read_bytes += line.size() + 1;
                std::istringstream ss(line);
                std::string key0;
                if (!(ss >> key0) || key0[0] == '#') continue;
                std::string key = key0;
                for (auto& c : key) c = (char)std::toupper((unsigned char)c);
                std::vector<std::string> parts;
                std::string p;
                while (ss >> p) parts.push_back(p);
                std::string rest;
                for (auto& q : parts) rest += (rest.empty() ? "" : " ") + q;
                if (key == "LAYER_GROUP" && !parts.empty()) info.layer = lower(parts[0]);
                else if (key == "SURFACE" && !parts.empty()) info.surface = lower(parts[0]);
                else if (key == "LOAD_CENTER") info.ortho = true;
                else if (key.rfind("TEXTURE", 0) == 0) info.texture += " " + lower(rest);
                else if (key.rfind("DECAL", 0) == 0) info.decal += " " + lower(rest);
                else if (key == "SCALE" && !parts.empty()) scale = std::atof(parts[0].c_str());
                else if (key == "TEX_WIDTH" && !parts.empty()) tex_width = std::atof(parts[0].c_str());
                else if (key == "S_OFFSET" && parts.size() >= 4)
                    offsets.push_back({std::atof(parts[1].c_str()), std::atof(parts[2].c_str()), std::atof(parts[3].c_str())});
            }
            double tw = tex_width > 0 ? tex_width : 1.0;
            if (!offsets.empty() && scale > 0) {
                double reach = 0;
                for (auto& o : offsets) reach = std::max({reach, o[1] - o[0], o[2] - o[1]});
                info.half_width = reach / tw * scale;
            }
        }
    }
    return files_.emplace(key, std::move(fi)).first->second;
}

double GroundPolygons::half_width(const std::string& path, const std::string& pack) {
    const FileInfo& fi = read(path, pack);
    double half = fi.has_info && fi.info.half_width > 0 ? fi.info.half_width : kDefaultLineHalfM;
    return std::min(kMaxLineHalfM, std::max(kMinLineHalfM, half));
}

int GroundPolygons::classify_line(const std::string& low, const FileInfo& fi) {
    if (fi.has_info) {
        int s = surface_keyword(fi.info.surface);
        if (s != -2) return s;
    }
    if (contains_any(low, kLineSkipWords) || (fi.has_info && fi.info.layer == "markings")) return -1;
    std::string name = fi.has_info ? low + " " + lower(basename_noext(fi.real)) + " " + fi.info.texture : low;
    if (contains_any(name, kDirtWords)) return DIRT;
    if (contains_any(name, kGravelWords)) return GRAVEL;
    if (contains_any(name, kPavedWords) ||
        (fi.has_info && (fi.info.layer == "runways" || fi.info.layer == "taxiways" || fi.info.layer == "roads")))
        return PAVED;
    return -1;
}

int GroundPolygons::classify(const std::string& path, const std::string& pack) {
    auto key = std::make_pair(lower(path), pack);
    auto it = class_cache_.find(key);
    if (it != class_cache_.end()) return it->second;
    int result = -1;
    std::string low = lower(slashes(path));
    if (low.rfind("::", 0) == 0) {
        result = -1;
    } else if (contains_any(low, allow_)) {
        result = -1;
    } else if (contains_any(low, block_)) {
        result = PAVED;
    } else {
        const FileInfo& fi = read(path, pack);
        if (!fi.resolved) unresolved_.insert(path);
        if (ends_with(low, ".lin")) {
            result = classify_line(low, fi);
        } else {
            std::string name = low, paved_text = low;
            bool decided = false;
            if (fi.has_info) {
                int s = surface_keyword(fi.info.surface);
                if (s != -2) { result = s; decided = true; }
                else if (fi.info.ortho) { result = -1; decided = true; }
                else if (fi.info.layer == "runways" || fi.info.layer == "taxiways" || fi.info.layer == "shoulders" ||
                         fi.info.layer == "markings" || fi.info.layer == "roads") { result = PAVED; decided = true; }
                name = low + " " + lower(basename_noext(fi.real)) + " " + fi.info.texture;
                paved_text = name + " " + fi.info.decal;
            }
            if (!decided) {
                if (contains_any(paved_text, kPavedWords)) result = PAVED;
                else if (contains_any(name, kDirtWords)) result = DIRT;
                else if (contains_any(name, kGravelWords)) result = GRAVEL;
                else result = -1;
            }
        }
    }
    class_cache_[key] = result;
    return result;
}

namespace {

std::vector<BezNode> to_nodes(const std::vector<std::array<double, 4>>& w) {
    std::vector<BezNode> nodes;
    for (auto& p : w) {
        bool straight = std::fabs(p[2] - p[0]) < 1e-9 && std::fabs(p[3] - p[1]) < 1e-9;
        nodes.push_back({p[1], p[0], !straight, p[3], p[2]});
    }
    return nodes;
}

std::vector<Ring> poly_rings(const DsfPolygon& poly) {
    std::vector<Ring> rings;
    for (auto& w : poly.windings) {
        if (w.size() < 3) continue;
        Ring ring;
        if (poly.curved) ring = flatten_nodes(to_nodes(w), true);
        else
            for (auto& p : w) ring.push_back({p[1], p[0]});
        if (ring.size() >= 3) rings.push_back(std::move(ring));
    }
    return rings;
}

std::vector<Ring> line_bands(const DsfPolygon& poly, double half) {
    bool closed = poly.param == 1;
    std::vector<Ring> bands;
    for (auto& w : poly.windings) {
        if (w.size() < 2) continue;
        std::vector<LatLon> pts;
        if (poly.curved) pts = flatten_nodes(to_nodes(w), closed, kLineSegM);
        else
            for (auto& p : w) pts.push_back({p[1], p[0]});
        if (pts.size() < 2) continue;
        LocalProjection proj(pts[0].lat, pts[0].lon);
        std::vector<XY> xy;
        for (auto& p : pts) xy.push_back(proj.to_xy(p));
        size_t segs = closed ? xy.size() : xy.size() - 1;
        for (size_t k = 0; k < segs; ++k) {
            XY a = xy[k], b = xy[(k + 1) % xy.size()];
            double length = std::hypot(b.x - a.x, b.y - a.y);
            if (length < 1e-3) continue;
            double ux = (b.x - a.x) / length, uy = (b.y - a.y) / length;
            double ax = a.x - ux * half, ay = a.y - uy * half, bx = b.x + ux * half, by = b.y + uy * half;
            double vx = -uy * half, vy = ux * half;
            bands.push_back({proj.to_latlon(ax - vx, ay - vy), proj.to_latlon(bx - vx, by - vy),
                             proj.to_latlon(bx + vx, by + vy), proj.to_latlon(ax + vx, ay + vy)});
        }
    }
    return bands;
}

std::string dsf_path(const std::string& pack, int lat, int lon) {
    auto fl10 = [](int v) { return int(std::floor(v / 10.0)) * 10; };
    char dir[32], file[32];
    std::snprintf(dir, sizeof dir, "%+03d%+04d", fl10(lat), fl10(lon));
    std::snprintf(file, sizeof file, "%+03d%+04d.dsf", lat, lon);
    return join(join(join(pack, "Earth nav data"), dir), file);
}

}  // namespace

const std::vector<GroundPolygons::Found>& GroundPolygons::dsf_entries(const std::string& path, const std::string& pack) {
    static const std::vector<Found> empty;
    long long fsize, mtime;
    if (!file_key(path, fsize, mtime)) return empty;
    std::pair<long long, long long> key{fsize, mtime};
    auto it = dsf_cache_.find(path);
    if (it != dsf_cache_.end() && it->second.first == key) return it->second.second;

    std::vector<Found> entries;
    try {
        if (fsize == 0) throw std::runtime_error("empty file");
        MappedFile mf(path);
        if (!mf.data()) throw std::runtime_error("cannot map");
        std::string owner = pack;
        auto owns = [&](const std::string& ident) {
            const AirportEntry* e = index_->find(ident);
            if (!e) return false;
            std::string apt = index_->files[size_t(e->file)];
            return dirname(dirname(apt)) == owner;
        };
        auto want = [&](const std::string& def) {
            std::string l = lower(def);
            return (ends_with(l, ".pol") || ends_with(l, ".lin")) && classify(def, pack) >= 0;
        };
        std::vector<DsfPolygon> polys = parse_dsf_polygons(mf.data(), mf.size(), want, owns);
        for (auto& poly : polys) {
            int cls = classify(poly.definition, pack);
            if (cls < 0) continue;
            bool line = ends_with(lower(poly.definition), ".lin");
            std::vector<Ring> rings = line ? line_bands(poly, half_width(poly.definition, pack)) : poly_rings(poly);
            if (rings.empty()) continue;
            Found f{SurfaceClass(cls), std::move(rings), 1e9, 1e9, -1e9, -1e9, line};
            for (auto& r : f.rings)
                for (auto& q : r) {
                    f.s = std::min(f.s, q.lat); f.n = std::max(f.n, q.lat);
                    f.w = std::min(f.w, q.lon); f.e = std::max(f.e, q.lon);
                }
            entries.push_back(std::move(f));
        }
    } catch (const std::exception& ex) {
        once("dsf:" + path, path + ": draped polygons not read (" + ex.what() + ")");
        entries.clear();
    }
    dsf_cache_[path] = {key, std::move(entries)};
    dsf_order_.erase(std::remove(dsf_order_.begin(), dsf_order_.end(), path), dsf_order_.end());
    dsf_order_.push_back(path);
    while (dsf_order_.size() > kMaxCachedDsfs) {
        dsf_cache_.erase(dsf_order_.front());
        dsf_order_.erase(dsf_order_.begin());
    }
    return dsf_cache_[path].second;
}

std::vector<Pavement> GroundPolygons::collect(const Airport& ap, const AirportEntry& entry, double pad_m) {
    std::vector<Pavement> out;
    // box around the airport's core (runways, helipads) plus everything within kMaxGapM of it
    std::vector<LatLon> core, pts;
    for (auto& r : ap.runways) { core.push_back(r.ends[0]); core.push_back(r.ends[1]); }
    for (auto& h : ap.helipads) core.push_back({h.lat, h.lon});
    ap.for_each_point([&](const LatLon& p) { pts.push_back(p); });
    if (pts.empty()) return out;
    if (core.empty()) core = pts;
    double s = 1e9, n = -1e9, w = 1e9, e = -1e9;
    for (auto& p : core) { s = std::min(s, p.lat); n = std::max(n, p.lat); w = std::min(w, p.lon); e = std::max(e, p.lon); }
    double gap_lat = kMaxGapM / 111132.0;
    double gap_lon = kMaxGapM / (111320.0 * std::max(0.05, std::cos(deg2rad((s + n) / 2))));
    double s2 = s, n2 = n, w2 = w, e2 = e;
    for (auto& p : pts) {
        if (p.lat >= s - gap_lat && p.lat <= n + gap_lat && p.lon >= w - gap_lon && p.lon <= e + gap_lon) {
            s2 = std::min(s2, p.lat); n2 = std::max(n2, p.lat); w2 = std::min(w2, p.lon); e2 = std::max(e2, p.lon);
        }
    }
    double pad_lat = pad_m / 111132.0;
    double pad_lon = pad_m / (111320.0 * std::max(0.05, std::cos(deg2rad((s2 + n2) / 2))));
    double bs = s2 - pad_lat, bn = n2 + pad_lat, bw = w2 - pad_lon, be = e2 + pad_lon;

    if (!library_ready_) load_library();
    std::string apt_path = index_->files[size_t(entry.file)];
    std::string pack = dirname(dirname(apt_path));
    size_t total = 0;
    for (int lat = int(std::floor(bs)); lat <= int(std::floor(bn)); ++lat) {
        for (int lon = int(std::floor(bw)); lon <= int(std::floor(be)); ++lon) {
            std::string path = dsf_path(pack, lat, lon);
            if (!is_file(path)) continue;
            size_t hits = 0;
            for (auto& f : dsf_entries(path, pack)) {
                if (!(f.s <= bn && f.n >= bs && f.w <= be && f.e >= bw)) continue;
                ++hits;
                int code = f.cls == DIRT ? 4 : f.cls == GRAVEL ? 5 : 1;
                if (f.line) {
                    for (auto& ring : f.rings) out.push_back({code, {ring}, 0});
                } else {
                    out.push_back({code, f.rings, 0});
                }
            }
            total += hits;
            if (hits)
                log_(entry.ident + ": " + std::to_string(hits) + " draped pavement polygons/lines from " +
                     path.substr(path.find_last_of('/') + 1));
        }
    }
    for (auto& u : unresolved_) once("unresolved:" + u, "could not find the file for " + u + "; judging it by name only");
    (void)total;
    return out;
}

}  // namespace zg
