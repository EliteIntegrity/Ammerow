#!/usr/bin/env python3
"""Generate the SDL3 event-to-cue map from the authored audio manifest."""

# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

from __future__ import annotations

import argparse
import csv
import hashlib
import re
import sys
from pathlib import Path, PurePosixPath


MESSAGE_RE = re.compile(r"^MSG\(\s*([A-Z0-9_]+)\s*,", re.MULTILINE)
SAFE_PATH_RE = re.compile(r"^[a-z0-9_/{}/.]+$")
SEQUENCE_MAX_STEPS = 8
SEQUENCE_MAX_OFFSET_MS = 5000
IGNORED_MESSAGES = {"GENERIC", "BIRTH", "MAX"}
GROUP_TITLES = {"ui": "UI", "spelunk": "Spelunking"}


def read_messages(path: Path) -> list[str]:
    messages = [
        name
        for name in MESSAGE_RE.findall(path.read_text(encoding="utf-8"))
        if name not in IGNORED_MESSAGES
    ]
    if not messages:
        raise ValueError(f"no message events found in {path}")
    if len(messages) != len(set(messages)):
        raise ValueError(f"duplicate message event in {path}")
    return messages


def cue_names(pattern: str, variants: int) -> list[str]:
    if pattern.count("{nn}") != 1 or not pattern.endswith(".wav"):
        raise ValueError(f"invalid filename pattern: {pattern!r}")
    if not SAFE_PATH_RE.fullmatch(pattern):
        raise ValueError(f"unsafe filename pattern: {pattern!r}")
    path = PurePosixPath(pattern)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"unsafe filename pattern: {pattern!r}")
    if variants < 1 or variants > 15:
        raise ValueError(
            f"{pattern!r} requests {variants} variants; sound-core supports 1-15"
        )
    return [
        "ammerow/" + pattern.replace("{nn}", f"{index:02d}")[:-4]
        for index in range(1, variants + 1)
    ]


def sequence_offsets(value: str, context: str) -> list[int]:
    if not value:
        return []
    try:
        offsets = [int(part) for part in value.split("|")]
    except ValueError as error:
        raise ValueError(f"{context}: invalid sequence_offsets_ms") from error
    if (
        not offsets
        or len(offsets) > SEQUENCE_MAX_STEPS
        or offsets[0] != 0
        or any(offset < 0 or offset > SEQUENCE_MAX_OFFSET_MS for offset in offsets)
        or any(right <= left for left, right in zip(offsets, offsets[1:]))
    ):
        raise ValueError(f"{context}: invalid sequence_offsets_ms")
    return offsets


def load_rows(
    path: Path, messages: list[str]
) -> tuple[list[dict[str, object]], set[str], int]:
    current: list[dict[str, object]] = []
    expected_names: set[str] = set()
    planned_variants = 0
    event_owner: dict[str, str] = {}
    cue_ids: set[str] = set()

    with path.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        required = {
            "cue_id",
            "event_status",
            "event_names",
            "filename_pattern",
            "variants",
            "sequence_offsets_ms",
        }
        if not reader.fieldnames or not required.issubset(reader.fieldnames):
            raise ValueError(f"{path} is missing required columns")
        for number, row in enumerate(reader, start=2):
            cue_id = row["cue_id"].strip()
            if not cue_id or cue_id in cue_ids:
                raise ValueError(f"{path}:{number}: missing or duplicate cue_id {cue_id!r}")
            cue_ids.add(cue_id)
            try:
                variants = int(row["variants"])
            except ValueError as error:
                raise ValueError(f"{path}:{number}: invalid variant count") from error
            status = row["event_status"].strip()
            names = cue_names(row["filename_pattern"].strip(), variants)
            sequence = sequence_offsets(
                (row.get("sequence_offsets_ms") or "").strip(),
                f"{path}:{number}",
            )
            expected_names.update(names)
            if status == "planned":
                if sequence:
                    raise ValueError(
                        f"{path}:{number}: planned cue cannot define a sequence"
                    )
                planned_variants += variants
                continue
            if status != "current":
                raise ValueError(f"{path}:{number}: unknown event_status {status!r}")

            events = [event.strip() for event in row["event_names"].split("|")]
            if not events or any(not event for event in events):
                raise ValueError(f"{path}:{number}: invalid event_names")
            for event in events:
                if event not in messages:
                    raise ValueError(f"{path}:{number}: unknown message event {event}")
                if event in event_owner:
                    raise ValueError(
                        f"{path}:{number}: {event} is owned by both "
                        f"{event_owner[event]} and {cue_id}"
                    )
                event_owner[event] = cue_id
            current.append(
                {
                    "cue_id": cue_id,
                    "events": events,
                    "names": names,
                    "sequence": sequence,
                }
            )

    missing = [event for event in messages if event not in event_owner]
    if missing:
        raise ValueError("manifest has no current cue for: " + ", ".join(missing))
    return current, expected_names, planned_variants


