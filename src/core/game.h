// Top-level game object. Owns the world, player, simulation and renderer.
// Completely SDL-free so headless tests and screenshot tools can drive it.
#pragma once

#include <memory>
#include <string>

#include "core/input.h"
#include "core/stats.h"
#include "math/fixed.h"

namespace rf {

class Framebuffer;

class Game {
public:
    Game();
    ~Game();

    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    bool init(const std::string& mapPath, std::string* error);
    void update(const InputState& input, double dtSeconds);
    void render(Framebuffer& fb);

    Stats& stats() { return stats_; }
    const Stats& stats() const { return stats_; }

    // Debug/screenshot helper: teleports the camera. Angle in degrees.
    void setCamera(float x, float y, float angleDegrees);
    // Debug/screenshot helper: enables the top-down minimap and wall
    // wireframe overlays.
    void setDebugOverlays(bool minimap, bool wireframe);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Stats stats_;
};

}  // namespace rf
