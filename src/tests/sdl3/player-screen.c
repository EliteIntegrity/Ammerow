/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/player-screen.c */
/* Exercise the semantic character dossier and objective selection. */

#include "unit-test.h"
#include "test-utils.h"

#include "init.h"
#include "player.h"
#include "player-birth.h"
#include "player-quest.h"
#include "sdl3/screen-model.h"
#include "ui-player.h"
#include "ui-term.h"
#include "world-objective-data.h"

static struct sdl3_screen captured_screen;

static void capture_screen(const struct ui_screen *screen)
{
	sdl3_screen_capture(&captured_screen, screen);
}

int setup_tests(void **state)
{
	(void)state;
	set_file_paths();
	init_angband();
	return player_make_simple(NULL, NULL, "Dossier Tester") ? 0 : 1;
}

int teardown_tests(void *state)
{
	(void)state;
	cleanup_angband();
	return 0;
}

static int present_objectives(int cursor, bool briefing)
{
	term test_term = { 0 };
	term *previous_term = Term;
	int row_offset = 0;
	int count;

	test_term.wid = 100;
	test_term.hgt = 30;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	sdl3_screen_clear(&captured_screen);
	count = ui_player_present_objectives(&row_offset, cursor, briefing,
		briefing ? "Test expedition briefing" : NULL);
	Term = previous_term;
	return count;
}

static int test_quest_selection_owns_one_description(void *state)
{
	const struct world_objective_definition *first =
		world_objective_by_index(0);
	const struct world_objective_definition *second =
		world_objective_by_index(1);
	(void)state;

	require(first != NULL);
	require(second != NULL);
	eq(present_objectives(2, false), 5);
	eq(captured_screen.kind, UI_SCREEN_CHARACTER_DOSSIER);
	eq(captured_screen.cursor, 2);
	eq(captured_screen.row_count, 5);
	require(streq(captured_screen.context_title, first->title));
	require(streq(captured_screen.context, first->description));
	require(streq(captured_screen.rows[0].label, quests[0].name));
	require(streq(captured_screen.rows[0].prefix, "MAIN"));
	require(streq(captured_screen.rows[2].label, first->title));
	eq(captured_screen.rows[0].tag, 0);
	require(streq(captured_screen.tabs[0].label, "Overview"));
	require(streq(captured_screen.tabs[1].label, "Defences"));
	require(streq(captured_screen.tabs[2].label, "Quests"));
	require(captured_screen.tabs[2].active);

	eq(present_objectives(3, false), 5);
	eq(captured_screen.cursor, 3);
	require(streq(captured_screen.context_title, second->title));
	require(streq(captured_screen.context, second->description));
	require(!streq(captured_screen.context, first->description));
	ok;
}

static int test_selection_clamps_and_briefing_remains_collective(void *state)
{
	const struct world_objective_definition *first =
		world_objective_by_index(0);
	const struct world_objective_definition *second =
		world_objective_by_index(1);
	const struct world_objective_definition *third =
		world_objective_by_index(2);
	(void)state;

	require(first != NULL);
	require(second != NULL);
	require(third != NULL);
	eq(present_objectives(99, false), 5);
	eq(captured_screen.cursor, 4);
	require(streq(captured_screen.context_title, third->title));
	eq(present_objectives(-1, true), 3);
	eq(captured_screen.cursor, -1);
	require(strstr(captured_screen.context, first->description) != NULL);
	require(strstr(captured_screen.context, second->description) != NULL);
	require(strstr(captured_screen.context, third->description) != NULL);
	ok;
}

static int test_page_steps_follow_visual_order(void *state)
{
	(void)state;
	eq(ui_player_character_page_step(UI_PLAYER_CHARACTER_OVERVIEW, 1),
		UI_PLAYER_CHARACTER_DEFENCES);
	eq(ui_player_character_page_step(UI_PLAYER_CHARACTER_DEFENCES, 1),
		UI_PLAYER_CHARACTER_QUESTS);
	eq(ui_player_character_page_step(UI_PLAYER_CHARACTER_QUESTS, 1),
		UI_PLAYER_CHARACTER_CAVE_SURVEY);
	eq(ui_player_character_page_step(UI_PLAYER_CHARACTER_CAVE_SURVEY, 1),
		UI_PLAYER_CHARACTER_OVERVIEW);
	eq(ui_player_character_page_step(UI_PLAYER_CHARACTER_OVERVIEW, -1),
		UI_PLAYER_CHARACTER_CAVE_SURVEY);
	ok;
}

const char *suite_name = "sdl3/player-screen";
struct test tests[] = {
	{ "quest selection owns one description",
		test_quest_selection_owns_one_description },
	{ "selection clamps and briefing remains collective",
		test_selection_clamps_and_briefing_remains_collective },
	{ "page steps follow visual order", test_page_steps_follow_visual_order },
	{ NULL, NULL }
};
