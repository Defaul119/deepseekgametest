#include "core/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/doors.h"
#include "math/trig.h"
#include "math/vec2.h"
#include "render/font.h"
#include "render/framebuffer.h"
#include "render/renderer.h"
#include "render/texture.h"
#include "world/bsp.h"
#include "world/mapfile.h"
#include "world/world.h"

namespace rf {
namespace {

constexpr uint32_t kLookAnglePerPixel = 1200000u;  // ~0.10 degrees

struct ScreenPt {
    int x = 0;
    int y = 0;
};

// Top-down projection used by the debug minimap. The 3D world renderer has
// its own fixed-point projection; this view exists to make the BSP, sectors
// and camera position inspectable at a glance.
struct MapView {
    fixed_t scale = FRACUNIT;
    fixed_t worldMinX = 0;
    fixed_t worldMinY = 0;
    int originX = 0;
    int originY = 0;

    int sx(fixed_t wx) const { return originX + toInt(mul(wx - worldMinX, scale)); }
    int sy(fixed_t wy) const { return originY + toInt(mul(wy - worldMinY, scale)); }
};

void drawLine(Framebuffer& fb, int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
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

void fillCircle(Framebuffer& fb, int cx, int cy, int radius, uint32_t color) {
    for (int dy = -radius; dy <= radius; ++dy) {
        const int y = cy + dy;
        if (y < 0 || y >= fb.height()) continue;
        const int span = static_cast<int>(std::sqrt(static_cast<double>(radius * radius - dy * dy)));
        fb.hline(cx - span, cx + span, y, color);
    }
}

// Scanline fill of a convex polygon. Used only by the debug map view, so it
// uses floating point for its screen-space edge interpolation; the actual
// floor/ceiling rasteriser works in fixed point.
int fillConvex(Framebuffer& fb, const std::vector<Vec2>& polygon, const MapView& view,
               uint32_t color, std::vector<ScreenPt>& scratch) {
    scratch.clear();
    if (polygon.size() < 3) return 0;
    scratch.reserve(polygon.size());
    for (const Vec2& p : polygon) scratch.push_back(ScreenPt{view.sx(p.x), view.sy(p.y)});

    int minY = scratch[0].y;
    int maxY = scratch[0].y;
    for (const ScreenPt& p : scratch) {
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    minY = std::clamp(minY, 0, fb.height() - 1);
    maxY = std::clamp(maxY, 0, fb.height() - 1);

    int drawn = 0;
    for (int y = minY; y <= maxY; ++y) {
        const double yc = y + 0.5;
        double xMin = 1e30;
        double xMax = -1e30;
        for (size_t i = 0; i < scratch.size(); ++i) {
            const ScreenPt& a = scratch[i];
            const ScreenPt& b = scratch[(i + 1) % scratch.size()];
            if ((a.y <= yc && b.y > yc) || (b.y <= yc && a.y > yc)) {
                const double x = a.x + (b.x - a.x) * (yc - a.y) / (b.y - a.y);
                xMin = std::min(xMin, x);
                xMax = std::max(xMax, x);
            }
        }
        if (xMin > xMax) continue;
        const int left = std::clamp(static_cast<int>(std::ceil(xMin)), 0, fb.width() - 1);
        const int right = std::clamp(static_cast<int>(std::floor(xMax)), 0, fb.width() - 1);
        if (left <= right) {
            fb.hline(left, right, y, color);
            drawn += right - left + 1;
        }
    }
    return drawn;
}

uint32_t sectorBaseColor(int sector) {
    static const uint32_t palette[] = {
        colorRGB(150, 100, 55),   // start room (brick)
        colorRGB(95, 125, 135),   // stone hall
        colorRGB(70, 100, 155),   // tech room
        colorRGB(75, 125, 85),    // corridor
        colorRGB(185, 150, 55),   // doors
        colorRGB(185, 150, 55),
        colorRGB(185, 150, 55),
        colorRGB(185, 150, 55),
        colorRGB(185, 150, 55),
        colorRGB(140, 90, 140),
        colorRGB(80, 140, 140),
        colorRGB(160, 110, 80),
    };
    constexpr int count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    return palette[((sector % count) + count) % count];
}

}  // namespace

struct Game::Impl {
    TextureSet textures;
    World world;
    Bsp bsp;
    Renderer renderer;
    DoorSystem doors;

    fixed_t x = 0;
    fixed_t y = 0;
    angle_t yaw = 0;
    fixed_t horizonOffset = 0;

    bool showStats = true;
    bool showBsp = false;    // F2: 3D wall wireframe + minimap BSP lines
    bool showMap = false;    // F3: top-down minimap overlay
    bool showBlockmap = false;
    bool fireFullscreen = false;

    std::string mapName;
    std::vector<int> traversalOrder;
    std::vector<ScreenPt> fillScratch;
    int cameraSector = -1;

    bool init(const std::string& mapPath, std::string* error) {
        if (!loadMap(mapPath, textures, world, error)) return false;
        if (!bsp.build(world, error)) return false;
        doors.reset(world);

        const Thing* start = nullptr;
        for (const Thing& t : world.things) {
            if (t.type == kThingPlayerStart) {
                start = &t;
                break;
            }
        }
        if (!start) {
            if (error) *error = "map has no player start (thing type 1)";
            return false;
        }
        x = start->x;
        y = start->y;
        yaw = start->angle;

        const size_t slash = mapPath.find_last_of("/\\");
        mapName = slash == std::string::npos ? mapPath : mapPath.substr(slash + 1);
        return true;
    }

    void update(const InputState& in, double dtSeconds) {
        if (in.mouseDX) {
            yaw += static_cast<angle_t>(static_cast<int64_t>(in.mouseDX) *
                                        static_cast<int64_t>(kLookAnglePerPixel));
        }
        if (in.mouseDY) {
            horizonOffset -= fromInt(in.mouseDY) / 2;
            horizonOffset = clampv(horizonOffset, fromInt(-120), fromInt(120));
        }

        if (in.toggleStats) showStats = !showStats;
        if (in.toggleBsp) showBsp = !showBsp;
        if (in.toggleMinimap) showMap = !showMap;
        if (in.toggleBlockmap) showBlockmap = !showBlockmap;
        if (in.toggleFire) fireFullscreen = !fireFullscreen;

        // Use key: activate the nearest door sharing a line with the sector
        // the player stands in. Doors then animate in update() below.
        if (in.usePressed) {
            const int useSector = bsp.sectorAtPoint(Vec2{x, y});
            doors.use(world, Vec2{x, y}, useSector);
        }

        // Movement. This phase has no radius-based wall collision yet; a
        // target point is accepted only when the BSP places it in a sector,
        // which already blocks the solid void and solid pillars.
        const fixed_t dt = fromFloat(static_cast<float>(std::min(dtSeconds, 0.1)));
        const fixed_t speed = in.sprint ? fromInt(320) : fromInt(160);
        const fixed_t distance = mul(speed, dt);
        int forwardAxis = (in.forward ? 1 : 0) - (in.back ? 1 : 0);
        int strafeAxis = (in.strafeRight ? 1 : 0) - (in.strafeLeft ? 1 : 0);
        if (forwardAxis != 0 || strafeAxis != 0) {
            fixed_t f = fromInt(forwardAxis);
            fixed_t s = fromInt(strafeAxis);
            if (forwardAxis != 0 && strafeAxis != 0) {
                static const fixed_t diagonal = fromFloat(0.70710678f);
                f = mul(f, diagonal);
                s = mul(s, diagonal);
            }
            const Vec2 dir = angleVector(yaw);
            const Vec2 right = rightVector(yaw);
            const fixed_t moveF = mul(distance, f);
            const fixed_t moveS = mul(distance, s);
            const fixed_t nx = x + mul(dir.x, moveF) + mul(right.x, moveS);
            const fixed_t ny = y + mul(dir.y, moveF) + mul(right.y, moveS);

            auto walkable = [&](fixed_t px, fixed_t py) {
                const int sector = bsp.sectorAtPoint(Vec2{px, py});
                if (sector < 0) return false;
                // Doors are passable only once their ceiling is high enough
                // for the player to fit through.
                if (doors.blocked(world, sector)) return false;
                return true;
            };
            if (walkable(nx, ny)) {
                x = nx;
                y = ny;
            } else {
                // Slide along whichever axis remains walkable.
                if (walkable(nx, y)) x = nx;
                if (walkable(x, ny)) y = ny;
            }
        }

        // Animate doors after movement so a player who just stepped into a
        // doorway is already counted as being inside it.
        doors.update(world, dtSeconds, bsp.sectorAtPoint(Vec2{x, y}));
    }

    // Small top-down overlay (F3): sector fills in front-to-back BSP order,
    // linedefs, BSP partitions and the camera arrow. This is the BSP debug
    // visualisation; the 3D wireframe (F2) shows the projected walls.
    void drawMinimap(Framebuffer& fb) {
        const int w = fb.width();
        const int h = fb.height();
        const int footer = 20;
        const int panelW = 230;
        const int panelH = 178;
        const int px0 = w - panelW - 8;
        const int py0 = h - footer - panelH - 8;
        fb.rectFill(px0, py0, px0 + panelW - 1, py0 + panelH - 1, colorRGB(8, 10, 14));
        const uint32_t border = colorRGB(72, 82, 96);
        drawLine(fb, px0, py0, px0 + panelW - 1, py0, border);
        drawLine(fb, px0, py0 + panelH - 1, px0 + panelW - 1, py0 + panelH - 1, border);
        drawLine(fb, px0, py0, px0, py0 + panelH - 1, border);
        drawLine(fb, px0 + panelW - 1, py0, px0 + panelW - 1, py0 + panelH - 1, border);
        Font::drawTextShadow(fb, px0 + 6, py0 + 5, "BSP / SECTOR MAP", colorRGB(160, 205, 175));

        const int mapX = px0 + 6;
        const int mapY = py0 + 17;
        const int mapW = panelW - 12;
        const int mapH = panelH - 23;
        fixed_t worldW = world.maxX - world.minX;
        fixed_t worldH = world.maxY - world.minY;
        if (worldW <= 0) worldW = FRACUNIT;
        if (worldH <= 0) worldH = FRACUNIT;
        const fixed_t scale =
            std::min(div(fromInt(mapW - 4), worldW), div(fromInt(mapH - 4), worldH));
        const int drawW = toInt(mul(worldW, scale));
        const int drawH = toInt(mul(worldH, scale));
        MapView view;
        view.scale = scale;
        view.worldMinX = world.minX;
        view.worldMinY = world.minY;
        view.originX = mapX + (mapW - drawW) / 2;
        view.originY = mapY + (mapH - drawH) / 2;

        traversalOrder.clear();
        bsp.traverse(Vec2{x, y}, true, [&](int leaf) { traversalOrder.push_back(leaf); });
        int order = 0;
        for (int leaf : traversalOrder) {
            const Subsector& sub = bsp.subsectors()[static_cast<size_t>(leaf)];
            if (sub.sector >= 0 && sub.polygon.size() >= 3) {
                const Sector& sector = world.sectors[static_cast<size_t>(sub.sector)];
                const uint32_t base =
                    sector.special == 1 ? colorRGB(205, 155, 60) : sectorBaseColor(sub.sector);
                const int fade = 220 - std::min(150, order * 5);
                const int factor = std::min(255, shadeFactor(sector.light) * fade / 255 + 25);
                fillConvex(fb, sub.polygon, view, shadeRGB(base, factor), fillScratch);
            }
            ++order;
        }

        {
            const fixed_t extent = std::max(worldW, worldH);
            for (const BspNode& node : bsp.nodes()) {
                // Clip the infinite partition line to the map bounds first so
                // it cannot paint outside the minimap panel.
                fixed_t t0 = -extent;
                fixed_t t1 = extent;
                bool visible = true;
                auto clipAxis = [&](fixed_t originComp, fixed_t dirComp, fixed_t lo,
                                    fixed_t hi) {
                    if (dirComp == 0) {
                        if (originComp < lo || originComp > hi) visible = false;
                        return;
                    }
                    fixed_t a = div(lo - originComp, dirComp);
                    fixed_t b = div(hi - originComp, dirComp);
                    if (a > b) std::swap(a, b);
                    t0 = std::max(t0, a);
                    t1 = std::min(t1, b);
                };
                clipAxis(node.origin.x, node.dir.x, world.minX, world.maxX);
                clipAxis(node.origin.y, node.dir.y, world.minY, world.maxY);
                if (!visible || t0 > t1) continue;
                const Vec2 a = node.origin + node.dir * t0;
                const Vec2 b = node.origin + node.dir * t1;
                drawLine(fb, view.sx(a.x), view.sy(a.y), view.sx(b.x), view.sy(b.y),
                         colorRGB(200, 175, 60));
            }
        }

        for (size_t i = 0; i < world.lines.size(); ++i) {
            const Line& line = world.lines[i];
            const bool door =
                (line.frontSector >= 0 &&
                 world.sectors[static_cast<size_t>(line.frontSector)].special == 1) ||
                (line.backSector >= 0 &&
                 world.sectors[static_cast<size_t>(line.backSector)].special == 1);
            const uint32_t color = door ? colorRGB(245, 175, 65)
                                        : (line.backSector >= 0 ? colorRGB(120, 128, 142)
                                                                : colorRGB(220, 220, 210));
            const Vec2 a = world.lineStart(static_cast<int>(i));
            const Vec2 b = world.lineEnd(static_cast<int>(i));
            drawLine(fb, view.sx(a.x), view.sy(a.y), view.sx(b.x), view.sy(b.y), color);
        }

        for (const Thing& thing : world.things) {
            uint32_t color = colorRGB(230, 90, 80);
            int radius = 2;
            if (thing.type == kThingPlayerStart) {
                color = colorRGB(110, 240, 140);
                radius = 3;
            } else if (thing.type == kThingHealth) {
                color = colorRGB(120, 200, 255);
            } else if (thing.type == kThingAmmo) {
                color = colorRGB(240, 220, 100);
            }
            fillCircle(fb, view.sx(thing.x), view.sy(thing.y), radius, color);
        }

        const Vec2 camera{x, y};
        const Vec2 dir = angleVector(yaw);
        const Vec2 right = rightVector(yaw);
        const Vec2 tip = camera + dir * fromInt(16);
        const Vec2 tailL = camera + dir * fromInt(-6) - right * fromInt(6);
        const Vec2 tailR = camera + dir * fromInt(-6) + right * fromInt(6);
        drawLine(fb, view.sx(tip.x), view.sy(tip.y), view.sx(tailL.x), view.sy(tailL.y),
                 colorRGB(120, 255, 150));
        drawLine(fb, view.sx(tip.x), view.sy(tip.y), view.sx(tailR.x), view.sy(tailR.y),
                 colorRGB(120, 255, 150));
        fillCircle(fb, view.sx(x), view.sy(y), 2, colorRGB(235, 255, 240));
    }

    void drawStatsOverlay(Framebuffer& fb, const Stats& stats) {
        const int panelW = 252;
        const int panelH = 190;
        fb.rectFill(8, 8, 8 + panelW - 1, 8 + panelH - 1, colorRGB(10, 12, 17));
        const uint32_t border = colorRGB(52, 60, 74);
        drawLine(fb, 8, 8, 8 + panelW - 1, 8, border);
        drawLine(fb, 8, 8 + panelH - 1, 8 + panelW - 1, 8 + panelH - 1, border);
        drawLine(fb, 8, 8, 8, 8 + panelH - 1, border);
        drawLine(fb, 8 + panelW - 1, 8, 8 + panelW - 1, 8 + panelH - 1, border);

        Font::drawTextShadow(fb, 16, 14, "RETRO FPS", colorRGB(245, 225, 175), 0xFF000000, 2);
        Font::drawTextShadow(fb, 16, 34, "PHASE 3 / PORTAL RENDERER", colorRGB(150, 200, 170));
        char buf[256];
        std::snprintf(buf, sizeof(buf), "MAP %s", mapName.c_str());
        Font::drawTextShadow(fb, 16, 46, buf, colorRGB(125, 135, 150));
        drawLine(fb, 16, 58, 8 + panelW - 9, 58, colorRGB(45, 50, 62));

        int yy = 68;
        auto line = [&](uint32_t color) {
            Font::drawTextShadow(fb, 16, yy, buf, color);
            yy += 12;
        };
        std::snprintf(buf, sizeof(buf), "FPS %.1f   FRAME %.2f MS", stats.fps, stats.frameMs);
        line(colorRGB(150, 225, 160));
        std::snprintf(buf, sizeof(buf), "BSP NODES %d   LEAVES %d", stats.bspNodesVisited,
                      stats.bspLeavesRendered);
        line(colorRGB(195, 205, 215));
        std::snprintf(buf, sizeof(buf), "WALLS %d   CLIPPED %d", stats.wallsProcessed,
                      stats.wallsClippedOut);
        line(colorRGB(195, 205, 215));
        std::snprintf(buf, sizeof(buf), "VISPLANES %d   COLS %d", stats.visplanesCreated,
                      stats.visplaneColumns);
        line(colorRGB(195, 205, 215));
        std::snprintf(buf, sizeof(buf), "PIXELS %d   DEPTH REJ %d", stats.pixelsRendered,
                      stats.pixelsDepthRejected);
        line(colorRGB(195, 205, 215));
        std::snprintf(buf, sizeof(buf), "SOLID RANGES %d", stats.solidRanges);
        line(colorRGB(195, 205, 215));
        std::snprintf(buf, sizeof(buf), "SECTOR %d   POS %.1f %.1f", cameraSector, toFloat(x),
                      toFloat(y));
        line(colorRGB(170, 182, 195));
        std::snprintf(buf, sizeof(buf), "YAW %.1f DEG   EYE %.1f", angleToDegrees(yaw),
                      toFloat(cameraSector >= 0
                                  ? world.sectors[static_cast<size_t>(cameraSector)].floorHeight +
                                        fromInt(41)
                                  : fromInt(41)));
        line(colorRGB(170, 182, 195));
    }

    void render(Framebuffer& fb, Stats& stats) {
        stats.resetFrame();
        const int w = fb.width();
        const int h = fb.height();
        fb.clear(colorRGB(9, 10, 14));

        cameraSector = bsp.sectorAtPoint(Vec2{x, y});
        fixed_t eyeZ = fromInt(41);
        if (cameraSector >= 0) {
            eyeZ += world.sectors[static_cast<size_t>(cameraSector)].floorHeight;
        }

        CameraView view;
        view.pos = Vec2{x, y};
        view.yaw = yaw;
        view.eyeZ = eyeZ;
        view.horizonOffset = horizonOffset;
        view.focal = fromInt(w / 2);  // 90 degree horizontal field of view
        renderer.render(world, bsp, textures, view, fb, stats, showBsp);

        // Crosshair.
        const int cx = w / 2;
        const int cy = h / 2;
        const uint32_t cross = colorRGB(220, 235, 220);
        fb.hline(cx - 4, cx + 4, cy, cross);
        fb.vline(cx, cy - 4, cy + 4, cross);

        if (showMap) drawMinimap(fb);
        if (showStats) drawStatsOverlay(fb, stats);

        // Footer strip with controls and timing.
        const int footerY = h - 20;
        fb.rectFill(0, footerY, w - 1, h - 1, colorRGB(12, 14, 19));
        fb.hline(0, w - 1, footerY, colorRGB(48, 54, 66));
        Font::drawTextShadow(fb, 8, footerY + 6,
                             "WASD MOUSE SHIFT  E USE  F1 STATS  F2 WALLS  F3 MAP",
                             colorRGB(150, 158, 170));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.1f FPS  %.2f MS", stats.fps, stats.frameMs);
        Font::drawTextShadow(fb, w - 160, footerY + 6, buf, colorRGB(140, 220, 150));
    }
};

Game::Game() : impl_(std::make_unique<Impl>()) {}
Game::~Game() = default;

bool Game::init(const std::string& mapPath, std::string* error) { return impl_->init(mapPath, error); }

void Game::update(const InputState& input, double dtSeconds) { impl_->update(input, dtSeconds); }

void Game::render(Framebuffer& fb) { impl_->render(fb, stats_); }

void Game::setCamera(float x, float y, float angleDegrees) {
    impl_->x = fromFloat(x);
    impl_->y = fromFloat(y);
    impl_->yaw = angleFromDegrees(angleDegrees);
}

void Game::setDebugOverlays(bool minimap, bool wireframe) {
    impl_->showMap = minimap;
    impl_->showBsp = wireframe;
}

}  // namespace rf
