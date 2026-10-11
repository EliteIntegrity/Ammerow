/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */

/* Drive the production inventory/action/details controllers without a window. */
#include "unit-test.h"
#include "test-utils.h"
#include "cmd-core.h"
#include "game-event.h"
#include "game-input.h"
#include "game-world.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-tval.h"
#include "object.h"
#include "option.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "sdl3/screen-model.h"
#include "ui-game.h"
#include "ui-command.h"
#include "ui-input.h"
#include "ui-knowledge.h"
#include "ui-object.h"
#include "ui-object-screen.h"
#include "ui-output.h"
#include "ui-prefs.h"
#include "z-textblock.h"

static term terminal;
static struct sdl3_screen current, parent;
static struct step {
	enum ui_screen_kind kind;
	int cursor;
	keycode_t key;
	bool pointer;
	int offset;
} steps[128];
static int position, count, enters, leaves, detail_frames;
static bool parent_ok;

static void capture(const struct ui_screen *screen)
{
	sdl3_screen_present(&current, &parent, screen);
	if (screen && screen->kind == UI_SCREEN_ITEM_DETAILS) detail_frames++;
}

static void selector_event(game_event_type type, game_event_data *data, void *user)
{
	(void)data;
	(void)user;
	if (type == EVENT_ENTER_ITEM_SELECTOR) enters++;
	else {
		leaves++;
		sdl3_screen_clear(&current);
		sdl3_screen_clear(&parent);
	}
}

static errr input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		struct step *step;
		if (position >= count) quit("Unexpected item-details input request");
		step = &steps[position++];
		if (current.kind != step->kind ||
				(step->cursor >= 0 && current.cursor != step->cursor) ||
				(step->offset >= 0 && current.document_line_offset != step->offset)) {
			printf("Step %d expected kind %d cursor %d, got %d cursor %d (%s)\n",
				position, step->kind, step->cursor, current.kind, current.cursor,
				current.title);
			quit("Wrong item inspection transition");
		}
		if (current.modal && (!parent.active ||
				parent.kind != UI_SCREEN_ITEM_SELECTOR || leaves)) parent_ok = false;
		if (step->pointer) {
			if (current.kind == UI_SCREEN_ITEM_DETAILS) Term_mousepress(10, 10, 1);
			else {
				keycode_t keys[64];
				int n = sdl3_screen_action_menu_keys(&current, 0, true, keys, 64);
				for (int i = 0; i < n; i++) Term_keypress(keys[i], 0);
			}
		} else Term_keypress(step->key, 0);
	}
	return 0;
}

static void key(enum ui_screen_kind kind, int cursor, keycode_t code)
{
	assert(count < (int)N_ELEMENTS(steps));
	steps[count++] = (struct step){kind, cursor, code, false, -1};
}

static void begin(int cols, int rows)
{
	Term_flush();
	Term_resize(cols, rows);
	inkey_next = NULL;
	position = count = enters = leaves = detail_frames = 0;
	parent_ok = true;
	sdl3_screen_clear(&current);
	sdl3_screen_clear(&parent);
}

