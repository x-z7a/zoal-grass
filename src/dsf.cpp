#include "dsf.h"

#include "platform.h"
#include "dsf_archive.h"


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
    if (a.empty() || (!b.empty() && b[0] == '/') || (b.size() > 1 && b[1] == ':')) return b;
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
        if (pos > size || n > size - pos) throw std::runtime_error("truncated DSF");
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
    size_t start, stop;
    bool wide = false;
    std::vector<float> scale;
    bool loaded = false;
    int nplanes = 0;
    std::vector<std::vector<uint32_t>> raw;
    std::vector<double> k, o;

    void load(const Reader& source) {
        if (loaded) return;
        Reader r{source.d, stop};
        uint32_t count = r.u32(start);
        int planes = r.u8(start + 4);
        if (count > 16000000 || planes < 2 || planes > 32 || uint64_t(count) * planes > 64000000) throw std::runtime_error("invalid DSF point pool size");
        size_t stride = wide ? 4 : 2;
        auto value = [&](size_t at) { return wide ? r.u32(at) : uint32_t(r.u16(at)); };
        size_t pos = start + 5;
        for (int p = 0; p < planes; ++p) {
            int enc = r.u8(pos++);
            std::vector<uint32_t> values;
            values.reserve(count);
            if (enc == 0 || enc == 1) {
                r.need(pos, size_t(count) * stride);
                for (uint32_t i = 0; i < count; ++i) values.push_back(value(pos + stride * i));
                pos += size_t(count) * stride;
            } else if (enc == 2 || enc == 3) {
                while (values.size() < count) {
                    int n = r.u8(pos++);
                    if ((n & 0x7f) == 0 || size_t(n & 0x7f) > count - values.size())
                        throw std::runtime_error("invalid DSF RLE run");
                    if (n & 0x80) {
                        uint32_t v = value(pos);
                        pos += stride;
                        for (int i = 0; i < (n & 0x7f); ++i) values.push_back(v);
                    } else {
                        for (int i = 0; i < n; ++i) values.push_back(value(pos + stride * size_t(i)));
                        pos += stride * size_t(n);
                    }
                }
                values.resize(count);
            } else {
                throw std::runtime_error("unknown plane encoding");
            }
            if (enc == 1 || enc == 3) {
                uint32_t acc = 0;
                for (auto& v : values) {
                    acc = (acc + v) & (wide ? 0xffffffffu : 0xffffu);
                    v = acc;
                }
            }
            raw.push_back(std::move(values));
        }
        if (raw.size() < 2 || scale.size() < 2 * raw.size()) throw std::runtime_error("point pool without coordinates");
        for (size_t p = 0; p < raw.size(); ++p) {
            k.push_back(scale[2 * p] / (wide ? 4294967295.0 : 65535.0));
            o.push_back(scale[2 * p + 1]);
        }
        nplanes = planes;
        loaded = true;
    }
    double coord(uint32_t i, size_t plane) const {
        if (plane >= raw.size() || i >= raw[plane].size()) throw std::runtime_error("pool index out of range");
        double value = k[plane] == 0 ? double(raw[plane][i]) : o[plane] + raw[plane][i] * k[plane];
        if (!std::isfinite(value)) throw std::runtime_error("non-finite DSF coordinate");
        return value;
    }
    std::array<double, 4> point(uint32_t i, bool curved, int control = 2) const {
        if (i >= raw[0].size()) throw std::runtime_error("pool index out of range");
        std::array<double, 4> p{coord(i, 0), coord(i, 1), 0, 0};
        if (curved) {
            p[2] = coord(i, control);
            p[3] = coord(i, control + 1);
        }
        return p;
    }
};

}  // namespace

