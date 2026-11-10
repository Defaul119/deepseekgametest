// Binary Space Partitioning over 2D linedefs.
//
// Build:
//   * every linedef contributes one seg per side, so each subsector boundary
//     carries the sector on its own side;
//   * a partition is picked from the seg list with a DOOM-style score
//     (splits heavily penalised, then balance);
//   * segs crossing the partition are split at the exact intersection and
//     the halves inherit the original line's sector/edge data;
//   * leaves are subsectors: convex sets of segs sharing one front sector.
//
// Use:
//   * front-to-back traversal drives the portal renderer (near child first);
//   * back-to-front traversal exists for painter-style debugging and tests;
//   * point location maps a world point to its subsector and sector.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math/fixed.h"
#include "math/vec2.h"
#include "world/world.h"

namespace rf {

struct Seg {
    Vec2 a;
    Vec2 b;
    int lineIndex = -1;
    int side = 0;           // 0 = line front side, 1 = line back side
    int frontSector = -1;   // sector containing the subsector this seg bounds
    int backSector = -1;    // sector on the other side (-1 = solid)
    fixed_t u1 = 0;         // horizontal texture offset at a (map units)
    fixed_t u2 = 0;         // horizontal texture offset at b
};

struct Subsector {
    int firstSeg = 0;
    int numSegs = 0;
    int sector = -1;
    // Convex polygon of the leaf, ordered counter-clockwise in the
    // mathematical sense. Used for floor/ceiling span generation.
    std::vector<Vec2> polygon;
};

struct BspNode {
    Vec2 origin;
    Vec2 dir;               // unit-length partition direction
    int child[2] = {-1, -1};  // >= 0 node index; < 0 => subsector index = -1-v
    int bbox[4] = {0, 0, 0, 0};  // minx, miny, maxx, maxy in map units
    int partitionLine = -1; // debug: source linedef
};

class Bsp {
public:
    bool build(const World& world, std::string* error = nullptr);

    int root() const { return root_; }
    bool empty() const { return subsectors_.empty(); }

    const std::vector<Seg>& segs() const { return segs_; }
    const std::vector<Subsector>& subsectors() const { return subsectors_; }
    const std::vector<BspNode>& nodes() const { return nodes_; }

    int numSplits() const { return splits_; }
    int maxDepth() const { return maxDepth_; }
    int badLeaves() const { return badLeaves_; }

    // 0 = in front (left of the partition), 1 = behind (right).
    static int pointSide(const Vec2& p, const Vec2& origin, const Vec2& dir);

    // Segment vs. infinite line intersection parameter t along a->b in
    // [0,1]. Returns false if parallel/degenerate.
    static bool intersectionParameter(const Vec2& a, const Vec2& b, const Vec2& origin,
                                      const Vec2& dir, fixed_t* tOut);

    int subsectorAtPoint(const Vec2& p) const;
    int sectorAtPoint(const Vec2& p) const;  // -1 when outside all subsectors

    const Subsector* subsectorForPoint(const Vec2& p) const;

    // Iterative traversal. For leaves, fn(subsectorIndex) is called.
    // nodesVisitedOut, when provided, receives the number of internal nodes
    // popped (useful for the performance overlay). stopOut lets a caller end
    // the traversal early once its callback sets it (e.g. every column is
    // solid); it is checked after each leaf.
    template <typename LeafFn>
    void traverse(const Vec2& camera, bool frontToBack, LeafFn&& fn,
                  int* nodesVisitedOut = nullptr, bool* stopOut = nullptr) const {
        if (nodesVisitedOut) *nodesVisitedOut = 0;
        if (subsectors_.empty()) return;
        if (root_ < 0) {  // single-leaf tree
            fn(0);
            return;
        }
        std::vector<int> stack;
        stack.reserve(64);
        stack.push_back(root_);
        int visited = 0;
        while (!stack.empty()) {
            const int idx = stack.back();
            stack.pop_back();
            if (idx < 0) {
                fn(-1 - idx);
                if (stopOut && *stopOut) {
                    if (nodesVisitedOut) *nodesVisitedOut = visited;
                    return;
                }
                continue;
            }
            ++visited;
            const BspNode& n = nodes_[static_cast<size_t>(idx)];
            const int side = pointSide(camera, n.origin, n.dir);  // near side
            if (frontToBack) {
                stack.push_back(n.child[1 - side]);  // far visited later
                stack.push_back(n.child[side]);      // near visited first
            } else {
                stack.push_back(n.child[side]);
                stack.push_back(n.child[1 - side]);
            }
        }
        if (nodesVisitedOut) *nodesVisitedOut = visited;
    }

private:
    std::vector<Seg> segs_;
    std::vector<Subsector> subsectors_;
    std::vector<BspNode> nodes_;
    int root_ = -1;
    int splits_ = 0;
    int maxDepth_ = 0;
    int badLeaves_ = 0;
};

}  // namespace rf
