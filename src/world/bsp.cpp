#include "world/bsp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

#include "math/trig.h"

namespace rf {
namespace {

constexpr fixed_t kOnLineEps = FRACUNIT / 32;  // ~0.03 map units
constexpr int kMaxDepth = 64;

enum PointClass : int { kFront = 0, kBack = 1, kOnLine = 2 };

#if defined(__SIZEOF_INT128__)
__extension__ typedef __int128 int128_t;
#endif

int classifyPoint(const Vec2& p, const Vec2& origin, const Vec2& dir) {
    const int64_t cross =
        static_cast<int64_t>(dir.x) * (p.y - origin.y) - static_cast<int64_t>(dir.y) * (p.x - origin.x);
    if (cross > kOnLineEps) return kFront;
    if (cross < -kOnLineEps) return kBack;
    return kOnLine;
}

// Side of a seg whose two endpoints lie on the partition. The seg's own
// front side is to its left; if it runs with the partition its front faces
// the partition's front child, and if it runs against the partition its
// front faces the back child. This keeps the two sides of a two-sided line
// in different children instead of dumping both into the front child.
int collinearSide(const Seg& s, const Vec2& dir) {
    const int64_t d = static_cast<int64_t>(s.b.x - s.a.x) * dir.x +
                      static_cast<int64_t>(s.b.y - s.a.y) * dir.y;
    return d >= 0 ? kFront : kBack;
}

int classifySeg(const Seg& s, const Vec2& origin, const Vec2& dir) {
    const int c0 = classifyPoint(s.a, origin, dir);
    const int c1 = classifyPoint(s.b, origin, dir);
    if (c0 == kOnLine && c1 == kOnLine) return collinearSide(s, dir);
    if (c0 != kBack && c1 != kBack) return kFront;
    if (c0 != kFront && c1 != kFront) return kBack;
    return 2;  // split
}

// Exact-ish 64-bit cross for vector pairs already in fixed point.
int64_t cross64(const Vec2& a, const Vec2& b) {
    return static_cast<int64_t>(a.x) * b.y - static_cast<int64_t>(a.y) * b.x;
}

int64_t sideOf(const Vec2& p, const Vec2& origin, const Vec2& dir) {
    return cross64(dir, p - origin);
}

// Clips a convex polygon to one side of an infinite line. keepFront selects
// cross(dir, p - origin) >= 0. Used to carve exact leaf regions out of the
// map bounds and to bound them by each leaf seg's front half-plane.
std::vector<Vec2> clipPolygon(const std::vector<Vec2>& polygon, const Vec2& origin, const Vec2& dir,
                              bool keepFront) {
    std::vector<Vec2> out;
    if (polygon.size() < 3) return out;
    out.reserve(polygon.size() + 4);

    auto inside = [keepFront](int64_t side) {
        return keepFront ? side >= 0 : side <= 0;
    };

    for (size_t i = 0; i < polygon.size(); ++i) {
        const Vec2& cur = polygon[i];
        const Vec2& next = polygon[(i + 1) % polygon.size()];
        const int64_t sc = sideOf(cur, origin, dir);
        const int64_t sn = sideOf(next, origin, dir);
        const bool curIn = inside(sc);
        const bool nextIn = inside(sn);

        if (curIn) out.push_back(cur);
        if (curIn == nextIn) continue;

        const int64_t denom = sc - sn;
        if (denom == 0) continue;
#if defined(__SIZEOF_INT128__)
        const int128_t scaled = (static_cast<int128_t>(sc) << FRACBITS) / denom;
        int64_t t = static_cast<int64_t>(scaled);
#else
        const double ratio = static_cast<double>(sc) / static_cast<double>(denom);
        int64_t t = static_cast<int64_t>(ratio * FRACUNIT);
#endif
        if (t < 0) t = 0;
        if (t > FRACUNIT) t = FRACUNIT;
        out.push_back(cur + (next - cur) * static_cast<fixed_t>(t));
    }
    return out;
}

// Removes consecutive duplicate vertices (including the wrap-around pair)
// that clipping can produce when an edge lies exactly on a clip line.
void removeDuplicates(std::vector<Vec2>& polygon) {
    if (polygon.empty()) return;
    std::vector<Vec2> out;
    out.reserve(polygon.size());
    for (const Vec2& p : polygon) {
        if (out.empty() || out.back().x != p.x || out.back().y != p.y) out.push_back(p);
    }
    if (out.size() > 1 && out.front().x == out.back().x && out.front().y == out.back().y) {
        out.pop_back();
    }
    polygon = std::move(out);
}

bool pointInPolygon(const std::vector<Vec2>& poly, const Vec2& p) {
    if (poly.size() < 3) return false;
    // Convex polygon: the point must be on the same side of every edge.
    bool hasPos = false;
    bool hasNeg = false;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2& a = poly[i];
        const Vec2& b = poly[(i + 1) % poly.size()];
        const int64_t c =
            static_cast<int64_t>(b.x - a.x) * (p.y - a.y) - static_cast<int64_t>(b.y - a.y) * (p.x - a.x);
        if (c > 0) hasPos = true;
        if (c < 0) hasNeg = true;
        if (hasPos && hasNeg) return false;
    }
    return true;
}

struct BuildContext {
    std::vector<Seg>& outSegs;
    std::vector<Subsector>& subs;
    std::vector<BspNode>& nodes;
    int splits = 0;
    int maxDepth = 0;
    int badLeaves = 0;