DsfData parse_dsf(const uint8_t* data, size_t size,
                                           const std::function<bool(const std::string&)>& want,
                                           const std::function<bool(const std::string&)>& owns) {
    DsfData out;
    std::vector<uint8_t> unpacked;
    if (size >= 6 && std::memcmp(data, "7z\xbc\xaf\x27\x1c", 6) == 0) {
        unpacked = unpack_dsf(data, size);
        data = unpacked.data();
        size = unpacked.size();
    }
    if (size < 28 || std::memcmp(data, "XPLNEDSF", 8) != 0) throw std::runtime_error("not a DSF file");
    Reader r{data, size};
    size_t end = size - 16;   // MD5 footer
    auto& props = out.properties;
    std::vector<std::string> poly_defs, obj_defs, net_defs;
    std::vector<Atom> raw_pools, raw_roads;
    std::vector<std::vector<float>> scales, road_scales;
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
                if (s.id == "POLY" || s.id == "OBJT" || s.id == "NETW") {
                    auto items = strings(r, s.start, s.stop);
                    auto& defs = s.id == "POLY" ? poly_defs : s.id == "OBJT" ? obj_defs : net_defs;
                    defs.insert(defs.end(), items.begin(), items.end());
                }
        } else if (a.id == "GEOD") {
            for (auto& s : atoms(r, a.start, a.stop)) {
                if (s.id == "POOL") raw_pools.push_back(s);
                else if (s.id == "PO32") raw_roads.push_back(s);
                else if (s.id == "SCAL" || s.id == "SC32") {
                    std::vector<float> f;
                    for (size_t p = s.start; p + 4 <= s.stop; p += 4) f.push_back(r.f32(p));
                    (s.id == "SCAL" ? scales : road_scales).push_back(std::move(f));
                }
            }
        } else if (a.id == "CMDS") {
            cmd_start = a.start;
            cmd_stop = a.stop;
        }
    }
    if (cmd_stop == 0) return out;
    const Reader command{data, cmd_stop}; // commands may not escape into a following atom
    std::vector<Pool> pools, road_pools;
    for (size_t i = 0; i < raw_pools.size() && i < scales.size(); ++i) { Pool pl; pl.start = raw_pools[i].start; pl.stop = raw_pools[i].stop; pl.scale = scales[i]; pools.push_back(std::move(pl)); }
    for (size_t i = 0; i < raw_roads.size() && i < road_scales.size(); ++i) {
        Pool pl; pl.start = raw_roads[i].start; pl.stop = raw_roads[i].stop; pl.wide = true; pl.scale = road_scales[i]; road_pools.push_back(std::move(pl));
    }
    std::vector<std::string> filter_ids;
    for (auto& p : props)
        if (p.first == "sim/filter/aptid") filter_ids.push_back(p.second);
    std::vector<char> keep(poly_defs.size());
    for (size_t i = 0; i < poly_defs.size(); ++i) keep[i] = want && want(poly_defs[i]);

    size_t pos = cmd_start, stop = cmd_stop;
    Pool* pool = nullptr;
    Pool* road_pool = nullptr;
    uint32_t definition = 0, junction_offset = 0;
    int filter_index = -1, subtype = 0;
    auto owned = [&] { return filter_index < 0 || !owns ||
        (size_t(filter_index) < filter_ids.size() && owns(filter_ids[size_t(filter_index)])); };
    while (pos < stop) {
        int cmd = command.u8(pos++);
        if (cmd >= 12 && cmd <= 15) {
            bool keep_it = definition < keep.size() && keep[definition];
            int param = command.u16(pos);
            std::vector<std::vector<uint32_t>> windings;
            if (cmd == 12) {
                int m = command.u8(pos + 2);
                pos += 3;
                std::vector<uint32_t> w;
                for (int k = 0; k < m; ++k) w.push_back(command.u16(pos + 2 * size_t(k)));
                pos += 2 * size_t(m);
                windings.push_back(std::move(w));
            } else if (cmd == 13) {
                uint32_t a = command.u16(pos + 2), b = command.u16(pos + 4);
                pos += 6;
                std::vector<uint32_t> w;
                for (uint32_t k = a; k < b; ++k) w.push_back(k);
                windings.push_back(std::move(w));
            } else if (cmd == 14) {
                int count = command.u8(pos + 2);
                pos += 3;
                for (int c = 0; c < count; ++c) {
                    int m = command.u8(pos++);
                    std::vector<uint32_t> w;
                    for (int k = 0; k < m; ++k) w.push_back(command.u16(pos + 2 * size_t(k)));
                    pos += 2 * size_t(m);
                    windings.push_back(std::move(w));
                }
            } else {
                int count = command.u8(pos + 2);
                pos += 3;
                std::vector<uint32_t> idx;
                for (int k = 0; k <= count; ++k) idx.push_back(command.u16(pos + 2 * size_t(k)));
                pos += 2 * size_t(count + 1);
                for (int c = 0; c < count; ++c) {
                    std::vector<uint32_t> w;
                    for (uint32_t k = idx[size_t(c)]; k < idx[size_t(c) + 1]; ++k) w.push_back(k);
                    windings.push_back(std::move(w));
                }
            }
            if (!keep_it || !pool) continue;
            if (!owned()) continue;
            pool->load(r);
            bool facade = ends_with(lower(poly_defs[definition]), ".fac");
            bool curved = facade ? pool->nplanes == 4 || pool->nplanes == 5 :
                                  (pool->nplanes == 4 && param != kUvParam) || pool->nplanes == 8;
            int control = facade && pool->nplanes == 5 ? 3 : 2;
            if (curved && pool->raw.size() < 4) curved = false;
            DsfPolygon poly;
            poly.definition = poly_defs[definition];
            poly.param = param;
            poly.curved = curved;
            for (auto& w : windings) {
                std::vector<std::array<double, 4>> ring;
                ring.reserve(w.size());
                for (uint32_t i : w) ring.push_back(pool->point(i, curved, control));
                poly.windings.push_back(std::move(ring));
            }
            out.polygons.push_back(std::move(poly));
        } else if (cmd == 1) {
            uint16_t pi = command.u16(pos);
            pos += 2;
            pool = pi < pools.size() ? &pools[pi] : nullptr;
            road_pool = pi < road_pools.size() ? &road_pools[pi] : nullptr;
        } else if (cmd == 3) {
            definition = command.u8(pos);
            pos += 1;
        } else if (cmd == 4) {
            definition = command.u16(pos);
            pos += 2;
        } else if (cmd == 5) {
            definition = command.u32(pos);
            pos += 4;
        } else if (cmd == 2) {
            junction_offset = command.u32(pos); pos += 4;
        } else if (cmd == 6) {
            subtype = command.u8(pos++);
        } else if (cmd == 7 || cmd == 8) {
            uint32_t first = command.u16(pos), last = cmd == 7 ? first + 1 : command.u16(pos + 2);
            pos += cmd == 7 ? 2 : 4;
            if (pool && definition < obj_defs.size() && owned() && (!want || want(obj_defs[definition]))) {
                pool->load(r);
                for (uint32_t i = first; i < last; ++i)
                    out.objects.push_back({obj_defs[definition], pool->coord(i, 0), pool->coord(i, 1), pool->coord(i, 2)});
            }
        } else if (cmd >= 9 && cmd <= 11) {
            std::vector<uint32_t> indices;
            if (cmd == 10) {
                uint32_t a = command.u16(pos), b = command.u16(pos + 2); pos += 4;
                for (uint32_t i = a; i < b; ++i) indices.push_back(junction_offset + i);
            } else {
                int n = command.u8(pos++);
                for (int i = 0; i < n; ++i) {
                    indices.push_back(cmd == 11 ? command.u32(pos) : junction_offset + command.u16(pos));
                    pos += cmd == 11 ? 4 : 2;
                }
            }
            if (!road_pool || definition >= net_defs.size() || !owned()) continue;
            road_pool->load(r);
            DsfRoad chain{net_defs[definition], subtype, {}};
            for (auto i : indices) {
                std::array<double, 4> q{road_pool->coord(i, 0), road_pool->coord(i, 1),
                                        road_pool->coord(i, 2), road_pool->coord(i, 3)};
                double padding = 0;
                if (road_pool->nplanes >= 7) {
                    padding = approx_metres({q[1], q[0]}, {road_pool->coord(i, 5), road_pool->coord(i, 4)});
                    if (!std::isfinite(padding) || padding > 10000) throw std::runtime_error("invalid legacy road control");
                    chain.curve_padding_m = std::max(chain.curve_padding_m, padding);
                }
                chain.points.push_back(q);
                if (q[3] != 0 && chain.points.size() > 1) {
                    out.roads.push_back(chain);
                    // An interior junction ends this chain and starts the next one.
                    chain.points.assign(1, q);
                    chain.curve_padding_m = padding;
                }
            }
        } else if (cmd == 25 || cmd == 28 || cmd == 31) {
            pos += 4;
        } else if (cmd == 23 || cmd == 26 || cmd == 29) {
            pos += 1 + 2 * size_t(command.u8(pos));
        } else if (cmd == 24 || cmd == 27 || cmd == 30) {
            pos += 1 + 4 * size_t(command.u8(pos));
        } else if (cmd == 17) {
            pos += 1;
        } else if (cmd == 16) {
        } else if (cmd == 18) {
            pos += 9;
        } else if (cmd == 32 || cmd == 33 || cmd == 34) {
            size_t sz = cmd == 32 ? 1 : cmd == 33 ? 2 : 4;
            size_t length = sz == 1 ? command.u8(pos) : sz == 2 ? command.u16(pos) : command.u32(pos);
            size_t body = pos + sz;
            if (length >= 6 && command.u16(body) == 1) filter_index = command.i32(body + 2);
            command.need(body, length);
            pos = body + length;
        } else {
            throw std::runtime_error("unknown DSF command " + std::to_string(cmd));
        }
    }
    if (pos != stop) throw std::runtime_error("truncated DSF command");
    return out;
}

