# AMMEROW™: Lands Beyond

**A free, single-player roguelike where the descent is only half the journey.**

The lands beyond have been cut off. The village endures, the roads are dangerous,
and beneath it all the Meridian leads towards the Upwelling. Pack for an
expedition, choose your risks, and try to bring something back.

Ammerow builds on **Angband's turn-based combat and permanent death**, with its
own peoples, creatures and relics, an overworld to explore, and connected
procedural caves seen from the side. A good sword matters. So does remembering
to bring a rope.

**[Download for Windows x64](https://github.com/EliteIntegrity/Ammerow/releases/latest)**
&nbsp; · &nbsp; [Play on Steam](https://store.steampowered.com/app/5247100/Ammerow_Lands_Beyond/)
&nbsp; · &nbsp; [Itch.io](https://eliteintegrity.itch.io/ammerow-lands-beyond)
&nbsp; · &nbsp; [Android edition](https://github.com/EliteIntegrity/Ammerow-Android/releases/latest)

**Free to play. Single-player. Offline.** The Windows download includes the
artwork, sound effects and title music. Extract the ZIP and run `ammerow.exe`;
no installer or Steam account is needed for the GitHub download.

[![The village in Full Tile mode, with masonry shops, numbered entrances and a worn road.](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-01-village-full-tile.png)](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-01-village-full-tile.png)

*Back in the village: sell your finds, replenish supplies and decide how much
you dare carry into the next expedition.*

## Plan the journey. Survive the return.

- **Make each turn count.** Classic grid-based combat, spells, equipment and
  dangerous encounters reward preparation and knowing when to retreat.
- **Descend into the Meridian.** Explore a hundred-level main descent through
  procedurally generated dungeons, gathering knowledge and strange relics along
  the way.
- **Take another route underground.** Explore connected side-view caves. Climb
  rock, hammer pitons, deploy ropes, peer into shafts and manage stamina and air.
  Doors, traps, chests, creatures and tunnelling make the journey more than a climb.
- **Fish for more than supper.** Watch fish approach your hook, adjust your line,
  and bring the catch home to eat, sell or donate to the village larder.
- **Choose your adventurer.** Different peoples and classes, magical practices,
  equipment and procedurally assembled artefacts give each run its own character.
- **Keep the decisions, ease the repetition.** Optional continuous auto-explore
  stops for monsters and new loot. Fighting stays entirely in your hands.

## The dungeon is not the only way down

The caves have their own geography: ledges, deep shafts, rock formations and
underground pools. Your route back matters as much as your route in. A rope
can save stamina; a careless fall or an overambitious swim can end a run.

| Beneath the village's world | A quieter kind of expedition |
| --- | --- |
| [![A side-view cave in Full Tile mode, with stalactites, passages and a deep blue pool.](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-02-deepwell.png)](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-02-deepwell.png) | [![The fishing view shows fish at different depths and a lowered line in an underground pool.](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-03-fishing.png)](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-03-fishing.png) |

## Read the world your way

Choose **ASCII**, **Hybrid** or **Full Tile**. ASCII offers Classic and Square
cells; Hybrid combines glyph terrain with creature and item sprites. Full Tile
adds textured terrain in **32×32 or 64×64**, with a **Keep floors ASCII** option
if you prefer a quieter map. Zoom and interface size are adjustable.

| Coloured ASCII | Hybrid: glyphs and sprites together |
| --- | --- |
| [![The village rendered in coloured ASCII, with glyph buildings, roads and numbered shops.](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-04-village-ascii.png)](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-04-village-ascii.png) | [![A cave spider and a potion rendered as sprites among ASCII dungeon walls and floor dots.](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-05-dungeon-hybrid.png)](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-05-dungeon-hybrid.png) |

## Look before you commit

Creature cards combine detailed character-art portraits with descriptions and
what your adventurer has learned. The unfamiliar becomes recognisable without
giving away everything at first sight. Items and shopkeepers have their own
illustrations too.

[![A young red dragon's ASCII-art portrait and known information beside its position in the tiled Meridian dungeon.](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-06-dragon-card.png)](https://github.com/EliteIntegrity/Ammerow/releases/download/v0.1.3/ammerow-06-dragon-card.png)

*These six screenshots show the Windows 0.1.3 renderer in staged gameplay
scenarios, not painted mock-ups. Click any image to view it at full size.*

## Start playing

1. Open the [latest release](https://github.com/EliteIntegrity/Ammerow/releases/latest)
   and download `Ammerow-<version>-windows-x64.zip`, not GitHub's automatic
   **Source code** archives. Requires 64-bit Windows 10 or 11.
2. Extract the **whole ZIP** into a writable folder and run `ammerow.exe`.
3. Choose **New Game**. Press **F1** or **?** for the Field Guide, **Enter** for
   the Commands menu, and **Esc** for the pause menu and settings.

Ammerow is in active Early Access development. **Back up your saves before
updating** and read the release notes: saves written by a newer version may not
work in an older one. The Android edition is maintained in its own repository;
follow its release and installation instructions.

[What's new](CHANGELOG.md) ·
[Report a bug](https://github.com/EliteIntegrity/Ammerow/issues) ·
[Browse releases](https://github.com/EliteIntegrity/Ammerow/releases)

## Build, contribute and explore the source

This repository builds a playable Windows SDL3 game using ASCII glyphs and
synthesised effects. The official downloads additionally contain the separately
licensed illustrations, sprites and audio. Screenshots are release attachments;
the asset packs are not stored in this source repository.

See [Building and testing](docs/building.md), [Contributing](CONTRIBUTING.md),
[Data authoring](docs/data-authoring.md) and [Packaging](packaging/README.md).
[Randomness](docs/randomness.md) explains the simulation's random streams and
save contract.

Official playable releases include their exact corresponding program source in
`source/Ammerow-source.zip`; see [SOURCE-CODE.txt](SOURCE-CODE.txt). The repository
front page can be updated independently of those frozen release snapshots.

**AI disclosure:** Ammerow was developed with AI assistance under human direction
and review, including code, writing and visual assets. Illustrations and sprites
derive from image-generated source artwork; sound effects use code-driven
procedural synthesis, and music was composed through AI-assisted code and virtual
instruments. No AI service or live AI generation runs while you play. See the
[component notices](THIRD_PARTY_NOTICES.md) for the asset details.

## Licence and attribution

The program is distributed under GNU GPL version 2; see [COPYING](COPYING).
Ammerow began from Angband commit
`5c45eb9588b8227d4f1b1998e0a627ad7ee11a75`, 174 commits after Angband 4.2.6.
Inherited authorship and component licences remain in [AUTHORS](AUTHORS.md),
[NOTICE](NOTICE.md), [copying](docs/copying.rst),
[acknowledgements](docs/thanks.rst) and [third-party notices](THIRD_PARTY_NOTICES.md).

Ammerow artwork and audio: Copyright © 2026 John Horton. All rights reserved
except as expressly permitted. Tiles, ASCII illustrations, sound effects and music
are separately licensed, not GPL-covered assets. Redistribution, repackaging,
sale or reuse, separately or within a modified or unmodified game, requires
prior written permission except where applicable law permits otherwise.
See [ASSET-LICENSE.md](ASSET-LICENSE.md) for complete terms and the reservation
of rights to the extent they subsist.

Normal play, personal backups, private modifications and normal gameplay
coverage, including monetised videos and streams, are permitted; asset dumps
are not. GPL forks, including commercial forks, must omit or replace these
assets unless separately authorised. Third-party fonts retain their own licences.

AMMEROW™ and AMMEROW: LANDS BEYOND™ are trade marks of John Horton. Public
forks must use distinct, non-confusing branding; truthful attribution remains permitted.