    // The leaf region comes from half-plane clipping along the BSP path,
    // then from the front half-plane of every seg that bounds the leaf.
    // (A subsector always lies on the front side of each of its segs.)
    // Clipping gives exact convex polygons, including corners formed by
    // partition lines that are not themselves segs.
    int makeLeaf(std::vector<Seg> list, std::vector<Vec2> region, int depth) {
        (void)depth;
        Subsector sub;
        sub.firstSeg = static_cast<int>(outSegs.size());
        sub.numSegs = static_cast<int>(list.size());
        if (!list.empty()) {
            const int sector = list.front().frontSector;
            sub.sector = sector;
            for (const Seg& s : list) {
                if (s.frontSector != sector) {
                    ++badLeaves;
                    break;
                }
            }
            for (const Seg& s : list) {
                const Vec2 segDir = s.b - s.a;
                if (segDir.x == 0 && segDir.y == 0) continue;
                region = clipPolygon(region, s.a, segDir, true);
                if (region.size() < 3) break;
            }
            removeDuplicates(region);
        }
        const int leafIndex = static_cast<int>(subs.size());
        for (Seg& s : list) outSegs.push_back(s);
        sub.polygon = std::move(region);
        subs.push_back(std::move(sub));
        return -1 - leafIndex;
    }

