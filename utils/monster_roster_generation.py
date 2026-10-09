#!/usr/bin/env python3
"""Shared validation and rendering for Ammerow-authored monster rosters."""

# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

from __future__ import annotations

from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import textwrap


def directives(path: Path) -> dict[str, str]:
	result: dict[str, str] = {}
	for line in path.read_text(encoding="utf-8").splitlines():
		if line.startswith(("origin:", "review:")):
			key, value = line.split(":", 1)
			if key in result:
				raise ValueError(f"{path}: repeated {key}")
			result[key] = value.strip()
	return result


def parse_records(path: Path) -> list[dict[str, str]]:
	result: list[dict[str, str]] = []
	current: dict[str, str] | None = None
	for raw_line in path.read_text(encoding="utf-8").splitlines():
		line = raw_line.strip()
		if not line or line.startswith("#") or ":" not in line:
			continue
		key, value = line.split(":", 1)
		if key in {"origin", "review"} and current is None:
			continue
		if key == "name":
			if current is not None:
				result.append(current)
			current = {}
		if current is None:
			raise ValueError(f"{path}: field before first name: {key}")
		if key in current:
			raise ValueError(f"{path}: repeated {key} in {current.get('name')}")
		current[key] = value.strip()
	if current is not None:
		result.append(current)
	return result


def validate_provenance(path: Path, origin: str) -> None:
	values = directives(path)
	if values != {"origin": origin, "review": "release"}:
		raise ValueError(f"{path}: invalid origin/review directives")


def load_families(path: Path) -> dict[str, object]:
	data = json.loads(path.read_text(encoding="utf-8"))
	if (data.get("schema_version") != 1 or
			data.get("origin") != "ammerow-original" or
			data.get("review") != "release"):
		raise ValueError(f"{path}: invalid family authority header")
	families = data.get("families")
	if not isinstance(families, dict) or not families:
		raise ValueError(f"{path}: no monster families")
	required = {"base", "colors", "methods", "effect", "companion",
		"action", "offence", "summons", "wall_mobility"}
	for name, family in families.items():
		if not isinstance(family, dict) or set(family) != required:
			raise ValueError(f"{path}: invalid family fields for {name}")
		if (not all(isinstance(family[key], str) and family[key]
				for key in ("base", "colors", "effect", "companion", "action")) or
				not isinstance(family["methods"], list) or
				not isinstance(family["offence"], list) or
				len(family["methods"]) != 3 or len(family["offence"]) != 3 or
				not all(isinstance(value, str) and value
					for value in family["methods"] + family["offence"]) or
				not isinstance(family["summons"], list) or
				len(family["summons"]) != 5 or
				not all(isinstance(value, str) and value
					for value in family["summons"]) or
				family["wall_mobility"] not in {"PASS_WALL", "KILL_WALL"}):
			raise ValueError(f"{path}: invalid family values for {name}")
	for key in ("control_by_tier", "control_blow_by_tier"):
		if (not isinstance(data.get(key), list) or len(data[key]) != 10 or
				not all(isinstance(value, str) and value for value in data[key])):
			raise ValueError(f"{path}: invalid {key}")
	return data


