/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/store-screen.c */
/* Exercise the shared semantic ordinary-store presentation adapter. */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include "init.h"
#include "game-input.h"
#include "option.h"
#include "obj-gear.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-birth.h"
#include "sdl3/screen-model.h"
#include "store.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-game.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-prefs.h"
#include "ui-store.h"
#include "ui-store-screen.h"
#include "ui-term.h"
#include "world-larder-data.h"

static struct sdl3_screen captured_screen;
static term controller_term;
enum expected_view { STOCK, ACTIONS, RECALL, REJECT, DONATION_REJECT, ITEM, STORY };
static struct { keycode_t key; enum expected_view view; } input_steps[8];
static int input_count, input_pos;
static bool views_ok;
static const char *story_text;

static errr controller_input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		if (input_pos >= input_count) quit("Unexpected store-controller prompt");
		switch (input_steps[input_pos].view) {
		case ITEM:
			views_ok &= captured_screen.active &&
				captured_screen.kind == UI_SCREEN_ITEM_SELECTOR;
			break;
		case STORY:
			views_ok &= captured_screen.active &&
				captured_screen.kind == UI_SCREEN_STORY &&
				strstr(captured_screen.context, story_text) != NULL;
			break;
		case STOCK:
			views_ok &= captured_screen.active &&
				captured_screen.kind == UI_SCREEN_STORE &&
				strstr(captured_screen.help, "Enter actions") != NULL;
			break;
		case ACTIONS:
			views_ok &= captured_screen.active && streq(captured_screen.title, "Shop item");
			break;
		case RECALL:
			views_ok &= !captured_screen.active;
			break;
		case REJECT:
			views_ok &= captured_screen.active &&
				strstr(captured_screen.context, "You have nothing the shopkeeper wants.") != NULL;
			break;
		case DONATION_REJECT:
			views_ok &= captured_screen.active &&
				strstr(captured_screen.context, "You are not carrying a fishing catch.") != NULL;
			break;
		}
		if (!views_ok) printf("Store step %d: active=%d title=%s context=%s\n",
			input_pos, captured_screen.active, captured_screen.title,
			captured_screen.context);
		Term_keypress(input_steps[input_pos++].key, 0);
	}
	return 0;
}

static void input_step(keycode_t key, enum expected_view view)
{
	assert(input_count < (int)N_ELEMENTS(input_steps));
	input_steps[input_count].key = key;
	input_steps[input_count++].view = view;
}

static void begin_input(void)
{
	Term_flush();
	inkey_next = NULL;
	input_count = input_pos = 0;
	views_ok = true;
}

static void print_log(const char *message)
{
	printf("%s\n", message);
}

static void capture_screen(const struct ui_screen *screen)
{
	sdl3_screen_capture(&captured_screen, screen);
}

