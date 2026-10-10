# Ammerow release notes

## 0.1.1 (unreleased)

Interface and presentation fixes for the first update.

- The camera keeps the player and nearby terrain clear of the stats and messages,
  including at map edges. A previously selected target no longer pulls the
  camera away from the player during ordinary movement.
- Hybrid monster artwork stays visible through attack and death animations,
  without briefly changing to an ASCII glyph.
- Clicking to change interface size now cycles through all sizes repeatedly.
  Redundant manual camera-centering controls have been removed from menus and
  help; existing keyboard shortcuts remain recognised.
- Large Settings pages keep controls, hints and footer text separate.
- Long messages wrap correctly. Bottom messages stay left-aligned, and the
  sidebar fits above them instead of showing text underneath them. Inactive
  meters no longer leave unnecessary gaps; food remains visible on short panels.
- The lean public source configures without requiring private audit tests.

No gameplay balance, content or save-format changes are included. Existing
0.1.0 saves and settings are intended to carry forward; keep a backup before
updating.
