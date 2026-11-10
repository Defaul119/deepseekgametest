// Backend-independent input snapshot. The SDL layer fills one of these per
// frame; gameplay code never includes SDL headers.
#pragma once

namespace rf {

struct InputState {
    bool quit = false;

    // Held movement / action state.
    bool forward = false;
    bool back = false;
    bool strafeLeft = false;
    bool strafeRight = false;
    bool sprint = false;
    bool fire = false;
    bool use = false;

    // Edge-triggered this frame (consumed by one fixed-step update).
    bool firePressed = false;
    bool usePressed = false;

    // Relative mouse motion in pixels since the previous frame.
    int mouseDX = 0;
    int mouseDY = 0;

    // Debug / presentation toggles (edge-triggered).
    bool toggleStats = false;
    bool toggleBsp = false;
    bool toggleMinimap = false;
    bool toggleBlockmap = false;
    bool toggleFire = false;
    bool screenshot = false;

    void clearEdges() {
        firePressed = false;
        usePressed = false;
        toggleStats = false;
        toggleBsp = false;
        toggleMinimap = false;
        toggleBlockmap = false;
        toggleFire = false;
        screenshot = false;
        mouseDX = 0;
        mouseDY = 0;
    }
};

}  // namespace rf