int setup_tests(void **state)
{
	char path[1024];
	*state = NULL;
	plog_aux = print_log;
	set_file_paths();
	path_build(path, sizeof(path), ANGBAND_DIR_USER, "store-screen-test");
	if (!dir_create(path)) return 1;
	string_free(ANGBAND_DIR_USER);
	string_free(ANGBAND_DIR_ARCHIVE);
	string_free(ANGBAND_DIR_SAVE);
	ANGBAND_DIR_USER = string_make(path);
	ANGBAND_DIR_ARCHIVE = string_make(path);
	ANGBAND_DIR_SAVE = string_make(path);
	init_angband();
	textui_prefs_init();
	if (!player_make_simple("Nearlander", "Warrior", "Store Screen Tester")) return 1;
	cave = t_build_arena(9, 11);
	player->grid = loc(5, 4);
	term_init(&controller_term, 100, 30, 128);
	controller_term.screen_hook = capture_screen;
	controller_term.xtra_hook = controller_input;
	term_screen = angband_term[0] = &controller_term;
	Term_activate(&controller_term);
	cmd_init();
	textui_input_init();
	event_add_handler(EVENT_MESSAGE, display_message, NULL);
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	event_remove_handler(EVENT_MESSAGE, display_message, NULL);
	term_nuke(&controller_term);
	term_screen = angband_term[0] = NULL;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static int test_commercial_store_snapshot(void *state)
{
	struct owner owner = { .name = "Mira", .max_cost = 10000 };
	struct store store = {
		.owner = &owner,
		.welcome = "Rope, lamp oil, and honest provisions for the road.",
		.feat = FEAT_STORE_GENERAL,
		.stock_num = 1
	};
	struct object *object = object_new();
	struct object *stock[] = { object };
	struct menu menu = { 0 };
	struct object_kind *kind = lookup_kind(TV_FOOD, 1);
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	require(kind != NULL);
	object_prep(object, kind, 1, AVERAGE);
	player->au = 1234;
	menu.count = 1;
	menu.selections = "a";
	test_term.wid = 120;
	test_term.hgt = 36;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	sdl3_screen_clear(&captured_screen);
	ui_store_screen_present(&menu, &store, stock, false, 80, NULL);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_STORE);
	require(streq(captured_screen.title, f_info[FEAT_STORE_GENERAL].name));
	require(strstr(captured_screen.subtitle, "Mira") != NULL);
	require(strstr(captured_screen.subtitle, "1234") != NULL);
	require(strstr(captured_screen.context, "honest provisions") != NULL);
	eq(captured_screen.row_count, 1);
	eq(captured_screen.rows[0].tag, 'a');
	require(captured_screen.rows[0].label[0]);
	require(captured_screen.rows[0].prefix[0]);
	require(captured_screen.rows[0].detail[0]);
	require(strstr(captured_screen.help, "Enter actions") != NULL);
	object_delete(NULL, NULL, &object);
	ok;
}

static int test_home_uses_same_foundation_without_prices(void *state)
{
	struct store store = { .feat = FEAT_HOME, .stock_num = 1 };
	struct object *object = object_new();
	struct object *stock[] = { object };
	struct menu menu = { 0 };
	struct object_kind *kind = lookup_kind(TV_FOOD, 1);
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	require(kind != NULL);
	object_prep(object, kind, 1, AVERAGE);
	menu.count = 1;
	menu.selections = "a";
	test_term.wid = 100;
	test_term.hgt = 30;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	ui_store_screen_present(&menu, &store, stock, false, 100, NULL);
	Term = previous_term;

	require(streq(captured_screen.title, "Your Home"));
	require(!captured_screen.rows[0].detail[0]);
	require(strstr(captured_screen.help, "stashes") != NULL);
	object_delete(NULL, NULL, &object);
	ok;
}

static int test_transaction_confirmation(void *state)
{
	struct owner owner = { .name = "Mira", .max_cost = 10000 };
	struct store store = { .owner = &owner, .feat = FEAT_STORE_GENERAL };
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	test_term.wid = 100;
	test_term.hgt = 30;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	ui_store_screen_present_confirmation(&store, "Confirm purchase",
		"Buy two Rations of Food?", 14, 80);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_STORE);
	require(streq(captured_screen.title, "Confirm purchase"));
	require(strstr(captured_screen.subtitle, "Mira") != NULL);
	require(strstr(captured_screen.subtitle, "14 gold") != NULL);
	require(strstr(captured_screen.context, "Rations of Food") != NULL);
	eq(captured_screen.cursor, -1);
	require(strstr(captured_screen.help, "Escape cancels") != NULL);
	ok;
}

static int test_transaction_quantity(void *state)
{
	struct owner owner = { .name = "Mira", .max_cost = 10000 };
	struct store store = { .owner = &owner, .feat = FEAT_STORE_GENERAL };
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	test_term.wid = 100;
	test_term.hgt = 30;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	ui_store_screen_present_quantity(&store, "Choose purchase quantity",
		"Buy how many?", "*", 17, 80);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_STORE);
	require(streq(captured_screen.title, "Choose purchase quantity"));
	require(strstr(captured_screen.subtitle, "Maximum: 17") != NULL);
	require(strstr(captured_screen.context, "Buy how many") != NULL);
	eq(captured_screen.row_count, 1);
	require(strstr(captured_screen.rows[0].label, "all (17)") != NULL);
	require(strstr(captured_screen.help, "* takes all") != NULL);
	ok;
}

