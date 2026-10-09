#!/usr/bin/env python3
"""Generate Ammerow's unique roster from anonymous budgets and original lore."""

# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

from __future__ import annotations

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from monster_roster_generation import (load_balance, load_families,
	load_roster, render_generated, validate_provenance)


def stable_story(path: Path, expected: int) -> tuple[list[str], set[str]]:
	validate_provenance(path, "ammerow-original")
	lines = path.read_text(encoding="utf-8").splitlines()
	start = next((index for index, line in enumerate(lines)
		if line.startswith("name:")), None)
	if start is None:
		raise ValueError("stable unique source contains no records")
	story_lines = lines[start:]
	names = {line.split(":", 1)[1] for line in story_lines
		if line.startswith("name:")}
	if len(names) != expected:
		raise ValueError("stable unique count differs from balance authority")
	return story_lines, names


def render(roster_path: Path, balance_path: Path, story_path: Path,
		families_path: Path) -> str:
	config = load_families(families_path)
	roster = load_roster(roster_path, config["families"])
	balance = load_balance(balance_path, roster, "unique")
	story_lines, stable_names = stable_story(story_path,
		int(balance["stable_slots"]))
	if stable_names & {record["name"] for record in roster}:
		raise ValueError("generated and stable unique names overlap")
	generated = render_generated(roster, balance, config,
		generator="utils/generate-unique-monsters.py",
		heading="ORIGINAL CHAMPIONS", unique=True).rstrip().splitlines()
	return "\n".join(generated + ["", "########## STABLE STORY UNIQUES ##########",
		""] + story_lines).rstrip() + "\n"


def record_ranges(lines: list[str]) -> list[tuple[int, int]]:
	starts = [index for index, line in enumerate(lines) if line.startswith("name:")]
	return [(start, starts[index + 1] if index + 1 < len(starts) else len(lines))
		for index, start in enumerate(starts)]


def strip_unique_records(path: Path) -> int:
	lines = path.read_text(encoding="utf-8").splitlines()
	remove: set[int] = set()
	count = 0
	for start, end in record_ranges(lines):
		is_unique = any(line.startswith("flags:") and
			"UNIQUE" in {token.strip() for token in line[6:].split("|")}
			for line in lines[start:end])
		if is_unique:
			remove.update(range(start, end))
			count += 1
	remaining = [line for index, line in enumerate(lines) if index not in remove]
	path.write_text("\n".join(remaining).rstrip() + "\n", encoding="utf-8",
		newline="\n")
	return count


def explicit_unique_count(path: Path) -> int:
	lines = path.read_text(encoding="utf-8").splitlines()
	return sum(any(line.startswith("flags:") and
		"UNIQUE" in {token.strip() for token in line[6:].split("|")}
		for line in lines[start:end]) for start, end in record_ranges(lines))


def main() -> int:
	root = Path(__file__).resolve().parents[1]
	parser = argparse.ArgumentParser()
	mode = parser.add_mutually_exclusive_group(required=True)
	mode.add_argument("--write", action="store_true")
	mode.add_argument("--check", action="store_true")
	parser.add_argument("--strip-ordinary", action="store_true")
	parser.add_argument("--roster", type=Path,
		default=root / "lib/authoring/monster_unique_roster.txt")
	parser.add_argument("--balance", type=Path,
		default=root / "lib/authoring/monster_unique_balance.json")
	parser.add_argument("--story", type=Path,
		default=root / "lib/authoring/monster_unique_story.txt")
	parser.add_argument("--families", type=Path,
		default=root / "lib/authoring/monster_original_families.json")
	parser.add_argument("--ordinary", type=Path,
		default=root / "lib/gamedata/monster.txt")
	parser.add_argument("--output", type=Path,
		default=root / "lib/gamedata/monster_unique.txt")
	args = parser.parse_args()
	expected = render(args.roster.resolve(), args.balance.resolve(),
		args.story.resolve(), args.families.resolve())
	if args.write:
		args.output.parent.mkdir(parents=True, exist_ok=True)
		args.output.write_text(expected, encoding="utf-8", newline="\n")
		if args.strip_ordinary:
			removed = strip_unique_records(args.ordinary.resolve())
			if removed != 101:
				raise ValueError(f"expected to strip 101 unique records, removed {removed}")
		print(f"wrote generated unique roster: {args.output}")
		return 0
	if args.strip_ordinary:
		parser.error("--strip-ordinary is valid only with --write")
	actual = args.output.read_text(encoding="utf-8") if args.output.exists() else ""
	if actual != expected:
		print(f"generated unique roster is stale: {args.output}")
		return 1
	if explicit_unique_count(args.ordinary.resolve()):
		print(f"ordinary monster authority still contains uniques: {args.ordinary}")
		return 1
	print("generated unique roster and ordinary boundary OK")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
