# Game data authoring and generators

Ammerow keeps content in editable data. Deterministic maintenance scripts
combine and validate that data into the files loaded by the game. These scripts
are ordinary Python programs, not AI models or services. Publishing them lets
contributors reproduce and modify generated records without hand-editing output.

## Sources of truth

| Content | Editable authority | Generator and runtime output |
| --- | --- | --- |
| Ordinary authored monsters | `lib/authoring/monster_original_roster.txt`, `monster_original_balance.json`, shared `monster_original_families.json` | `utils/generate-original-monsters.py` produces `lib/gamedata/monster_original.txt` |
| Champions and story monsters | `lib/authoring/monster_unique_roster.txt`, `monster_unique_balance.json`, `monster_unique_story.txt`, shared families | `utils/generate-unique-monsters.py` produces `lib/gamedata/monster_unique.txt` |
| Artefact slots and fixed quest objects | `lib/gamedata/artifact_balance.txt`, `lib/authoring/artifact_quest.txt` | `utils/generate-artifact-slots.py` produces `lib/gamedata/artifact.txt` |
| Original vault library | `docs/vault-originality-manifest.csv` | `utils/generate-ammerow-vaults.py` constructs the 33 authored vault records checked against `lib/gamedata/vault.txt` |
| Sound event assignments | `docs/audio-cue-manifest.csv`, `packaging/audio-assets.csv` | `utils/generate-sdl3-sound-map.py` produces sound preferences and runtime audio manifest; it does not create WAVs |

Ordinary hand-authored creatures remain in `lib/gamedata/monster.txt`. The
monster loader combines ordinary, generated ordinary and champion records.
Names, ecological premises, colours, glyphs, attacks, summons and movement
palettes remain data; generators own validation and deterministic assignment.
Explicit stable `art:` IDs connect records to optional media. Display names
must never substitute for those IDs.

The monster budgets contain anonymous distributions and capability counts by
broad depth band. They do not encode old-to-new identities. Individual numerical
measures and capabilities are assigned independently within a band. Preserve
counts, threat distributions and caster/summoner/control/mobility/drop coverage
unless a balance change is deliberate and tested.

The artefact slot generator is separate from runtime procedural artefacts.
Runtime C code constructs a mechanical set, then selects compatible authored
lore from `artifact_lore.txt`. It does not call AI. Aggregate calibration in
`artifact_balance.txt` is the editable aggregate calibration data.

The vault manifest owns names, category, dimensions, depth intervals, topology,
seed, encounter budget and theme. The deterministic generator owns construction
and structural checks, not a hardcoded per-vault gameplay branch. Other retained
vault records remain in `vault.txt`; do not overwrite the whole library with a
file containing only the generated subset.

## Regeneration and checking

```powershell
python utils/generate-original-monsters.py --check
python utils/generate-unique-monsters.py --check
python utils/generate-artifact-slots.py --check
python utils/generate-ammerow-vaults.py --check-vault lib/gamedata/vault.txt
python utils/generate-sdl3-sound-map.py --check
```

The first three tools accept `--write` for deliberate regeneration. The vault
tool's `--output` writes the generated subset for inspection/integration; its
`--check-vault` verifies that subset against the complete runtime library.
Consult each tool's `--help` before writing. Commit authority and output together.

After editing, run the generators' checks, native parser tests and relevant
headless scenarios. Generation checks do not constitute a content review.

## Preferences

`lib/customize/*.prf` contains editable display, message, keymap and sound
preferences. Each file identifies its directives in comments; `src/ui-prefs.c`
implements the parser. Visual overrides must not change gameplay data, and
sound-event assignments should be regenerated from the cue manifest rather
than edited in `sound-sdl3.prf`.

## Content and media boundaries

Author identities from Ammerow's geology, survey work, settlements and ecology.
Do not recreate another fictional cast through renamed roles or relationships.
Images, compiled plates and audio are separately licensed media, not program
source. Runtime mapping files remain editable text in `lib/`; loaders and
format definitions are in `src/`. Missing assets must fall back safely without
affecting gameplay. Media production tools are not needed to build the game.