static int test_store_actions(void *state)
{
	struct owner owner = { .name = "Mira", .max_cost = 10000 };
	struct store store = { .owner = &owner, .feat = FEAT_STORE_GENERAL };
	const char *labels[] = { "Examine", "Buy", "Buy one" };
	const char tags[] = { 'l', 'p', 'o' };
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	test_term.wid = 100;
	test_term.hgt = 30;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	ui_store_screen_present_actions(&store, "Shop item",
		"a Ration of Food", labels, tags, 3, 1, 80);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_STORE);
	require(streq(captured_screen.title, "Shop item"));
	require(strstr(captured_screen.subtitle, "Mira") != NULL);
	require(streq(captured_screen.context, "a Ration of Food"));
	eq(captured_screen.row_count, 3);
	eq(captured_screen.cursor, 1);
	require(streq(captured_screen.rows[1].label, "Buy"));
	eq(captured_screen.rows[1].tag, 'p');
	ok;
}

static int test_flavoured_stock_glyphs(void *state)
{
	struct owner owner = { .name = "Test owner", .max_cost = 10000 };
	struct store store = { .owner = &owner, .stock_num = 1 };
	struct object *obj = object_new();
	struct object *stock[] = { obj };
	struct object_kind *kind = lookup_kind(TV_POTION, 1);
	struct menu menu = { .count = 1, .selections = "a" };
	const int features[] = { FEAT_STORE_ALCHEMY, FEAT_STORE_BLACK };
	uint8_t old_attr;
	(void)state;
	notnull(kind);
	notnull(kind->flavor);
	object_prep(obj, kind, 1, AVERAGE);
	old_attr = flavor_x_attr[kind->flavor->fidx];
	for (int i = 0; i < (int)N_ELEMENTS(features); i++) {
		store.feat = features[i];
		flavor_x_attr[kind->flavor->fidx] = COLOUR_ORANGE;
		ui_store_screen_present(&menu, &store, stock, false, 100, NULL);
		eq(captured_screen.rows[0].glyph, object_char(obj));
		eq(captured_screen.rows[0].attr, COLOUR_ORANGE);
		flavor_x_attr[kind->flavor->fidx] = COLOUR_DARK;
		ui_store_screen_present(&menu, &store, stock, false, 100, NULL);
		eq(captured_screen.rows[0].attr, COLOUR_SLATE);
	}
	flavor_x_attr[kind->flavor->fidx] = old_attr;
	object_delete(NULL, NULL, &obj);
	ok;
}

static int test_examine_shortcuts_and_empty_sale(void *state)
{
	struct store *store;
	struct object_buy *old_buy;
	struct object_buy reject_all = { .tval = 0 };
	const char examine[] = "lLxX";
	(void)state;
	square_set_feat(cave, player->grid, FEAT_STORE_GENERAL);
	store = store_at(cave, player->grid);
	notnull(store);
	require(store->stock_num > 0);
	for (int mode = 0; mode < 2; mode++) {
		player->opts.opt[OPT_rogue_like_commands] = mode != 0;
		for (int n = 0; examine[n]; n++) {
			begin_input();
			input_step(examine[n], STOCK);
			input_step(ESCAPE, RECALL);
			input_step(ESCAPE, STOCK);
			use_store(EVENT_USE_STORE, NULL, NULL);
			eq(input_pos, input_count);
			require(views_ok);
			eq(screen_save_depth, 0);
		}
		begin_input();
		input_step(KC_ENTER, STOCK);
		input_step(mode ? 'x' : 'l', ACTIONS);
		input_step(ESCAPE, RECALL);
		input_step(ESCAPE, STOCK);
		use_store(EVENT_USE_STORE, NULL, NULL);
		eq(input_pos, input_count);
		require(views_ok);
		begin_input();
		input_step('D', STOCK);
		input_step(ESCAPE, DONATION_REJECT);
		use_store(EVENT_USE_STORE, NULL, NULL);
		eq(input_pos, input_count);
		require(views_ok);
	}
	/* Shift+D remains donation at the general store, selling elsewhere. */
	square_set_feat(cave, player->grid, FEAT_STORE_ALCHEMY);
	store = store_at(cave, player->grid);
	notnull(store);
	old_buy = store->buy;
	for (int mode = 0; mode < 2; mode++) {
		player->opts.opt[OPT_rogue_like_commands] = mode != 0;
		for (int upper = 0; upper < 2; upper++) {
			begin_input();
			input_step(upper ? 'D' : 'd', STOCK);
			input_step(ESCAPE, REJECT);
			store->buy = &reject_all;
			use_store(EVENT_USE_STORE, NULL, NULL);
			store->buy = old_buy;
			eq(input_pos, input_count);
			require(views_ok);
		}
	}
	ok;
}

