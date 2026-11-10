#include "world/mapfile.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "math/trig.h"

namespace rf {
namespace {

std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream ss(line);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

bool parseDouble(const std::string& s, double* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    *out = v;
    return true;
}

bool parseInt(const std::string& s, int* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno != 0 || end == s.c_str() || *end != '\0') return false;
    if (v < INT32_MIN || v > INT32_MAX) return false;
    *out = static_cast<int>(v);
    return true;
}

fixed_t unitsToFixed(double units) {
    const double scaled = units * 65536.0;
    if (scaled >= static_cast<double>(FIXED_MAX)) return FIXED_MAX;
    if (scaled <= static_cast<double>(FIXED_MIN)) return FIXED_MIN;
    return static_cast<fixed_t>(std::llround(scaled));
}

std::string fixedToUnits(fixed_t v) {
    // Five decimal places is well below the 1/65536 fixed-point resolution.
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.5f", static_cast<double>(v) / 65536.0);
    std::string s(buf);
    // Trim trailing zeroes but keep at least one decimal digit.
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.push_back('0');
    return s;
}

struct PendingSector {
    double floor = 0;
    double ceil = 0;
    std::string floorTex;
    std::string ceilTex;
    int light = 160;
    int tag = 0;
    int special = 0;
    int sourceLine = 0;
};

struct PendingVertex {
    double x = 0;
    double y = 0;
};

struct PendingLine {
    int v1 = 0;
    int v2 = 0;
    int front = 0;
    int back = -1;
    std::string middle;
    std::string upper;
    std::string lower;
    int flags = 0;
    int special = 0;
    int tag = 0;
    int sourceLine = 0;
};

bool resolveTexture(const std::string& name, const TextureSet& textures, int* out,
                    int lineNo, std::string* error) {
    if (name == "-") {
        *out = -1;
        return true;
    }
    const int id = textures.find(name);
    if (id < 0) {
        if (error) {
            std::ostringstream os;
            if (lineNo > 0) os << "line " << lineNo << ": ";
            os << "unknown texture '" << name << "'";
            *error = os.str();
        }
        return false;
    }
    *out = id;
    return true;
}

}  // namespace

