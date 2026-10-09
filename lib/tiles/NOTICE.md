# Ammerow tile atlases

`pixel-*.png` are generated pages of separately licensed Ammerow visual assets.
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
Terrain and subjects without bindings use ordinary ASCII glyphs. Inspection
cards and shopkeepers continue to use the compiled ASCII illustrations.

These sheets are governed by the separate asset terms above. Their optional
runtime mappings are editable text; image files are not included in the
program source release.