def read_asset_index(path: Path) -> dict[str, tuple[str, int, str]]:
    result: dict[str, tuple[str, int, str]] = {}
    with path.open(newline="", encoding="utf-8-sig") as source:
        reader = csv.DictReader(source)
        required = {"path", "sha256", "bytes", "event_status"}
        if not reader.fieldnames or set(reader.fieldnames) != required:
            raise ValueError(f"{path} has the wrong columns")
        for number, row in enumerate(reader, start=2):
            relative = row["path"]
            if relative in result or not SAFE_PATH_RE.fullmatch(relative):
                raise ValueError(f"{path}:{number}: unsafe or duplicate path")
            digest = row["sha256"]
            status = row["event_status"]
            if not re.fullmatch(r"[0-9a-f]{64}", digest) or status not in {
                "current", "planned"
            }:
                raise ValueError(f"{path}:{number}: invalid asset evidence")
            result[relative] = digest, int(row["bytes"]), status
    return result


def select_accepted_names(
    rows: list[dict[str, object]], expected_names: set[str], asset_root: Path,
    asset_index: dict[str, tuple[str, int, str]], require_assets: bool
) -> tuple[int, int]:
    accepted = {"ammerow/" + path[:-4] for path in asset_index}
    unexpected = sorted(accepted - expected_names)
    missing = sorted(expected_names - accepted)
    if unexpected or missing:
        raise ValueError(
            f"audio asset index differs from manifest; missing={missing}, extra={unexpected}"
        )

    current_names = {
        str(name).removeprefix("ammerow/") + ".wav"
        for row in rows
        for name in row["names"]
    }
    for relative, (_digest, _size, status) in asset_index.items():
        expected_status = "current" if relative in current_names else "planned"
        if status != expected_status:
            raise ValueError(
                f"audio asset index status differs from manifest: {relative} "
                f"is {status}, expected {expected_status}"
            )

    installed_paths: dict[str, Path] = {}
    if asset_root.exists():
        for path in asset_root.rglob("*.wav"):
            relative = path.relative_to(asset_root).as_posix()
            installed_paths[relative] = path
    if installed_paths or require_assets:
        indexed_paths = set(asset_index)
        installed = set(installed_paths)
        if installed != indexed_paths:
            raise ValueError(
                f"{asset_root} differs from audio asset index; "
                f"missing={sorted(indexed_paths - installed)}, "
                f"extra={sorted(installed - indexed_paths)}"
            )
        for relative, path in installed_paths.items():
            digest, size, _status = asset_index[relative]
            data = path.read_bytes()
            if len(data) != size or hashlib.sha256(data).hexdigest() != digest:
                raise ValueError(f"accepted audio asset changed: {relative}")

    current_accepted = 0
    fallback_families = 0
    for row in rows:
        names = list(row["names"])
        available = [name for name in names if name in accepted]
        row["installed_names"] = available
        if available:
            row["mapped_names"] = available
            current_accepted += len(available)
        else:
            row["mapped_names"] = names[:1]
            fallback_families += 1
    return current_accepted, fallback_families


