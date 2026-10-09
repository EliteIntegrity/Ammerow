/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/pause-menu-model.c */
/* Exercise platform-independent Escape-menu navigation and confirmation. */

#include "unit-test.h"

#include "sdl3/pause-menu-model.h"

int setup_tests(void **data)
{
	(void)data;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	return 0;
}

static int test_open_and_back(void *state)
{
	struct sdl3_pause_menu menu;
	(void)state;

	sdl3_pause_menu_init(&menu);
	require(!menu.visible);
	sdl3_pause_menu_open(&menu);
	require(menu.visible);
	eq(menu.page, SDL3_PAUSE_ROOT);
	eq(menu.selected_row, SDL3_PAUSE_BACK_TO_GAME);
	sdl3_pause_menu_back(&menu);
	eq(menu.action, SDL3_PAUSE_ACTION_BACK);
	ok;
}

static int test_root_actions(void *state)
{
	static const int expected[] = {
		SDL3_PAUSE_ACTION_BACK,
		SDL3_PAUSE_ACTION_SETTINGS,
		SDL3_PAUSE_ACTION_NONE,
		SDL3_PAUSE_ACTION_SAVE_QUIT
	};
	struct sdl3_pause_menu menu;
	int i;
	(void)state;

	for (i = 0; i < 4; i++) {
		sdl3_pause_menu_open(&menu);
		menu.selected_row = i;
		sdl3_pause_menu_activate(&menu);
		if (i == SDL3_PAUSE_HELP_CONTROLS) {
			eq(menu.page, SDL3_PAUSE_HELP);
			sdl3_pause_menu_back(&menu);
			eq(menu.page, SDL3_PAUSE_ROOT);
		} else {
			eq(menu.action, expected[i]);
		}
	}
	ok;
}

static int test_navigation_wrap(void *state)
{
	struct sdl3_pause_menu menu;
	(void)state;

	sdl3_pause_menu_open(&menu);
	sdl3_pause_menu_move(&menu, -1);
	eq(menu.selected_row, SDL3_PAUSE_ABANDON);
	sdl3_pause_menu_move(&menu, 1);
	eq(menu.selected_row, SDL3_PAUSE_BACK_TO_GAME);
	ok;
}

static int test_abandon_confirmation(void *state)
{
	struct sdl3_pause_menu menu;
	(void)state;

	sdl3_pause_menu_open(&menu);
	menu.selected_row = SDL3_PAUSE_ABANDON;
	sdl3_pause_menu_activate(&menu);
	eq(menu.page, SDL3_PAUSE_CONFIRM_ABANDON);
	require(!menu.abandon_confirmed);
	sdl3_pause_menu_activate(&menu);
	eq(menu.page, SDL3_PAUSE_ROOT);
	eq(menu.action, SDL3_PAUSE_ACTION_NONE);

	menu.selected_row = SDL3_PAUSE_ABANDON;
	sdl3_pause_menu_activate(&menu);
	sdl3_pause_menu_move(&menu, 1);
	require(menu.abandon_confirmed);
	sdl3_pause_menu_activate(&menu);
	eq(menu.action, SDL3_PAUSE_ACTION_ABANDON);
	ok;
}

static void open_confirmation(struct sdl3_pause_menu *menu)
{
	sdl3_pause_menu_open(menu);
	menu->selected_row = SDL3_PAUSE_ABANDON;
	sdl3_pause_menu_activate(menu);
}