std::vector<DsfPolygon> parse_dsf_polygons(const uint8_t* data, size_t size,
        const std::function<bool(const std::string&)>& want, const std::function<bool(const std::string&)>& owns) {
    return parse_dsf(data, size, want, owns).polygons;
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
    std::vector<std::string> skipped;
    scenery_packs_ = pack_dirs(xp_root_, &skipped);
    for (auto& folder : skipped) once("virtual:" + folder, "not reading " + folder + " (streaming/virtual file system)");
    // Base meshes are below custom overlays; only the first base tile encountered is effective.
    std::string global = join(xp_root_, "Global Scenery");
    std::set<std::string> disabled;
    std::ifstream ini(fs_path(join(xp_root_, "Custom Scenery/scenery_packs.ini")));
    std::string setting;
    while (std::getline(ini, setting)) {
        const std::string prefix = "SCENERY_PACK_DISABLED ";
        if (setting.rfind(prefix, 0) != 0) continue;
        std::string path = setting.substr(prefix.size());
        size_t first = path.find_first_not_of(" \t"), last = path.find_last_not_of(" \t\r/");
        if (first != std::string::npos && last >= first) disabled.insert(normpath(join(xp_root_, path.substr(first, last - first + 1))));
    }
    auto globals = on_virtual_fs(global) ? std::vector<std::string>{} : list_dir(global);
    std::sort(globals.begin(), globals.end());
    for (auto& name : globals) {
        if (name == "Global Airports") continue;
        std::string folder = join(global, name);
        if (!disabled.count(normpath(folder)) && !on_virtual_fs(folder) && std::find(scenery_packs_.begin(), scenery_packs_.end(), folder) == scenery_packs_.end())
            scenery_packs_.push_back(folder);
    }
    std::vector<std::string> folders = scenery_packs_;
    std::string base = join(xp_root_, "Resources/default scenery");
    std::vector<std::string> defaults;
    for (auto& n : (on_virtual_fs(base) ? std::vector<std::string>{} : list_dir(base)))
        if (!on_virtual_fs(join(base, n))) defaults.push_back(join(base, n));
    std::sort(defaults.begin(), defaults.end());
    folders.insert(folders.end(), defaults.begin(), defaults.end());
    for (auto& folder : folders) {
        if (on_virtual_fs(join(folder, "library.txt"))) continue;
        std::ifstream in(fs_path(join(folder, "library.txt")));
        if (!in) continue;
        std::string line;
        while (std::getline(in, line)) {
            if (line.size() < 8) continue;
            std::istringstream ss(line);
            std::string first;
            ss >> first;
            std::string ufirst = first;
            for (auto& c : ufirst) c = (char)std::toupper((unsigned char)c);
            if (ufirst.rfind("EXPORT", 0) != 0) continue;
            std::string tok;
            while (ss >> tok) {
                std::string lt = lower(tok);
                if (ends_with(lt, ".pol") || ends_with(lt, ".lin") || ends_with(lt, ".net") ||
                    ends_with(lt, ".obj") || ends_with(lt, ".agp") || ends_with(lt, ".fac")) {
                    std::string rest;
                    std::getline(ss, rest);
                    size_t a = rest.find_first_not_of(" \t"), b = rest.find_last_not_of(" \t\r");
                    if (a != std::string::npos) {
                        std::string virt = lower(slashes(tok));
                        auto& candidates = exports_[virt];
                        std::string real = normpath(join(folder, slashes(rest.substr(a, b - a + 1))));
                        if (std::find(candidates.begin(), candidates.end(), real) == candidates.end()) candidates.push_back(real);
                    }
                    break;
                }
            }
        }
    }
    library_ready_ = true;
}

