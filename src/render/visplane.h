// DOOM-style visplane manager for floors and ceilings.
//
// A visplane groups visible spans that share a flat key:
//   (plane height, flat texture, light level, floor/ceiling)
//
// Each visplane stores, for every screen column, one inclusive [top, bottom]
// interval (or -1 when the column is empty). Spans are added one column at a
// time. A new span is merged into a same-key plane when that column is empty
// or when the new span overlaps/touches the existing span; it is merged by
// extending the interval. When all same-key planes already hold a disjoint
// span in that column, a new visplane with the same key is allocated: this is
// the visplane "split" case, needed because a single visplane can represent
// only one interval per column.
//
// Rendering happens once per plane after the world pass. Distance comes from
// the classic perspective relation for a horizontal plane,
//
//     z = (eyeZ - planeZ) * focal / (screenY - horizon)
//
// and is constant for every pixel on a screen row, so each row gets one
// distance and a constant world-space step per column. This gives
// perspective-correct texture mapping without per-pixel divisions.
//
// A per-pixel depth buffer resolves the rare cases where two planes overlap
// on screen (different heights) and guarantees that a farther plane never
// overwrites nearer world geometry. Within one visplane every pixel is
// covered at most once because a column has exactly one interval.
#pragma once

#include <cstdint>
#include <vector>

#include "core/stats.h"
#include "math/fixed.h"
#include "math/vec2.h"
#include "render/framebuffer.h"
#include "render/texture.h"

namespace rf {

// Camera parameters needed by the flat rasteriser. Positions and heights are
// 16.16; screen coordinates are continuous pixels (column x has its centre at
// x + 0.5).
struct FlatCamera {
    Vec2 pos;
    Vec2 forward;
    Vec2 right;
    fixed_t eyeZ = 0;
    fixed_t focal = fromInt(320);
    fixed_t horizon = 0;
    int width = 0;
    int height = 0;
};

struct Visplane {
    fixed_t height = 0;
    int texture = -1;
    int light = 160;
    bool ceiling = false;
    int minx = 0;
    int maxx = -1;
    // Inclusive per-column span. -1 in top means "column not covered".
    std::vector<int16_t> top;
    std::vector<int16_t> bottom;
};

class VisplaneManager {
public:
    void beginFrame(int width, int height);

    // Adds one inclusive pixel span at column x. Returns the index of the
    // visplane that now owns it. Out-of-range or empty spans are ignored and
    // return -1.
    int addSpan(fixed_t height, int texture, int light, bool ceiling, int x, int top, int bottom);

    // Renders every active visplane once. depth is width*height Q16.16 world
    // distance, zero for "empty"; nearer pixels win.
    void render(Framebuffer& fb, const FlatCamera& camera, const TextureSet& textures,
                std::vector<fixed_t>& depth, Stats& stats);

    int planeCount() const { return activeCount_; }
    int columnsAdded() const { return columnsAdded_; }
    int mergedSpans() const { return mergedSpans_; }
    int splitCount() const { return splitCount_; }

    const std::vector<Visplane>& planes() const { return planes_; }

private:
    Visplane& activate(const Visplane& key);

    std::vector<Visplane> planes_;
    int activeCount_ = 0;
    int width_ = 0;
    int height_ = 0;
    int columnsAdded_ = 0;
    int mergedSpans_ = 0;
    int splitCount_ = 0;
};

}  // namespace rf
