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

struct test tests[] = {
	{ "known trap precedes floor", test_known_trap_precedes_floor },
	{ "target marker is not inspection", test_target_marker_is_not_inspection },
	{ NULL, NULL }
};
