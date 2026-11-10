# Phase 2 — World model, BSP, map loading, debug view

Phase 2 turns the Phase 1 maths/framebuffer skeleton into a real level pipeline:
a sector/linedef world, a genuine BSP tree built over the linedefs, a text map
format with a loader and serialiser, a playable sample map, and a top-down
debug view driven by actual BSP traversal and point location. Everything in
this phase is exercised by the unit tests and by a headless screenshot.

## 1. World model

`src/world/world.h` defines the geometry:

| Type | Fields |
|---|---|
| `Vertex` | 16.16 `x`, `y` |
| `Sector` | `floorHeight`, `ceilHeight`, `floorTex`, `ceilTex`, `light` (0–255), `tag`, `special`, `closedCeil`, `openCeil` |
| `Line` | vertex indices `v1`/`v2`, `frontSector`, `backSector` (`-1` = solid), middle/upper/lower texture indices, `flags`, `special`, `tag` |
| `Thing` | 16.16 `x`/`y`, BAM `angle`, `type` |

Coordinates are 16.16 fixed point in map units; the world is y-down, angle 0
points along +x, and increasing angles turn towards +y. A linedef's **front**
side is its left side (`cross(direction, point - start) >= 0`), matching the
BSP convention.

A sector is one convex floor/ceiling region: the BSP guarantees that all segs
in a subsector share the same front sector. Height differences between
neighbouring sectors become step-up/step-down geometry via the upper and lower
wall textures of the two-sided line between them.

`World::validate()` checks indices, degenerate lines, floor/ceiling ordering
and texture ranges. `World::computeBounds()` produces the map extents used to
seed the BSP leaf clipping.

## 2. Map format and serialisation

Levels are plain text (`.rmap`), described in `src/world/mapfile.h`:

```
version 1
sector  <id> <floor> <ceil> <floorTex> <ceilTex> <light> <tag> <special>
vertex  <id> <x> <y>
linedef <id> <v1> <v2> <front> <back|-1> <mid|-> <up|-> <low|-> <flags> <special> <tag>
thing   <x> <y> <angleDegrees> <type>
```

- Records may appear in any order; ids must be **dense from zero** so vectors
  can be indexed directly. The loader checks this and reports the expected and
  actual counts.
- `#` and `//` start comments; blank lines are ignored.
- Coordinates and heights are in map units and may be fractional; the loader
  converts with rounding to 16.16.
- `-` means "no texture". Names resolve through `TextureSet`; an unknown name
  is an error that includes the source line number.
- Doors: the loader records `closedCeil = ceilHeight` and
  `openCeil = floorHeight + 112`. Gameplay will animate the ceiling between
  them when the door is used; the BSP and REJECT treat the door as open.

`saveMap()` writes the same format with five decimal places per unit, below
the 1/65536 fixed-point resolution. `loadMapText()` is separated from
`loadMap()` so tests can feed malformed maps directly.

## 3. BSP construction

`src/world/bsp.{h,cpp}` builds the tree from the world:

1. **One seg per side.** Every linedef produces a front seg (`side = 0`,
   `frontSector = line.frontSector`, `backSector = line.backSector`) and, for
   two-sided lines, a reversed back seg (`side = 1`, with the sectors swapped).
   This is what lets each subsector boundary carry the sector on its own side.
   `u1`/`u2` are the horizontal texture offsets in map units (0 and the line
   length for the original line).
2. **Partition selection.** For each candidate seg the builder classifies every
   seg into front/back/collinear/crossing and scores
   `splits * 16 + abs(front - back)`; the lowest score wins. Candidates that do
   not separate the list are rejected. A DOOM-style score, heavily penalising
   splits.
3. **Exact splitting.** A crossing seg is split at the exact fixed-point line
   intersection parameter `t` (computed with a 128-bit intermediate). Both
   halves inherit line index, side, sectors and interpolated `u`. A
   numerically parallel crossing falls back to the midpoint side.