static int test_pointer_confirmation(void *state)
{
	static const int sizes[][2] = {
		{ 80, 24 }, { 120, 40 }, { 51, 19 }, { 48, 14 }, { 200, 70 }
	};
	struct sdl3_pause_menu menu;
	(void)state;

	for (size_t i = 0; i < N_ELEMENTS(sizes); i++) {
		int cols = sizes[i][0];
		int rows = sizes[i][1];
		int width = cols - 4 < 62 ? cols - 4 : 62;
		int left = (cols - width) / 2;
		int choice_row = (rows - 12) / 2 + 7;

		/* Click every cell of both drawn buttons, without first navigating
		 * there by keyboard.  The old vertical hit-test chose Back on the
		 * entire right-hand button as well. */
		for (int choice = 0; choice < 2; choice++) {
			int first_col = left + (choice ? width / 2 : 3);
			struct sdl3_menu_layout drawn =
				sdl3_pause_confirm_choice_layout(cols, rows, choice != 0);

			eq(drawn.col, first_col);
			eq(drawn.row, choice_row);
			eq(drawn.width, width / 2 - 4);
			for (int col = first_col; col < first_col + drawn.width; col++) {
				open_confirmation(&menu);
				menu.abandon_confirmed = !choice;
				require(sdl3_pause_menu_select_at(&menu, cols, rows,
					col, choice_row));
				eq(menu.abandon_confirmed, choice != 0);
				eq(menu.action, SDL3_PAUSE_ACTION_NONE);
				sdl3_pause_menu_activate(&menu);
				eq(menu.action, choice ? SDL3_PAUSE_ACTION_ABANDON :
					SDL3_PAUSE_ACTION_NONE);
				eq(menu.page, choice ? SDL3_PAUSE_CONFIRM_ABANDON :
					SDL3_PAUSE_ROOT);
			}
		}
	}
	ok;
}

static int test_pointer_confirmation_misses(void *state)
{
	struct sdl3_pause_menu menu;
	/* At 80x24 the two buttons cover columns 12..38 and 40..66,
	 * row 13.  Include the old erroneous second vertical target. */
	static const int misses[][2] = {
		{ 11, 13 }, { 39, 13 }, { 67, 13 }, { 45, 12 }, { 45, 14 },
		{ 45, 41 }, { 0, 0 }, { 79, 23 }, { -1, -1 }
	};
	(void)state;

	for (size_t i = 0; i < N_ELEMENTS(misses); i++) {
		for (int selected = 0; selected < 2; selected++) {
			open_confirmation(&menu);
			menu.abandon_confirmed = selected != 0;
			require(!sdl3_pause_menu_select_at(&menu, 80, 24,
				misses[i][0], misses[i][1]));
			eq(menu.abandon_confirmed, selected != 0);
			eq(menu.page, SDL3_PAUSE_CONFIRM_ABANDON);
			eq(menu.action, SDL3_PAUSE_ACTION_NONE);
		}
	}
	open_confirmation(&menu);
	require(!sdl3_pause_menu_select_at(&menu, 47, 24, 30, 13));
	require(!sdl3_pause_menu_select_at(&menu, 80, 13, 45, 8));
	menu.visible = false;
	require(!sdl3_pause_menu_select_at(&menu, 80, 24, 45, 13));
	sdl3_pause_menu_open(&menu);
	menu.page = SDL3_PAUSE_HELP;
	require(!sdl3_pause_menu_select_at(&menu, 80, 24, 45, 13));
	ok;
}

static int test_pointer_root(void *state)
{
	struct sdl3_pause_menu menu;
	(void)state;

	sdl3_pause_menu_open(&menu);
	for (int i = 0; i < SDL3_PAUSE_ROOT_ROW_COUNT; i++) {
		require(sdl3_pause_menu_select_at(&menu, 80, 24, 20, 8 + i * 2));
		eq(menu.selected_row, i);
		require(!sdl3_pause_menu_select_at(&menu, 80, 24, 20, 9 + i * 2));
		eq(menu.selected_row, i);
	}
	sdl3_pause_menu_activate(&menu);
	eq(menu.page, SDL3_PAUSE_CONFIRM_ABANDON);
	require(!menu.abandon_confirmed);
	sdl3_pause_menu_back(&menu);
	eq(menu.page, SDL3_PAUSE_ROOT);
	eq(menu.action, SDL3_PAUSE_ACTION_NONE);
	require(!sdl3_pause_menu_select_at(&menu, 43, 24, 20, 8));
	require(!sdl3_pause_menu_select_at(&menu, 80, 17, 20, 4));
	ok;
}

const char *suite_name = "sdl3/pause-menu-model";
struct test tests[] = {
	{ "open and back", test_open_and_back },
	{ "root actions", test_root_actions },
	{ "navigation wrap", test_navigation_wrap },
	{ "abandon confirmation", test_abandon_confirmation },
	{ "pointer confirmation", test_pointer_confirmation },
	{ "pointer confirmation misses", test_pointer_confirmation_misses },
	{ "pointer root", test_pointer_root },
	{ NULL, NULL },
};
