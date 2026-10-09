/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Exercise real command/prompt input against an in-memory terminal. */
#include "unit-test.h"
#include "cave.h"
#include "game-input.h"
#include "game-world.h"
#include "init.h"
#include "message.h"
#include "monster.h"
#include "player.h"
#include "player-timed.h"
#include "target.h"
#include "ui-game.h"
#include "ui-input.h"
#include "ui-term.h"

static term test_term;
static struct angband_constants constants = {
	.level_monster_max = 2, .max_range = 20
};
static struct monster_race target_race = { .name = "practice beast" };
static struct level test_level = {
	.id = "test-floor", .mode = WORLD_MODE_TOP_DOWN
};
static int pause_calls;
static int unexpected_input;

static errr terminal_extra(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		/* A missing scripted key should fail, never hang a test runner. */
		++unexpected_input;
		Term_keypress(ESCAPE, 0);
	}
	return 0;
}

static enum textui_escape_menu_action pause_menu(void)
{
	++pause_calls;
	return TEXTUI_ESCAPE_CANCEL;
}

int setup_tests(void **state)
{
	(void)state;
	z_info = &constants;
	world = &test_level;
	player = mem_zalloc(sizeof(*player));
	player->upkeep = mem_zalloc(sizeof(*player->upkeep));
	player->timed = mem_zalloc(TMD_MAX * sizeof(*player->timed));
	player->grid = loc(3, 3);
	my_strcpy(player->world_location.id, "test-floor",
		sizeof(player->world_location.id));
	cave = cave_new(9, 11);
	player->cave = cave_new(9, 11);
	f_info = mem_zalloc(FEAT_MAX * sizeof(*f_info));
	flag_on(f_info[FEAT_FLOOR].flags, TF_SIZE, TF_PROJECT);
	flag_on(f_info[FEAT_FLOOR].flags, TF_SIZE, TF_LOS);
	for (int y = 0; y < cave->height; ++y) {
		for (int x = 0; x < cave->width; ++x) {
			cave->squares[y][x].feat = FEAT_FLOOR;
		}
	}
	term_init(&test_term, 100, 30, 64);
	test_term.xtra_hook = terminal_extra;
	angband_term[0] = &test_term;
	Term_activate(&test_term);
	messages_init();
	cmd_init();
	textui_input_init();
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	textui_escape_menu_hook = NULL;
	target_set_monster(NULL);
	cave_free(cave);
	cave = NULL;
	cave_free(player->cave);
	world = NULL;
	mem_free(f_info);
	f_info = NULL;
	mem_free(player->timed);
	mem_free(player->upkeep);
	mem_free(player);
	player = NULL;
	messages_free();
	term_nuke(&test_term);
	angband_term[0] = NULL;
	z_info = NULL;
	return 0;
}

static void begin_case(void)
{
	target_set_monster(NULL);
	Term_flush();
	inkey_next = NULL;
	pause_calls = 0;
	unexpected_input = 0;
	textui_escape_menu_hook = pause_menu;
	player->upkeep->energy_use = 0;
}

static int test_selected_target_cancels_before_pause(void *state)
{
	struct monster tracked = { 0 };
	(void)state;
	begin_case();
	target_set_location(3, 5);
	player->upkeep->health_who = &tracked;
	require(target_is_set());
	Term_keypress(ESCAPE, 0);
	textui_process_command();
	require(!target_is_set());
	require(!player->upkeep->health_who);
	eq(pause_calls, 0);
	eq(player->upkeep->energy_use, 0);
	Term_keypress(ESCAPE, 0);
	textui_process_command();
	eq(pause_calls, 1);
	eq(unexpected_input, 0);
	ok;
}

static int test_aiming_prompt_owns_escape(void *state)
{
	int direction = 99;
	(void)state;
	begin_case();
	target_set_location(3, 5);
	/* Cancelling a shot is not a request to open Pause. */
	Term_keypress(ESCAPE, 0);
	require(!get_aim_dir(&direction));
	eq(direction, 0);
	eq(pause_calls, 0);
	require(target_is_set());
	/* Once back in gameplay, Escape clears the remembered selection. */
	Term_keypress(ESCAPE, 0);
	textui_process_command();
	require(!target_is_set());
	eq(pause_calls, 0);
	eq(unexpected_input, 0);
	ok;
}

static int test_shared_aiming_keeps_direction_and_target_keys(void *state)
{
	int direction;
	(void)state;
	begin_case();
	Term_keypress('6', 0);
	require(get_aim_dir(&direction));
	eq(direction, 6);
	target_set_location(3, 5);
	Term_keypress('5', 0);
	require(get_aim_dir(&direction));
	eq(direction, DIR_TARGET);
	eq(pause_calls, 0);
	eq(unexpected_input, 0);
	ok;
}

static int test_target_cancellation_without_pause_frontend(void *state)
{
	(void)state;
	begin_case();
	textui_escape_menu_hook = NULL;
	target_set_location(3, 5);
	Term_keypress(ESCAPE, 0);
	textui_process_command();
	require(!target_is_set());
	Term_keypress(ESCAPE, 0);
	textui_process_command();
	eq(pause_calls, 0);
	eq(unexpected_input, 0);
	ok;
}

static int test_quote_selection_and_out_of_sight_cancellation(void *state)
{
	struct monster *mon = &cave->monsters[1];
	(void)state;
	begin_case();
	mon->race = &target_race;
	mon->midx = 1;
	mon->grid = loc(5, 3);
	mon->hp = mon->maxhp = 10;
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	cave->squares[mon->grid.y][mon->grid.x].mon = 1;
	cave->mon_max = 2;
	require(target_able(mon));
	require(target_accept(mon->grid.y, mon->grid.x));
	Term_keypress('\'', 0);
	textui_process_command();
	require(target_is_set());
	require(target_get_monster() == mon);
	require(player->upkeep->health_who == mon);
	eq(player->upkeep->energy_use, 0);
	/* A lost line of sight must not make the selection impossible to clear. */
	mflag_off(mon->mflag, MFLAG_VISIBLE);
	require(!target_okay());
	Term_keypress(ESCAPE, 0);
	textui_process_command();
	require(!target_is_set());
	require(!player->upkeep->health_who);
	eq(pause_calls, 0);
	eq(unexpected_input, 0);
	cave->squares[mon->grid.y][mon->grid.x].mon = 0;
	memset(mon, 0, sizeof(*mon));
	cave->mon_max = 1;
	ok;
}

const char *suite_name = "ui/target";
struct test tests[] = {
	{ "selected target cancels before pause",
		test_selected_target_cancels_before_pause },
	{ "aiming prompt owns Escape", test_aiming_prompt_owns_escape },
	{ "shared aiming keeps direction and target keys",
		test_shared_aiming_keeps_direction_and_target_keys },
	{ "target cancellation without pause frontend",
		test_target_cancellation_without_pause_frontend },
	{ "quote selection and out-of-sight cancellation",
		test_quote_selection_and_out_of_sight_cancellation },
	{ NULL, NULL }
};