4. **Recursion.** The node stores origin, unit direction, source line and a
   per-subtree bounding box. Segs are partitioned into front/back lists and the
   recursion continues until one seg remains or `kMaxDepth` (64) is reached.
5. **Leaf regions.** The map bounding rectangle is clipped along every
   partition half-plane on the path to the leaf. At the leaf it is further
   clipped by the **front half-plane of every leaf seg**. The result is the
   exact convex subsector polygon, including boundaries that are BSP partition
   lines rather than linedefs.

Because a leaf is reached through the tree, every point inside its polygon
maps to that leaf; `subsectorForPoint()` validates the point against the
polygon, so points in solid void (outside the map or inside a pillar's hole)
return `nullptr` and `sectorAtPoint()` returns `-1`.

### Correctness fixes found by the Phase 2 tests

The first sample-map build produced `badLeaves > 0`, shrunk polygons and wrong
point location. Two root causes were fixed:

- **Direction-aware collinear classification.** Collinear segs were all sent to
  the front child, which placed both sides of a two-sided line in the same
  leaf (breaking the one-sector invariant) and could drop a wall out of its
  own leaf. `collinearSide()` now compares the seg direction with the
  partition direction: a seg running with the partition belongs to the front
  child, one running against it to the back child, preserving the sector on
  each side.
- **Exact leaf polygons.** Leaf polygons were originally convex hulls of seg
  endpoints. That is wrong when a leaf edge comes from an ancestor partition
  line that is not a seg. The builder now clips the path region by each seg's
  front half-plane, computing edge intersections in fixed point with a 128-bit
  intermediate (`__extension__ typedef __int128 int128_t` keeps `-Wpedantic`
  clean) and a double fallback.

After both fixes: sample map `badLeaves == 0`, all room/door/pillar/void point
queries correct, and all tests pass.

## 4. Traversal and point location

- `Bsp::traverse(camera, frontToBack, fn, nodesVisitedOut)` is an iterative
  stack traversal (no recursion depth risk). The camera side selects the near
  child, which is pushed last so it is visited first. Front-to-back drives the
  portal renderer; back-to-front exists for painter-style debugging and tests.
  Optionally reports how many internal nodes were popped (used by the stats
  overlay).
- `Bsp::subsectorAtPoint`, `sectorAtPoint`, `subsectorForPoint` do point
  location. A tree that is a single leaf is handled explicitly.
- `Bsp::numSplits`, `maxDepth`, `badLeaves` expose build statistics.

## 5. Sample map

`assets/sample.rmap` is generated by `tools/gen_map.py` from a 17×13 grid of
64-unit cells:

```
#################
#aaaaa#bbbbb#ccc#
#aaaaa#bbbbb#ccc#
#aa#aa#bbbbb#ccc#
#aaaaaDbbbbbEccc#
#aaaaa#bbbbb#c#c#
#aaaaa#bb#bb#ccc#
#aaaaa#bbbbb#ccc#
#aaaaa#bbbbb#ccc#
#####H##G####I###
#####fffffffffff#
#####fffffffffff#
#################
```

- 9 sectors: rooms `a` (0), `b` (1), `c` (2), corridor `f` (3), and five door
  sectors `D`/`E`/`H`/`G`/`I` (4–8), each with `special = 1` and tags 1–5.
- Varied heights (floors 0 / 16 / -8; ceilings 128 / 160 / 112 / 96), four
  wall textures plus floor/ceiling flats, per-sector light levels.
- Three pillars (black holes in the debug view) formed by one-sided inner
  boundaries.
- Things: player start at `(160, 288)` facing 0°, four imps, two health
  pickups, two ammo pickups.
- Build statistics: **9 sectors, 112 vertices, 122 linedefs, 9 things**;
  BSP **26 nodes, 27 leaves, 0 splits, depth 6**.

The generator emits each cell boundary with the owning cell on the front,
dedupes shared edges by sorted endpoints (first row-major cell wins) and
dedupes vertices by grid coordinate. One-sided lines get the cell's wall
texture as their middle texture; two-sided lines use `-` for the middle and
`DOOR` or `METAL`/`STEP` for upper/lower where neighbouring heights differ.

The sample map happens not to need BSP splits (all lines are axis-aligned and
partitions separate cleanly). Splitting is therefore tested separately with the
L-shaped-room test, which asserts `splits > 0`, pure leaves and correct point
location including the notch and the outside.

## 6. Tests

`tests/test_main.cpp` uses a small custom harness (`TEST`, `CHECK`, `REQUIRE`)
and covers, among others:

- fixed conversions, signed multiply/divide, precision/overflow, sqrt;
- trig accuracy and BAM wrapping (from Phase 1);
- BSP point side and intersection parameter;
- L-shape construction with a guaranteed split, leaf purity,
  `segs().size() == 6 + splits`, point location in the notch and outside;
- traversal order: front-to-back and back-to-front are exact reverses, every
  leaf is visited exactly once;
- degenerate input: empty world, single line, zero-length line;
- sample map loads and builds: counts, bounds, `badLeaves == 0`, every sector
  has a valid polygon, two-sided line indexing;
- sector location for every room, door, pillar and outside void; every thing
  is in a valid sector;
- complete traversal from the player start and from outside the map; order
  reversal;
- map save/reload round trip (sectors, vertices, linedefs, things);
- malformed maps: missing version, unknown texture, bad indices,
  non-contiguous ids, missing field.

Result: **2261 checks, 0 failures, 13/13 test groups passed.**

## 7. Debug view and screenshot

The Phase 2 build replaced the Phase 1 gradient with a top-down BSP/sector
debug view rendered by the real game code:

- the map fits into the area right of a 218 px info panel using fixed-point
  scale computation;
- subsector leaves are filled front-to-back in sector colours, shaded by
  sector light and traversal order;
- leaf outlines, BSP partition lines (F2), linedefs (one-sided bright,
  two-sided grey, doors amber), things, pillars and the camera arrow are drawn;
- the panel lists vertex/line/sector/thing counts, BSP nodes/leaves/splits/
  depth/bad leaves, nodes traversed and leaves drawn this frame, camera sector,
  position and yaw, the legend and the controls;
- the footer shows frame time and FPS.

`phase2.bmp` / `phase2.png` are the verified headless output. The screenshot
shows the four rooms in distinct colours, the corridor, all five amber doors,
the three pillars as voids, BSP partition lines and the player arrow. The
readouts and legend were moved into the side panel after an earlier layout
overlapped the map.

## 8. Known limitations at the end of Phase 2

- Movement has no collision radius or step/door rules yet: a point is accepted
  if it lies in a valid sector, so the camera can hug walls. Phase 4 adds the
  BLOCKMAP circle/cylinder collision.
- The rendered world is the temporary top-down view. The 3D portal renderer,
  visplanes and flats are Phase 3.
- Doors do not animate yet; their `special`/`tag` and `closedCeil`/`openCeil`
  data are loaded and ready for Phase 4.
- The BSP is built once at load time; there is no incremental rebuild.
- The sample map contains no crossing linedefs, so its BSP splits count is
  zero; splitting is proven by tests rather than by the sample.
- No sprites, enemies, weapons, HUD, fire, REJECT or BLOCKMAP yet; things are
  drawn only as debug markers.

## 9. Build and run

```powershell
cd C:\Users\ADMIN\Projects\retro-fps
.\build.ps1                 # Release
.\build.ps1 -Clean          # fresh configure + build
build\bin\retroffps_tests.exe
build\bin\retrofps.exe
build\bin\retrofps.exe --headless --frames 3 --shot phase2.bmp --map assets\sample.rmap
```
