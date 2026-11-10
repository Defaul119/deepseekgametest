// Per-frame performance counters exposed by the debug overlay and used by
// the benchmark tool.
#pragma once

#include <cstdint>

namespace rf {

struct Stats {
    // Frame timing (filled by the host loop).
    double frameMs = 0.0;
    double fps = 0.0;
    double simMs = 0.0;
    double renderMs = 0.0;

    // BSP / visibility.
    int bspNodesVisited = 0;
    int bspLeavesRendered = 0;
    int wallsProcessed = 0;
    int wallsClippedOut = 0;
    int solidRanges = 0;
    int visplanesCreated = 0;
    int visplaneColumns = 0;
    int rejectChecks = 0;
    int spritesDrawn = 0;

    // Rasterisation.
    int pixelsRendered = 0;
    int pixelsDepthRejected = 0;

    // Collision / spatial queries.
    int blockmapCellsQueried = 0;
    int collisionCandidates = 0;
    int collisionTests = 0;
    int blockmapDedupHits = 0;

    void resetFrame() {
        bspNodesVisited = 0;
        bspLeavesRendered = 0;
        wallsProcessed = 0;
        wallsClippedOut = 0;
        solidRanges = 0;
        visplanesCreated = 0;
        visplaneColumns = 0;
        rejectChecks = 0;
        spritesDrawn = 0;
        pixelsRendered = 0;
        pixelsDepthRejected = 0;
        blockmapCellsQueried = 0;
        collisionCandidates = 0;
        collisionTests = 0;
        blockmapDedupHits = 0;
    }
};

}  // namespace rf
