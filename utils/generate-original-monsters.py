#!/usr/bin/env python3
"""Generate Ammerow's original ordinary-monster replacement slice."""

# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

from __future__ import annotations

import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from monster_roster_generation import (load_balance, load_families,
	load_roster, render_generated)


def render(roster_path: Path, balance_path: Path, families_path: Path) -> str:
	config = load_families(families_path)
	roster = load_roster(roster_path, config["families"])
	balance = load_balance(balance_path, roster, "ordinary monster")
	return render_generated(roster, balance, config,
		generator="utils/generate-original-monsters.py",
		heading="ORIGINAL ORDINARY CREATURES", unique=False)


def main() -> int:
	root = Path(__file__).resolve().parents[1]
	parser = argparse.ArgumentParser()
	mode = parser.add_mutually_exclusive_group(required=True)
	mode.add_argument("--write", action="store_true")
	mode.add_argument("--check", action="store_true")
	parser.add_argument("--roster", type=Path,
		default=root / "lib/authoring/monster_original_roster.txt")
	parser.add_argument("--balance", type=Path,
		default=root / "lib/authoring/monster_original_balance.json")
	parser.add_argument("--families", type=Path,
		default=root / "lib/authoring/monster_original_families.json")
	parser.add_argument("--output", type=Path,
		default=root / "lib/gamedata/monster_original.txt")
	args = parser.parse_args()
	expected = render(args.roster.resolve(), args.balance.resolve(),
		args.families.resolve())
	if args.write:
		args.output.parent.mkdir(parents=True, exist_ok=True)
		args.output.write_text(expected, encoding="utf-8", newline="\n")
		print(f"wrote generated ordinary roster: {args.output}")
		return 0
	actual = args.output.read_text(encoding="utf-8") if args.output.exists() else ""
	if actual != expected:
		print(f"generated ordinary roster is stale: {args.output}")
		return 1
	print("generated ordinary roster OK")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