    int buildNode(std::vector<Seg> list, std::vector<Vec2> region, int depth) {
        maxDepth = std::max(maxDepth, depth);
        if (list.size() <= 1 || depth >= kMaxDepth) {
            return makeLeaf(std::move(list), std::move(region), depth);
        }

        // --- Pick the best partition -------------------------------------
        int bestIndex = -1;
        int bestScore = std::numeric_limits<int>::max();
        int bestFront = 0;
        int bestBack = 0;

        for (size_t ci = 0; ci < list.size(); ++ci) {
            const Seg& cand = list[ci];
            const Vec2 delta = cand.b - cand.a;
            const fixed_t len = length(delta);
            if (len == 0) continue;
            const Vec2 dir = delta / len;

            int front = 0;
            int back = 0;
            int split = 0;
            for (const Seg& s : list) {
                const int c = classifySeg(s, cand.a, dir);
                if (c == kFront) ++front;
                else if (c == kBack) ++back;
                else ++split;
            }
            if (front == 0 && back == 0) continue;
            if (split == 0 && (front == 0 || back == 0)) continue;
            const int score = split * 16 + std::abs(front - back);
            if (score < bestScore) {
                bestScore = score;
                bestIndex = static_cast<int>(ci);
                bestFront = front;
                bestBack = back;
            }
        }

        if (bestIndex < 0) return makeLeaf(std::move(list), std::move(region), depth);

        const Seg partition = list[static_cast<size_t>(bestIndex)];
        const Vec2 origin = partition.a;
        const Vec2 dir = normalized(partition.b - partition.a);

        // Each child's convex region is this node's region clipped by the
        // partition. Computed before the lists are moved into recursion.
        std::vector<Vec2> frontRegion = clipPolygon(region, origin, dir, true);
        std::vector<Vec2> backRegion = clipPolygon(region, origin, dir, false);

        const int nodeIndex = static_cast<int>(nodes.size());
        nodes.push_back(BspNode{});
        nodes[nodeIndex].origin = origin;
        nodes[nodeIndex].dir = dir;
        nodes[nodeIndex].partitionLine = partition.lineIndex;

        std::vector<Seg> frontList;
        std::vector<Seg> backList;
        frontList.reserve(static_cast<size_t>(bestFront + 2 * list.size()));
        backList.reserve(static_cast<size_t>(bestBack + 2 * list.size()));
        int actualSplits = 0;

        for (const Seg& s : list) {
            const int c = classifySeg(s, origin, dir);
            if (c == kFront) {
                frontList.push_back(s);
            } else if (c == kBack) {
                backList.push_back(s);
            } else {
                fixed_t t = 0;
                if (!Bsp::intersectionParameter(s.a, s.b, origin, dir, &t) || t <= 0 || t >= FRACUNIT) {
                    // Numerically parallel/degenerate: use the midpoint side.
                    const Vec2 mid = (s.a + s.b) / fromInt(2);
                    (classifyPoint(mid, origin, dir) == kBack ? backList : frontList).push_back(s);
                    continue;
                }
                const Vec2 p = s.a + (s.b - s.a) * t;
                const fixed_t uMid = s.u1 + mul(s.u2 - s.u1, t);
                Seg s1 = s;
                Seg s2 = s;
                s1.b = p;
                s1.u2 = uMid;
                s2.a = p;
                s2.u1 = uMid;
                const Vec2 mid1 = (s1.a + s1.b) / fromInt(2);
                const Vec2 mid2 = (s2.a + s2.b) / fromInt(2);
                (classifyPoint(mid1, origin, dir) == kBack ? backList : frontList).push_back(s1);
                (classifyPoint(mid2, origin, dir) == kBack ? backList : frontList).push_back(s2);
                ++actualSplits;
            }
        }

        if (frontList.empty() || backList.empty()) {
            // Defensive fallback; should not happen for valid map geometry.
            return makeLeaf(std::move(list), std::move(region), depth);
        }

        // Bounding box of this subtree, taken from the segs it contains.
        int minx = std::numeric_limits<int>::max();
        int miny = std::numeric_limits<int>::max();
        int maxx = std::numeric_limits<int>::min();
        int maxy = std::numeric_limits<int>::min();
        auto expand = [&](const Vec2& p) {
            const int x = toInt(p.x);
            const int y = toInt(p.y);
            minx = std::min(minx, x);
            miny = std::min(miny, y);
            maxx = std::max(maxx, x);
            maxy = std::max(maxy, y);
        };
        for (const Seg& s : list) {
            expand(s.a);
            expand(s.b);
        }
        nodes[nodeIndex].bbox[0] = minx;
        nodes[nodeIndex].bbox[1] = miny;
        nodes[nodeIndex].bbox[2] = maxx;
        nodes[nodeIndex].bbox[3] = maxy;

        splits += actualSplits;
        const int frontChild = buildNode(std::move(frontList), std::move(frontRegion), depth + 1);
        const int backChild = buildNode(std::move(backList), std::move(backRegion), depth + 1);
        nodes[nodeIndex].child[0] = frontChild;
        nodes[nodeIndex].child[1] = backChild;
        return nodeIndex;
    }
};

}  // namespace

int Bsp::pointSide(const Vec2& p, const Vec2& origin, const Vec2& dir) {
    const int64_t cross =
        static_cast<int64_t>(dir.x) * (p.y - origin.y) - static_cast<int64_t>(dir.y) * (p.x - origin.x);
    return cross >= 0 ? 0 : 1;
}

bool Bsp::intersectionParameter(const Vec2& a, const Vec2& b, const Vec2& origin, const Vec2& dir,
                                fixed_t* tOut) {
    const Vec2 e = b - a;
    const int64_t den = cross64(e, dir);
    if (den == 0) return false;
    const int64_t num = cross64(origin - a, dir);  // t = num/den
    // t is dimensionless; scale to Q16.16 with a 128-bit intermediate to keep
    // exactness even for very large coordinates.
#if defined(__SIZEOF_INT128__)
    const int128_t scaled = (static_cast<int128_t>(num) << FRACBITS);
    const int128_t t = scaled / den;
    if (t > std::numeric_limits<fixed_t>::max()) return false;
    if (t < std::numeric_limits<fixed_t>::min()) return false;
    *tOut = static_cast<fixed_t>(t);
#else
    const double t = static_cast<double>(num) / static_cast<double>(den);
    if (t > 2.0 || t < -1.0) return false;
    *tOut = static_cast<fixed_t>(t * FRACUNIT);
#endif
    return true;
}

