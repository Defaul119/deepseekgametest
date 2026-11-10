// Sector-based 2.5D world geometry.
//
// Vertices are in map units stored as Q16.16 fixed point (1 unit == 65536).
// A linedef is oriented: the front sector lies on its left side, i.e. for a
// point p and line origin a with direction d, p is in front when
//     cross(d, p - a) >= 0.
// The BSP builder relies on this convention and emits one seg per side.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/fixed.h"
#include "math/trig.h"
#include "math/vec2.h"

namespace rf {

struct Vertex {
    fixed_t x = 0;
    fixed_t y = 0;
};

struct Sector {
    fixed_t floorHeight = 0;
    fixed_t ceilHeight = 0;
    int floorTex = -1;
    int ceilTex = -1;
    int light = 160;   // 0..255
    int tag = 0;
    int special = 0;   // 1 = door

    // Door runtime data (not part of the text format; derived at load time).
    fixed_t closedCeil = 0;
    fixed_t openCeil = 0;
};

struct Line {
    int v1 = 0;
    int v2 = 0;
    int frontSector = -1;
    int backSector = -1;
    int texMiddle = -1;
    int texUpper = -1;
    int texLower = -1;
    int flags = 0;     // bit 0 = block everything, bit 1 = block monsters
    int special = 0;
    int tag = 0;
};

// Thing types used by the sample level and gameplay loader.
enum ThingType : int {
    kThingPlayerStart = 1,
    kThingImp = 2,
    kThingHealth = 3,
    kThingAmmo = 4,
};

struct Thing {
    fixed_t x = 0;
    fixed_t y = 0;
    angle_t angle = 0;
    int type = kThingImp;
};

struct World {
    std::vector<Vertex> vertices;
    std::vector<Sector> sectors;
    std::vector<Line> lines;
    std::vector<Thing> things;

    fixed_t minX = 0;
    fixed_t maxX = 0;
    fixed_t minY = 0;
    fixed_t maxY = 0;

    void computeBounds();
    bool validate(std::string* error) const;

    Vec2 lineStart(int lineIndex) const;
    Vec2 lineEnd(int lineIndex) const;
    fixed_t lineLength(int lineIndex) const;
    Vec2 lineDirection(int lineIndex) const;  // unit vector

    // Convenience for builders/tests.
    int addVertex(fixed_t x, fixed_t y);
    int addSector(const Sector& s);
    int addLine(const Line& l);
};

// Point on the left/front side of an oriented line? Returns true when the
// cross product is non-negative (inclusive).
bool pointInFrontOfLine(const Vec2& p, const Vec2& a, const Vec2& dir);

}  // namespace rf
