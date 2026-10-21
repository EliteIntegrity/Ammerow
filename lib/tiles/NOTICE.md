# Ammerow tile atlases

`pixel-*.png`, `pickups-*.png`, `terrain-*.png`, `vegetation-*.png`,
`village-*.png`, `scenery-*.png`, `markers-*.png`, `expedition-*.png` and `caves-*.png`
are generated pages of separately licensed Ammerow visual assets.
They are not covered by the GPL that applies to the program.

Copyright © 2026 John Horton. All rights reserved except as expressly permitted.

You may not redistribute, repackage, sell or reuse these assets, separately or
within a modified or unmodified game, without prior written permission from
John Horton, except where applicable law permits otherwise. Normal play,
personal backups, private modifications and normal gameplay coverage,
including monetised videos and streams, are permitted; asset dumps are not.

Rights are reserved to the extent copyright and related rights subsist. The
complete terms are in the Ammerow Asset Licence 1.1, `ASSET-LICENSE.md` at the
root of the game package or source tree.

The text index and mapping files in this directory remain part of the
GPL-covered source unless a file says otherwise.

## Current Hybrid sprites

The shipped 32x32 and 64x64 Hybrid sprites in `pixel-*.png` are deterministic
conversions of the image-generated source illustrations also used for Ammerow's
ASCII art, made under human direction. Conversion crops transparent
margins and samples foreground colours; it uses no new image generation and
does not change the source provenance. It does not convert rendered ASCII
glyphs. The game displays the sprites using nearest-neighbour scaling.
The two sizes share the same monster/object identities and cell positions.
Additional common pickups use generic category sprites in `pickups-*.png`,
sampled from human-directed image-generated illustrations. They do not reveal
unidentified properties. Terrain and subjects without bindings use ordinary ASCII glyphs. Inspection
cards and shopkeepers continue to use the compiled ASCII illustrations.

These sheets are governed by the separate asset terms above. Their optional
runtime mappings are editable text; image files are not included in the
program source release.

## Full Tile scenery

Full Tile uses the same Hybrid sprites plus 32x32 or 64x64 scenery sheets.
The walls, ore, woodland, weathered rock banks, cut masonry and violet-veined
slate began as OpenAI-generated source images under human direction, then
were deterministically sampled. Opaque wall edges were matched for seamless
repetition; vegetation retains transparency. Traversable boundary openings
use a transparent cell. Additional human-directed OpenAI-generated imagery
supplies quiet ground, water, lava, doors, stairs, rubble and pits. Ordinary
ground can instead retain coloured glyphs on black with Keep floors ASCII;
side-view cave air stays black in either case. These scenery additions do not
replace the Hybrid terrain presentation.

The marker and expedition supplements add discovered traps, blank shop signs,
fish, rope, breathing equipment, fishing rods and loot piles. Side-view cave
art adds rock materials, water, formations, seams, rope, anchors, passages and
the cave actor. These were also sampled from human-directed OpenAI-generated
source artwork. Shop numbers/letters and cave passage glyphs remain readable
overlays. Hidden traps are not revealed or identified by these pictures.

Full Tile scenery has the same separate asset terms above. Source images and
PNG sheets are excluded from the program source release; text mappings remain
GPL-covered. There is no live image generation during play.
