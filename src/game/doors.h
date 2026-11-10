// Door controller.
//
// A door is a sector with special == 1 whose ceiling starts at closedCeil
// (equal to its floor at load time) and rises to openCeil. The renderer already
// treats the ceiling height as the portal opening, so animating the sector's
// ceilHeight is all the visual work required.
//
// Use detection: the player may activate a door if it shares a two-sided line
// with the sector they are standing in and the distance to that shared line is
// within reach. This is deliberately simple and geometric; there is no
// line-of-sight requirement, matching the forgiving feel of DOOM's use key.
//
// The controller is independent of the renderer and of the BSP, which makes it
// unit-testable with a hand-built World. Game supplies the player's sector
// (from Bsp::sectorAtPoint).
#pragma once

#include <vector>

#include "math/fixed.h"
#include "math/vec2.h"
#include "world/world.h"

namespace rf {

struct DoorRuntime {
    enum class Phase { Closed, Opening, Open, Closing };

    int sector = -1;
    Phase phase = Phase::Closed;
    double waitRemaining = 0.0;  // seconds until an open door closes itself
};

class DoorSystem {
public:
    // The player is a 56-unit-tall cylinder; a door opening is only passable
    // once its ceiling is at least this high above the floor.
    static constexpr fixed_t kPlayerHeight = fromInt(56);
    // Maximum distance from the player to the shared door line.
    static constexpr fixed_t kReach = fromInt(80);
    // Ceiling travel speed in map units per second.
    static constexpr fixed_t kOpenSpeed = fromInt(96);
    // How long a fully open door waits before closing again.
    static constexpr double kHoldSeconds = 4.0;

    // Finds the door sectors in the world and resets them to their closed
    // ceiling height.
    void reset(const World& world);

    // Activates the nearest door adjacent to playerSector. A closed or closing
    // door starts opening, an opening door keeps moving, and a fully open door
    // starts closing. Returns the door sector index or -1 if nothing was in
    // reach.
    int use(World& world, const Vec2& pos, int playerSector, fixed_t reach = kReach);

    // Animates ceilings. A closing door reopens instead of crushing a player
    // standing inside it. dtSeconds is clamped internally to 0.1 s.
    void update(World& world, double dtSeconds, int playerSector);

    // True when the sector is a door whose opening is lower than the player.
    // Used by movement to block passage through closed or partly open doors.
    bool blocked(const World& world, int sector) const;

    int doorCount() const { return static_cast<int>(doors_.size()); }
    const DoorRuntime& door(int index) const { return doors_[static_cast<size_t>(index)]; }
    int indexForSector(int sector) const;

private:
    std::vector<DoorRuntime> doors_;
};

}  // namespace rf
