/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/item-screen.c */
/* Exercise the semantic item-screen adapter against initialized game data. */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include "game-input.h"
#include "init.h"
#include "obj-util.h"
#include "player.h"
#include "player-birth.h"
#include "sdl3/screen-model.h"
#include "ui-menu.h"
#include "ui-object-screen.h"
#include "ui-prefs.h"
#include "ui-term.h"

static struct sdl3_screen captured_screen;

static void print_log(const char *message)
{
	printf("%s\n", message);
}

int setup_tests(void **state)
{
	*state = NULL;
	plog_aux = print_log;
	set_file_paths();
	init_angband();
	textui_prefs_init();
	return player_make_simple(NULL, NULL, "Screen Tester") ? 0 : 1;
}

int teardown_tests(void *state)
{
	(void)state;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static void capture_screen(const struct ui_screen *screen)
{
	sdl3_screen_capture(&captured_screen, screen);
}

static void present(struct menu *menu,
		const struct ui_object_screen_item *items, int item_count,
		olist_detail_t detail_mode, int item_mode, bool allow_all,
		const char *prompt, const char *header, int work)
{
	term test_term = { 0 };
	term *previous_term = Term;
	int previous_work = player->upkeep->command_wrk;

	test_term.screen_hook = capture_screen;
	Term = &test_term;
	player->upkeep->command_wrk = work;
	sdl3_screen_clear(&captured_screen);
	ui_object_screen_present(menu, items, item_count, detail_mode, item_mode,
		allow_all, prompt, header);
	player->upkeep->command_wrk = previous_work;
	Term = previous_term;
}

static struct menu basic_menu(int count)
{
	struct menu menu = { 0 };

	menu.count = count;
	menu.active.col = 7;
	menu.active.row = 8;
	menu.active.width = 76;
	menu.active.page_rows = 16;
	return menu;
}

static int active_tab(void)
{
	int i;
	int active = -1;

	for (i = 0; i < captured_screen.tab_count; i++) {
		if (!captured_screen.tabs[i].active) continue;
		require(active < 0);
		active = i;
	}
	return active;
}

static int test_real_object_snapshot(void *state)
{
	struct ui_object_screen_item items[2] = { 0 };
	struct menu menu = basic_menu(2);
	struct object *obj = player->gear;
	int tab;
	(void)state;

	notnull(obj);
	items[0].object = obj;
	items[0].equipment_label = "in pack       ";
	items[0].tag = 'a';
	items[1].fallback_name = "Empty equipment slot";
	items[1].equipment_label = "on body       ";
	items[1].tag = 'b';
	present(&menu, items, N_ELEMENTS(items), OLIST_WEIGHT,
		USE_INVEN | USE_EQUIP, false, "Wear or wield which item?",
		"Inventory", USE_INVEN);

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_ITEM_SELECTOR);
	require(streq(captured_screen.title, "Wear or wield which item?"));
	require(streq(captured_screen.subtitle, "Inventory"));
	eq(captured_screen.content_col, 7);
	eq(captured_screen.content_row, 8);
	eq(captured_screen.row_count, 2);
	require(captured_screen.rows[0].label[0] != '\0');
	require(captured_screen.rows[0].glyph == object_char(obj));
	eq(captured_screen.rows[0].attr, object_attr(obj));
	eq(captured_screen.rows[0].tag, 'a');
	require(captured_screen.rows[0].enabled);
	require(strstr(captured_screen.rows[0].detail, "lb") != NULL);
	require(streq(captured_screen.rows[1].label, "Empty equipment slot"));
	require(!captured_screen.rows[1].enabled);
	tab = active_tab();
	require(tab >= 0);
	require(streq(captured_screen.tabs[tab].label, "Inventory"));
	ok;
}

static int test_filter_compacts_rows_and_position(void *state)
{
	struct ui_object_screen_item items[] = {
		{ NULL, "Zero", NULL, 'a' },
		{ NULL, "One", NULL, 'b' },
		{ NULL, "Two", NULL, 'c' }
	};
	int filter[] = { 2, -1, 0, 99, 1 };
	struct menu menu = basic_menu(N_ELEMENTS(items));
	(void)state;

	menu.filter_list = filter;
	menu.filter_count = N_ELEMENTS(filter);
	menu.cursor = 2;
	menu.top = 2;
	present(&menu, items, N_ELEMENTS(items), OLIST_NONE, USE_INVEN, false,
		"Choose", "Filtered", USE_INVEN);

	eq(captured_screen.row_count, 3);
	require(streq(captured_screen.rows[0].label, "Two"));
	require(streq(captured_screen.rows[1].label, "Zero"));
	require(streq(captured_screen.rows[2].label, "One"));
	eq(captured_screen.cursor, 1);
	eq(captured_screen.row_offset, 1);
	ok;
}