bool loadMapText(const std::string& text, const TextureSet& textures, World& out,
                 std::string* error) {
    std::map<int, PendingSector> sectors;
    std::map<int, PendingVertex> vertices;
    std::map<int, PendingLine> lines;
    std::vector<Thing> things;
    bool sawVersion = false;
    int version = 0;

    std::istringstream input(text);
    std::string raw;
    int lineNo = 0;
    while (std::getline(input, raw)) {
        ++lineNo;
        // Strip comments.
        size_t hash = raw.find('#');
        if (hash != std::string::npos) raw = raw.substr(0, hash);
        size_t slash = raw.find("//");
        if (slash != std::string::npos) raw = raw.substr(0, slash);
        const std::string line = trim(raw);
        if (line.empty()) continue;

        const std::vector<std::string> tok = tokenize(line);
        if (tok.empty()) continue;
        const std::string& kw = tok[0];

        auto err = [&](const std::string& msg) {
            if (error) {
                std::ostringstream os;
                os << "line " << lineNo << ": " << msg;
                *error = os.str();
            }
            return false;
        };

        if (kw == "version") {
            if (tok.size() != 2 || !parseInt(tok[1], &version)) return err("expected 'version N'");
            if (version != 1) return err("unsupported map version");
            sawVersion = true;
        } else if (kw == "sector") {
            if (tok.size() != 9) return err("sector needs 8 fields");
            PendingSector ps;
            int id = 0;
            if (!parseInt(tok[1], &id)) return err("bad sector id");
            if (!parseDouble(tok[2], &ps.floor) || !parseDouble(tok[3], &ps.ceil))
                return err("bad sector heights");
            ps.floorTex = tok[4];
            ps.ceilTex = tok[5];
            if (!parseInt(tok[6], &ps.light) || !parseInt(tok[7], &ps.tag) ||
                !parseInt(tok[8], &ps.special))
                return err("bad sector numeric field");
            if (sectors.count(id)) return err("duplicate sector id");
            ps.sourceLine = lineNo;
            sectors[id] = ps;
        } else if (kw == "vertex") {
            if (tok.size() != 4) return err("vertex needs 3 fields");
            PendingVertex pv;
            int id = 0;
            if (!parseInt(tok[1], &id)) return err("bad vertex id");
            if (!parseDouble(tok[2], &pv.x) || !parseDouble(tok[3], &pv.y))
                return err("bad vertex coordinates");
            if (vertices.count(id)) return err("duplicate vertex id");
            vertices[id] = pv;
        } else if (kw == "linedef") {
            if (tok.size() != 12) return err("linedef needs 11 fields");
            PendingLine pl;
            int id = 0;
            if (!parseInt(tok[1], &id)) return err("bad linedef id");
            if (!parseInt(tok[2], &pl.v1) || !parseInt(tok[3], &pl.v2) ||
                !parseInt(tok[4], &pl.front) || !parseInt(tok[5], &pl.back))
                return err("bad linedef indices");
            pl.middle = tok[6];
            pl.upper = tok[7];
            pl.lower = tok[8];
            if (!parseInt(tok[9], &pl.flags) || !parseInt(tok[10], &pl.special) ||
                !parseInt(tok[11], &pl.tag))
                return err("bad linedef numeric field");
            if (lines.count(id)) return err("duplicate linedef id");
            pl.sourceLine = lineNo;
            lines[id] = pl;
        } else if (kw == "thing") {
            if (tok.size() != 5) return err("thing needs 4 fields");
            Thing t;
            double x = 0;
            double y = 0;
            double angleDeg = 0;
            if (!parseDouble(tok[1], &x) || !parseDouble(tok[2], &y) ||
                !parseDouble(tok[3], &angleDeg) || !parseInt(tok[4], &t.type))
                return err("bad thing fields");
            t.x = unitsToFixed(x);
            t.y = unitsToFixed(y);
            t.angle = angleFromDegrees(static_cast<float>(angleDeg));
            things.push_back(t);
        } else {
            return err("unknown keyword '" + kw + "'");
        }
    }

    if (!sawVersion) {
        if (error) *error = "missing 'version' line";
        return false;
    }
    if (sectors.empty()) {
        if (error) *error = "map contains no sectors";
        return false;
    }
    if (vertices.empty()) {
        if (error) *error = "map contains no vertices";
        return false;
    }

    // Ids must be dense so they can be used as vector indices.
    auto checkDense = [&](size_t count, size_t expected, const char* what) -> bool {
        if (count != expected) {
            if (error) {
                std::ostringstream os;
                os << "non-contiguous " << what << " ids (expected " << expected << ", got " << count << ")";
                *error = os.str();
            }
            return false;
        }
        return true;
    };
    const int maxSector = sectors.rbegin()->first;
    const int maxVertex = vertices.rbegin()->first;
    const int maxLine = lines.empty() ? -1 : lines.rbegin()->first;
    if (!checkDense(sectors.size(), static_cast<size_t>(maxSector + 1), "sector")) return false;
    if (!checkDense(vertices.size(), static_cast<size_t>(maxVertex + 1), "vertex")) return false;
    if (!checkDense(lines.size(), static_cast<size_t>(maxLine + 1), "linedef")) return false;

    out = World{};
    out.sectors.resize(static_cast<size_t>(maxSector + 1));
    for (auto& [id, ps] : sectors) {
        Sector& s = out.sectors[static_cast<size_t>(id)];
        s.floorHeight = unitsToFixed(ps.floor);
        s.ceilHeight = unitsToFixed(ps.ceil);
        s.light = ps.light;
        s.tag = ps.tag;
        s.special = ps.special;
        s.closedCeil = s.ceilHeight;
        // Doors rise to 112 units above their floor. Gameplay animates
        // ceilHeight from closedCeil towards openCeil when the door is used.
        s.openCeil = s.floorHeight + fromInt(112);
        if (!resolveTexture(ps.floorTex, textures, &s.floorTex, ps.sourceLine, error)) return false;
        if (!resolveTexture(ps.ceilTex, textures, &s.ceilTex, ps.sourceLine, error)) return false;
    }

    out.vertices.resize(static_cast<size_t>(maxVertex + 1));
    for (auto& [id, pv] : vertices) {
        out.vertices[static_cast<size_t>(id)] = Vertex{unitsToFixed(pv.x), unitsToFixed(pv.y)};
    }

    out.lines.resize(static_cast<size_t>(maxLine + 1));
    for (auto& [id, pl] : lines) {
        Line& l = out.lines[static_cast<size_t>(id)];
        l.v1 = pl.v1;
        l.v2 = pl.v2;
        l.frontSector = pl.front;
        l.backSector = pl.back;
        l.flags = pl.flags;
        l.special = pl.special;
        l.tag = pl.tag;
        if (!resolveTexture(pl.middle, textures, &l.texMiddle, pl.sourceLine, error)) return false;
        if (!resolveTexture(pl.upper, textures, &l.texUpper, pl.sourceLine, error)) return false;
        if (!resolveTexture(pl.lower, textures, &l.texLower, pl.sourceLine, error)) return false;
    }
    out.things = std::move(things);
    out.computeBounds();
    return out.validate(error);
}

bool loadMap(const std::string& path, const TextureSet& textures, World& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open map file: " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return loadMapText(ss.str(), textures, out, error);
}

bool saveMap(const std::string& path, const World& world, const TextureSet& textures,
             std::string* error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write map file: " + path;
        return false;
    }
    out << "# retro-fps level\nversion 1\n\n";
    for (size_t i = 0; i < world.sectors.size(); ++i) {
        const Sector& s = world.sectors[i];
        out << "sector " << i << ' ' << fixedToUnits(s.floorHeight) << ' '
            << fixedToUnits(s.ceilHeight) << ' '
            << (s.floorTex >= 0 ? textures.name(s.floorTex) : "-") << ' '
            << (s.ceilTex >= 0 ? textures.name(s.ceilTex) : "-") << ' ' << s.light << ' ' << s.tag
            << ' ' << s.special << '\n';
    }
    out << '\n';
    for (size_t i = 0; i < world.vertices.size(); ++i) {
        out << "vertex " << i << ' ' << fixedToUnits(world.vertices[i].x) << ' '
            << fixedToUnits(world.vertices[i].y) << '\n';
    }
    out << '\n';
    for (size_t i = 0; i < world.lines.size(); ++i) {
        const Line& l = world.lines[i];
        out << "linedef " << i << ' ' << l.v1 << ' ' << l.v2 << ' ' << l.frontSector << ' '
            << l.backSector << ' ' << (l.texMiddle >= 0 ? textures.name(l.texMiddle) : "-") << ' '
            << (l.texUpper >= 0 ? textures.name(l.texUpper) : "-") << ' '
            << (l.texLower >= 0 ? textures.name(l.texLower) : "-") << ' ' << l.flags << ' '
            << l.special << ' ' << l.tag << '\n';
    }
    out << '\n';
    for (const Thing& t : world.things) {
        out << "thing " << fixedToUnits(t.x) << ' ' << fixedToUnits(t.y) << ' '
            << angleToDegrees(t.angle) << ' ' << t.type << '\n';
    }
    if (!out.good()) {
        if (error) *error = "write error";
        return false;
    }
    return true;
}

}  // namespace rf
