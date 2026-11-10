#include "render/visplane.h"

#include <algorithm>
#include <cstdint>

namespace rf {
namespace {

constexpr fixed_t kHalfPixel = FRACUNIT / 2;
constexpr fixed_t kMinViewZ = FRACUNIT;       // 1 map unit
constexpr fixed_t kMaxViewZ = fromInt(8192);  // distant rows clamp here

}  // namespace

void VisplaneManager::beginFrame(int width, int height) {
    width_ = width;
    height_ = height;
    activeCount_ = 0;
    columnsAdded_ = 0;
    mergedSpans_ = 0;
    splitCount_ = 0;
}

Visplane& VisplaneManager::activate(const Visplane& key) {
    Visplane* plane = nullptr;
    if (activeCount_ < static_cast<int>(planes_.size())) {
        plane = &planes_[static_cast<size_t>(activeCount_)];
    } else {
        planes_.emplace_back();
        plane = &planes_.back();
    }
    if (static_cast<int>(plane->top.size()) != width_) {
        plane->top.resize(static_cast<size_t>(width_));
        plane->bottom.resize(static_cast<size_t>(width_));
    }
    std::fill(plane->top.begin(), plane->top.end(), static_cast<int16_t>(-1));
    std::fill(plane->bottom.begin(), plane->bottom.end(), static_cast<int16_t>(-1));
    plane->height = key.height;
    plane->texture = key.texture;
    plane->light = key.light;
    plane->ceiling = key.ceiling;
    plane->minx = width_;
    plane->maxx = -1;
    ++activeCount_;
    return *plane;
}

int VisplaneManager::addSpan(fixed_t height, int texture, int light, bool ceiling, int x, int top,
                             int bottom) {
    if (x < 0 || x >= width_ || height_ <= 0 || width_ <= 0) return -1;
    if (top < 0) top = 0;
    if (bottom >= height_) bottom = height_ - 1;
    if (top > bottom) return -1;

    // 1) A same-key plane with either an empty column or a touching/
    //    overlapping interval can absorb the span.
    for (int i = 0; i < activeCount_; ++i) {
        Visplane& p = planes_[static_cast<size_t>(i)];
        if (p.height != height || p.texture != texture || p.light != light ||
            p.ceiling != ceiling) {
            continue;
        }
        const int16_t oldTop = p.top[static_cast<size_t>(x)];
        if (oldTop < 0) {
            p.top[static_cast<size_t>(x)] = static_cast<int16_t>(top);
            p.bottom[static_cast<size_t>(x)] = static_cast<int16_t>(bottom);
            p.minx = std::min(p.minx, x);
            p.maxx = std::max(p.maxx, x);
            ++columnsAdded_;
            return i;
        }
        const int16_t oldBottom = p.bottom[static_cast<size_t>(x)];
        // Overlapping or directly adjacent intervals describe one contiguous
        // run of pixels, so they merge. A one-pixel gap must stay a split.
        if (top <= oldBottom + 1 && bottom >= oldTop - 1) {
            p.top[static_cast<size_t>(x)] = static_cast<int16_t>(std::min<int>(top, oldTop));
            p.bottom[static_cast<size_t>(x)] =
                static_cast<int16_t>(std::max<int>(bottom, oldBottom));
            p.minx = std::min(p.minx, x);
            p.maxx = std::max(p.maxx, x);
            ++columnsAdded_;
            ++mergedSpans_;
            return i;
        }
    }

    // 2) Same key exists but every matching plane already has a disjoint
    //    interval in this column: this is the visplane split case.
    bool sameKeyExists = false;
    for (int i = 0; i < activeCount_; ++i) {
        const Visplane& p = planes_[static_cast<size_t>(i)];
        if (p.height == height && p.texture == texture && p.light == light &&
            p.ceiling == ceiling) {
            sameKeyExists = true;
            break;
        }
    }

    Visplane key;
    key.height = height;
    key.texture = texture;
    key.light = light;
    key.ceiling = ceiling;
    Visplane& p = activate(key);
    p.top[static_cast<size_t>(x)] = static_cast<int16_t>(top);
    p.bottom[static_cast<size_t>(x)] = static_cast<int16_t>(bottom);
    p.minx = x;
    p.maxx = x;
    ++columnsAdded_;
    if (sameKeyExists) ++splitCount_;
    return activeCount_ - 1;
}

void VisplaneManager::render(Framebuffer& fb, const FlatCamera& camera,
                             const TextureSet& textures, std::vector<fixed_t>& depth,
                             Stats& stats) {
    if (camera.width <= 0 || camera.height <= 0) return;
    const int w = std::min({camera.width, fb.width(), width_});
    const int h = std::min({camera.height, fb.height(), height_});
    if (w <= 0 || h <= 0) return;
    const fixed_t half = kHalfPixel;
    const fixed_t screenCenter = fromInt(camera.width) / 2;

    for (int pi = 0; pi < activeCount_; ++pi) {
        const Visplane& p = planes_[static_cast<size_t>(pi)];
        if (p.texture < 0 || p.texture >= textures.count() || p.maxx < p.minx) continue;
        if (p.ceiling ? (p.height <= camera.eyeZ) : (p.height >= camera.eyeZ)) continue;
        const Texture& tex = textures.get(p.texture);
        if (tex.w <= 0 || tex.h <= 0) continue;
        const int lightFactor = shadeFactor(p.light);

        int yStart = h;
        int yEnd = -1;
        for (int x = std::max(0, p.minx); x <= std::min(w - 1, p.maxx); ++x) {
            const int16_t t = p.top[static_cast<size_t>(x)];
            if (t < 0) continue;
            yStart = std::min(yStart, static_cast<int>(t));
            yEnd = std::max(yEnd, static_cast<int>(p.bottom[static_cast<size_t>(x)]));
        }
        if (yStart > yEnd) continue;
        yStart = std::max(yStart, 0);
        yEnd = std::min(yEnd, h - 1);

        const int64_t num = static_cast<int64_t>(camera.eyeZ - p.height) * camera.focal;
        for (int y = yStart; y <= yEnd; ++y) {
            const fixed_t sy = fromInt(y) + half;
            const fixed_t dy = sy - camera.horizon;
            if (dy == 0) continue;
            const int64_t zRaw = num / static_cast<int64_t>(dy);
            if (zRaw <= 0) continue;  // plane is behind the viewer for this row
            const fixed_t z = static_cast<fixed_t>(
                std::min<int64_t>(std::max<int64_t>(zRaw, kMinViewZ), kMaxViewZ));

            // World point at the screen centre for this row, then a constant
            // world-space step per column. For a horizontal plane at constant
            // z, world X/Y are affine in screen x, so this is exact apart from
            // the fixed-point quantisation of the step.
            const fixed_t wx = camera.pos.x + mul(camera.forward.x, z);
            const fixed_t wy = camera.pos.y + mul(camera.forward.y, z);
            const fixed_t rightScale = div(z, camera.focal);  // map units / pixel
            const int xFirst = std::max(0, p.minx);
            const int xLast = std::min(w - 1, p.maxx);
            if (xFirst > xLast) continue;
            const fixed_t dx0 = fromInt(xFirst) + half - screenCenter;
            const fixed_t lateral0 = mul(dx0, rightScale);
            const fixed_t uStep = mul(camera.right.x, rightScale);
            const fixed_t vStep = mul(camera.right.y, rightScale);
            int64_t u = static_cast<int64_t>(wx) + mul(camera.right.x, lateral0);
            int64_t v = static_cast<int64_t>(wy) + mul(camera.right.y, lateral0);

            uint32_t* row = fb.row(y);
            for (int x = xFirst; x <= xLast; ++x) {
                const int64_t uu = u;
                const int64_t vv = v;
                u += uStep;
                v += vStep;
                const int16_t t = p.top[static_cast<size_t>(x)];
                if (t < 0 || y < t || y > p.bottom[static_cast<size_t>(x)]) continue;
                const size_t idx = static_cast<size_t>(y) * static_cast<size_t>(fb.width()) +
                                   static_cast<size_t>(x);
                if (idx < depth.size() && depth[idx] != 0 && z >= depth[idx]) {
                    ++stats.pixelsDepthRejected;
                    continue;
                }
                const int tx = static_cast<int>((static_cast<uint64_t>(uu) >> FRACBITS) &
                                                static_cast<uint64_t>(tex.w - 1));
                const int ty = static_cast<int>((static_cast<uint64_t>(vv) >> FRACBITS) &
                                                static_cast<uint64_t>(tex.h - 1));
                row[x] = shadeRGB(tex.pixels[static_cast<size_t>(ty) * static_cast<size_t>(tex.w) +
                                             static_cast<size_t>(tx)],
                                  lightFactor);
                if (idx < depth.size()) depth[idx] = z;
                ++stats.pixelsRendered;
            }
        }
    }
}

}  // namespace rf