std::vector<std::string> GroundPolygons::resolve_all(const std::string& path, const std::string& pack) const {
    std::vector<std::string> candidates;
    auto it = exports_.find(lower(slashes(path)));
    if (it != exports_.end()) {
        for (const auto& real : it->second)
            if (!on_virtual_fs(real) && is_file(real)) candidates.push_back(real);
    }
    if (candidates.empty()) {
        std::string local = normpath(join(pack, slashes(path)));
        if (!on_virtual_fs(local) && is_file(local)) candidates.push_back(local);
    }
    return candidates;
}

std::string GroundPolygons::resolve(const std::string& path, const std::string& pack) const {
    auto candidates = resolve_all(path, pack);
    return candidates.empty() ? std::string() : candidates.front();
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

// Bounding circles are deliberately conservative: they include every OBJ vertex and every AGP tile variant,
// independently of the chosen heading/LOD. AGP annotations may extend beyond their tile, so include those too.
double GroundPolygons::object_radius(const std::string& path, const std::string& pack) {
    auto key = std::make_pair(lower(path), pack);
    auto found = object_radii_.find(key);
    if (found != object_radii_.end()) return found->second;
    object_radii_[key] = 10.0;  // unresolved/cyclic art: a conservative fallback, with a diagnostic
    auto candidates = resolve_all(path, pack);
    if (candidates.size() > 1) {
        double radius = 0;
        for (const auto& real : candidates) radius = std::max(radius, object_radius(real, ""));
        return object_radii_[key] = radius;
    }
    std::string real = candidates.empty() ? std::string() : candidates.front();
    if (real.empty()) {
        once("object:" + path, "unresolved object " + path + "; using a 10 m keep-out radius");
        return 10.0;
    }
    std::ifstream in(fs_path(real));
    double radius = 0, pixels = 0, metres = 0;
    struct TileBounds { double l, b, r, t, ax, ay, extra = 0; };
    std::vector<TileBounds> tiles;
    std::vector<std::string> objects;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        double x, y, z;
        if (cmd == "VT" && ss >> x >> y >> z) radius = std::max(radius, std::hypot(x, z));
        else if (cmd == "TEXTURE_SCALE") ss >> pixels;
        else if (cmd == "TEXTURE_WIDTH") ss >> metres;
        else if (cmd == "OBJECT") { std::string obj; if (ss >> obj) objects.push_back(obj); }
        else if (cmd == "TILE") {
            TileBounds t{};
            if (ss >> t.l >> t.b >> t.r >> t.t) {
                t.ax = (t.l + t.r) / 2; t.ay = (t.b + t.t) / 2;
                tiles.push_back(t);
            }
        } else if (cmd == "ANCHOR_PT" && !tiles.empty()) ss >> tiles.back().ax >> tiles.back().ay;
        else if (cmd.rfind("OBJ_", 0) == 0 && !tiles.empty()) {
            int idx;
            if (!(ss >> x >> y >> z)) continue;
            double height;
            if (cmd == "OBJ_DELTA") ss >> height;
            if (ss >> idx && idx >= 0 && size_t(idx) < objects.size())
                tiles.back().extra = std::max(tiles.back().extra, object_radius(objects[size_t(idx)], dirname(real)));
        }
    }
    if (pixels > 0 && metres > 0) {
        for (const auto& t : tiles) {
            double dx = std::max(std::abs(t.l - t.ax), std::abs(t.r - t.ax));
            double dy = std::max(std::abs(t.b - t.ay), std::abs(t.t - t.ay));
            radius = std::max(radius, std::hypot(dx, dy) * metres / pixels + t.extra);
        }
    }
    if (!std::isfinite(radius) || radius <= 0 || radius > 10000) {
        once("bounds:" + path, "no usable footprint for " + path + "; using a 10 m keep-out radius");
        radius = 10;
    }
    return object_radii_[key] = radius;
}