static int test_command_and_list_matrix(void *state)
{
	struct command_case {
		const char *prompt;
		int mode;
		bool allow_all;
		int work;
		const char *active;
	} cases[] = {
		{ "Wear or wield which item?", USE_INVEN, false, USE_INVEN,
			"Inventory" },
		{ "Take off or unwield which item?", USE_EQUIP, false, USE_EQUIP,
			"Equipment" },
		{ "Quaff which potion?", USE_INVEN | USE_FLOOR, false, USE_INVEN,
			"Inventory" },
		{ "Eat which food?", USE_INVEN | USE_FLOOR, false, USE_FLOOR,
			"Floor" },
		{ "Use which item?", USE_INVEN | USE_EQUIP | USE_QUIVER | USE_FLOOR,
			false, USE_QUIVER, "Quiver" },
		{ "Drop which item?", USE_INVEN | USE_EQUIP, false, USE_EQUIP,
			"Equipment" },
		{ "Examine which item?", 0, true, USE_FLOOR, "Floor" },
		{ "Inventory", USE_INVEN, false, USE_INVEN, "Inventory" },
		{ "Equipment", USE_EQUIP, false, USE_EQUIP, "Equipment" },
		{ "Quiver", USE_QUIVER, false, USE_QUIVER, "Quiver" },
		{ "Floor", USE_FLOOR, false, USE_FLOOR, "Floor" },
		{ "Throw which item?", USE_INVEN, false, SHOW_THROWING,
			"Throwing" }
	};
	struct ui_object_screen_item item = { NULL, "Test item", NULL, 'a' };
	struct menu menu = basic_menu(1);
	int i;
	(void)state;

	for (i = 0; i < (int)N_ELEMENTS(cases); i++) {
		int tab;

		present(&menu, &item, 1, OLIST_NONE, cases[i].mode,
			cases[i].allow_all, cases[i].prompt, cases[i].active,
			cases[i].work);
		require(streq(captured_screen.title, cases[i].prompt));
		tab = active_tab();
		require(tab >= 0);
		require(streq(captured_screen.tabs[tab].label, cases[i].active));
	}
	ok;
}

static int test_capacity_and_scrolling_are_bounded(void *state)
{
	struct ui_object_screen_item items[55];
	char names[55][16];
	struct menu menu = basic_menu(N_ELEMENTS(items));
	int i;
	(void)state;

	memset(items, 0, sizeof(items));
	for (i = 0; i < (int)N_ELEMENTS(items); i++) {
		strnfmt(names[i], sizeof(names[i]), "Item %d", i);
		items[i].fallback_name = names[i];
		items[i].tag = I2A(i % 26);
	}
	menu.cursor = 54;
	menu.top = 48;
	present(&menu, items, N_ELEMENTS(items), OLIST_NONE, USE_INVEN, false,
		"Inventory", "Inventory", USE_INVEN);

	eq(captured_screen.row_count, 50);
	eq(captured_screen.cursor, 49);
	eq(captured_screen.row_offset, 48);
	require(streq(captured_screen.rows[0].label, "Item 0"));
	require(streq(captured_screen.rows[49].label, "Item 49"));
	ok;
}

static int test_tab_steps_follow_visual_order(void *state)
{
	int all = USE_INVEN | USE_EQUIP | USE_QUIVER | USE_FLOOR;
	(void)state;

	eq(ui_object_screen_step_tab(USE_INVEN, 1, all), USE_EQUIP);
	eq(ui_object_screen_step_tab(USE_EQUIP, 1, all), USE_QUIVER);
	eq(ui_object_screen_step_tab(USE_QUIVER, 1, all), USE_FLOOR);
	eq(ui_object_screen_step_tab(USE_FLOOR, 1, all), USE_INVEN);
	eq(ui_object_screen_step_tab(USE_INVEN, -1, all), USE_FLOOR);
	eq(ui_object_screen_step_tab(USE_FLOOR, -1, all), USE_QUIVER);

	/* Empty or disallowed lists are skipped without changing direction. */
	all = USE_INVEN | USE_QUIVER;
	eq(ui_object_screen_step_tab(USE_INVEN, 1, all), USE_QUIVER);
	eq(ui_object_screen_step_tab(USE_INVEN, -1, all), USE_QUIVER);
	eq(ui_object_screen_step_tab(SHOW_THROWING, -1,
		all | SHOW_THROWING), USE_QUIVER);
	eq(ui_object_screen_step_tab(SHOW_THROWING, 1,
		all | SHOW_THROWING), USE_INVEN);
	ok;
}

static int test_missing_hook_is_a_noop(void *state)
{
	struct ui_object_screen_item item = { NULL, "Untouched", NULL, 'a' };
	struct menu menu = basic_menu(1);
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	sdl3_screen_clear(&captured_screen);
	Term = &test_term;
	ui_object_screen_present(&menu, &item, 1, OLIST_NONE, USE_INVEN,
		false, "Inventory", "Inventory");
	Term = previous_term;
	require(!captured_screen.active);
	ok;
}

const char *suite_name = "sdl3/item-screen";
struct test tests[] = {
	{ "filter compacts rows and position", test_filter_compacts_rows_and_position },
	{ "command and list matrix", test_command_and_list_matrix },
	{ "capacity and scrolling are bounded", test_capacity_and_scrolling_are_bounded },
	{ "tab steps follow visual order", test_tab_steps_follow_visual_order },
	{ "missing hook is a noop", test_missing_hook_is_a_noop },
	{ "real object snapshot", test_real_object_snapshot },
	{ NULL, NULL }
};
