# Ammerow release notes

## 0.1.2 (2026-10-11)

- Inspecting an item keeps inventory or equipment behind the details panel,
  retaining the selected item when returning. Item details now show their
  ASCII illustration, or a large coloured glyph when no illustration exists.
  Unidentified items use only their unidentified appearance; long descriptions
  scroll with arrows, Page Up/Down or the mouse wheel.
- The sidebar combines character level and XP needed for the next level,
  and labels money as Gold. Identity and individual attributes remain visible.
- An optional, restrained warning border highlights urgent conditions without
  flashing or pulsing. It can be disabled in Display / Performance.
- Controls & Convenience makes exploration, walking to stairs and existing
  pickup/ignore settings easier to find. Traditional exploration remains
  available alongside Ammerow's continuous mode, which stops for monsters,
  newly discovered non-ignored loot and safety interruptions. Fighting stays
  entirely manual.
- Level feelings are off by default for new characters, with a clearer
  description of their unseen danger/treasure hints. Existing characters keep
  their setting; feeling announcements do not interrupt continuous exploration.
- Each new personal highest level brings a card and sound. Every five levels
  offers a one-time choice of potion supplies, scroll supplies or gold. Deferred
  choices are available through Enter > Information > Level rewards and survive
  save/reload; level drain and recovery cannot duplicate rewards.
- Fixed a missing input declaration for strict compilers and missing exploration
  and item-details test files in the public source export.

Existing saves can be loaded without retrospective level rewards. Back up your
game folder before updating: saves written by 0.1.2 include the new reward state
and cannot be reopened by older releases.

## 0.1.1 (2026-10-10)

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