// Return a symmetric envelope around the road centerline, and whether elevation is a stacking level.
std::pair<double, bool> GroundPolygons::road_width(const std::string& path, const std::string& pack, int subtype) {
    auto key = std::make_pair(lower(path), pack);
    auto found = road_widths_.find(key);
    if (found == road_widths_.end()) {
        auto candidates = resolve_all(path, pack);
        if (candidates.size() > 1) {
            // Bound all regional/random/backup exports, since the plugin cannot ask which art variant was chosen.
            std::map<int, std::pair<double, bool>> combined;
            for (const auto& real : candidates) {
                (void)road_width(real, "", subtype); // populate the concrete file's complete subtype table
                for (const auto& type : road_widths_.at({lower(real), ""})) {
                    auto& envelope = combined[type.first];
                    envelope.first = std::max(envelope.first, type.second.first);
                    envelope.second = envelope.second || type.second.second;
                }
            }
            road_widths_.emplace(key, std::move(combined));
            return road_width(path, pack, subtype);
        }
        std::ifstream in(fs_path(candidates.empty() ? std::string() : candidates.front()));
        std::map<int, double> widths, centers;
        std::map<int, std::vector<int>> choices;
        std::string line;
        int real = -1, virt = -1;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string cmd;
            ss >> cmd;
            double width;
            if (cmd == "ROAD_TYPE" && ss >> real >> width) widths[real] = width;
            else if (cmd == "ROAD_CENTER" && real >= 0 && ss >> width) centers[real] = width;
            else if (cmd == "ROAD_DRAPED") { int mode; ss >> mode >> virt; choices[virt]; }
            else if (cmd == "ROAD_DRAPE_CHOICE" && virt >= 0) {
                int type;
                if (ss >> width >> type) choices[virt].push_back(type);
            }
        }
        std::map<int, std::pair<double, bool>> result;
        for (const auto& w : widths) {
            double center = centers.count(w.first) ? centers[w.first] : w.second / 2;
            result[w.first] = {std::max(std::abs(center), std::abs(w.second - center)), false};
        }
        for (const auto& c : choices) {
            double half = 0;
            for (int type : c.second) {
                auto it = result.find(type);
                half = std::max(half, it == result.end() ? 10.0 : it->second.first);
            }
            result[c.first] = {half > 0 ? half : 10.0, true};
        }
        found = road_widths_.emplace(key, std::move(result)).first;
    }
    auto width = found->second.find(subtype);
    if (width == found->second.end()) {
        once("road:" + path + ":" + std::to_string(subtype), "unknown road width " + path + ":" +
             std::to_string(subtype) + "; using a 20 m corridor");
        return {10.0, false};
    }
    return width->second;
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

