#include "world/world.h"

#include <sstream>

#include "math/trig.h"

namespace rf {

void World::computeBounds() {
    if (vertices.empty()) {
        minX = maxX = minY = maxY = 0;
        return;
    }
    minX = maxX = vertices[0].x;
    minY = maxY = vertices[0].y;
    for (const Vertex& v : vertices) {
        if (v.x < minX) minX = v.x;
        if (v.x > maxX) maxX = v.x;
        if (v.y < minY) minY = v.y;
        if (v.y > maxY) maxY = v.y;
    }
}

bool World::validate(std::string* error) const {
    auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    if (sectors.empty()) return fail("map has no sectors");
    if (lines.empty()) return fail("map has no linedefs");

    for (size_t i = 0; i < vertices.size(); ++i) {
        if (vertices[i].x == 0 && vertices[i].y == 0) {
            // Not an error, just noting zero coordinates are legal.
        }
    }

    for (size_t i = 0; i < lines.size(); ++i) {
        const Line& l = lines[i];
        if (l.v1 < 0 || l.v2 < 0 || l.v1 >= static_cast<int>(vertices.size()) ||
            l.v2 >= static_cast<int>(vertices.size())) {
            std::ostringstream os;
            os << "linedef " << i << " references an invalid vertex";
            return fail(os.str());
        }
        if (l.v1 == l.v2) {
            std::ostringstream os;
            os << "linedef " << i << " has identical endpoints";
            return fail(os.str());
        }
        if (l.frontSector < 0 || l.frontSector >= static_cast<int>(sectors.size())) {
            std::ostringstream os;
            os << "linedef " << i << " has an invalid front sector";
            return fail(os.str());
        }
        if (l.backSector >= static_cast<int>(sectors.size())) {
            std::ostringstream os;
            os << "linedef " << i << " has an invalid back sector";
            return fail(os.str());
        }
        if (l.backSector == l.frontSector) {
            std::ostringstream os;
            os << "linedef " << i << " has the same front and back sector";
            return fail(os.str());
        }
        if (lineLength(static_cast<int>(i)) == 0) {
            std::ostringstream os;
            os << "linedef " << i << " has zero length";
            return fail(os.str());
        }
    }

    for (size_t i = 0; i < sectors.size(); ++i) {
        const Sector& s = sectors[i];
        if (s.floorHeight > s.ceilHeight) {
            std::ostringstream os;
            os << "sector " << i << " has floor above ceiling";
            return fail(os.str());
        }
        if (s.floorTex < 0 || s.ceilTex < 0) {
            std::ostringstream os;
            os << "sector " << i << " is missing a floor/ceiling texture";
            return fail(os.str());
        }
    }
    return true;
}

Vec2 World::lineStart(int lineIndex) const {
    const Vertex& v = vertices[static_cast<size_t>(lines[static_cast<size_t>(lineIndex)].v1)];
    return {v.x, v.y};
}

Vec2 World::lineEnd(int lineIndex) const {
    const Vertex& v = vertices[static_cast<size_t>(lines[static_cast<size_t>(lineIndex)].v2)];
    return {v.x, v.y};
}

fixed_t World::lineLength(int lineIndex) const {
    return distance(lineStart(lineIndex), lineEnd(lineIndex));
}

Vec2 World::lineDirection(int lineIndex) const {
    return normalized(lineEnd(lineIndex) - lineStart(lineIndex));
}

int World::addVertex(fixed_t x, fixed_t y) {
    vertices.push_back(Vertex{x, y});
    return static_cast<int>(vertices.size()) - 1;
}

int World::addSector(const Sector& s) {
    sectors.push_back(s);
    return static_cast<int>(sectors.size()) - 1;
}

int World::addLine(const Line& l) {
    lines.push_back(l);
    return static_cast<int>(lines.size()) - 1;
}

bool pointInFrontOfLine(const Vec2& p, const Vec2& a, const Vec2& dir) {
    const int64_t cross =
        static_cast<int64_t>(dir.x) * (p.y - a.y) - static_cast<int64_t>(dir.y) * (p.x - a.x);
    return cross >= 0;
}

}  // namespace rf
