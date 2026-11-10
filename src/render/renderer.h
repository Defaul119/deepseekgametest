// Software 3D world renderer: BSP front-to-back portal traversal, per-column
// clipping, textured walls and DOOM-style visplanes for flats.
//
// The renderer owns no world data; Game passes the level, BSP, textures and a
// camera each frame. All hot-path projection and texture stepping use 16.16
// fixed point. A per-pixel depth buffer (Q16.16 distance along the view
// direction) is filled by walls and by the flat pass so that sprite work in a
// later phase can reuse it and so cross-plane overlaps resolve correctly.
#pragma once

#include <cstdint>
#include <vector>

#include "core/stats.h"
#include "math/fixed.h"
#include "math/trig.h"
#include "math/vec2.h"
#include "render/framebuffer.h"
#include "render/texture.h"
#include "render/visplane.h"
#include "world/bsp.h"
#include "world/world.h"

namespace rf {

struct CameraView {
    Vec2 pos;
    angle_t yaw = 0;
    fixed_t eyeZ = fromInt(41);
    fixed_t horizonOffset = 0;  // fake vertical look, in pixels
    fixed_t focal = fromInt(320);
};

class Renderer {
public:
    void render(const World& world, const Bsp& bsp, const TextureSet& textures,
                const CameraView& view, Framebuffer& fb, Stats& stats, bool debugWalls);

    const std::vector<fixed_t>& depthBuffer() const { return depth_; }

private:
    struct WallEnd {
        fixed_t z = 0;
        int64_t x = 0;
        fixed_t u = 0;
        int64_t ceilFront = 0;
        int64_t floorFront = 0;
        int64_t ceilBack = 0;
        int64_t floorBack = 0;
    };
    struct ScreenPt {
        int64_t x = 0;
        int64_t y = 0;
    };
    struct DebugLine {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        uint32_t color = 0;
    };

    void renderLeaf(int leaf);
    void addFlatSpans(fixed_t height, int texture, int light, bool ceiling,
                      const std::vector<Vec2>& polygon);
    void renderSeg(const Seg& seg);
    bool drawWallPiece(int x, fixed_t z, fixed_t u, fixed_t yTop, fixed_t yBot,
                       fixed_t worldTop, fixed_t worldBot, int texture, int lightFactor);
    void setColumnSolid(int x);
    void setColumnInterval(int x, int top, int bottom);

    int64_t pointDepth64(const Vec2& p) const;
    int64_t pointLateral64(const Vec2& p) const;
    int64_t projectX64(int64_t lateral, int64_t depth) const;
    int64_t projectY64(fixed_t height, int64_t depth) const;

    const World* world_ = nullptr;
    const Bsp* bsp_ = nullptr;
    const TextureSet* textures_ = nullptr;
    Framebuffer* fb_ = nullptr;
    Stats* stats_ = nullptr;
    CameraView view_;
    Vec2 forward_;
    Vec2 right_;
    fixed_t focal_ = fromInt(320);
    fixed_t horizon_ = 0;
    fixed_t screenCenter_ = 0;
    fixed_t nearZ_ = fromInt(4);
    int width_ = 0;
    int height_ = 0;
    int solidColumns_ = 0;
    int defaultWallTexture_ = 0;
    bool debugWalls_ = false;

    std::vector<int> clipTop_;
    std::vector<int> clipBot_;
    std::vector<fixed_t> depth_;
    VisplaneManager visplanes_;
    std::vector<Vec2> flatPoly_;
    std::vector<ScreenPt> screenPoly_;
    std::vector<DebugLine> debugLines_;
};

}  // namespace rf