bool Bsp::build(const World& world, std::string* error) {
    segs_.clear();
    subsectors_.clear();
    nodes_.clear();
    splits_ = 0;
    maxDepth_ = 0;
    badLeaves_ = 0;
    root_ = -1;

    if (world.lines.empty()) {
        if (error) *error = "BSP: world has no linedefs";
        return false;
    }

    std::vector<Seg> initial;
    initial.reserve(world.lines.size() * 2);
    for (size_t i = 0; i < world.lines.size(); ++i) {
        const Line& l = world.lines[i];
        const Vec2 a = world.lineStart(static_cast<int>(i));
        const Vec2 b = world.lineEnd(static_cast<int>(i));
        const fixed_t len = distance(a, b);
        if (len == 0) {
            if (error) *error = "BSP: zero-length linedef";
            return false;
        }
        Seg s;
        s.a = a;
        s.b = b;
        s.lineIndex = static_cast<int>(i);
        s.side = 0;
        s.frontSector = l.frontSector;
        s.backSector = l.backSector;
        s.u1 = 0;
        s.u2 = len;
        initial.push_back(s);

        if (l.backSector >= 0) {
            Seg back = s;
            back.a = b;
            back.b = a;
            back.side = 1;
            back.frontSector = l.backSector;
            back.backSector = l.frontSector;
            initial.push_back(back);
        }
    }

    if (initial.empty()) {
        if (error) *error = "BSP: no segs generated";
        return false;
    }

    // Seed region for leaf clipping. It must contain the whole map; solid
    // void inside these bounds simply becomes leaves with no sector.
    fixed_t minX = world.minX;
    fixed_t minY = world.minY;
    fixed_t maxX = world.maxX;
    fixed_t maxY = world.maxY;
    if (minX == maxX) {
        minX -= FRACUNIT;
        maxX += FRACUNIT;
    }
    if (minY == maxY) {
        minY -= FRACUNIT;
        maxY += FRACUNIT;
    }
    std::vector<Vec2> seed = {
        {minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY},
    };

    BuildContext ctx{segs_, subsectors_, nodes_};
    root_ = ctx.buildNode(std::move(initial), std::move(seed), 0);
    splits_ = ctx.splits;
    maxDepth_ = ctx.maxDepth;
    badLeaves_ = ctx.badLeaves;
    return true;
}

const Subsector* Bsp::subsectorForPoint(const Vec2& p) const {
    if (root_ < 0) {
        // No tree at all, or a tree that is a single leaf stored as
        // -1-leafIndex. An unbuilt BSP has no subsectors either way.
        const int leafOnly = -1 - root_;
        if (leafOnly >= 0 && leafOnly < static_cast<int>(subsectors_.size())) {
            const Subsector& sub = subsectors_[static_cast<size_t>(leafOnly)];
            if (sub.polygon.size() >= 3 && !pointInPolygon(sub.polygon, p)) return nullptr;
            return &sub;
        }
        return nullptr;
    }
    int idx = root_;
    while (idx >= 0) {
        const BspNode& n = nodes_[static_cast<size_t>(idx)];
        idx = n.child[pointSide(p, n.origin, n.dir)];
    }
    const int leaf = -1 - idx;
    if (leaf < 0 || leaf >= static_cast<int>(subsectors_.size())) return nullptr;
    const Subsector& sub = subsectors_[static_cast<size_t>(leaf)];
    if (!sub.polygon.empty() && !pointInPolygon(sub.polygon, p)) return nullptr;
    return &sub;
}

int Bsp::subsectorAtPoint(const Vec2& p) const {
    const Subsector* sub = subsectorForPoint(p);
    if (!sub) return -1;
    return static_cast<int>(sub - subsectors_.data());
}

int Bsp::sectorAtPoint(const Vec2& p) const {
    const Subsector* sub = subsectorForPoint(p);
    return sub ? sub->sector : -1;
}

}  // namespace rf
