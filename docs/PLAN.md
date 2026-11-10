# Technical Implementation Plan

A CPU software-rendered 2.5D FPS engine in the spirit of DOOM (1993) and
Wolfenstein 3D. Everything below is implemented in C++17 against SDL2, with
SDL used **only** for windowing, input and presenting the finished 32-bit
framebuffer. No GPU 3D API is used at any stage.

## 1. Environment and toolchain

The development machine initially had no C/C++ compiler. A self-contained
MSYS2 UCRT64 toolchain was installed under `C:\Users\ADMIN\tools\msys64`:

| Component | Version |
|---|---|
| GCC / G++ | 16.2.0 (MSYS2 UCRT64) |
| CMake | 4.4.5 |
| Ninja | 1.13.2 |
| SDL2 | 2.32 (UCRT64 package, CMake config) |

Build: `.\build.ps1` (sets PATH, configures Ninja, builds into `build/bin`).
SDL2 and the MinGW threading runtime are copied next to the executable.

## 2. Project layout

```
src/
  math/      fixed.h/.cpp, trig.h/.cpp, vec2.h     16.16 maths, BAM angles
  core/      game, input, stats, rng, timer         engine/game loop support
  world/     world, bsp, blockmap, reject, floodfill, mapfile
  render/    framebuffer, texture, visplane, renderer, sprites,
             fire, font, hud, debugdraw
  game/      player, collision, doors, enemies, weapons, projectiles
  platform/  SDL2 window, input, presentation
assets/      sample map, generated screenshots
tests/       unit + algorithmic tests (custom harness, CTest)
tools/       map generator, benchmarks
docs/        this plan, architecture, maths, limitations
```

The SDL-free core (`rf_core`) contains all mathematical and rendering code,
which lets tests and the headless screenshot mode run without a window.

## 3. Rendering architecture

### 3.1 Geometry and BSP

- The world is a set of **sectors** (floor height, ceiling height, textures,
  light), **linedefs** (two vertices, front/back sector, wall textures) and
  **vertices**.
- Two sidedefs exist per two-sided line. The BSP builder creates one **seg per
  side**, so every subsector boundary carries the sector that owns it.
- BSP construction recursively chooses a partition seg using a DOOM-style
  score (splits weighted heavily, then balance), classifies all segs as
  front/back/collinear, splits crossing segs at the exact line intersection,
  and stores child indices. Leaves are **subsectors**: convex sets of segs
  that all share one front sector.
- The tree is used for:
  - front-to-back rendering traversal (near child first),
  - point-to-subsector/sector lookup (player, actors, spawning),
  - sprite visibility tests and debugging.
- Traversal and splitting are unit-tested, including collinear, degenerate
  and crossing geometry.

### 3.2 Portal wall rendering with column clipping

Render traversal is **front-to-back**. Arrays `clipTop[x]` / `clipBot[x]`
track the still-visible vertical interval of each screen column.

For each subsector:
1. Project its convex floor and ceiling polygons (near-plane clipped) and add
   their per-column spans to the **visplane manager**, intersected with the
   current visible interval. Spans are half-open in screen space so they meet
   wall spans without gaps or double coverage.
2. Draw each boundary seg that faces the camera. Project its endpoints with a
   fixed-point perspective divide; interpolate `1/z` and `u/z` across columns
   for perspective-correct horizontal texture coordinates. Upper/lower wall
   pieces are clipped to the column interval.
3. One-sided walls close their columns. Two-sided walls shrink the interval to
   the portal opening, letting the back sector draw next.

A merged solid-column list lets the traversal skip segs and, once every column
is solid, stop entirely. Stats count nodes, walls, clipped walls and pixels.

### 3.3 Visplanes

`VisplaneManager` groups spans by `(height, flat texture, light, floor/ceiling)`.
Each visplane stores `top[x]`, `bottom[x]`, `minx`, `maxx` and a drawable span
list. New spans are merged when they overlap or touch an existing column span;
when a span is disjoint in a column, a **new visplane with the same key** is
created (the "split" case). This mirrors DOOM's `R_CheckPlane`/`R_CopyPlane`
behaviour, including its limitation: a visplane can hold only one interval per
column.

At end of frame every visplane is rendered exactly once. Because spans were
already clipped by nearer geometry and merges are exact, no pixel is written
twice by the flat pass. Floor/ceiling texture coordinates use the classic
perspective relation `distance = (cameraZ - planeZ) * focal / (y - horizon)`;
per-row distances are computed once per visplane and reused for every column.

Flats are therefore rasterised as **visible span sets**, not as complete floor
polygons: a full polygon would paint hidden pixels behind walls/steps and
would have to be depth-tested or overdraw. The visplane pass paints only
visible area.

### 3.4 Painter's algorithm and sprites

Opaque world geometry uses the BSP/clipping pass (which is not painter's
algorithm ordering). Sprites use painter ordering:

- Compute camera-space depth for every visible sprite.
- Sort back-to-front.
- Reject sprites in sectors that the REJECT table rules out (conservative
  early-out), then project.
- Draw with a per-column/per-pixel depth buffer that walls and flats filled
  during the world pass. Depth testing fixes cases painter ordering alone
  cannot (a sprite partly behind a wall corner), while painter order resolves
  sprite-vs-sprite overlap and alpha blending.

Limitations are documented: cyclic overlap between intersecting billboards
and completely self-intersecting geometry are not representable by either
ordering plus a single depth value per billboard pixel.

