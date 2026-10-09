#!/usr/bin/env python3
# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

"""Generate and verify Ammerow's independently authored vault replacement set.

The CSV manifest is the creative and mechanical source for this one-time data
generation pass.  The game continues to read only lib/gamedata/vault.txt.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import heapq
from dataclasses import dataclass
from pathlib import Path
import sys


ALLOWED_SYMBOLS = set(" %@#*:`/;&+^<>1234567890~$]|=\"!?_-,.abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
BLOCKING_SYMBOLS = set(" %@#")
TYPE_LIMITS = {
    "Lesser vault (new)": (22, 22),
    "Medium vault (new)": (22, 33),
    "Greater vault (new)": (44, 66),
}
EXPECTED_COUNTS = {
    "Lesser vault (new)": 6,
    "Medium vault (new)": 15,
    "Greater vault (new)": 12,
}
EXPECTED_RATINGS = {
    "Lesser vault (new)": 31,
    "Medium vault (new)": 161,
    "Greater vault (new)": 300,
}
EXPECTED_BUDGETS = {
    "Lesser vault (new)": {"pressure": 100, "rewards": 58, "hazards": 6},
    "Medium vault (new)": {"pressure": 125, "rewards": 55, "hazards": 140},
    "Greater vault (new)": {"pressure": 740, "rewards": 180, "hazards": 520},
}
# The ranges are within ten percent of the frozen inherited footprint.
EXPECTED_CELL_RANGES = {
    "Lesser vault (new)": (1356, 1658),
    "Medium vault (new)": (6739, 8237),
    "Greater vault (new)": (16177, 19771),
}
DEPTH_SAMPLES = (1, 20, 40, 60, 80, 99)


@dataclass(frozen=True)
class Blueprint:
    name: str
    vault_type: str
    rating: int
    rows: int
    columns: int
    min_depth: int
    max_depth: int
    flags: str
    family: str
    seed: int
    pressure: int
    rewards: int
    hazards: int
    terrain: int
    theme: str


class StableRng:
    """Small explicitly specified generator for reproducible data placement."""

    def __init__(self, seed: int) -> None:
        self.state = seed & 0xFFFFFFFF or 0x6D2B79F5

    def next(self) -> int:
        value = self.state
        value ^= (value << 13) & 0xFFFFFFFF
        value ^= value >> 17
        value ^= (value << 5) & 0xFFFFFFFF
        self.state = value & 0xFFFFFFFF
        return self.state

    def choice(self, values: str) -> str:
        return values[self.next() % len(values)]

    def shuffle(self, values: list[tuple[int, int]]) -> None:
        for index in range(len(values) - 1, 0, -1):
            other = self.next() % (index + 1)
            values[index], values[other] = values[other], values[index]


class Canvas:
    def __init__(self, blueprint: Blueprint) -> None:
        self.blueprint = blueprint
        self.rows = blueprint.rows
        self.columns = blueprint.columns
        self.grid = [[" " for _ in range(self.columns)] for _ in range(self.rows)]
        self.protected: set[tuple[int, int]] = set()
        self._make_shell()

    def _make_shell(self) -> None:
        wall = "@" if self.blueprint.flags == "FEW_ENTRANCES" else "%"
        for y in range(self.rows):
            for x in range(self.columns):
                self.grid[y][x] = wall if self.is_boundary(y, x) else "."

        if self.blueprint.flags == "FEW_ENTRANCES":
            entrances = [(self.rows // 2, 0)]
            if self.rows * self.columns >= 700:
                entrances.append((self.rows - 1, self.columns // 2))
            for y, x in entrances:
                self.grid[y][x] = "%"
                inside_y = y + (1 if y == 0 else -1 if y == self.rows - 1 else 0)
                inside_x = x + (1 if x == 0 else -1 if x == self.columns - 1 else 0)
                for py in range(max(1, inside_y - 1), min(self.rows - 1, inside_y + 2)):
                    for px in range(max(1, inside_x - 1), min(self.columns - 1, inside_x + 2)):
                        self.protected.add((py, px))

    def is_boundary(self, y: int, x: int) -> bool:
        return y == 0 or x == 0 or y == self.rows - 1 or x == self.columns - 1

    def put(self, y: int, x: int, symbol: str, replace: str = ".") -> None:
        if (y, x) in self.protected or self.is_boundary(y, x):
            return
        if self.grid[y][x] in replace:
            self.grid[y][x] = symbol

    def horizontal(self, y: int, start: int, end: int, gaps: set[int], symbol: str = "#") -> None:
        for x in range(max(1, start), min(self.columns - 1, end + 1)):
            if x not in gaps:
                self.put(y, x, symbol)

    def vertical(self, x: int, start: int, end: int, gaps: set[int], symbol: str = "#") -> None:
        for y in range(max(1, start), min(self.rows - 1, end + 1)):
            if y not in gaps:
                self.put(y, x, symbol)

    def room(self, top: int, left: int, bottom: int, right: int, door_side: int) -> None:
        if bottom - top < 3 or right - left < 3:
            return
        for x in range(left, right + 1):
            self.put(top, x, "#")
            self.put(bottom, x, "#")
        for y in range(top, bottom + 1):
            self.put(y, left, "#")
            self.put(y, right, "#")
        if door_side == 0:
            self.grid[top][(left + right) // 2] = "+"
        elif door_side == 1:
            self.grid[(top + bottom) // 2][right] = "+"
        elif door_side == 2:
            self.grid[bottom][(left + right) // 2] = "+"
        else:
            self.grid[(top + bottom) // 2][left] = "+"

    def floor_cells(self) -> list[tuple[int, int]]:
        return [
            (y, x)
            for y in range(1, self.rows - 1)
            for x in range(1, self.columns - 1)
            if self.grid[y][x] == "." and (y, x) not in self.protected
        ]

    def lines(self) -> list[str]:
        return ["".join(row) for row in self.grid]


def load_blueprints(path: Path) -> list[Blueprint]:
    blueprints: list[Blueprint] = []
    with path.open(newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            blueprints.append(
                Blueprint(
                    name=row["name"],
                    vault_type=row["type"],
                    rating=int(row["rating"]),
                    rows=int(row["rows"]),
                    columns=int(row["columns"]),
                    min_depth=int(row["min_depth"]),
                    max_depth=int(row["max_depth"]),
                    flags=row["flags"],
                    family=row["family"],
                    seed=int(row["seed"]),
                    pressure=int(row["pressure"]),
                    rewards=int(row["rewards"]),
                    hazards=int(row["hazards"]),
                    terrain=int(row["terrain"]),
                    theme=row["theme"],
                )
            )
    return blueprints


def pattern_rings(canvas: Canvas, rng: StableRng) -> None:
    limit = min(canvas.rows, canvas.columns) // 2
    for margin in range(3, limit - 1, 4):
        top, left = margin, margin
        bottom, right = canvas.rows - margin - 1, canvas.columns - margin - 1
        if bottom - top < 4 or right - left < 4:
            break
        horizontal_gaps = {left + 1 + rng.next() % (right - left - 1)}
        vertical_gaps = {top + 1 + rng.next() % (bottom - top - 1)}
        canvas.horizontal(top, left, right, horizontal_gaps)
        canvas.horizontal(bottom, left, right, {right - (next(iter(horizontal_gaps)) - left)})
        canvas.vertical(left, top, bottom, vertical_gaps)
        canvas.vertical(right, top, bottom, {bottom - (next(iter(vertical_gaps)) - top)})


def pattern_terraces(canvas: Canvas, rng: StableRng) -> None:
    for index, y in enumerate(range(3, canvas.rows - 2, 4)):
        first = 3 + (rng.next() % max(1, canvas.columns - 8))
        second = max(2, min(canvas.columns - 3, first + (-5 if index % 2 else 5)))
        canvas.horizontal(y, 2, canvas.columns - 3, {first, second})


def pattern_ribs(canvas: Canvas, rng: StableRng) -> None:
    for index, x in enumerate(range(3, canvas.columns - 2, 4)):
        first = 3 + (rng.next() % max(1, canvas.rows - 8))
        second = max(2, min(canvas.rows - 3, first + (-4 if index % 2 else 4)))
        canvas.vertical(x, 2, canvas.rows - 3, {first, second})


def pattern_spokes(canvas: Canvas, rng: StableRng) -> None:
    centre_y, centre_x = canvas.rows // 2, canvas.columns // 2
    canvas.horizontal(centre_y, 2, canvas.columns - 3, {centre_x - 1, centre_x, centre_x + 1})
    canvas.vertical(centre_x, 2, canvas.rows - 3, {centre_y - 1, centre_y, centre_y + 1})
    for offset in range(3, min(centre_y, centre_x), 3):
        canvas.put(centre_y - offset, centre_x - offset, "#")
        canvas.put(centre_y - offset, centre_x + offset, "#")
        canvas.put(centre_y + offset, centre_x - offset, "#")
        canvas.put(centre_y + offset, centre_x + offset, "#")


def pattern_districts(canvas: Canvas, rng: StableRng) -> None:
    room_height = 5 if canvas.rows < 25 else 7
    room_width = 7 if canvas.columns < 35 else 9
    index = 0
    for top in range(2, canvas.rows - room_height, room_height + 2):
        shift = 0 if index % 2 == 0 else room_width // 2
        for left in range(2 + shift, canvas.columns - room_width, room_width + 3):
            canvas.room(top, left, top + room_height - 1, left + room_width - 1,
                        (index + rng.next()) % 4)
            index += 1


def pattern_islands(canvas: Canvas, rng: StableRng) -> None:
    for y in range(3, canvas.rows - 2, 5):
        shift = 2 if (y // 5) % 2 else 0
        for x in range(3 + shift, canvas.columns - 3, 7):
            height = 2 + rng.next() % 2
            width = 3 + rng.next() % 3
            for iy in range(y, min(canvas.rows - 1, y + height)):
                for ix in range(x, min(canvas.columns - 1, x + width)):
                    canvas.put(iy, ix, "#")


def pattern_sluice(canvas: Canvas, rng: StableRng) -> None:
    for index, y in enumerate(range(3, canvas.rows - 2, 5)):
        for x in range(1, canvas.columns - 1):
            if canvas.grid[y][x] == ".":
                canvas.grid[y][x] = "/"
        gate = 3 + rng.next() % max(1, canvas.columns - 8)
        wall_y = y + 1 if y + 1 < canvas.rows - 1 else y - 1
        canvas.horizontal(wall_y, 2, canvas.columns - 3, {gate, gate + 1})


def pattern_cloister(canvas: Canvas, rng: StableRng) -> None:
    pattern_rings(canvas, rng)
    centre_y, centre_x = canvas.rows // 2, canvas.columns // 2
    for dy, dx in ((-3, -4), (-3, 4), (3, -4), (3, 4)):
        top, left = centre_y + dy - 2, centre_x + dx - 2
        canvas.room(top, left, top + 4, left + 4, (dy + dx) % 4)


def pattern_causeway(canvas: Canvas, rng: StableRng) -> None:
    first = canvas.rows // 3
    second = canvas.rows - first - 1
    for y in (first, second):
        gaps = {2 + rng.next() % max(1, canvas.columns - 5)}
        gap = next(iter(gaps))
        gaps.add(min(canvas.columns - 3, gap + 1))
        canvas.horizontal(y, 2, canvas.columns - 3, gaps)
    for y in range(first + 1, second):
        if y % 3 == 0:
            for x in range(2, canvas.columns - 2):
                if canvas.grid[y][x] == ".":
                    canvas.grid[y][x] = "/"


def pattern_braid(canvas: Canvas, rng: StableRng) -> None:
    span = max(3, canvas.columns - 4)
    for y in range(2, canvas.rows - 2):
        left = 2 + ((y * 2 + rng.next() % 3) % span)
        right = canvas.columns - left - 1
        if y % 4 != 0:
            canvas.put(y, left, "#")
        if y % 5 != 0:
            canvas.put(y, right, "#")


def pattern_galleries(canvas: Canvas, rng: StableRng) -> None:
    for y in range(3, canvas.rows - 2, 3):
        gate = 1 + (y // 3 + rng.next()) % (canvas.columns - 2)
        canvas.horizontal(y, 1, canvas.columns - 2, {gate})
        canvas.grid[y][gate] = "+"


def pattern_quarry(canvas: Canvas, rng: StableRng) -> None:
    for y in range(2, canvas.rows - 2, 3):
        inset = 2 + (y // 3) % max(2, canvas.columns // 6)
        gate = inset + rng.next() % max(1, canvas.columns - 2 * inset)
        canvas.horizontal(y, inset, canvas.columns - inset - 1, {gate, min(gate + 1, canvas.columns - 2)})
        for x in range(inset + 1, canvas.columns - inset - 1, 4):
            canvas.put(min(canvas.rows - 2, y + 1), x, "*", replace=".")


def pattern_engine(canvas: Canvas, rng: StableRng) -> None:
    pattern_rings(canvas, rng)
    centre_y, centre_x = canvas.rows // 2, canvas.columns // 2
    canvas.horizontal(centre_y, 2, canvas.columns - 3,
                      {centre_x - 2, centre_x - 1, centre_x, centre_x + 1, centre_x + 2})
    canvas.vertical(centre_x, 2, canvas.rows - 3,
                    {centre_y - 2, centre_y - 1, centre_y, centre_y + 1, centre_y + 2})


def pattern_switchyard(canvas: Canvas, rng: StableRng) -> None:
    for index, x in enumerate(range(4, canvas.columns - 3, 6)):
        gaps = set()
        for y in range(3 + index % 3, canvas.rows - 2, 7):
            gaps.update((y, min(canvas.rows - 3, y + 1)))
        canvas.vertical(x, 2, canvas.rows - 3, gaps)
    for y in range(5, canvas.rows - 4, 8):
        start = 2 + rng.next() % max(1, canvas.columns // 3)
        canvas.horizontal(y, start, min(canvas.columns - 3, start + canvas.columns // 2),
                          {start + 2})


PATTERNS = {
    "rings": pattern_rings,
    "terraces": pattern_terraces,
    "ribs": pattern_ribs,
    "spokes": pattern_spokes,
    "districts": pattern_districts,
    "islands": pattern_islands,
    "sluice": pattern_sluice,
    "cloister": pattern_cloister,
    "causeway": pattern_causeway,
    "braid": pattern_braid,
    "galleries": pattern_galleries,
    "quarry": pattern_quarry,
    "engine": pattern_engine,
    "switchyard": pattern_switchyard,
}


def place_content(canvas: Canvas, blueprint: Blueprint, rng: StableRng) -> None:
    cells = canvas.floor_cells()
    rng.shuffle(cells)
    required = blueprint.terrain + blueprint.hazards + blueprint.rewards + blueprint.pressure
    if required > len(cells):
        raise ValueError(f"{blueprint.name}: content budget exceeds available floor")

    water_theme = any(word in blueprint.name for word in (
        "Rain", "Cistern", "Tide", "Silt", "Water", "Sluice", "Weir",
        "Aqueduct", "Drowned", "Seep", "Reservoir", "Watershed", "Blackwater",
    ))
    fire_theme = any(word in blueprint.name for word in ("Cinder", "Pumice", "Ember"))
    mineral_theme = any(word in blueprint.name for word in ("Ashlar", "Quarry", "Brakeworks"))

    cursor = 0
    terrain_pool = "/::**" if water_theme else "`::**" if fire_theme else "**:::" if mineral_theme else ":**;;"
    for y, x in cells[cursor:cursor + blueprint.terrain]:
        canvas.grid[y][x] = rng.choice(terrain_pool)
    cursor += blueprint.terrain

    hazard_pool = "^^^`" if fire_theme else "^^^^"
    for y, x in cells[cursor:cursor + blueprint.hazards]:
        canvas.grid[y][x] = rng.choice(hazard_pool)
    cursor += blueprint.hazards

    reward_pool = "&&&&3355~7"
    for y, x in cells[cursor:cursor + blueprint.rewards]:
        canvas.grid[y][x] = rng.choice(reward_pool)
    cursor += blueprint.rewards

    pressure_pool = "112244"
    if blueprint.min_depth >= 20 or blueprint.rating >= 15:
        pressure_pool += "466"
    if blueprint.min_depth >= 45 or blueprint.rating >= 25:
        pressure_pool += "990"
    if blueprint.min_depth >= 80 or blueprint.rating >= 40:
        pressure_pool += "08"
    for y, x in cells[cursor:cursor + blueprint.pressure]:
        canvas.grid[y][x] = rng.choice(pressure_pool)


def reachable_cells(lines: list[str]) -> tuple[set[tuple[int, int]], set[tuple[int, int]]]:
    rows, columns = len(lines), len(lines[0])
    starts: list[tuple[int, int]] = []
    for y in range(rows):
        for x in range(columns):
            if lines[y][x] != "%":
                continue
            for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < rows and 0 <= nx < columns and lines[ny][nx] not in BLOCKING_SYMBOLS:
                    starts.append((ny, nx))
    traversable = {
        (y, x)
        for y in range(rows)
        for x in range(columns)
        if lines[y][x] not in BLOCKING_SYMBOLS
    }
    seen: set[tuple[int, int]] = set()
    pending = list(starts)
    while pending:
        cell = pending.pop()
        if cell in seen or cell not in traversable:
            continue
        seen.add(cell)
        y, x = cell
        pending.extend(((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)))
    return seen, traversable


def connect_layout(canvas: Canvas) -> None:
    """Join isolated walkable pockets with the fewest possible new doors."""
    while True:
        lines = canvas.lines()
        reached, traversable = reachable_cells(lines)
        missing = traversable - reached
        if not missing:
            return
        if not reached:
            raise ValueError(f"{canvas.blueprint.name}: no walkable entrance")

        start = min(missing)
        pending: list[tuple[int, int, int, int]] = [(0, 0, start[0], start[1])]
        best: dict[tuple[int, int], tuple[int, int]] = {start: (0, 0)}
        previous: dict[tuple[int, int], tuple[int, int]] = {}
        destination: tuple[int, int] | None = None
        while pending:
            cost, steps, y, x = heapq.heappop(pending)
            cell = (y, x)
            if best.get(cell) != (cost, steps):
                continue
            if cell in reached:
                destination = cell
                break
            for dy, dx in ((-1, 0), (0, -1), (0, 1), (1, 0)):
                ny, nx = y + dy, x + dx
                if not (1 <= ny < canvas.rows - 1 and 1 <= nx < canvas.columns - 1):
                    continue
                next_cell = (ny, nx)
                next_cost = cost + (1 if canvas.grid[ny][nx] in BLOCKING_SYMBOLS else 0)
                candidate = (next_cost, steps + 1)
                if candidate >= best.get(next_cell, (sys.maxsize, sys.maxsize)):
                    continue
                best[next_cell] = candidate
                previous[next_cell] = cell
                heapq.heappush(pending, (next_cost, steps + 1, ny, nx))

        if destination is None:
            raise ValueError(f"{canvas.blueprint.name}: cannot connect isolated floor")
        cursor = destination
        while cursor != start:
            y, x = cursor
            if canvas.grid[y][x] in BLOCKING_SYMBOLS:
                canvas.grid[y][x] = "+"
            cursor = previous[cursor]


def validate_blueprint_set(blueprints: list[Blueprint]) -> None:
    names = [blueprint.name for blueprint in blueprints]
    seeds = [blueprint.seed for blueprint in blueprints]
    if len(names) != len(set(names)):
        raise ValueError("blueprint names must be unique")
    if len(seeds) != len(set(seeds)):
        raise ValueError("blueprint seeds must be unique")
    counts = {vault_type: 0 for vault_type in EXPECTED_COUNTS}
    for blueprint in blueprints:
        if blueprint.vault_type not in TYPE_LIMITS:
            raise ValueError(f"{blueprint.name}: unsupported type {blueprint.vault_type}")
        max_rows, max_columns = TYPE_LIMITS[blueprint.vault_type]
        if not 5 <= blueprint.rows <= max_rows or not 5 <= blueprint.columns <= max_columns:
            raise ValueError(f"{blueprint.name}: dimensions exceed the room contract")
        if blueprint.max_depth and blueprint.min_depth > blueprint.max_depth:
            raise ValueError(f"{blueprint.name}: inverted depth range")
        if blueprint.flags not in ("", "FEW_ENTRANCES"):
            raise ValueError(f"{blueprint.name}: unsupported flags")
        if blueprint.family not in PATTERNS:
            raise ValueError(f"{blueprint.name}: unknown family {blueprint.family}")
        counts[blueprint.vault_type] += 1
    if counts != EXPECTED_COUNTS:
        raise ValueError(f"wrong category counts: {counts}")
    for vault_type in EXPECTED_COUNTS:
        group = [blueprint for blueprint in blueprints if blueprint.vault_type == vault_type]
        rating = sum(blueprint.rating for blueprint in group)
        if rating != EXPECTED_RATINGS[vault_type]:
            raise ValueError(f"{vault_type}: rating total changed from the frozen contract")
        cells = sum(blueprint.rows * blueprint.columns for blueprint in group)
        low, high = EXPECTED_CELL_RANGES[vault_type]
        if not low <= cells <= high:
            raise ValueError(f"{vault_type}: footprint total left the balance envelope")
        for field, expected in EXPECTED_BUDGETS[vault_type].items():
            if sum(getattr(blueprint, field) for blueprint in group) != expected:
                raise ValueError(f"{vault_type}: {field} budget changed from the contract")
        for depth in DEPTH_SAMPLES:
            candidates = sum(
                blueprint.min_depth <= depth
                and (not blueprint.max_depth or blueprint.max_depth >= depth)
                for blueprint in group
            )
            if candidates < 4:
                raise ValueError(f"{vault_type}: only {candidates} candidates at depth {depth}")


def generate(blueprint: Blueprint) -> list[str]:
    rng = StableRng(blueprint.seed)
    canvas = Canvas(blueprint)
    PATTERNS[blueprint.family](canvas, rng)
    connect_layout(canvas)
    place_content(canvas, blueprint, rng)
    lines = canvas.lines()
    if len(lines) != blueprint.rows or any(len(line) != blueprint.columns for line in lines):
        raise ValueError(f"{blueprint.name}: generated dimensions do not match declaration")
    if any(character not in ALLOWED_SYMBOLS for line in lines for character in line):
        raise ValueError(f"{blueprint.name}: generated an unsupported symbol")
    for x in range(blueprint.columns):
        if lines[0][x] not in " %@#" or lines[-1][x] not in " %@#":
            raise ValueError(f"{blueprint.name}: open top or bottom boundary")
    for y in range(blueprint.rows):
        if lines[y][0] not in " %@#" or lines[y][-1] not in " %@#":
            raise ValueError(f"{blueprint.name}: open side boundary")
    reached, traversable = reachable_cells(lines)
    if not traversable or reached != traversable:
        raise ValueError(
            f"{blueprint.name}: {len(reached)}/{len(traversable)} "
            "traversable cells reach an entrance"
        )
    return lines


def block(blueprint: Blueprint, lines: list[str]) -> str:
    result = [
        f"# Original Ammerow blueprint: {blueprint.theme}.",
        f"# Reproducible family {blueprint.family}; seed {blueprint.seed}.",
        f"name:{blueprint.name}",
        f"type:{blueprint.vault_type}",
        f"rating:{blueprint.rating}",
        f"rows:{blueprint.rows}",
        f"columns:{blueprint.columns}",
        f"min-depth:{blueprint.min_depth}",
        f"max-depth:{blueprint.max_depth}",
    ]
    if blueprint.flags:
        result.append(f"flags:{blueprint.flags}")
    result.extend(f"D:{line}" for line in lines)
    return "\n".join(result)


def parse_vault_file(path: Path) -> dict[str, dict[str, object]]:
    records: dict[str, dict[str, object]] = {}
    current: dict[str, object] | None = None
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if raw_line.startswith("name:"):
            current = {"name": raw_line[5:], "lines": []}
            records[str(current["name"])] = current
        elif current is not None and raw_line.startswith("D:"):
            current["lines"].append(raw_line[2:])
        elif current is not None and ":" in raw_line:
            key, value = raw_line.split(":", 1)
            current[key] = value
    return records


def verify_installed(path: Path, blueprints: list[Blueprint], generated: dict[str, list[str]]) -> None:
    records = parse_vault_file(path)
    installed_hashes: dict[str, list[str]] = {}
    for name, record in records.items():
        lines = record["lines"]
        if not lines:
            continue
        digest = hashlib.sha256("\n".join(lines).encode("ascii")).hexdigest()
        installed_hashes.setdefault(digest, []).append(name)
    geometry_hashes: set[str] = set()
    for blueprint in blueprints:
        record = records.get(blueprint.name)
        if not record:
            raise ValueError(f"{blueprint.name}: missing from {path}")
        expected = {
            "type": blueprint.vault_type,
            "rating": str(blueprint.rating),
            "rows": str(blueprint.rows),
            "columns": str(blueprint.columns),
            "min-depth": str(blueprint.min_depth),
            "max-depth": str(blueprint.max_depth),
        }
        if blueprint.flags:
            expected["flags"] = blueprint.flags
        for key, value in expected.items():
            if record.get(key) != value:
                raise ValueError(f"{blueprint.name}: installed {key} does not match manifest")
        if record["lines"] != generated[blueprint.name]:
            raise ValueError(f"{blueprint.name}: installed geometry is not reproducible")
        digest = hashlib.sha256("\n".join(record["lines"]).encode("ascii")).hexdigest()
        if digest in geometry_hashes:
            raise ValueError(f"{blueprint.name}: duplicate replacement geometry")
        other_owners = [name for name in installed_hashes[digest] if name != blueprint.name]
        if other_owners:
            raise ValueError(
                f"{blueprint.name}: duplicates installed vault {other_owners[0]}"
            )
        geometry_hashes.add(digest)


def main() -> int:
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path,
                        default=repository / "docs" / "vault-originality-manifest.csv")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--check-vault", type=Path)
    arguments = parser.parse_args()

    blueprints = load_blueprints(arguments.manifest)
    validate_blueprint_set(blueprints)
    generated = {blueprint.name: generate(blueprint) for blueprint in blueprints}
    text = "\n\n\n".join(block(blueprint, generated[blueprint.name]) for blueprint in blueprints) + "\n"
    if arguments.output:
        arguments.output.write_text(text, encoding="utf-8", newline="\n")
    elif not arguments.check_vault:
        sys.stdout.write(text)
    if arguments.check_vault:
        verify_installed(arguments.check_vault, blueprints, generated)
        cells = sum(blueprint.rows * blueprint.columns for blueprint in blueprints)
        print(f"Verified {len(blueprints)} original Ammerow vaults ({cells} declared cells).")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"vault generation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
