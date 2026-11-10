#include "render/renderer.h"

#include <algorithm>
#include <cstdint>

namespace rf {
namespace {

constexpr fixed_t kHalfPixel = FRACUNIT / 2;

// The renderer keeps projected screen coordinates in 64-bit Q16.16. Near-plane
// clipping can leave endpoints far outside the viewport (a wall crossing the
// camera plane), and 32-bit screen coordinates would saturate there, bending
// the projected edge. Columns are clamped to the framebuffer later.
// (Renderer::ScreenPt is declared in the header.)

int64_t ceilFixed(int64_t v) {
    if (v >= 0) return (v + FRACUNIT - 1) / FRACUNIT;
    return -((-v) / FRACUNIT);
}

int64_t floorFixed(int64_t v) {
    if (v >= 0) return v / FRACUNIT;
    return -(((-v) + FRACUNIT - 1) / FRACUNIT);
}

// Interpolation parameter t = num/den in Q16, clamped to [0,1].
fixed_t ratioFixed(int64_t num, int64_t den) {
    if (den == 0) return 0;
    if (den < 0) {
        num = -num;
        den = -den;
    }
    const int64_t scaled = (num << FRACBITS) / den;
    if (scaled <= 0) return 0;
    if (scaled >= FRACUNIT) return FRACUNIT;
    return static_cast<fixed_t>(scaled);
}

int64_t lerp64(int64_t a, int64_t b, fixed_t t) {
    return a + ((b - a) * static_cast<int64_t>(t)) / FRACUNIT;
}

int64_t clampToInt64(int64_t v, int64_t lo, int64_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

void drawDebugLine(Framebuffer& fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (int guard = 0; guard < 8192; ++guard) {
        if (x0 >= 0 && x0 < fb.width() && y0 >= 0 && y0 < fb.height()) {
            fb.putPixel(x0, y0, color);
        }
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

int toScreenInt(int64_t fixedPixels) {
    const int64_t v = floorFixed(fixedPixels);
    if (v < -4096) return -4096;
    if (v > 4096) return 4096;
    return static_cast<int>(v);
}

}  // namespace

void Renderer::render(const World& world, const Bsp& bsp, const TextureSet& textures,
                      const CameraView& view, Framebuffer& fb, Stats& stats, bool debugWalls) {
    world_ = &world;
    bsp_ = &bsp;
    textures_ = &textures;
    fb_ = &fb;
    stats_ = &stats;
    view_ = view;
    debugWalls_ = debugWalls;
    width_ = fb.width();
    height_ = fb.height();
    if (width_ <= 0 || height_ <= 0 || textures.count() == 0) return;

    focal_ = clampv(view.focal, fromInt(32), fromInt(2000));
    forward_ = angleVector(view.yaw);
    right_ = rightVector(view.yaw);
    horizon_ = fromInt(height_) / 2 + view.horizonOffset;
    screenCenter_ = fromInt(width_) / 2;
    nearZ_ = fromInt(4);
    defaultWallTexture_ = textures.find("STONE");
    if (defaultWallTexture_ < 0) defaultWallTexture_ = 0;

    clipTop_.assign(static_cast<size_t>(width_), 0);
    clipBot_.assign(static_cast<size_t>(width_), height_ - 1);
    depth_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_));
    std::fill(depth_.begin(), depth_.end(), 0);
    solidColumns_ = 0;
    visplanes_.beginFrame(width_, height_);
    debugLines_.clear();

    int nodesVisited = 0;
    bool stop = false;
    bsp.traverse(view_.pos, true,
                 [&](int leaf) {
                     renderLeaf(leaf);
                     if (solidColumns_ >= width_) stop = true;
                 },
                 &nodesVisited, &stop);
    stats.bspNodesVisited = nodesVisited;

    FlatCamera flat;
    flat.pos = view_.pos;
    flat.forward = forward_;
    flat.right = right_;
    flat.eyeZ = view_.eyeZ;
    flat.focal = focal_;
    flat.horizon = horizon_;
    flat.width = width_;
    flat.height = height_;
    visplanes_.render(fb, flat, textures, depth_, stats);

    stats.visplanesCreated = visplanes_.planeCount();
    stats.visplaneColumns = visplanes_.columnsAdded();

    int ranges = 0;
    bool inSolid = false;
    for (int x = 0; x < width_; ++x) {
        const bool solid = clipTop_[static_cast<size_t>(x)] >
                           clipBot_[static_cast<size_t>(x)];
        if (solid && !inSolid) ++ranges;
        inSolid = solid;
    }
    stats.solidRanges = ranges;

    for (const DebugLine& line : debugLines_) {
        drawDebugLine(fb, line.x0, line.y0, line.x1, line.y1, line.color);
    }
}

void Renderer::renderLeaf(int leaf) {
    if (!world_ || !bsp_ || !stats_) return;
    if (solidColumns_ >= width_) return;
    const std::vector<Subsector>& subs = bsp_->subsectors();
    if (leaf < 0 || leaf >= static_cast<int>(subs.size())) return;
    const Subsector& sub = subs[static_cast<size_t>(leaf)];
    if (sub.sector < 0 || sub.sector >= static_cast<int>(world_->sectors.size())) return;
    const Sector& sector = world_->sectors[static_cast<size_t>(sub.sector)];

    if (sector.ceilHeight > sector.floorHeight && sub.polygon.size() >= 3) {
        addFlatSpans(sector.floorHeight, sector.floorTex, sector.light, false, sub.polygon);
        addFlatSpans(sector.ceilHeight, sector.ceilTex, sector.light, true, sub.polygon);
    }

    const int first = sub.firstSeg;
    const int count = sub.numSegs;
    const std::vector<Seg>& segs = bsp_->segs();
    for (int i = first; i < first + count && i < static_cast<int>(segs.size()); ++i) {
        if (solidColumns_ >= width_) break;
        renderSeg(segs[static_cast<size_t>(i)]);
    }
    ++stats_->bspLeavesRendered;
}

int64_t Renderer::pointDepth64(const Vec2& p) const {
    const int64_t dx = static_cast<int64_t>(p.x) - static_cast<int64_t>(view_.pos.x);
    const int64_t dy = static_cast<int64_t>(p.y) - static_cast<int64_t>(view_.pos.y);
    return (dx * static_cast<int64_t>(forward_.x) + dy * static_cast<int64_t>(forward_.y)) >>
           FRACBITS;
}

int64_t Renderer::pointLateral64(const Vec2& p) const {
    const int64_t dx = static_cast<int64_t>(p.x) - static_cast<int64_t>(view_.pos.x);
    const int64_t dy = static_cast<int64_t>(p.y) - static_cast<int64_t>(view_.pos.y);
    return (dx * static_cast<int64_t>(right_.x) + dy * static_cast<int64_t>(right_.y)) >>
           FRACBITS;
}

// Perspective divide in 64-bit Q16.16. depth has already been clipped to the
// near plane by every caller, so the division is safe.
int64_t Renderer::projectX64(int64_t lateral, int64_t depth) const {
    if (depth <= 0) return 0;
    return static_cast<int64_t>(screenCenter_) +
           (lateral * static_cast<int64_t>(focal_)) / depth;
}

int64_t Renderer::projectY64(fixed_t height, int64_t depth) const {
    if (depth <= 0) return static_cast<int64_t>(horizon_);
    const int64_t rel = static_cast<int64_t>(view_.eyeZ) - static_cast<int64_t>(height);
    return static_cast<int64_t>(horizon_) +
           (rel * static_cast<int64_t>(focal_)) / depth;
}

void Renderer::addFlatSpans(fixed_t height, int texture, int light, bool ceiling,
                            const std::vector<Vec2>& polygon) {
    if (!textures_ || !stats_ || texture < 0 || texture >= textures_->count()) return;
    if (ceiling ? (height <= view_.eyeZ) : (height >= view_.eyeZ)) return;
    if (polygon.size() < 3) return;
    if (solidColumns_ >= width_) return;

    // Clip the leaf polygon to the near plane in world space.
    flatPoly_.clear();
    const size_t n = polygon.size();
    for (size_t i = 0; i < n; ++i) {
        const Vec2& cur = polygon[i];
        const Vec2& next = polygon[(i + 1) % n];
        const int64_t dc = pointDepth64(cur);
        const int64_t dn = pointDepth64(next);
        const bool inCur = dc >= nearZ_;
        const bool inNext = dn >= nearZ_;
        if (inCur) flatPoly_.push_back(cur);
        if (inCur != inNext) {
            const fixed_t t = ratioFixed(nearZ_ - dc, dn - dc);
            flatPoly_.push_back(cur + (next - cur) * t);
        }
    }
    if (flatPoly_.size() < 3) return;

    screenPoly_.clear();
    int64_t minX = INT64_MAX;
    int64_t maxX = INT64_MIN;
    for (const Vec2& p : flatPoly_) {
        // Clipped vertices are meant to lie exactly on the near plane, but the
        // fixed-point interpolation that created them can land a unit or two
        // behind it. Clamp rather than skip: dropping such a vertex would
        // collapse the convex polygon and leave a wedge of floor/ceiling
        // unrendered.
        const int64_t d = std::max<int64_t>(pointDepth64(p), static_cast<int64_t>(nearZ_));
        const int64_t lateral = pointLateral64(p);
        const int64_t sx = projectX64(lateral, d);
        const int64_t sy = projectY64(height, d);
        screenPoly_.push_back(ScreenPt{sx, sy});
        minX = std::min(minX, sx);
        maxX = std::max(maxX, sx);
    }
    if (screenPoly_.size() < 3) return;

    int64_t xStart = ceilFixed(minX - kHalfPixel);
    int64_t xEnd = floorFixed(maxX - kHalfPixel);
    xStart = std::max<int64_t>(xStart, 0);
    xEnd = std::min<int64_t>(xEnd, width_ - 1);
    if (xStart > xEnd) return;

    for (int64_t xi = xStart; xi <= xEnd; ++xi) {
        const int x = static_cast<int>(xi);
        const int ct = clipTop_[static_cast<size_t>(x)];
        const int cb = clipBot_[static_cast<size_t>(x)];
        if (ct > cb) continue;
        const int64_t xc = (xi << FRACBITS) + kHalfPixel;
        int64_t yMin = INT64_MAX;
        int64_t yMax = INT64_MIN;
        bool found = false;
        for (size_t i = 0; i < screenPoly_.size(); ++i) {
            const ScreenPt& a = screenPoly_[i];
            const ScreenPt& b = screenPoly_[(i + 1) % screenPoly_.size()];
            if (a.x == b.x) continue;  // vertical edge: adjacent edges cover it
            if ((a.x <= xc && xc <= b.x) || (b.x <= xc && xc <= a.x)) {
                const fixed_t t = ratioFixed(xc - a.x, b.x - a.x);
                const int64_t y = lerp64(a.y, b.y, t);
                yMin = std::min(yMin, y);
                yMax = std::max(yMax, y);
                found = true;
            }
        }
        if (!found || yMin > yMax) continue;
        int64_t top = ceilFixed(yMin - kHalfPixel);
        int64_t bottom = floorFixed(yMax - kHalfPixel);
        top = std::max<int64_t>(top, std::max(0, ct));
        bottom = std::min<int64_t>(bottom, std::min(height_ - 1, cb));
        if (top > bottom) continue;
        // The wall pass and the flat pass compute the shared boundary row from
        // different projections (interpolated seg endpoints vs. polygon edge
        // intersections), so fixed-point rounding can leave a one-pixel crack
        // at the seams. Overlap one row into the clip window; the depth buffer
        // keeps whatever the wall already drew there.
        if (top > std::max(0, ct)) --top;
        if (bottom < std::min(height_ - 1, cb)) ++bottom;
        visplanes_.addSpan(height, texture, light, ceiling, x, static_cast<int>(top),
                           static_cast<int>(bottom));
    }
}

void Renderer::renderSeg(const Seg& seg) {
    if (!world_ || !textures_ || !stats_) return;
    if (seg.lineIndex < 0 || seg.lineIndex >= static_cast<int>(world_->lines.size())) return;
    if (seg.frontSector < 0 || seg.frontSector >= static_cast<int>(world_->sectors.size())) return;
    const Line& line = world_->lines[static_cast<size_t>(seg.lineIndex)];
    const Sector& front = world_->sectors[static_cast<size_t>(seg.frontSector)];
    const Sector* back = nullptr;
    if (seg.backSector >= 0 && seg.backSector < static_cast<int>(world_->sectors.size())) {
        back = &world_->sectors[static_cast<size_t>(seg.backSector)];
    }

    ++stats_->wallsProcessed;

    // Front (left) side only. A seg whose front faces away belongs to the far
    // side of a two-sided line and is handled by its own subsector.
    const int64_t side =
        static_cast<int64_t>(seg.b.x - seg.a.x) * (view_.pos.y - seg.a.y) -
        static_cast<int64_t>(seg.b.y - seg.a.y) * (view_.pos.x - seg.a.x);
    if (side < 0) {
        ++stats_->wallsClippedOut;
        return;
    }

    Vec2 a = seg.a;
    Vec2 b = seg.b;
    fixed_t ua = seg.u1;
    fixed_t ub = seg.u2;
    int64_t da = pointDepth64(a);
    int64_t db = pointDepth64(b);
    if (da < nearZ_ && db < nearZ_) {
        ++stats_->wallsClippedOut;
        return;
    }
    if (da < nearZ_) {
        const fixed_t t = ratioFixed(nearZ_ - da, db - da);
        a = a + (b - a) * t;
        ua = ua + mul(ub - ua, t);
        da = nearZ_;
    } else if (db < nearZ_) {
        const fixed_t t = ratioFixed(nearZ_ - db, da - db);
        b = b + (a - b) * t;
        ub = ub + mul(ua - ub, t);
        db = nearZ_;
    }
    if (da < nearZ_ || db < nearZ_) return;
    if (a.x == b.x && a.y == b.y) {
        ++stats_->wallsClippedOut;
        return;
    }

    const fixed_t za = static_cast<fixed_t>(std::min<int64_t>(da, FIXED_MAX));
    const fixed_t zb = static_cast<fixed_t>(std::min<int64_t>(db, FIXED_MAX));
    const int64_t xa = projectX64(pointLateral64(a), da);
    const int64_t xb = projectX64(pointLateral64(b), db);
    if (xa == xb) {
        ++stats_->wallsClippedOut;
        return;
    }

    WallEnd e0;
    WallEnd e1;
    e0.z = za;
    e1.z = zb;
    e0.x = xa;
    e1.x = xb;
    e0.u = ua;
    e1.u = ub;
    e0.ceilFront = projectY64(front.ceilHeight, da);
    e0.floorFront = projectY64(front.floorHeight, da);
    e1.ceilFront = projectY64(front.ceilHeight, db);
    e1.floorFront = projectY64(front.floorHeight, db);
    if (back) {
        e0.ceilBack = projectY64(back->ceilHeight, da);
        e0.floorBack = projectY64(back->floorHeight, da);
        e1.ceilBack = projectY64(back->ceilHeight, db);
        e1.floorBack = projectY64(back->floorHeight, db);
    }

    if (e0.x > e1.x) std::swap(e0, e1);

    int64_t xStart = ceilFixed(e0.x - kHalfPixel);
    int64_t xEnd = floorFixed(e1.x - kHalfPixel);
    xStart = std::max<int64_t>(xStart, 0);
    xEnd = std::min<int64_t>(xEnd, width_ - 1);
    if (xStart > xEnd || e1.x == e0.x) {
        ++stats_->wallsClippedOut;
        return;
    }

    const int lightFactor = shadeFactor(front.light);
    const int32_t upperTex = line.texUpper >= 0 ? line.texUpper : defaultWallTexture_;
    const int32_t lowerTex = line.texLower >= 0 ? line.texLower : defaultWallTexture_;
    const int32_t middleTex = line.texMiddle >= 0 ? line.texMiddle : defaultWallTexture_;

    bool drewAny = false;
    for (int64_t xi = xStart; xi <= xEnd; ++xi) {
        const int x = static_cast<int>(xi);
        const int ct = clipTop_[static_cast<size_t>(x)];
        const int cb = clipBot_[static_cast<size_t>(x)];
        if (ct > cb) continue;

        const int64_t xc = (xi << FRACBITS) + kHalfPixel;
        const fixed_t t = ratioFixed(xc - e0.x, e1.x - e0.x);

        // Screen y is linear along the projected straight line, so it can be
        // interpolated directly. Depth and u are interpolated as 1/z and u/z
        // to be perspective-correct.
        const int64_t ceilF = lerp64(e0.ceilFront, e1.ceilFront, t);
        const int64_t floorF = lerp64(e0.floorFront, e1.floorFront, t);
        const int64_t ceilB = lerp64(e0.ceilBack, e1.ceilBack, t);
        const int64_t floorB = lerp64(e0.floorBack, e1.floorBack, t);

        const fixed_t iz0 = div(FRACUNIT, e0.z);
        const fixed_t iz1 = div(FRACUNIT, e1.z);
        fixed_t iz = iz0 + mul(t, iz1 - iz0);
        if (iz <= 0) continue;
        fixed_t z = div(FRACUNIT, iz);
        if (z <= 0) z = kHalfPixel;

        const fixed_t uoz0 = div(e0.u, e0.z);
        const fixed_t uoz1 = div(e1.u, e1.z);
        const fixed_t uoz = uoz0 + mul(t, uoz1 - uoz0);
        const fixed_t u = mul(uoz, z);

        if (back) {
            if (back->ceilHeight < front.ceilHeight) {
                drewAny |= drawWallPiece(x, z, u, static_cast<fixed_t>(ceilF),
                                         static_cast<fixed_t>(ceilB), front.ceilHeight,
                                         back->ceilHeight, upperTex, lightFactor);
            }
            if (back->floorHeight > front.floorHeight) {
                drewAny |= drawWallPiece(x, z, u, static_cast<fixed_t>(floorB),
                                         static_cast<fixed_t>(floorF), back->floorHeight,
                                         front.floorHeight, lowerTex, lightFactor);
            }
            int64_t openTop = ceilFixed(ceilB - kHalfPixel);
            int64_t openBot = floorFixed(floorB - kHalfPixel);
            openTop = clampToInt64(openTop, 0, height_ - 1);
            openBot = clampToInt64(openBot, 0, height_ - 1);
            setColumnInterval(x, std::max(ct, static_cast<int>(openTop)),
                              std::min(cb, static_cast<int>(openBot)));
        } else {
            drewAny |= drawWallPiece(x, z, u, static_cast<fixed_t>(ceilF),
                                     static_cast<fixed_t>(floorF), front.ceilHeight,
                                     front.floorHeight, middleTex, lightFactor);
            setColumnSolid(x);
        }
    }

    if (!drewAny) {
        // Either every column was already solid or the wall projected to no
        // visible rows. It still may have narrowed the portal interval above;
        // the counter records that no pixels were produced by this seg.
        ++stats_->wallsClippedOut;
    }

    if (debugWalls_) {
        const uint32_t wallColor = back ? colorRGB(90, 205, 255) : colorRGB(245, 215, 80);
        auto addEdge = [&](int64_t y0, int64_t y1) {
            debugLines_.push_back(DebugLine{toScreenInt(e0.x), toScreenInt(y0),
                                            toScreenInt(e1.x), toScreenInt(y1), wallColor});
        };
        if (!back) {
            addEdge(e0.ceilFront, e1.ceilFront);
            addEdge(e0.floorFront, e1.floorFront);
        } else {
            if (back->ceilHeight < front.ceilHeight) {
                addEdge(e0.ceilFront, e1.ceilFront);
                addEdge(e0.ceilBack, e1.ceilBack);
            }
            if (back->floorHeight > front.floorHeight) {
                addEdge(e0.floorBack, e1.floorBack);
                addEdge(e0.floorFront, e1.floorFront);
            }
            addEdge(e0.ceilBack, e0.floorBack);
            addEdge(e1.ceilBack, e1.floorBack);
        }
    }
}

bool Renderer::drawWallPiece(int x, fixed_t z, fixed_t u, fixed_t yTop, fixed_t yBot,
                             fixed_t worldTop, fixed_t worldBot, int texture,
                             int lightFactor) {
    if (!textures_ || !fb_ || !stats_) return false;
    if (texture < 0 || texture >= textures_->count()) return false;
    const fixed_t worldSpan = worldTop - worldBot;
    const int64_t ySpan = static_cast<int64_t>(yBot) - static_cast<int64_t>(yTop);
    if (worldSpan <= 0 || ySpan <= 0) return false;
    const Texture& tex = textures_->get(texture);
    if (tex.w <= 0 || tex.h <= 0) return false;

    const int ct = std::max(clipTop_[static_cast<size_t>(x)], 0);
    const int cb = std::min(clipBot_[static_cast<size_t>(x)], height_ - 1);
    if (ct > cb) return false;

    int64_t topRow = ceilFixed(static_cast<int64_t>(yTop) - kHalfPixel);
    int64_t botRow = floorFixed(static_cast<int64_t>(yBot) - kHalfPixel);
    topRow = std::max<int64_t>(topRow, ct);
    botRow = std::min<int64_t>(botRow, cb);
    if (topRow > botRow) return false;

    // Texture v is in map units, one texel per unit; vertical scale is
    // constant within a column because the wall is a vertical surface.
    const int64_t vStep = (static_cast<int64_t>(worldSpan) << FRACBITS) / ySpan;
    int64_t v = ((topRow * FRACUNIT + kHalfPixel) - static_cast<int64_t>(yTop)) * vStep /
                FRACUNIT;
    if (v < 0) v = 0;

    const uint32_t texX = static_cast<uint32_t>((static_cast<uint64_t>(u) >> FRACBITS) &
                                                static_cast<uint64_t>(tex.w - 1));
    const size_t depthStride = static_cast<size_t>(fb_->width());
    for (int y = static_cast<int>(topRow); y <= static_cast<int>(botRow); ++y) {
        const uint32_t texY = static_cast<uint32_t>((static_cast<uint64_t>(v) >> FRACBITS) &
                                                    static_cast<uint64_t>(tex.h - 1));
        fb_->row(y)[x] = shadeRGB(tex.pixels[static_cast<size_t>(texY) *
                                             static_cast<size_t>(tex.w) + texX],
                                  lightFactor);
        const size_t idx = static_cast<size_t>(y) * depthStride + static_cast<size_t>(x);
        if (idx < depth_.size()) depth_[idx] = z;
        ++stats_->pixelsRendered;
        v += vStep;
    }
    return true;
}

void Renderer::setColumnSolid(int x) {
    if (x < 0 || x >= width_) return;
    if (clipTop_[static_cast<size_t>(x)] <= clipBot_[static_cast<size_t>(x)]) {
        ++solidColumns_;
    }
    clipTop_[static_cast<size_t>(x)] = height_;
    clipBot_[static_cast<size_t>(x)] = -1;
}

void Renderer::setColumnInterval(int x, int top, int bottom) {
    if (x < 0 || x >= width_) return;
    if (top > bottom) {
        setColumnSolid(x);
        return;
    }
    clipTop_[static_cast<size_t>(x)] = top;
    clipBot_[static_cast<size_t>(x)] = bottom;
}

}  // namespace rf