// DSF exclusions test original vertices, not the expanded pavement/footprint geometry.
bool contains(const Ring& ring, const LatLon& p) {
    bool inside = false;
    if (ring.size() < 3) return false;
    LatLon a = ring.back();
    for (const auto& b : ring) {
        double cross = (p.lon - a.lon) * (b.lat - a.lat) - (p.lat - a.lat) * (b.lon - a.lon);
        if (std::abs(cross) <= 1e-15 && p.lat >= std::min(a.lat,b.lat) && p.lat <= std::max(a.lat,b.lat) &&
            p.lon >= std::min(a.lon,b.lon) && p.lon <= std::max(a.lon,b.lon)) return true;
        if ((a.lat > p.lat) != (b.lat > p.lat) &&
            p.lon < (b.lon - a.lon) * (p.lat - a.lat) / (b.lat - a.lat) + a.lon) inside = !inside;
        a = b;
    }
    return inside;
}

Ring exclusion_ring(const std::string& value) {
    size_t semi = value.find(';');
    if (semi != std::string::npos) {
        std::string points = value.substr(semi + 1);
        std::replace(points.begin(), points.end(), ',', ' ');
        std::istringstream ss(points);
        Ring ring;
        double lon, lat;
        while (ss >> lon >> lat) ring.push_back({lat, lon});
        return ring;
    }
    std::string bounds = value;
    std::replace(bounds.begin(), bounds.end(), '/', ' ');
    std::istringstream ss(bounds);
    double w, s, e, n;
    if (!(ss >> w >> s >> e >> n) || w > e || s > n) return {};
    return {{s, w}, {s, e}, {n, e}, {n, w}};
}

// Flatten modern road curves to <0.25 m error. Expanding the road bands by that amount covers the error.
void road_curve(const XY& a, const XY& b, const XY& c, const XY& d, std::vector<XY>& out, int depth = 0) {
    auto distance = [&](XY q) {
        double dx = d.x - a.x, dy = d.y - a.y, len2 = dx * dx + dy * dy;
        double t = len2 > 0 ? std::clamp(((q.x - a.x) * dx + (q.y - a.y) * dy) / len2, 0.0, 1.0) : 0;
        return std::hypot(q.x - a.x - t * dx, q.y - a.y - t * dy);
    };
    if (depth >= 20 || std::max(distance(b), distance(c)) <= 0.25) { out.push_back(d); return; }
    auto mid = [](XY p, XY q) { return XY{(p.x + q.x) / 2, (p.y + q.y) / 2}; };
    XY ab = mid(a,b), bc = mid(b,c), cd = mid(c,d), abc = mid(ab,bc), bcd = mid(bc,cd), m = mid(abc,bcd);
    road_curve(a, ab, abc, m, out, depth + 1);
    road_curve(m, bcd, cd, d, out, depth + 1);
}