int setup_tests(void **state)
{
	char path[1024];
	*state = NULL;
	set_file_paths();
	path_build(path, sizeof(path), ANGBAND_DIR_USER, "item-details-test");
	if (!dir_create(path)) return 1;
	string_free(ANGBAND_DIR_USER);
	ANGBAND_DIR_USER = string_make(path);
	init_angband();
	textui_prefs_init();
	if (!player_make_simple("Nearlander", "Warrior", "Item Tester")) return 1;
	cave = t_build_arena(9, 11);
	player->grid = loc(5, 4);
	term_init(&terminal, 100, 30, 128);
	terminal.screen_hook = capture;
	terminal.xtra_hook = input;
	term_screen = angband_term[0] = &terminal;
	Term_activate(&terminal);
	cmd_init();
	textui_input_init();
	event_add_handler(EVENT_ENTER_ITEM_SELECTOR, selector_event, NULL);
	event_add_handler(EVENT_LEAVE_ITEM_SELECTOR, selector_event, NULL);
	calc_inventory(player);
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	event_remove_handler(EVENT_ENTER_ITEM_SELECTOR, selector_event, NULL);
	event_remove_handler(EVENT_LEAVE_ITEM_SELECTOR, selector_event, NULL);
	term_nuke(&terminal);
	term_screen = angband_term[0] = NULL;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static int test_inventory_return_and_pointer(void *state)
{
	int32_t before = turn;
	(void)state;
	notnull(player->upkeep->inven[1]);
	for (int size = 0; size < 2; size++) {
		begin(size ? 140 : 80, size ? 42 : 24);
		player->opts.opt[OPT_rogue_like_commands] = size != 0;
		key(UI_SCREEN_ITEM_SELECTOR, 0, ARROW_DOWN);
		key(UI_SCREEN_ITEM_SELECTOR, 1, KC_ENTER);
		key(UI_SCREEN_ITEM_ACTIONS, 0, 'I');
		steps[count - 1].pointer = size != 0;
		key(UI_SCREEN_ITEM_DETAILS, -1, ESCAPE);
		steps[count - 1].pointer = size != 0;
		key(UI_SCREEN_ITEM_ACTIONS, 0, ESCAPE);
		key(UI_SCREEN_ITEM_SELECTOR, 1, ESCAPE);
		do_cmd_inven();
		eq(position, count);
		require(parent_ok);
		eq(enters, 1);
		eq(leaves, 1);
		eq(detail_frames, 1);
		eq(screen_save_depth, 0);
		require(!current.active && !parent.active);
		eq(turn, before);
	}
	player->opts.opt[OPT_rogue_like_commands] = false;
	ok;
}

static int test_equipment_and_tab_return(void *state)
{
	(void)state;
	begin(100, 30);
	key(UI_SCREEN_ITEM_SELECTOR, 0, ARROW_RIGHT);
	key(UI_SCREEN_ITEM_SELECTOR, 0, KC_ENTER);
	key(UI_SCREEN_ITEM_ACTIONS, 0, 'I');
	key(UI_SCREEN_ITEM_DETAILS, -1, KC_ENTER);
	key(UI_SCREEN_ITEM_ACTIONS, 0, ESCAPE);
	key(UI_SCREEN_ITEM_SELECTOR, 0, ARROW_LEFT);
	key(UI_SCREEN_ITEM_SELECTOR, 0, ESCAPE);
	do_cmd_inven();
	eq(position, count);
	require(parent_ok);
	eq(leaves, 1);
	eq(screen_save_depth, 0);
	begin(80, 24);
	key(UI_SCREEN_ITEM_SELECTOR, 0, KC_ENTER);
	key(UI_SCREEN_ITEM_ACTIONS, 0, KC_ENTER);
	key(UI_SCREEN_ITEM_DETAILS, -1, ESCAPE);
	key(UI_SCREEN_ITEM_ACTIONS, 0, ESCAPE);
	key(UI_SCREEN_ITEM_SELECTOR, 0, ESCAPE);
	do_cmd_equip();
	require(parent_ok);
	eq(position, count);
	eq(screen_save_depth, 0);
	ok;
}

static int test_knowledge_safe_full_document(void *state)
{
	struct object *obj;
	textblock *tb;
	bool aware;
	(void)state;
	for (obj = player->gear; obj && !obj->kind->flavor; obj = obj->next) {}
	notnull(obj);
	aware = obj->kind->aware;
	begin(80, 24);
	obj->kind->aware = false;
	ui_item_details_present(obj, 0);
	require(current.subject_unidentified);
	require(streq(current.subject_art_id, obj->kind->base->unaware_art_id ?
		obj->kind->base->unaware_art_id : ""));
	tb = object_info(obj, OINFO_NONE);
	eq(current.document_length, wcslen(textblock_text(tb)));
	require(wcscmp(current.document_text, textblock_text(tb)) == 0);
	textblock_free(tb);
	obj->kind->aware = true;
	ui_item_details_present(obj, 0);
	require(!current.subject_unidentified);
	require(streq(current.subject_art_id, obj->kind->art_id ? obj->kind->art_id : ""));
	obj->kind->aware = aware;
	sdl3_screen_clear(&current);
	sdl3_screen_clear(&parent);
	ok;
}

static int test_layout_bounds(void *state)
{
	(void)state;
	for (int cols = 60; cols <= 160; cols += 10) {
		for (int rows = 20; rows <= 50; rows += 5) {
			struct ui_item_details_layout l;
			ui_item_details_measure(cols, rows, &l);
			require(l.text_cols > 0 && l.text_rows > 0);
			require(l.text_col + l.text_cols <= l.panel_col + l.panel_cols);
			require(l.text_row + l.text_rows < rows - 4);
			require(l.art_col + l.art_cols <= l.panel_col + l.panel_cols);
			require(cols >= 100 ? l.text_col + l.text_cols < l.art_col :
				l.art_row + l.art_rows <= l.text_row);
		}
	}
	ok;
}

static int test_scrolling_and_glyph_fallback(void *state)
{
	struct object *obj = player->upkeep->inven[0];
	char *original_text = obj->kind->text;
	char *original_art = obj->kind->art_id;
	char description[5000] = "";
	struct ui_item_details_layout layout;
	int maximum;
	(void)state;
	for (int i = 0; i < 50; i++) {
		my_strcat(description, "A lengthy item description still needs every line.\n",
			sizeof(description));
	}
	obj->kind->text = string_make(description);
	obj->kind->art_id = NULL;
	begin(80, 24);
	maximum = ui_item_details_present(obj, 0);
	require(maximum > 16);
	eq(current.subject_art_id[0], '\0');
	eq(current.subject_glyph, object_char(obj));
	eq(current.subject_attr, object_attr(obj));
	{
		textblock *expected = object_info(obj, OINFO_NONE);
		bool matches = wcscmp(current.document_text, textblock_text(expected)) == 0;
		textblock_free(expected);
		require(matches);
	}
	ui_item_details_measure(80, 24, &layout);
	key(UI_SCREEN_ITEM_DETAILS, -1, KC_PGDOWN);
	steps[count - 1].offset = 0;
	key(UI_SCREEN_ITEM_DETAILS, -1, ARROW_DOWN);
	steps[count - 1].offset = layout.text_rows;
	key(UI_SCREEN_ITEM_DETAILS, -1, KC_END);
	steps[count - 1].offset = layout.text_rows + 1;
	key(UI_SCREEN_ITEM_DETAILS, -1, KC_PGDOWN);
	steps[count - 1].offset = maximum;
	key(UI_SCREEN_ITEM_DETAILS, -1, KC_HOME);
	steps[count - 1].offset = maximum;
	key(UI_SCREEN_ITEM_DETAILS, -1, ARROW_UP);
	steps[count - 1].offset = 0;
	key(UI_SCREEN_ITEM_DETAILS, -1, ESCAPE);
	steps[count - 1].offset = 0;
	ui_item_details_show(obj);
	string_free(obj->kind->text);
	obj->kind->text = original_text;
	obj->kind->art_id = original_art;
	eq(position, count);
	require(!current.active && !parent.active);
	eq(screen_save_depth, 0);
	ok;
}

static int test_command_dispatch_after_inspection(void *state)
{
	struct object *chosen = NULL;
	struct command *command;
	(void)state;
	begin(100, 30);
	cmdq_flush();
	key(UI_SCREEN_ITEM_SELECTOR, 0, KC_ENTER);
	key(UI_SCREEN_ITEM_ACTIONS, 0, 'I');
	key(UI_SCREEN_ITEM_DETAILS, -1, ESCAPE);
	key(UI_SCREEN_ITEM_ACTIONS, 0, cmd_lookup_key(CMD_DROP, KEYMAP_MODE_ORIG));
	do_cmd_inven();
	eq(position, count);
	command = cmdq_peek();
	notnull(command);
	eq(command->code, CMD_DROP);
	eq(cmd_get_arg_item(command, "item", &chosen), CMD_OK);
	require(chosen == player->upkeep->inven[0]);
	require(parent_ok);
	require(!current.active && !parent.active);
	eq(screen_save_depth, 0);
	cmdq_flush();
	ok;
}

const char *suite_name = "sdl3/item-details";
struct test tests[] = {
	{ "inventory preserves parent, selection and pointer return", test_inventory_return_and_pointer },
	{ "equipment and tab return", test_equipment_and_tab_return },
	{ "knowledge-safe full document", test_knowledge_safe_full_document },
	{ "wide and compact layout bounds", test_layout_bounds },
	{ "long descriptions scroll; missing art retains glyph", test_scrolling_and_glyph_fallback },
	{ "commands still dispatch after inspection", test_command_dispatch_after_inspection },
	{ NULL, NULL }
};
