# Ammerow release notes

## 0.1.4 (2026-10-24)

- Three new fish give expeditions different destinations: Fernscale in forest
  pools, Cistern Bream in ruined cisterns, and Blind Deepfish in deeper caves.
  Each has its own illustration and 32x32/64x64 sprites, food value and rewards.
- Newly generated cave sections can contain abandoned survey camps, ledge
  caches and specimen niches, with short authored notes, modest supplies and
  saleable curios. Finds are optional and remember when they have been looted.
- Some discoveries have nearby cave wildlife. Encounters are placed away from
  arrivals and the main route, with a clear approach and retreat; visited caves
  are not repopulated when loading a save.
- Three optional village requests reward donating one of each new fish:
  Fernscale 40 gold, Cistern Bream 120 gold and Blind Deepfish 240 gold.
  Use Shift+D at the General Store and Shift+C > Quests for details. Bonuses
  are once per character; normal larder portions and donation XP still apply.
- Expanded the Field Guide with fishing habitats, discoveries, curios and cave
  wildlife. The quest log shows full request details on smaller interfaces.
- Fixed a rare crash caused by stale item references and repeat state surviving
  a return to Home or reload. Command history is now fully cleared on teardown.

Back up saves before updating. Older saves can load without retrospective
request rewards; new discoveries appear in newly generated cave sections.
Saves written by 0.1.4 contain new request state and cannot be reopened in older
releases. Keep the original save if you may want to go back.

## 0.1.3 (2026-10-21)

- Full Tile joins ASCII and Hybrid, with 32x32 and 64x64 artwork for walls,
  quiet floors, vegetation, features and pickups, plus dedicated cave scenery.
  Keep floors ASCII is available within Full Tile; cave air stays black and
  shop numbers remain readable.
- ASCII offers Classic and Square cells, with Square the new default. An
  optional Enhanced Square toggle widens wall glyphs only and is off by default.
  Hybrid and Full Tile retain their own cell geometry.
- Surface movement is more responsive: map preparation avoids unnecessary
  border-route checks, and queued input takes priority over cosmetic redraws.
  Rain keeps animating during movement, with smaller zoom-independent splashes.
- Amber hatching and directional arrows mark the actual border-crossing tiles
  in every presentation. Look explains that stepping there triggers travel.
- Continuous exploration distinguishes seen or detected creatures from a
  generic interruption instead of guessing that every stop was a monster.
- Cave Survey separates character-tab navigation from selecting charted cave
  sections: Enter/click focuses the survey, arrows select spatially, Tab cycles,
  Home selects the current section, and Escape returns to the tabs.
- Newly generated cave sections gain sparse creature variety, doors, physical
  traps and supply chests. Opening, closing, searching, detection, disarming
  and one-time chest loot use persistent cave-owned state.
- Cave tunnelling and supported rock-removal magic update footing, sight,
  loose objects and creatures while protecting pools, anchors and return routes.
- Cave combat gains data-defined traits/resistances, persistent creature status
  effects and additional supported spatial magic. Unsupported effect chains
  reject before spending resources; broad destruction and fluid simulation
  remain outside the supported cave mechanics.
- Safe cave Rest, nearby creature/item lists, automatic pickup and ignore use
  cave geometry and existing knowledge, inventory and inscription rules.
  Combat remains manual; automatic cave navigation is not included.

Back up saves before updating. Older Ammerow saves are intended to load;
already visited cave sections retain their saved contents rather than being
repopulated. New saves use newer cave-state data: do not reopen them in older
releases. Keep the original save if you may want to go back.

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