DsfPolygon road_line(const DsfRoad& road, bool draped) {
    DsfPolygon line;
    line.windings.emplace_back();
    if (road.points.empty()) return line;
    LocalProjection proj(road.points[0][1], road.points[0][0]);
    std::vector<XY> points, controls;
    for (const auto& p : road.points) {
        XY xy = proj.to_xy(p[1], p[0]);
        if (draped && p[3] == 0 && p[2] > 0.5) { controls.push_back(xy); continue; }
        if (!points.empty() && !controls.empty()) {
            XY a = points.back(), b = controls.front(), c = controls.back();
            if (controls.size() == 1) { // quadratic -> cubic
                b = {a.x + (b.x - a.x) * 2 / 3, a.y + (b.y - a.y) * 2 / 3};
                c = {xy.x + (c.x - xy.x) * 2 / 3, xy.y + (c.y - xy.y) * 2 / 3};
            }
            road_curve(a, b, c, xy, points);
        } else points.push_back(xy);
        controls.clear();
    }
    for (auto xy : points) {
        LatLon p = proj.to_latlon(xy.x, xy.y);
        line.windings[0].push_back({p.lon, p.lat, 0, 0});
    }
    return line;
}

std::string dsf_path(const std::string& pack, int lat, int lon) {
    auto fl10 = [](int v) { return int(std::floor(v / 10.0)) * 10; };
    char dir[32], file[32];
    std::snprintf(dir, sizeof dir, "%+03d%+04d", fl10(lat), fl10(lon));
    std::snprintf(file, sizeof file, "%+03d%+04d.dsf", lat, lon);
    return join(join(join(pack, "Earth nav data"), dir), file);
}

}  // namespace