def render(
    rows: list[dict[str, object]], planned_variants: int,
    current_installed: int, fallback_families: int
) -> str:
    event_count = sum(len(row["events"]) for row in rows)
    current_variants = sum(len(row["names"]) for row in rows)
    lines = [
        "# sound-sdl3.prf",
        "#",
        "# GENERATED FILE. Do not edit by hand.",
        "# Source: docs/audio-cue-manifest.csv",
        "# Generator: utils/generate-sdl3-sound-map.py",
        "#",
        "# Authored ammerow/... identities prefer an exact PCM WAV beneath",
        "# lib/sounds. Missing effects use deterministic synthesis; missing music",
        "# is silent. Variant rotation is presentation-only; it does not consume",
        "# the gameplay random stream.",
        "# Multi-step timings are authored in that manifest and run without blocking",
        "# input, rendering, or the turn scheduler.",
        "#",
        f"# {len(rows)} current cue families; {event_count} message events;",
        f"# {current_variants} current runtime WAV identities are expected;",
        f"# {current_installed} accepted current WAV identities are mapped;",
        f"# {fallback_families} families have no accepted WAV and use only their stable synth identity;",
        f"# {planned_variants} reserved WAV identities are intentionally not mapped.",
        "",
    ]
    previous_group = None
    for row in rows:
        cue_id = str(row["cue_id"])
        group = cue_id.split(".", 1)[0]
        if previous_group is not None and group != previous_group:
            lines.append("")
        if group != previous_group:
            title = GROUP_TITLES.get(group, group.replace("_", " ").title())
            lines.append(f"# {title}.")
        sound_names = " ".join(str(name) for name in row["mapped_names"])
        for event in row["events"]:
            lines.append(f"sound:{event}:{sound_names}")
            if row["sequence"]:
                offsets = " ".join(str(value) for value in row["sequence"])
                lines.append(f"sound-sequence:{event}:{offsets}")
        previous_group = group
    lines.append("")
    return "\n".join(lines)


def render_package_manifest(rows: list[dict[str, object]]) -> str:
    lines = [
        "# Copyright (c) 2026 John Horton",
        "# SPDX-License-Identifier: GPL-2.0-only",
        "#",
        "# GENERATED FILE. Do not edit by hand.",
        "# Source: docs/audio-cue-manifest.csv and accepted files beneath",
        "#         lib/sounds/ammerow/",
        "# Generator: utils/generate-sdl3-sound-map.py",
        "#",
        "# Exact authored-audio files for the full-content release package.",
        "# Reserved planned-UI variants are retained in the source tree but are",
        "# not packaged until their semantic events exist.",
        "",
        "data|lib/sounds/ammerow/NOTICE.md|sounds/ammerow/NOTICE.md",
    ]
    for row in rows:
        for name in row["installed_names"]:
            relative = str(name).removeprefix("ammerow/") + ".wav"
            lines.append(
                f"data|lib/sounds/ammerow/{relative}|sounds/ammerow/{relative}"
            )
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=root / "docs/audio-cue-manifest.csv")
    parser.add_argument("--messages", type=Path, default=root / "src/list-message.h")
    parser.add_argument("--output", type=Path, default=root / "lib/customize/sound-sdl3.prf")
    parser.add_argument(
        "--package-output",
        type=Path,
        default=root / "packaging/runtime-audio.txt",
    )
    parser.add_argument(
        "--asset-root", type=Path, default=root / "lib/sounds/ammerow",
        help="accepted authored WAV tree; installed variants are mapped"
    )
    parser.add_argument(
        "--asset-index", type=Path, default=root / "packaging/audio-assets.csv"
    )
    parser.add_argument(
        "--require-assets", action="store_true",
        help="require every indexed proprietary WAV to be present and hash-identical"
    )
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true", help="replace the generated map")
    action.add_argument("--check", action="store_true", help="fail if the generated map is stale")
    arguments = parser.parse_args()

    try:
        messages = read_messages(arguments.messages)
        rows, expected_names, planned_variants = load_rows(arguments.manifest, messages)
        asset_index = read_asset_index(arguments.asset_index)
        current_installed, fallback_families = select_accepted_names(
            rows, expected_names, arguments.asset_root, asset_index,
            arguments.require_assets
        )
        expected = render(
            rows, planned_variants, current_installed, fallback_families
        )
        expected_package = render_package_manifest(rows)
    except (OSError, ValueError) as error:
        print(f"audio map error: {error}", file=sys.stderr)
        return 1

    if arguments.check:
        try:
            actual = arguments.output.read_text(encoding="utf-8")
            actual_package = arguments.package_output.read_text(encoding="utf-8")
        except OSError as error:
            print(f"audio map error: {error}", file=sys.stderr)
            return 1
        if (
            actual.replace("\r\n", "\n") != expected
            or actual_package.replace("\r\n", "\n") != expected_package
        ):
            print(
                f"audio map is stale: run {Path(__file__).name} --write",
                file=sys.stderr,
            )
            return 1
        print(f"audio map OK: {arguments.output}")
        return 0

    arguments.output.write_text(expected, encoding="utf-8", newline="\n")
    arguments.package_output.write_text(
        expected_package, encoding="utf-8", newline="\n"
    )
    print(f"wrote {arguments.output} and {arguments.package_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
