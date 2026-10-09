# Ammerow third-party notices

This file describes the third-party components in the audited Windows x64
SDL3 package.  It is an inventory, not a substitute for the complete licence
texts in `third_party/licenses/`.  Re-run the package audit and update this
file whenever a binary or bundled font changes.

## SDL runtime libraries

The package contains these dynamically linked DLLs from the official Visual C
development archives.  Each source revision and packaged binary is pinned so
that a silent SDK replacement fails release verification.

| packaged file | release | source revision | SHA-256 |
|---|---:|---|---|
| `SDL3.dll` | 3.4.4 | `5848e584a1b606de26e3dbd1c7e4ecbc34f807a6` | `132e7f953951944f7628861227ecaea8c37a5a484f004c2072b7f3a1c343fa73` |
| `SDL3_ttf.dll` | 3.2.2 | `a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b` | `95a20af07f194f057b6e778bee0ce608d3c90c3dbd64a4b5120dc4879f43a6be` |
| `SDL3_image.dll` | 3.4.2 | `96a73a551a857b7c8d0ca3cc553a266eabbab6a7` | `2a77cdf3e8a3ae67f43c5dfcbc3bee6d5a641fee493ed8cbdb95badcff152ff9` |

SDL, SDL_ttf, and SDL_image use the zlib licence. Their exact
copyright notices and common terms are in
`third_party/licenses/SDL-family-zlib.txt`.

The official source revisions are available from:

- <https://github.com/libsdl-org/SDL/tree/5848e584a1b606de26e3dbd1c7e4ecbc34f807a6>
- <https://github.com/libsdl-org/SDL_ttf/tree/a1ce3670aec736ecbf0936c43f2f0cc53aa61e5b>
- <https://github.com/libsdl-org/SDL_image/tree/96a73a551a857b7c8d0ca3cc553a266eabbab6a7>

### Code embedded in SDL3_ttf

The audited SDL3_ttf DLL vendors these components:

- FreeType 2.13.2 (2023), revision
  `9973564cfa63763a3e4ac67c09147899539b1e07`, used under the FreeType
  Project Licence.  Portions of this software are copyright © 2023 The
  FreeType Project (www.freetype.org).  All rights reserved.
- HarfBuzz 8.5.0, revision
  `564bf9818a18709776856533829c0c04950773d6`, under its old MIT licence,
  including the separately noticed Microsoft Universal Shaping Engine data.
- PlutoSVG 0.0.6, revision
  `2983eb6919feea272d793bc386384e3f5b97b03c`, under MIT.
- PlutoVG 0.0.12, revision
  `3e6f922f453da1c9e7d1d7f66cac1d9724a18b47`, under MIT.
- `stb_rect_pack` 1.01 and the STB-derived files carried by PlutoVG, under
  their offered MIT/public-domain terms.

The complete applicable texts are `FreeType-FTL.txt`, `HarfBuzz.txt`,
`HarfBuzz-Microsoft-USE.txt`, `PlutoSVG-MIT.txt`, `PlutoVG-MIT.txt`, and
`STB-dual-license.txt` in the licence directory.

## Bundled fonts

| packaged file | embedded version | pinned source | SHA-256 | licence |
|---|---:|---|---|---|
| `CozetteVector.ttf` | 1.0; release v1.30.0 | Cozette tag `v.1.30.0`, revision `d2282e728aa64949b0a1e6489920ceb0772944fa` | `2d54a586f824f32d9d2dd01ec3b990c2a4d6445efa77e20177efe382ebe51627` | MIT |
| `IBMPlexMono-Regular.ttf` | 2.3 | Google Fonts path revision `633f3200539c52ee0aba2dfd7f46921417a81877` | `6a3412f058c7d8dfd9170c41e85ade48e5156ecb89356110ca57a0a27734af46` | OFL 1.1; reserved name `Plex` |
| `RobotoMono-SemiBold.ttf` | 3.001 | Roboto Mono tag `v3.001`, revision `111eb14e367888c9374da4da0b018e72cf8ac46d` | `dd877afb1ac59aa54fc142bf7e080ead574189ddac899dbffb0698554d1473b2` | OFL 1.1 |
| `ShareTechMono-Regular.ttf` | 1.003 | Google Fonts path revision `1b86e1e716445daccc346253911955073b0d254a` | `9ceab1f87414829af259c0f537573ae03ef7dd3147c0b27a36a1a0beb6732677` | OFL 1.1; reserved name `Share` |
| `VT323-Regular.ttf` | 2.000 | Google Fonts path revision `eb8781e516576b414603df8cd267c0f21c9b4ee2` | `cf4de751ada78ceac033dbe16a687742939995b77bc2a052ae17a4957958594d` | OFL 1.1 |

Sources:

- <https://github.com/the-moonwitch/Cozette/tree/v.1.30.0>
- <https://github.com/googlefonts/RobotoMono/tree/v3.001>
- <https://github.com/google/fonts/tree/633f3200539c52ee0aba2dfd7f46921417a81877/ofl/ibmplexmono>
- <https://github.com/google/fonts/tree/1b86e1e716445daccc346253911955073b0d254a/ofl/sharetechmono>
- <https://github.com/google/fonts/tree/eb8781e516576b414603df8cd267c0f21c9b4ee2/ofl/vt323>

The font copyright notices and full OFL 1.1 terms are in
`Fonts-OFL-1.1.txt`; Cozette's terms are in `Cozette-MIT.txt`.  Roboto Mono's
embedded metadata and its v3.001 source both identify OFL 1.1.

## Embedded random-number generator

`src/z-rand.c` embeds the state transition from xoshiro128** 1.1, written in
2018 by David Blackman and Sebastiano Vigna, and uses SplitMix64, written in
2015 by Sebastiano Vigna, to expand Ammerow's seeds. The canonical published
sources are:

- <https://prng.di.unimi.it/xoshiro128starstar.c>
- <https://prng.di.unimi.it/splitmix64.c>

Both sources include a public-domain dedication to the extent possible and an
unrestricted permission fallback covering use, copying, modification, and
distribution for any purpose, with or without fee. Their complete notices are
retained in `third_party/licenses/Xoshiro-SplitMix-public-domain.txt`.

The program's random-stream contract is documented in `docs/randomness.md`.

## Ammerow artwork and audio

The full-content game release includes 404 authored sound effects and one home
theme in WAV format, listed in `packaging/runtime-audio.txt`. The SDL3 frontend
loads and mixes these through SDL3, with code-synthesised fallbacks for missing
effects. It does not use SDL_mixer. The separate soundtrack album is not part
of the game package, and no Oryx audio is included.

The release includes compiled `FROGART3` ASCII-art plates and the Ammerow-owned
32x32 and 64x64 Hybrid sprite sheets, `lib/tiles/pixel-*.png`, decoded by
SDL_image. Both the plates and Hybrid sprites derive from the same
image-generated source illustrations, made under human direction.
The deterministic Hybrid conversion crops and samples those illustrations;
it uses no new image generation and does not change their provenance or asset
terms. Portrait source PNGs and the earlier code-drawn 16px map tiles are not
installed. No optional image codec DLL is installed; the pinned SDL_image
binary depends only on SDL3 and Windows system libraries.

These Ammerow assets are separately licensed under `ASSET-LICENSE.md`, not
under the GPL or the third-party component licences listed above. Their
attribution and production notices are in `lib/art/NOTICE.md`,
`lib/tiles/NOTICE.md`, and `lib/sounds/ammerow/NOTICE.md`. Ammerow's asset
restrictions do not apply to the third-party libraries or fonts.
