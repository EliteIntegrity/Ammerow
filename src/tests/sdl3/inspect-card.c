/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "player.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "trap.h"
#include "ui-prefs.h"
#include "ui-term.h"
#include "ui-target.h"
#include "ui-input.h"
#include "target.h"
#include "mon-util.h"
#include "mon-predicate.h"
#include "sdl3/inspect-card-presenter.h"
#include "sdl3/map-view.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-system-data.h"
#include "world-spelunking-actor-data.h"
#include "map-visual-data.h"

int setup_tests(void **state)
{
	(void)state;
	set_file_paths();
	init_angband();
	textui_prefs_init();
	if (!player_make_simple(NULL, NULL, "Inspect Tester")) return 1;
	prepare_next_level(player);
	on_new_level();
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static int test_known_trap_precedes_floor(void *state)
{
	struct sdl3_inspect_card card;
	struct sdl3_map_view view = { 0 };
	term main_term = { 0 };
	struct loc grid = player->grid;
	struct trap_kind *kind = lookup_trap("poison gas trap");
	(void)state;
	notnull(kind);
	view.active = true;
	view.term_cols = view.term_rows = 3;
	main_term.offset_x = grid.x - 1;
	main_term.offset_y = grid.y - 1;
	tile_width = tile_height = 1;
	square_set_feat(cave, grid, FEAT_FLOOR);
	square_set_feat(player->cave, grid, FEAT_FLOOR);
	sqinfo_on(square(cave, grid)->info, SQUARE_SEEN);
	place_trap(cave, grid, kind->tidx, 1);
	notnull(square_trap(cave, grid));
	/* Hidden actual trap must not be exposed by a presentation fallback. */
	sdl3_inspect_card_configure(&card, &view, &main_term, 1, 1, true, false, false);
	require(card.active);
	require(streq(card.name, f_info[FEAT_FLOOR].name));
	require(streq(card.status, "Open terrain"));
	require(!strstr(card.status, "Diggable"));
	require(square_reveal_trap(cave, grid, true, false));
	sdl3_inspect_card_configure(&card, &view, &main_term, 1, 1, true, false, false);
	require(card.active);
	require(streq(card.name, kind->desc));
	require(streq(card.status, "Known trap"));
	require(streq(card.flavor, kind->text));
	eq(card.glyph, kind->d_char);
	square_trap(player->cave, grid)->timeout = 5;
	sdl3_inspect_card_configure(&card, &view, &main_term, 1, 1, true, false, false);
	require(streq(card.name, kind->desc));
	require(streq(card.status, "Temporarily disabled"));
	square_trap(player->cave, grid)->timeout = 0;
	/* Memory uses the known trap, while hallucination suppresses the card. */
	sqinfo_off(square(cave, grid)->info, SQUARE_SEEN);
	sdl3_inspect_card_configure(&card, &view, &main_term, 1, 1, true, false, false);
	require(streq(card.name, kind->desc));
	player->timed[TMD_IMAGE] = 1;
	sdl3_inspect_card_configure(&card, &view, &main_term, 1, 1, true, false, false);
	require(!card.active);
	player->timed[TMD_IMAGE] = 0;
	ok;
}

const char *suite_name = "sdl3/inspect-card";

static bool saw_inspection;
static uint32_t target_key = 't';
static errr target_input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		saw_inspection |= textui_target_is_inspecting();
		Term_keypress(target_key, 0);
	}
	return 0;
}