def load_roster(path: Path, families: dict[str, object]) -> list[dict[str, str]]:
	validate_provenance(path, "ammerow-original")
	records = parse_records(path)
	names: set[str] = set()
	art_ids: set[str] = set()
	for record in records:
		if (set(record) not in ({"name", "band", "family", "premise"},
				{"name", "art", "band", "family", "premise"})):
			raise ValueError(f"invalid roster fields for {record.get('name')}")
		if ("art" in record and (len(record["art"]) >= 128 or not
				re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", record["art"]))):
			raise ValueError(f"invalid art ID for {record.get('name')}")
		if record.get("art") in art_ids:
			raise ValueError(f"duplicate art ID: {record['art']}")
		if record["name"] in names:
			raise ValueError(f"duplicate roster name: {record['name']}")
		if record["family"] not in families:
			raise ValueError(f"unknown family: {record['family']}")
		if not record["premise"].endswith("."):
			raise ValueError(f"premise must be a sentence: {record['name']}")
		names.add(record["name"])
		if record.get("art"):
			art_ids.add(record["art"])
	return records


def load_balance(path: Path, roster: list[dict[str, str]], label: str
		) -> dict[str, object]:
	data = json.loads(path.read_text(encoding="utf-8"))
	if data.get("schema_version") != 1 or data.get("origin") != "anonymous-aggregate":
		raise ValueError(f"unsupported {label} balance authority")
	if data.get("review") != "release" or data.get("generated_slots") != len(roster):
		raise ValueError(f"{label} balance authority is not release-complete")
	counts: dict[str, int] = defaultdict(int)
	for record in roster:
		counts[record["band"]] += 1
	bands = data.get("bands")
	if not isinstance(bands, dict) or set(counts) != set(bands):
		raise ValueError("roster and balance bands differ")
	for band, count in counts.items():
		if bands[band].get("count") != count:
			raise ValueError(f"roster count differs for band {band}")
	return data


def digest_key(name: str, salt: str) -> bytes:
	return hashlib.sha256(f"{salt}\0{name}".encode("utf-8")).digest()


def selected(records: list[dict[str, str]], count: int, salt: str,
		within: set[str] | None = None) -> set[str]:
	pool = [record for record in records
		if within is None or record["name"] in within]
	if count < 0 or count > len(pool):
		raise ValueError(f"invalid capability count {count} for {salt}")
	pool.sort(key=lambda record: digest_key(record["name"], salt))
	return {record["name"] for record in pool[:count]}


def interpolate(values: dict[str, float], quantile: float) -> float:
	points = (
		(0.0, float(values["minimum"])),
		(0.5, float(values["median"])),
		(0.9, float(values["p90"])),
		(1.0, float(values["maximum"])),
	)
	for (left_q, left), (right_q, right) in zip(points, points[1:]):
		if quantile <= right_q:
			portion = (quantile - left_q) / (right_q - left_q)
			return left + portion * (right - left)
	return points[-1][1]


def assigned_values(records: list[dict[str, str]], values: dict[str, float],
		salt: str) -> dict[str, int]:
	ordered = sorted(records, key=lambda record: digest_key(record["name"], salt))
	count = len(ordered)
	result: dict[str, int] = {}
	for index, record in enumerate(ordered):
		quantile = index / (count - 1) if count > 1 else 0.5
		result[record["name"]] = int(round(interpolate(values, quantile)))
	return result


def tier(depth: int) -> int:
	return max(0, min(9, depth // 10))


def spells_for(record: dict[str, str], depth: int, control: bool,
		mobility: bool, summoner: bool, config: dict[str, object]) -> list[str]:
	level = tier(depth)
	family = config["families"][record["family"]]
	palette = family["offence"]
	count = 1 if depth < 25 else (2 if depth < 65 else 3)
	spells = list(palette[:count])
	if control:
		spells.append(config["control_by_tier"][level])
	if mobility:
		spells.append("BLINK" if depth < 40 else "TPORT")
	if summoner:
		summon_tier = 0 if depth < 20 else (1 if depth < 40 else
			(2 if depth < 60 else (3 if depth < 80 else 4)))
		spells.append(family["summons"][summon_tier])
	return list(dict.fromkeys(spells))


def dice_for_target(target: int, depth: int, blows: int) -> str:
	sides = 4 if depth < 10 else (6 if depth < 30 else
		(8 if depth < 50 else (10 if depth < 70 else 12)))
	per_blow = target / max(1, blows)
	dice = max(1, int(math.floor(per_blow / ((sides + 1) / 2))))
	bonus = max(0, int(round(per_blow - dice * ((sides + 1) / 2))))
	return f"{bonus}+{dice}d{sides}" if bonus else f"{dice}d{sides}"


def wrapped_desc(text: str) -> list[str]:
	return [f"desc:{line}" for line in textwrap.wrap(text, width=76,
		initial_indent="", subsequent_indent=" ")]


def mechanics_sentence(record: dict[str, str], depth: int, blows: int,
		caster: bool, summoner: bool, control: bool, mobility: bool,
		companions: bool, families: dict[str, object]) -> str:
	family = families[record["family"]]
	parts = [str(family["action"])]
	if blows:
		parts.append("with one separate close attack" if blows == 1 else
			f"with {blows} separate close attacks")
	if caster:
		parts.append("projects stored force at range")
	if control:
		parts.append("disrupts an explorer's control of the encounter")
	if mobility:
		parts.append("changes the geometry of pursuit")
	if summoner:
		parts.append("draws opportunistic life through nearby joins")
	if companions:
		parts.append("is usually surrounded by lesser cave life")
	return "In conflict it " + ", ".join(parts) + "."


def render_generated(roster: list[dict[str, str]], balance: dict[str, object],
		config: dict[str, object], *, generator: str, heading: str,
		unique: bool) -> str:
	by_band: dict[str, list[dict[str, str]]] = defaultdict(list)
	for record in roster:
		by_band[record["band"]].append(record)
	lines = [
		f"# Generated by {generator}.",
		"# Do not hand-edit; identities are original reviewed authoring data and",
		"# mechanics come from anonymous broad depth-band budgets.",
		"",
	]
	for band in sorted(by_band, key=lambda value: int(value.split("-", 1)[0])):
		records = by_band[band]
		budget = balance["bands"][band]
		caps = budget["capability_counts"]
		caster = selected(records, caps["spellcaster"], f"{band}:caster")
		summoner = selected(records, caps["summoner"], f"{band}:summoner", caster)
		control = selected(records, caps["control"], f"{band}:control")
		mobility = selected(records, caps["mobility"], f"{band}:mobility")
		companions = selected(records, caps["companions"], f"{band}:companions")
		drops = selected(records, caps["good_or_great_drop"], f"{band}:drops")
		metrics = {name: assigned_values(records, budget[name], f"{band}:{name}")
			for name in ("depth", "hit_points", "armor_class", "speed",
				"experience", "rarity", "melee_expected_per_round")}

		zero_names = [name for name, value in
			metrics["melee_expected_per_round"].items() if value <= 0]
		for name in zero_names:
			if name not in caster:
				donor = next(iter(caster - set(zero_names)), None)
				if donor:
					metrics["melee_expected_per_round"][name], \
						metrics["melee_expected_per_round"][donor] = (
							metrics["melee_expected_per_round"][donor], 0)

		lines.extend((f"########## {heading} {band} ##########", ""))
		for record in records:
			name = record["name"]
			family = config["families"][record["family"]]
			depth = metrics["depth"][name]
			hp = metrics["hit_points"][name]
			if hp <= 0:
				hp = max(25, depth * 20)
			ac = max(1, metrics["armor_class"][name])
			speed = (int(round(metrics["speed"][name] / 10) * 10) if unique
				else metrics["speed"][name])
			xp = max(0, metrics["experience"][name])
			rarity = max(1, metrics["rarity"][name])
			target_melee = metrics["melee_expected_per_round"][name]
			blow_count = (0 if target_melee <= 0 else 1 if depth < 10 else
				2 if depth < 30 else 3 if depth < 60 else 4)
			flags = ["UNIQUE"] if unique else []
			if unique and "," in name:
				flags.append("NAME_COMMA")
			if depth > 0:
				flags.append("FORCE_SLEEP")
				if unique:
					flags.append("MOVE_BODY")
			if name in caster:
				flags.append("SMART")
			if depth >= 50 and name in caster:
				flags.append("POWERFUL")
			if name in drops:
				flags.extend(("DROP_4" if depth >= 40 else "DROP_2", "DROP_GOOD"))
				if depth >= 70:
					flags.append("DROP_GREAT")
			if name in mobility and name not in caster:
				flags.append(family["wall_mobility"])
			color = family["colors"][digest_key(name, "color")[0] %
				len(family["colors"])]
			lines.append(f"name:{name}")
			if record.get("art"):
				lines.append(f"art:{record['art']}")
			lines.extend((
				f"base:{family['base']}",
				f"color:{color}",
				f"speed:{speed}",
				f"hit-points:{hp}",
				f"hearing:{min(100, 20 + depth)}",
				f"smell:{min(100, 10 + depth // 2)}",
				f"armor-class:{ac}",
				f"sleepiness:{max(0, 40 - depth // 2)}",
				f"depth:{depth}",
				f"rarity:{rarity}",
				f"experience:{xp}",
			))
			if blow_count:
				dice = dice_for_target(target_melee, depth, blow_count)
				for blow_index in range(blow_count):
					method = family["methods"][blow_index % len(family["methods"])]
					effect = family["effect"]
					if name in control and name not in caster and blow_index == blow_count - 1:
						effect = config["control_blow_by_tier"][tier(depth)]
					lines.append(f"blow:{method}:{effect}:{dice}")
			lines.append("flags:" + " | ".join(dict.fromkeys(flags)))
			if name in caster:
				lines.append(f"spell-freq:{max(2, 8 - depth // 15)}")
				lines.append(f"spell-power:{max(1, depth)}")
				lines.append("spells:" + " | ".join(spells_for(record, depth,
					name in control, name in mobility, name in summoner, config)))
			if name in companions:
				lines.append(f"friends-base:50:1d2:{family['companion']}:servant")
			description = record["premise"] + " " + mechanics_sentence(
				record, depth, blow_count, name in caster, name in summoner,
				name in control, name in mobility, name in companions,
				config["families"])
			lines.extend(wrapped_desc(description))
			lines.append("")
	return "\n".join(lines).rstrip() + "\n"