const GroundPolygons::Tile& GroundPolygons::dsf_entries(const std::string& path, const std::string& pack) {
    static const Tile empty;
    long long fsize, mtime;
    if (on_virtual_fs(path) || !file_key(path, fsize, mtime)) return empty;
    std::pair<long long, long long> key{fsize, mtime};
    auto it = dsf_cache_.find(path);
    if (it != dsf_cache_.end() && it->second.first == key) return it->second.second;

    Tile tile;
    auto add = [&](SurfaceClass cls, std::vector<Ring> rings, bool line, const std::string& category, Ring anchors) {
        if (rings.empty()) return;
        Found f{cls, std::move(rings), 1e9, 1e9, -1e9, -1e9, line, category, std::move(anchors)};
        for (auto& r : f.rings)
            for (auto& q : r) {
                if (!std::isfinite(q.lat) || !std::isfinite(q.lon)) throw std::runtime_error("non-finite DSF coordinate");
                f.s = std::min(f.s, q.lat); f.n = std::max(f.n, q.lat);
                f.w = std::min(f.w, q.lon); f.e = std::max(f.e, q.lon);
            }
        tile.entries.push_back(std::move(f));
    };
    try {
        if (fsize == 0) throw std::runtime_error("empty file");
        MappedFile mf(path);
        if (!mf.data()) throw std::runtime_error("cannot map");
        auto owns = [&](const std::string& ident) {
            const AirportEntry* e = index_->find(ident);
            return e && dirname(dirname(index_->files[size_t(e->file)])) == pack;
        };
        auto want = [&](const std::string& def) {
            std::string l = lower(def);
            return ends_with(l, ".agb") || ends_with(l, ".ags") || ends_with(l, ".agp") ||
                   ends_with(l, ".fac") || ends_with(l, ".obj") ||
                   ((ends_with(l, ".pol") || ends_with(l, ".lin")) && classify(def, pack) >= 0);
        };
        DsfData data = parse_dsf(mf.data(), mf.size(), want, owns);
        for (const auto& prop : data.properties) {
            if (prop.first == "sim/overlay") tile.overlay = prop.second == "1";
            if (prop.first.rfind("sim/exclude_", 0) == 0) {
                Ring ring = exclusion_ring(prop.second);
                if (ring.size() >= 3) tile.exclusions.emplace_back(prop.first.substr(12), std::move(ring));
            }
        }
        for (auto& poly : data.polygons) {
            std::string name = lower(poly.definition);
            bool occupied = ends_with(name, ".agb") || ends_with(name, ".ags") || ends_with(name, ".fac");
            bool line = ends_with(name, ".lin");
            int cls = occupied ? PAVED : classify(poly.definition, pack);
            if (cls < 0) continue;
            Ring anchors;
            for (const auto& w : poly.windings)
                for (const auto& q : w) anchors.push_back({q[1], q[0]});
            // AGS/AGB are culled by object exclusions, facades by facade exclusions.
            std::string category = ends_with(name, ".fac") ? "fac" : occupied ? "obj" : line ? "lin" : "pol";
            auto rings = line ? line_bands(poly, half_width(poly.definition, pack)) : poly_rings(poly);
            add(SurfaceClass(cls), std::move(rings), line, category, std::move(anchors));
        }
        for (const auto& road : data.roads) {
            auto width = road_width(road.definition, pack, road.subtype);
            // Zero-width powerline types have no road surface. Elevated roads are conservatively masked too.
            if (width.first <= 0) continue;
            DsfPolygon line = road_line(road, width.second);
            Ring anchors;
            for (auto& p : road.points) anchors.push_back({p[1], p[0]});
            add(PAVED, line_bands(line, width.first + road.curve_padding_m + 0.25), true, "net", std::move(anchors));
        }
        for (const auto& object : data.objects) {
            double r = object_radius(object.definition, pack);
            LocalProjection proj(object.lat, object.lon);
            // A square enclosing the bounding circle is safe at every heading, including AGP tile rotations.
            Ring ring{proj.to_latlon(-r,-r), proj.to_latlon(r,-r), proj.to_latlon(r,r), proj.to_latlon(-r,r)};
            add(PAVED, {ring}, false, "obj", {{object.lat, object.lon}});
        }
    } catch (const std::exception& ex) {
        once("dsf:" + path, path + ": grass withheld for this tile; scenery mask not read (" + ex.what() + ")");
        tile = Tile{};
        tile.failed = true; // Withhold grass when an existing local tile cannot be interpreted safely.
    }
    dsf_cache_[path] = {key, std::move(tile)};
    dsf_order_.erase(std::remove(dsf_order_.begin(), dsf_order_.end(), path), dsf_order_.end());
    dsf_order_.push_back(path);
    while (dsf_order_.size() > kMaxCachedDsfs) {
        dsf_cache_.erase(dsf_order_.front());
        dsf_order_.erase(dsf_order_.begin());
    }
    return dsf_cache_.at(path).second;
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
    for (int lat = int(std::floor(bs)); lat <= int(std::floor(bn)); ++lat) {
        for (int lon = int(std::floor(bw)); lon <= int(std::floor(be)); ++lon) {
            std::vector<std::pair<std::string, Ring>> exclusions;
            for (const auto& pack : scenery_packs_) {
                std::string path = dsf_path(pack, lat, lon);
                if (on_virtual_fs(path) || !is_file(path)) continue;
                const Tile& tile = dsf_entries(path, pack);
                if (tile.failed) {
                    out.push_back({1, {{{double(lat), double(lon)}, {double(lat), double(lon + 1)},
                                       {double(lat + 1), double(lon + 1)}, {double(lat + 1), double(lon)}}}, 0});
                    break;
                }
                size_t hits = 0;
                for (const auto& f : tile.entries) {
                    if (!(f.s <= bn && f.n >= bs && f.w <= be && f.e >= bw)) continue;
                    bool excluded = false;
                    for (const auto& ex : exclusions) {
                        if (ex.first != f.category) continue;
                        for (const auto& p : f.anchors)
                            if (contains(ex.second, p)) { excluded = true; break; }
                        if (excluded) break;
                    }
                    if (excluded) continue;
                    ++hits;
                    int code = f.cls == DIRT ? 4 : f.cls == GRAVEL ? 5 : 1;
                    if (f.line) {
                        for (const auto& ring : f.rings) out.push_back({code, {ring}, 0});
                    } else out.push_back({code, f.rings, 0});
                }
                if (hits) log_(entry.ident + ": " + std::to_string(hits) + " pavement/road/occupied footprints from " + path);
                // An overlay's exclusions affect lower packs only, never its own features.
                exclusions.insert(exclusions.end(), tile.exclusions.begin(), tile.exclusions.end());
                if (!tile.overlay) break;
            }
        }
    }
    for (auto& u : unresolved_) once("unresolved:" + u, "could not find the file for " + u + "; judging it by name only");
    return out;
}

}  // namespace zg
