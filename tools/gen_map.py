#!/usr/bin/env python3
"""Generates assets/sample.rmap (the playable demo level).

The level is authored as a grid of 64-unit cells. Rooms, doors and solid
cells are turned into oriented linedefs: each boundary is emitted once, with
the owning cell's sector on the front (left) side. Vertices are shared by
grid coordinate, so the BSP receives a watertight sector partition.

Run from anywhere:  python tools/gen_map.py [output.rmap]
"""

from __future__ import annotations

import sys
from pathlib import Path

CELL = 64

# Cell characters: lowercase = sectors, uppercase = door sectors, '#' = solid.
GRID = [
    "#################",
    "#aaaaa#bbbbb#ccc#",
    "#aaaaa#bbbbb#ccc#",
    "#aa#aa#bbbbb#ccc#",
    "#aaaaaDbbbbbEccc#",
    "#aaaaa#bbbbb#c#c#",
    "#aaaaa#bb#bb#ccc#",
    "#aaaaa#bbbbb#ccc#",
    "#aaaaa#bbbbb#ccc#",
    "#####H##G####I###",
    "#####fffffffffff#",
    "#####fffffffffff#",
    "#################",
]

# sector id, floor, ceiling, floor texture, ceiling texture, light, tag, special
SECTORS = {
    "a": (0, 0, 128, "FLOOR", "CEIL", 180, 0, 0),
    "b": (1, 16, 160, "TILE", "CEIL", 150, 0, 0),
    "c": (2, 16, 112, "TILE", "CEIL", 120, 0, 0),
    "f": (3, -8, 96, "FLOOR", "CEIL", 80, 0, 0),
    # Door sectors start closed: floor == ceiling. Tag identifies the door.
    "D": (4, 16, 16, "FLOOR", "CEIL", 130, 1, 1),
    "E": (5, 16, 16, "FLOOR", "CEIL", 130, 2, 1),
    "H": (6, -8, -8, "FLOOR", "CEIL", 100, 3, 1),
    "G": (7, 16, 16, "FLOOR", "CEIL", 100, 4, 1),
    "I": (8, 16, 16, "FLOOR", "CEIL", 100, 5, 1),
}

WALL_TEXTURE = {
    0: "BRICK",
    1: "STONE",
    2: "TECH",
    3: "GRATE",
    4: "DOOR",
    5: "DOOR",
    6: "DOOR",
    7: "DOOR",
    8: "DOOR",
}

THINGS = [
    # x, y, angle degrees, type (1 player start, 2 imp, 3 health, 4 ammo)
    (160, 288, 0, 1),
    (544, 160, 180, 2),
    (992, 288, 180, 2),
    (768, 704, 180, 2),
    (416, 704, 0, 2),
    (224, 96, 0, 3),
    (944, 704, 0, 3),
    (608, 96, 0, 4),
    (704, 704, 0, 4),
]

HEIGHT = len(GRID)
WIDTH = len(GRID[0])


class MapBuilder:
    def __init__(self) -> None:
        self.vertices: list[tuple[int, int]] = []
        self.vertex_ids: dict[tuple[int, int], int] = {}
        self.lines: list[tuple[int, int, int, int, str, str, str]] = []
        self.edge_keys: set[tuple[tuple[int, int], tuple[int, int]]] = set()

    def vertex(self, gx: int, gy: int) -> int:
        key = (gx, gy)
        if key not in self.vertex_ids:
            self.vertex_ids[key] = len(self.vertices)
            self.vertices.append((gx * CELL, gy * CELL))
        return self.vertex_ids[key]

    def add_edge(
        self,
        a: tuple[int, int],
        b: tuple[int, int],
        front: int,
        back: int,
    ) -> None:
        # Each shared edge is emitted by both neighbouring cells in opposite
        # directions. Keep the first (row-major) one, which puts that cell's
        # sector on the front side.
        key = (min(a, b), max(a, b))
        if key in self.edge_keys:
            return
        self.edge_keys.add(key)

        if back < 0:
            middle = WALL_TEXTURE[front]
            upper = "-"
            lower = "-"
        else:
            middle = "-"
            sf = SECTORS_BY_ID[front]
            sb = SECTORS_BY_ID[back]
            door = sf[7] == 1 or sb[7] == 1
            # A single texture per side is used for both viewing directions.
            upper = "DOOR" if door else "METAL"
            lower = "DOOR" if door else "STEP"
            if sf[2] == sb[2]:
                upper = "-"
            if sf[1] == sb[1]:
                lower = "-"

        self.lines.append(
            (self.vertex(*a), self.vertex(*b), front, back, middle, upper, lower)
        )


SECTORS_BY_ID = {data[0]: data for data in SECTORS.values()}


def cell_sector(cx: int, cy: int) -> int:
    if cx < 0 or cy < 0 or cy >= HEIGHT or cx >= WIDTH:
        return -1
    ch = GRID[cy][cx]
    if ch == "#":
        return -1
    return SECTORS[ch][0]


def build() -> MapBuilder:
    b = MapBuilder()
    for cy in range(HEIGHT):
        for cx in range(WIDTH):
            front = cell_sector(cx, cy)
            if front < 0:
                continue

            neighbours = [
                # (neighbour, oriented edge for this cell)
                ((cx, cy - 1), ((cx, cy), (cx + 1, cy))),                  # north
                ((cx, cy + 1), ((cx + 1, cy + 1), (cx, cy + 1))),          # south
                ((cx - 1, cy), ((cx, cy + 1), (cx, cy))),                  # west
                ((cx + 1, cy), ((cx + 1, cy), (cx + 1, cy + 1))),          # east
            ]
            for (nx, ny), (a, c) in neighbours:
                back = cell_sector(nx, ny)
                if back == front:
                    continue
                b.add_edge(a, c, front, back)
    return b


def main() -> int:
    if WIDTH != 17 or HEIGHT != 13:
        print(f"unexpected grid size {WIDTH}x{HEIGHT}", file=sys.stderr)
        return 1

    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path(__file__).resolve().parent.parent / "assets" / "sample.rmap"
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)

    b = build()
    lines = ["# Generated by tools/gen_map.py -- do not edit by hand.", "version 1", ""]
    for ch in "abcfDEHGI":
        sid, floor, ceil, ftex, ctex, light, tag, special = SECTORS[ch]
        lines.append(
            f"sector {sid} {floor} {ceil} {ftex} {ctex} {light} {tag} {special}"
        )
    lines.append("")
    for i, (x, y) in enumerate(b.vertices):
        lines.append(f"vertex {i} {x} {y}")
    lines.append("")
    for i, (v1, v2, front, back, mid, up, low) in enumerate(b.lines):
        lines.append(
            f"linedef {i} {v1} {v2} {front} {back} {mid} {up} {low} 0 0 0"
        )
    lines.append("")
    for x, y, angle, kind in THINGS:
        lines.append(f"thing {x} {y} {angle} {kind}")
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="ascii")
    print(
        f"Wrote {out_path} ({len(SECTORS)} sectors, {len(b.vertices)} vertices, "
        f"{len(b.lines)} linedefs, {len(THINGS)} things)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
