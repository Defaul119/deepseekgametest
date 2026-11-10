#include "game/doors.h"

#include <algorithm>
#include <cmath>

namespace rf {
namespace {

constexpr double kUnitsPerSecond = 96.0;
constexpr double kMaxStepSeconds = 0.1;

double toDouble(fixed_t v) { return static_cast<double>(v) / static_cast<double>(FRACUNIT); }

// Distance from p to the segment a-b, computed in double precision. Use-key
// handling runs at most once per key press, so there is no reason to pay the
// overflow risk of 32-bit fixed-point squared lengths on long lines.
double segmentDistance(const Vec2& p, const Vec2& a, const Vec2& b) {
    const double px = toDouble(p.x);
    const double py = toDouble(p.y);
    const double ax = toDouble(a.x);
    const double ay = toDouble(a.y);
    const double bx = toDouble(b.x);
    const double by = toDouble(b.y);
    const double abx = bx - ax;
    const double aby = by - ay;
    const double len2 = abx * abx + aby * aby;
    double t = 0.0;
    if (len2 > 0.0) {
        t = ((px - ax) * abx + (py - ay) * aby) / len2;
        t = std::max(0.0, std::min(1.0, t));
    }
    const double dx = px - (ax + abx * t);
    const double dy = py - (ay + aby * t);
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

void DoorSystem::reset(const World& world) {
    doors_.clear();
    for (int i = 0; i < static_cast<int>(world.sectors.size()); ++i) {
        const Sector& s = world.sectors[static_cast<size_t>(i)];
        if (s.special != 1) continue;
        DoorRuntime door;
        door.sector = i;
        door.phase = DoorRuntime::Phase::Closed;
        door.waitRemaining = 0.0;
        doors_.push_back(door);
    }
}

int DoorSystem::indexForSector(int sector) const {
    for (int i = 0; i < static_cast<int>(doors_.size()); ++i) {
        if (doors_[static_cast<size_t>(i)].sector == sector) return i;
    }
    return -1;
}

int DoorSystem::use(World& world, const Vec2& pos, int playerSector, fixed_t reach) {
    if (playerSector < 0) return -1;

    const double maxReach = toDouble(reach);
    int bestDoor = -1;
    double bestDistance = maxReach;

    for (int di = 0; di < static_cast<int>(doors_.size()); ++di) {
        const int doorSector = doors_[static_cast<size_t>(di)].sector;
        for (int li = 0; li < static_cast<int>(world.lines.size()); ++li) {
            const Line& line = world.lines[static_cast<size_t>(li)];
            const bool adjacent =
                (line.frontSector == playerSector && line.backSector == doorSector) ||
                (line.backSector == playerSector && line.frontSector == doorSector);
            if (!adjacent) continue;
            const double d = segmentDistance(pos, world.lineStart(li), world.lineEnd(li));
            if (d <= bestDistance) {
                bestDistance = d;
                bestDoor = di;
            }
        }
    }

    if (bestDoor < 0) return -1;

    DoorRuntime& door = doors_[static_cast<size_t>(bestDoor)];
    switch (door.phase) {
        case DoorRuntime::Phase::Closed:
        case DoorRuntime::Phase::Closing:
            door.phase = DoorRuntime::Phase::Opening;
            break;
        case DoorRuntime::Phase::Opening:
            // Already moving; pressing use again does not reverse it.
            break;
        case DoorRuntime::Phase::Open:
            door.phase = DoorRuntime::Phase::Closing;
            door.waitRemaining = 0.0;
            break;
    }
    return door.sector;
}

void DoorSystem::update(World& world, double dtSeconds, int playerSector) {
    if (dtSeconds <= 0.0) return;
    const double dt = std::min(dtSeconds, kMaxStepSeconds);
    const fixed_t step = fromFloat(static_cast<float>(kUnitsPerSecond * dt));

    for (DoorRuntime& door : doors_) {
        Sector& sector = world.sectors[static_cast<size_t>(door.sector)];
        if (sector.openCeil <= sector.closedCeil) continue;

        switch (door.phase) {
            case DoorRuntime::Phase::Closed:
                break;
            case DoorRuntime::Phase::Opening:
                sector.ceilHeight = static_cast<fixed_t>(sector.ceilHeight + step);
                if (sector.ceilHeight >= sector.openCeil) {
                    sector.ceilHeight = sector.openCeil;
                    door.phase = DoorRuntime::Phase::Open;
                    door.waitRemaining = kHoldSeconds;
                }
                break;
            case DoorRuntime::Phase::Open:
                door.waitRemaining -= dt;
                if (door.waitRemaining <= 0.0) {
                    door.phase = DoorRuntime::Phase::Closing;
                    door.waitRemaining = 0.0;
                }
                break;
            case DoorRuntime::Phase::Closing:
                // Never crush: a player standing in the doorway holds it open.
                if (playerSector == door.sector) {
                    door.phase = DoorRuntime::Phase::Opening;
                    break;
                }
                sector.ceilHeight = static_cast<fixed_t>(sector.ceilHeight - step);
                if (sector.ceilHeight <= sector.closedCeil) {
                    sector.ceilHeight = sector.closedCeil;
                    door.phase = DoorRuntime::Phase::Closed;
                }
                break;
        }
    }
}

bool DoorSystem::blocked(const World& world, int sector) const {
    if (sector < 0 || sector >= static_cast<int>(world.sectors.size())) return false;
    if (indexForSector(sector) < 0) return false;
    const Sector& s = world.sectors[static_cast<size_t>(sector)];
    return s.ceilHeight - s.floorHeight < kPlayerHeight;
}

}  // namespace rf