### 3.5 Flood fill (distinct from visplanes)

A stack-based **scanline flood fill** labels connected regions of the level's
occupancy raster. It is used at load time to:

- classify enclosed/outdoor regions and validate that the player starts in
  the largest reachable region,
- verify that all enemies and pickups are reachable,
- provide region colouring for the debug minimap.

It is explicitly *not* used as the visplane algorithm. Visplanes are built by
BSP traversal and clipping; flood fill is a separate load-time analysis.

## 4. Fixed-point math

16.16 signed fixed point is used for positions, heights, velocities, all
render projection, texture stepping, collision and AI distances:

- `mul(a,b) = (int64)a*b >> 16`, documented overflow saturation.
- `div(a,b) = ((int64)a << 16)/b`, zero-protected.
- `mulDiv` for 64-bit products used in projection.
- integer `sqrt` via Newton iteration on a 64-bit scaled value.
- BAM angles (`uint32_t`, full turn 2^32), 8192-entry sine table, 4097-entry
  atan table for `atan2Fixed`.
- Vec2 operations, dot/cross, normalisation, angle helpers.

Floating point appears only in content generation, debug output, timing and
conversions at the API boundary. Tests cover sign handling, precision loss,
saturation boundaries and trigonometric accuracy.

## 5. Spatial structures and visibility

### 5.1 REJECT

`Reject` stores an `N*N` bit array, one bit per ordered sector pair. Pair
(i,i) is always visible. A pair is *rejected* only when the two sectors are
not connected through any chain of two-sided portal lines (evaluated with
doors assumed open, because a door can be opened later). This is
**conservative by construction**: every portal-connected pair is marked
possibly-visible, so no genuinely visible target is ever rejected. It is not
DOOM's exact precomputed line-of-sight table; the limitation is documented.

AI perception checks the REJECT bit before doing any ray work. Sprite
collection and the debug overlay also use it. LOS rays are segment tests
accelerated by the BLOCKMAP.

### 5.2 BLOCKMAP

A 128-unit grid covers the world bounding box. Each linedef is inserted into
every cell its bounding box overlaps. Actor queries walk only cells touched by
a circle's bounding box. Duplicate candidate lines are eliminated with a
visited-stamp array, so a line spanning many cells is tested once per query.
Counters expose cells queried, candidates, checks and dedup hits.

Collision uses an infinite-height 2D cylinder:

- circle vs. segment (closest point, push-out normal),
- circle vs. circle for actors/projectiles,
- blocking rules from sector floor/ceiling (step height, head room),
- movement split into substeps small enough to prevent tunnelling.

Broad phase (BLOCKMAP candidate gathering) is kept separate from narrow phase
(exact circle/segment tests). A benchmark compares accelerated and brute-force
queries and verifies identical results.

## 6. Doom fire

An integer intensity grid is seeded at the bottom row, propagates upward with
random horizontal displacement and cooling, and is converted through a
palette LUT. It updates on its own accumulator at 35 Hz independent of the
render frame rate. Size, seed intensity, cooling and RNG seed are
configurable; fixed seeds are deterministic and unit-tested. It is rendered in
the HUD (and as a full-screen demo mode).

## 7. Gameplay

Sectors, doors, step-up/step-down movement, mouse yaw (plus a shifted-horizon
vertical look), WASD, sprint, collision sliding, hitscan pistol, imp
projectiles, health/ammo pickups, HUD, damage flash, weapon bob, minimap,
debug overlays and pause/stat screens.

Enemy AI state machine: Idle → Patrol → Detect → Chase → Attack → Pain → Dead,
with REJECT early-out followed by a geometric LOS ray. Enemies move with the
same collision cylinder and can open/close doors indirectly through the player
(a closed door blocks sight and movement).

## 8. Development phases

| Phase | Contents | Status |
|---|---|---|
| 1 | Toolchain, CMake, app loop, framebuffer, fixed-point, trig, font, SDL present, headless screenshot | **Complete** |
| 2 | World/sector model, BSP build + split tests + point location, map format/loader, sample map generator, texture generator | **Complete** |
| 3 | Wall/portal renderer, visplanes, flats, clipping, debug BSP view | In progress |
| 4 | BLOCKMAP, collision, doors, REJECT, flood-fill validation | Pending |
| 5 | Sprites, painter ordering, enemies, weapons, pickups, HUD | Pending |
| 6 | Doom fire, effects, full HUD, debug overlays | Pending |
| 7 | Tests, benchmarks, docs, limitations, performance pass | Pending |

Each phase ends with a compile, the relevant tests, and a headless screenshot
reviewed before proceeding.

## 9. Verification strategy

- A custom assertion harness (no external test framework) builds one test
  executable, also registered with CTest.
- Tests cover: fixed-point arithmetic/trig, BSP splitting/traversal/degenerate
  geometry, visplane span merging/splitting/boundaries, REJECT indexing and
  conservative behaviour, BLOCKMAP indexing/dedup/collision/tunnelling,
  flood-fill boundaries/regions, fire propagation/cooling/determinism, map
  round-trip serialisation.
- `retrofps --headless --frames N --shot file.bmp` with optional `--cam`
  renders deterministic frames for visual inspection (converted to PNG).
- `retrofps_bench` measures BLOCKMAP vs brute force.
- Every known shortcut or incomplete area is listed in `docs/LIMITATIONS.md`.