static int test_quest_briefing_requires_acknowledgement(void *state)
{
	(void)state;
	square_set_feat(cave, player->grid, FEAT_STORE_FISHING);
	player->upkeep->fishing_quests_announced = false;
	story_text = "Shift+C > Quests";
	begin_input();
	input_step(ARROW_DOWN, STORY); /* Navigation must not silently dismiss it. */
	input_step(KC_ENTER, STORY);
	input_step(ESCAPE, STOCK);
	use_store(EVENT_USE_STORE, NULL, NULL);
	eq(input_pos, input_count);
	require(views_ok);
	require(player->upkeep->fishing_quests_announced);
	eq(screen_save_depth, 0);
	ok;
}

static int test_donation_receipt_returns_to_shop(void *state)
{
	struct object_kind *kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Rain Minnow"));
	(void)state;
	notnull(kind);
	square_set_feat(cave, player->grid, FEAT_STORE_GENERAL);
	for (int completion = 0; completion < 2; completion++) {
		struct object *obj = object_new();
		int32_t experience = player->exp;
		world_larder_reset(&player->larder);
		if (completion) {
			const struct world_larder_milestone_definition *next =
				world_larder_next_milestone(&player->larder);
			notnull(next);
			player->larder.food_points = next->target - 1;
		}
		object_prep(obj, kind, 1, AVERAGE);
		obj->known = object_new();
		object_copy(obj->known, obj);
		obj->known->known = NULL;
		inven_carry(player, obj, false, false);
		for (obj = player->gear; obj && obj->kind != kind; obj = obj->next) { }
		notnull(obj);
		story_text = completion ? "Expedition Rations" : "Experience earned: 1 XP";
		begin_input();
		input_step('D', STOCK);
		input_step(gear_to_label(player, obj), ITEM);
		input_step(ARROW_DOWN, STORY);
		input_step(KC_ENTER, STORY);
		input_step(ESCAPE, STOCK);
		use_store(EVENT_USE_STORE, NULL, NULL);
		eq(input_pos, input_count);
		require(views_ok);
		eq(screen_save_depth, 0);
		eq(player->exp, experience + 1);
	}
	ok;
}

const char *suite_name = "sdl3/store-screen";
struct test tests[] = {
	{ "commercial store snapshot", test_commercial_store_snapshot },
	{ "home uses same foundation without prices", test_home_uses_same_foundation_without_prices },
	{ "transaction confirmation", test_transaction_confirmation },
	{ "transaction quantity", test_transaction_quantity },
	{ "store actions", test_store_actions },
	{ "flavoured stock glyphs", test_flavoured_stock_glyphs },
	{ "examine shortcuts and visible rejection", test_examine_shortcuts_and_empty_sale },
	{ "quest briefing requires acknowledgement", test_quest_briefing_requires_acknowledgement },
	{ "donation receipt returns to shop", test_donation_receipt_returns_to_shop },
	{ NULL, NULL }
};
