/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/player-screen.c */
/* Exercise the semantic character dossier and objective selection. */

#include "unit-test.h"
#include "test-utils.h"

#include "init.h"
#include "game-world.h"
#include "ui-input.h"
#include "ui-output.h"
#include "player.h"
#include "player-birth.h"
#include "player-quest.h"
#include "sdl3/screen-model.h"
#include "ui-player.h"
#include "ui-term.h"
#include "world-objective-data.h"
#include "ui-spelunking-survey.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-system-data.h"

static struct sdl3_screen captured_screen;
static struct survey_step {
	enum ui_screen_kind kind;
	int cursor, tabs;
	keycode_t key;
	bool pointer;
} survey_steps[32];
static int survey_step_index, survey_step_count;

static errr survey_input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		struct survey_step *step;
		if (survey_step_index >= survey_step_count) quit("Unexpected survey input request");
		step = &survey_steps[survey_step_index++];
		if (captured_screen.kind != step->kind || captured_screen.tab_count != step->tabs ||
				(step->cursor >= 0 && captured_screen.cursor != step->cursor)) {
			printf("Survey step %d: kind %d cursor %d tabs %d\n", survey_step_index,
				captured_screen.kind, captured_screen.cursor, captured_screen.tab_count);
			quit("Wrong survey controller transition");
		}
		if (step->pointer) {
			struct sdl3_screen_map_layout map;
			int selected = -1;
			if (!sdl3_screen_map_measure(&captured_screen, Term->wid, Term->hgt, &map) ||
					!sdl3_screen_map_node_at(&captured_screen, Term->wid, Term->hgt,
						map.x[2], map.y[2], &selected)) quit("Survey pointer missed section");
			Term_keypress(KC_SURVEY_SELECT_FIRST + selected, 0);
		} else Term_keypress(step->key, 0);
	}
	return 0;
}

static void survey_step(enum ui_screen_kind kind, int cursor, int tabs, keycode_t key)
{
	assert(survey_step_count < (int)N_ELEMENTS(survey_steps));
	survey_steps[survey_step_count++] = (struct survey_step){kind, cursor, tabs, key, false};
}

static void capture_screen(const struct ui_screen *screen)
{
	sdl3_screen_capture(&captured_screen, screen);
}

int setup_tests(void **state)
{
	char path[1024];
	(void)state;
	set_file_paths();
	path_build(path, sizeof(path), ANGBAND_DIR_USER, "player-screen-test");
	if (!dir_create(path)) return 1;
	string_free(ANGBAND_DIR_USER);
	ANGBAND_DIR_USER = string_make(path);
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

static int test_survey_controller_focus_and_return(void *state)
{
	term test_term;
	int32_t before = turn;
	int discovered = 0;
	struct world_spelunk_system *system = NULL;
	const struct world_spelunk_system_profile *profile =
		world_spelunk_system_profile_by_id("deepwell");
	(void)state;
	notnull(profile);
	eq(world_spelunk_publish_system_candidate(profile, 424242u, 100,
		"shaft.surface", &system), WORLD_SPELUNK_PUBLICATION_OK);
	notnull(system);
	for (size_t i = 0; i < system->edge_count && discovered < 2; i++) {
		if (streq(system->edges[i].node_a_id, system->active_node_id)) {
			require(world_spelunk_system_discover_edge(system, system->edges[i].id));
			discovered++;
		}
	}
	eq(discovered, 2);
	player->spelunking_system = system;
	term_init(&test_term, 80, 24, 128);
	test_term.screen_hook = capture_screen;
	test_term.xtra_hook = survey_input;
	term_screen = angband_term[0] = &test_term;
	Term_activate(&test_term);
	inkey_next = NULL;
	survey_step_index = survey_step_count = 0;
	survey_step(UI_SCREEN_CHARACTER_DOSSIER, -1, 4, ARROW_LEFT);
	survey_step(UI_SCREEN_CAVE_SURVEY, 0, 4, KC_ENTER);
	survey_step(UI_SCREEN_CAVE_SURVEY, 0, 0, ARROW_DOWN);
	survey_step(UI_SCREEN_CAVE_SURVEY, 1, 0, ARROW_RIGHT);
	survey_step(UI_SCREEN_CAVE_SURVEY, 2, 0, ARROW_UP);
	survey_step(UI_SCREEN_CAVE_SURVEY, 0, 0, ESCAPE);
	survey_step(UI_SCREEN_CAVE_SURVEY, 0, 4, 0);
	survey_steps[survey_step_count - 1].pointer = true;
	survey_step(UI_SCREEN_CAVE_SURVEY, 2, 0, KC_HOME);
	survey_step(UI_SCREEN_CAVE_SURVEY, 0, 0, ESCAPE);
	survey_step(UI_SCREEN_CAVE_SURVEY, 0, 4, ARROW_LEFT);
	survey_step(UI_SCREEN_CHARACTER_DOSSIER, -1, 4, ESCAPE);
	do_cmd_change_name();
	eq(survey_step_index, survey_step_count);
	eq(screen_save_depth, 0);
	eq(turn, before);
	require(!captured_screen.active);
	term_nuke(&test_term);
	term_screen = angband_term[0] = NULL;
	world_spelunk_system_free(system);
	player->spelunking_system = NULL;
	ok;
}

static int test_empty_survey_cannot_focus(void *state)
{
	term test_term = { 0 };
	term *old = Term;
	int cursor = -1;
	(void)state;
	test_term.wid = 80;
	test_term.hgt = 24;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	eq(ui_player_present_cave_survey(&cursor, true), 0);
	eq(cursor, -1);
	eq(captured_screen.tab_count, 4);
	require(strstr(captured_screen.subtitle, "No connected cave"));
	Term = old;
	ok;
}

const char *suite_name = "sdl3/player-screen";
struct test tests[] = {
	{ "quest selection owns one description",
		test_quest_selection_owns_one_description },
	{ "selection clamps and briefing remains collective",
		test_selection_clamps_and_briefing_remains_collective },
	{ "page steps follow visual order", test_page_steps_follow_visual_order },
	{ "empty survey cannot focus", test_empty_survey_cannot_focus },
	{ "survey controller focus pointer and return", test_survey_controller_focus_and_return },
	{ NULL, NULL }
};
