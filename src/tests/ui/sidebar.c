/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "unit-test.h"
#include "test-utils.h"
#include "angband.h"
#include "cave.h"
#include "effects.h"
#include "game-event.h"
#include "init.h"
#include "obj-util.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "ui-display.h"
#include "ui-map.h"
#include "ui-output.h"
#include "ui-prefs.h"

static term test_term;

int setup_tests(void **state)
{
	char path[1024];
	(void)state;
	set_file_paths();
	path_build(path, sizeof(path), ANGBAND_DIR_USER, "sidebar-test");
	if (!dir_create(path)) return 1;
	string_free(ANGBAND_DIR_USER);
	string_free(ANGBAND_DIR_ARCHIVE);
	string_free(ANGBAND_DIR_SAVE);
	ANGBAND_DIR_USER = string_make(path);
	ANGBAND_DIR_ARCHIVE = string_make(path);
	ANGBAND_DIR_SAVE = string_make(path);
	init_angband();
	textui_prefs_init();
	if (!player_make_simple("Riftlistener", "Ranger", "Sidebar Tester")) return 1;
	cave = t_build_arena(9, 11);
	player->grid = loc(5, 4);
	term_init(&test_term, 100, 32, 128);
	term_screen = angband_term[0] = &test_term;
	Term_activate(&test_term);
	init_display();
	event_signal(EVENT_ENTER_WORLD);
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	event_signal(EVENT_LEAVE_WORLD);
	event_remove_all_handlers();
	term_nuke(&test_term);
	term_screen = angband_term[0] = NULL;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static int find_text(const char *text, int cols, int first, int last)
{
	char row[128];
	for (int y = first; y < last; y++) {
		for (int x = 0; x < cols; x++) {
			int attr;
			wchar_t ch;
			Term_what(x, y, &attr, &ch);
			row[x] = (char)ch;
		}
		row[cols] = '\0';
		if (strstr(row, text)) return y;
	}
	return -1;
}

static int test_poison_appears_and_clears_without_overflow(void *state)
{
	const int heights[] = { 18, 24, 32 };
	(void)state;
	test_term.sidebar_mode = SIDEBAR_LEFT;
	for (int i = 0; i < (int)N_ELEMENTS(heights); i++) {
		int poison_row, hp_row, attr;
		wchar_t ch;
		Term_resize(100, heights[i]);
		Term_clear();
		Term_putstr(0, Term->hgt - 1, -1, COLOUR_WHITE, "KEEP");
		Term_putch(COL_MAP, 4, COLOUR_WHITE, L'@');
		player->timed[TMD_POISONED] = 30;
		event_signal(EVENT_STATUS);
		poison_row = find_text("Poisoned", COL_MAP, 1, Term->hgt - 1);
		hp_row = find_text("HP ", COL_MAP, 1, Term->hgt - 1);
		require(hp_row >= 1);
		eq(poison_row, hp_row + 1);
		require(find_text("KEEP", COL_MAP, Term->hgt - 1, Term->hgt) >= 0);
		Term_what(COL_MAP, 4, &attr, &ch);
		eq(ch, L'@');
		player->timed[TMD_POISONED] = 0;
		event_signal(EVENT_STATUS);
		eq(find_text("Poisoned", COL_MAP, 1, Term->hgt - 1), -1);
		require(find_text("HP ", COL_MAP, 1, Term->hgt - 1) >= 1);
		require(find_text("KEEP", COL_MAP, Term->hgt - 1, Term->hgt) >= 0);
	}
	ok;
}

static int test_topbar_keeps_poison_next_to_hp(void *state)
{
	(void)state;
	Term_resize(100, 32);
	Term_clear();
	test_term.sidebar_mode = SIDEBAR_TOP;
	player->timed[TMD_POISONED] = 30;
	event_signal(EVENT_STATUS);
	eq(find_text("Poisoned", 100, 2, 3), 2);
	player->timed[TMD_POISONED] = 0;
	event_signal(EVENT_STATUS);
	eq(find_text("Poisoned", 100, 2, 3), -1);
	ok;
}

static int test_food_effects_refresh_within_fed_grade(void *state)
{
	const char *foods[] = { "Rain Minnow", "Mudbelly", "Glassfin",
		"Ration of Food" };
	const int modes[] = { SIDEBAR_LEFT, SIDEBAR_TOP };
	(void)state;
	Term_resize(100, 32);
	for (int mode = 0; mode < (int)N_ELEMENTS(modes); mode++) {
		test_term.sidebar_mode = modes[mode];
		for (int i = 0; i < (int)N_ELEMENTS(foods); i++) {
			struct object_kind *kind = lookup_kind(TV_FOOD,
				lookup_sval(TV_FOOD, foods[i]));
			bool ident = false;
			char expected[32];
			notnull(kind);
			notnull(kind->effect);
			Term_clear();
			player->timed[TMD_FOOD] = 50 * z_info->food_value;
			event_signal(EVENT_STATUS);
			require(find_text("Fed 50", 100, 1, Term->hgt) >= 0);
			player->upkeep->redraw = 0;

			/* Use the food's real effect data, without a hunger-grade change
			 * or an unrelated HP/stamina event masking a missing refresh. */
			require(effect_do(kind->effect, source_player(), NULL,
				&ident, true, 0, 0, 0, NULL));
			require(ident);
			require(player->timed[TMD_FOOD] > 50 * z_info->food_value);
			require(player_timed_grade_eq(player, TMD_FOOD, "Fed"));
			require(player->upkeep->redraw & PR_STATUS);
			redraw_stuff(player);
			strnfmt(expected, sizeof(expected), "Fed %d",
				player->timed[TMD_FOOD] / z_info->food_value);
			require(find_text(expected, 100, 1, Term->hgt) >= 0);
			eq(find_text("Fed 50", 100, 1, Term->hgt), -1);
		}
	}
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL - 1);
	ok;
}

static int test_quiet_digestion_refreshes_but_no_change_does_not(void *state)
{
	(void)state;
	test_term.sidebar_mode = SIDEBAR_LEFT;
	Term_clear();
	player->timed[TMD_FOOD] = 50 * z_info->food_value;
	event_signal(EVENT_STATUS);
	player->upkeep->redraw = 0;
	require(!player_set_timed(player, TMD_FOOD,
		player->timed[TMD_FOOD], false, false));
	eq(player->upkeep->redraw, 0);
	/* A quiet change must remain quiet while still updating the percentage. */
	require(!player_dec_timed(player, TMD_FOOD, z_info->food_value,
		false, false));
	require(player->upkeep->redraw & PR_STATUS);
	redraw_stuff(player);
	require(find_text("Fed 49%", COL_MAP, 1, Term->hgt - 1) >= 0);
	eq(find_text("Fed 50%", COL_MAP, 1, Term->hgt - 1), -1);
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL - 1);
	ok;
}

const char *suite_name = "ui/sidebar";
struct test tests[] = {
	{ "poison is adjacent to HP and does not overwrite map or footer", test_poison_appears_and_clears_without_overflow },
	{ "topbar poison appears and clears", test_topbar_keeps_poison_next_to_hp },
	{ "food effects refresh percentages within Fed", test_food_effects_refresh_within_fed_grade },
	{ "quiet digestion refreshes; unchanged food does not", test_quiet_digestion_refreshes_but_no_change_does_not },
	{ NULL, NULL }
};