static int test_target_marker_is_not_inspection(void *unused)
{
	term test_term;
	struct loc grid = loc(player->grid.x + 1, player->grid.y);
	struct monster *mon;
	struct sdl3_inspect_card card;
	struct sdl3_map_view view = { 0 };
	int x, y;
	bool visible;
	(void)unused;
	term_init(&test_term, 100, 30, 128);
	test_term.xtra_hook = target_input;
	term_screen = angband_term[0] = &test_term;
	Term_activate(&test_term);
	textui_input_init();
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = 0; dx <= 2; dx++) {
			struct loc cell = loc(player->grid.x + dx, player->grid.y + dy);
			square_set_feat(cave, cell, FEAT_FLOOR);
			sqinfo_on(square(cave, cell)->info, SQUARE_SEEN);
			sqinfo_on(square(cave, cell)->info, SQUARE_VIEW);
			sqinfo_on(square(cave, cell)->info, SQUARE_GLOW);
		}
	}
	mon = t_add_monster(cave, grid, "giant white mouse");
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	require(target_able(mon));
	/* Choosing nearest gives a temporary cue, not an enduring Look cursor. */
	Term_gotoxy(2, 3);
	Term_set_cursor(false);
	textui_target_closest();
	Term_locate(&x, &y);
	Term_get_cursor(&visible);
	eq(x, 2);
	eq(y, 3);
	require(!visible);
	require(target_is_set());
	require(!textui_target_is_inspecting());
	/* A genuine interactive target shows details and closes them on confirm. */
	saw_inspection = false;
	require(target_set_interactive(TARGET_KILL, grid.x, grid.y, false));
	require(saw_inspection);
	require(!textui_target_is_inspecting());
	require(target_is_set());
	health_track(player->upkeep, mon);
	view.active = true;
	view.term_cols = view.term_rows = 3;
	test_term.offset_x = grid.x - 1;
	test_term.offset_y = grid.y - 1;
	sdl3_inspect_card_configure(&card, &view, &test_term, 1, 1,
		textui_target_is_inspecting(), false, false);
	require(!card.active);
	sdl3_inspect_card_configure(&card, &view, &test_term, 1, 1, true, false, false);
	require(card.active);
	require(streq(card.name, mon->race->name));
	/* Esc must release the same modal ownership as confirming a target. */
	target_key = ESCAPE;
	saw_inspection = false;
	require(!target_set_interactive(TARGET_KILL, grid.x, grid.y, false));
	require(saw_inspection);
	require(!textui_target_is_inspecting());
	term_nuke(&test_term);
	term_screen = angband_term[0] = NULL;
	ok;
}

static int test_cave_creature_art_and_knowledge(void *unused)
{
	struct world_spelunk_system *system = NULL;
	struct world_spelunk_runtime *runtime;
	struct sdl3_map_view view = { 0 };
	struct sdl3_inspect_card card;
	int i, x, y;
	(void)unused;
	view.active = true;
	view.cols = view.source_cols = 30;
	view.rows = view.source_rows = 20;
	eq(world_spelunk_publish_system_candidate(world_spelunk_system_profile_by_id("deepwell"),
		0, 100, "shaft.surface", &system), WORLD_SPELUNK_PUBLICATION_OK);
	runtime = system->nodes[0].runtime;
	x = runtime->actors[0].x; y = runtime->actors[0].y;
	while (runtime->actor_count) require(world_spelunk_runtime_remove_actor(runtime, &runtime->actors[0]));
	for (i = 0; i < world_spelunk_actor_definition_count(); i++) {
		const struct world_spelunk_actor_definition *kind = world_spelunk_actor_by_index(i);
		const struct map_visual_binding *binding = map_visual_for("cave-actor", kind->id);
		require(world_spelunk_runtime_add_actor(runtime, kind->id, x, y));
		memset(runtime->visible, 0, runtime->state.map.width * runtime->state.map.height);
		runtime->visible[y * runtime->state.map.width + x] = 1;
		runtime->explored[y * runtime->state.map.width + x] = 1;
		notnull(binding);
		sdl3_inspect_card_configure_spelunking(&card, system, runtime, &view, x, y, true, false, true);
		require(card.active);
		require(streq(card.name, kind->name));
		require(streq(card.flavor, kind->description));
		eq(card.glyph, kind->glyph);
		if (streq(binding->domain, "monster")) require(strstr(card.asset_key, binding->art_id));
		else require(!card.asset_key[0]); /* Glyph fallback for the skitter. */
		/* Detection/remembering are not permission to reveal a creature portrait. */
		runtime->visible[y * runtime->state.map.width + x] = 0;
		runtime->detected[y * runtime->state.map.width + x] = 1;
		sdl3_inspect_card_configure_spelunking(&card, system, runtime, &view, x, y, true, false, true);
		require(card.active && !card.asset_key[0] && !card.flavor[0]);
		require(streq(card.name, kind->name)); /* Existing detection knowledge. */
		runtime->detected[y * runtime->state.map.width + x] = 0;
		sdl3_inspect_card_configure_spelunking(&card, system, runtime, &view, x, y, true, false, true);
		require(!card.active && !card.asset_key[0]);
		require(world_spelunk_runtime_remove_actor(runtime, &runtime->actors[0]));
	}
	world_spelunk_system_free(system);
	ok;
}

struct test tests[] = {
	{ "cave creature art and knowledge", test_cave_creature_art_and_knowledge },
	{ "known trap precedes floor", test_known_trap_precedes_floor },
	{ "target marker is not inspection", test_target_marker_is_not_inspection },
	{ NULL, NULL }
};
