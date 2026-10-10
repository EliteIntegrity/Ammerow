/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Drive the real Commands controller with one physical key per input event. */
#include "unit-test.h"
#include "test-utils.h"
#include "game-event.h"
#include "game-input.h"
#include "game-world.h"
#include "init.h"
#include "option.h"
#include "obj-gear.h"
#include "object.h"
#include "player.h"
#include "player-birth.h"
#include "sdl3/screen-model.h"
#include "ui-context.h"
#include "ui-game.h"
#include "ui-knowledge.h"
#include "ui-output.h"
#include "ui-prefs.h"
#include "z-quark.h"

static term test_term;
static struct sdl3_screen screen;
static struct input_step {
	const char *title;
	int cursor;
	keycode_t key;
	int pointer_index;
	bool activate;
} steps[256];
static int step_count, step_position, root_cursor;
static int messages, checks, hooks;
static bool allow_command, hook_saw_menu;
static bool layout_ok, saw_scroll;

static void capture(const struct ui_screen *source)
{
	sdl3_screen_capture(&screen, source);
	if (source && source->kind == UI_SCREEN_ACTION_MENU) {
		struct sdl3_screen_layout layout;
		int index;
		if (!sdl3_screen_measure(&screen, test_term.wid, test_term.hgt, &layout) ||
				screen.content_row <= 4 || layout.detail_width < 2 ||
				screen.cursor < layout.first_row ||
				screen.cursor >= layout.first_row + layout.visible_rows ||
				!sdl3_screen_row_at(&screen, test_term.wid, test_term.hgt,
					layout.left, screen.content_row + screen.cursor - layout.first_row,
					&index) || index != screen.cursor) layout_ok = false;
		if (screen.row_offset) saw_scroll = true;
	}
}

static void count_message(game_event_type type, game_event_data *data, void *user)
{
	(void)type;
	(void)data;
	(void)user;
	messages++;
}

static errr input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		struct input_step *step;
		if (step_position == step_count)
			quit("Commands regression exhausted its input (unexpected prompt)");
		step = &steps[step_position++];
		if (step->title && (!screen.active ||
				!streq(screen.title, step->title) || screen.cursor != step->cursor)) {
			printf("Step %d: expected %s [%d], got %s [%d]\n", step_position,
				step->title, step->cursor, screen.title, screen.cursor);
			quit("One key did not produce the expected Commands menu transition");
		}
		if (step->key) {
			Term_keypress(step->key, 0);
		} else {
			keycode_t keys[SDL3_SCREEN_ROW_CAPACITY];
			int count = sdl3_screen_action_menu_keys(&screen, step->pointer_index,
				step->activate, keys, N_ELEMENTS(keys));
			for (int i = 0; i < count; i++) Term_keypress(keys[i], 0);
		}
	}
	return 0;
}

static void key(const char *title, int cursor, keycode_t code)
{
	assert(step_count < (int)N_ELEMENTS(steps));
	steps[step_count++] = (struct input_step){ title, cursor, code };
}

static void move_to(const char *title, int from, int to)
{
	while (from < to) key(title, from++, ARROW_DOWN);
	while (from > to) key(title, from--, ARROW_UP);
}

static void pointer(const char *title, int cursor, int index, bool activate)
{
	key(title, cursor, 0);
	steps[step_count - 1].pointer_index = index;
	steps[step_count - 1].activate = activate;
}

/* Resolve menu paths from the authoritative table, including all debug leaves. */
static void enter_list(int index)
{
	struct command_list *list = &cmds_all[index];
	if (!list->menu_level) {
		move_to("COMMANDS", root_cursor, index);
		key("COMMANDS", index, KC_ENTER);
		root_cursor = index;
		return;
	}
	for (int i = 0; cmds_all[i].name; i++) {
		for (int j = 0; j < (int)cmds_all[i].len; j++) {
			struct cmd_info *cmd = &cmds_all[i].list[j];
			if (cmd->nested_name && streq(cmd->nested_name, list->name)) {
				enter_list(i);
				move_to(cmds_all[i].name, 0, j);
				key(cmds_all[i].name, j, KC_ENTER);
				return;
			}
		}
	}
	quit("Command list is unreachable from the Commands menu");
}

static void begin(void)
{
	Term_flush();
	inkey_next = NULL;
	step_count = step_position = messages = checks = hooks = 0;
	hook_saw_menu = false;
	layout_ok = true;
	saw_scroll = false;
	sdl3_screen_clear(&screen);
}

int setup_tests(void **state)
{
	char test_user[1024];
	*state = NULL;
	set_file_paths();
	path_build(test_user, sizeof(test_user), ANGBAND_DIR_USER, "command-screen-test");
	if (!dir_create(test_user)) return 1;
	string_free(ANGBAND_DIR_USER);
	string_free(ANGBAND_DIR_ARCHIVE);
	string_free(ANGBAND_DIR_SAVE);
	ANGBAND_DIR_USER = string_make(test_user);
	ANGBAND_DIR_ARCHIVE = string_make(test_user);
	ANGBAND_DIR_SAVE = string_make(test_user);
	init_angband();
	textui_prefs_init();
	if (!player_make_simple("Nearlander", "Warrior", "Commands Tester")) return 1;
	cave = t_build_arena(9, 11);
	player->grid = loc(5, 4);
	term_init(&test_term, 100, 30, 128);
	test_term.screen_hook = capture;
	test_term.xtra_hook = input;
	term_screen = angband_term[0] = &test_term;
	Term_activate(&test_term);
	cmd_init();
	textui_input_init();
	event_add_handler(EVENT_MESSAGE, count_message, NULL);
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	event_remove_handler(EVENT_MESSAGE, count_message, NULL);
	term_nuke(&test_term);
	term_screen = angband_term[0] = NULL;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static int test_items_and_information_are_quiet(void *state)
{
	const char *names[] = { "Items", "Information" };
	(void)state;
	for (int n = 0; n < (int)N_ELEMENTS(names); n++) {
		int index = (int)cmd_list_lookup_by_name(names[n]);
		begin();
		enter_list(index);
		key(names[n], 0, ARROW_DOWN);
		key(names[n], 1, ARROW_UP);
		key(names[n], 0, ESCAPE);
		key("COMMANDS", index, ESCAPE);
		require(!textui_action_menu_choose());
		eq(messages, 0);
		eq(step_position, step_count);
		eq(screen_save_depth, 0);
		require(!screen.active);
	}
	ok;
}

static int test_every_leaf_selects_its_authoritative_command(void *state)
{
	uint16_t noscore = player->noscore;
	(void)state;
	for (int mode = 0; mode < 2; mode++) {
		Term_resize(mode ? 140 : 80, mode ? 42 : 24);
		player->opts.opt[OPT_rogue_like_commands] = mode != 0;
		for (int i = 0; cmds_all[i].name; i++) {
			for (int j = 0; j < (int)cmds_all[i].len; j++) {
				struct cmd_info *cmd = &cmds_all[i].list[j];
				if (!cmd->cmd && !cmd->hook) continue;
				begin();
				enter_list(i);
				move_to(cmds_all[i].name, 0, j);
				key(cmds_all[i].name, j, KC_ENTER);
				require(textui_action_menu_choose() == cmd);
				eq(messages, 0);
				eq(step_position, step_count);
				eq(screen_save_depth, 0);
				eq(player->noscore, noscore);
				require(layout_ok);
				require(!screen.active);
			}
		}
	}
	player->opts.opt[OPT_rogue_like_commands] = false;
	Term_resize(100, 30);
	ok;
}

static bool prerequisite(void)
{
	checks++;
	return allow_command;
}

static void command_hook(void)
{
	hooks++;
	hook_saw_menu = screen.active;
}

static int test_dispatch_checks_once_after_menu_closes(void *state)
{
	(void)state;
	/* Probe every dispatch boundary without actually saving, retiring,
	 * spawning monsters, etc. These tests exercise the chooser, not gameplay. */
	for (int i = 0; cmds_all[i].name; i++) {
		for (int j = 0; j < (int)cmds_all[i].len; j++) {
			struct cmd_info *cmd = &cmds_all[i].list[j];
			struct cmd_info saved = *cmd;
			if (!cmd->cmd && !cmd->hook) continue;
			for (int allowed = 0; allowed < 2; allowed++) {
				begin();
				cmd->prereq = prerequisite;
				cmd->hook = command_hook;
				allow_command = allowed != 0;
				key(NULL, 0, KC_ENTER);
				enter_list(i);
				move_to(cmds_all[i].name, 0, j);
				key(cmds_all[i].name, j, KC_ENTER);
				textui_process_command();
				*cmd = saved;
				eq(checks, 1);
				eq(hooks, allowed);
				require(!hook_saw_menu);
				eq(step_position, step_count);
			}
		}
	}
	ok;
}

static int test_escape_unwinds_nested_menus_one_level(void *state)
{
	int hidden = (int)cmd_list_lookup_by_name("Hidden");
	int debug = (int)cmd_list_lookup_by_name("Debug");
	int debug_row = -1;
	(void)state;
	for (int i = 0; i < (int)cmds_all[hidden].len; i++) {
		const char *nested = cmds_all[hidden].list[i].nested_name;
		if (nested && streq(nested, "Debug")) debug_row = i;
	}
	require(debug_row >= 0);
	begin();
	enter_list(debug);
	key("Debug", 0, KC_ENTER);
	key("DbgObj", 0, ESCAPE);
	key("Debug", 0, ESCAPE);
	key("Hidden", debug_row, ESCAPE);
	key("COMMANDS", hidden, ESCAPE);
	require(!textui_action_menu_choose());
	eq(step_position, step_count);
	eq(messages, 0);
	eq(screen_save_depth, 0);
	require(!screen.active);
	ok;
}

static int test_cancel_is_not_an_unknown_command(void *state)
{
	int attr;
	wchar_t ch;
	(void)state;
	begin();
	Term_clear();
	key(NULL, 0, KC_ENTER);
	key("COMMANDS", root_cursor, ESCAPE);
	textui_process_command();
	Term_what(0, 0, &attr, &ch);
	eq(ch, L' ');
	eq(messages, 0);
	eq(step_position, step_count);
	require(!screen.active);
	ok;
}

static int test_real_failed_commands_explain_once(void *state)
{
	const cmd_code failures[] = { CMD_FIRE, CMD_CAST, CMD_STUDY };
	int ammo_tval = player->state.ammo_tval;
	(void)state;
	player->state.ammo_tval = 0;
	for (int n = 0; n < (int)N_ELEMENTS(failures); n++) {
		for (int i = 0; cmds_all[i].name; i++) {
			for (int j = 0; j < (int)cmds_all[i].len; j++) {
				struct cmd_info *cmd = &cmds_all[i].list[j];
				void (*saved_hook)(void);
				if (cmd->cmd != failures[n]) continue;
				begin();
				saved_hook = cmd->hook;
				cmd->hook = command_hook;
				key(NULL, 0, KC_ENTER);
				enter_list(i);
				move_to(cmds_all[i].name, 0, j);
				key(cmds_all[i].name, j, KC_ENTER);
				textui_process_command();
				cmd->hook = saved_hook;
				eq(messages, 1);
				eq(hooks, 0);
				eq(step_position, step_count);
			}
		}
	}
	player->state.ammo_tval = ammo_tval;
	ok;
}

static bool refuse_confirmation(const char *prompt)
{
	(void)prompt;
	checks++;
	hook_saw_menu = screen.active;
	return false;
}

static int test_selected_command_respects_equipment_inscription(void *state)
{
	int index = (int)cmd_list_lookup_by_name("Items");
	struct cmd_info *cmd = &cmds_all[index].list[0];
	struct object *obj = slot_object(player, 0);
	struct cmd_info saved = *cmd;
	bool (*saved_check)(const char *) = get_check_hook;
	quark_t note;
	(void)state;
	notnull(obj);
	note = obj->note;
	begin();
	obj->note = quark_add("^{");
	get_check_hook = refuse_confirmation;
	cmd->hook = command_hook;
	key(NULL, 0, KC_ENTER);
	enter_list(index);
	key("Items", 0, KC_ENTER);
	textui_process_command();
	obj->note = note;
	get_check_hook = saved_check;
	*cmd = saved;
	eq(checks, 1);
	eq(hooks, 0);
	require(!hook_saw_menu);
	eq(step_position, step_count);
	ok;
}

static int test_pointer_hover_and_click_select_same_command(void *state)
{
	int index = (int)cmd_list_lookup_by_name("Items");
	(void)state;
	begin();
	pointer("COMMANDS", root_cursor, index, true);
	root_cursor = index;
	pointer("Items", 0, 4, false);
	pointer("Items", 4, 4, true);
	require(textui_action_menu_choose() == &cmds_all[index].list[4]);
	eq(step_position, step_count);
	begin();
	pointer("COMMANDS", root_cursor, index, true);
	pointer("Items", 0, 9, true);
	require(textui_action_menu_choose() == &cmds_all[index].list[9]);
	eq(step_position, step_count);
	eq(messages, 0);
	ok;
}

static int test_compact_scrolling_and_legacy_fallback(void *state)
{
	int index = (int)cmd_list_lookup_by_name("Information");
	int last = (int)cmds_all[index].len - 1;
	(void)state;
	begin();
	Term_resize(80, 24);
	enter_list(index);
	move_to("Information", 0, last);
	key("Information", last, '8');
	key("Information", last - 1, '2');
	key("Information", last, ARROW_LEFT);
	key("COMMANDS", index, ESCAPE);
	require(!textui_action_menu_choose());
	require(saw_scroll);
	require(layout_ok);
	eq(step_position, step_count);
	/* The same controller must still work without SDL's semantic hook. */
	begin();
	test_term.screen_hook = NULL;
	key(NULL, 0, KC_ENTER);
	key(NULL, 0, ARROW_DOWN);
	key(NULL, 0, KC_ENTER);
	require(textui_action_menu_choose() == &cmds_all[index].list[1]);
	test_term.screen_hook = capture;
	Term_resize(100, 30);
	eq(step_position, step_count);
	eq(messages, 0);
	eq(screen_save_depth, 0);
	ok;
}

static int test_capture_previews_are_read_only(void *state)
{
	uint16_t noscore = player->noscore;
	(void)state;
	begin();
	Term_resize(80, 24);
	require(textui_action_menu_present(NULL, 0));
	require(streq(screen.title, "COMMANDS"));
	require(textui_action_menu_present("Information", 100));
	eq(screen.cursor, screen.row_count - 1);
	require(saw_scroll);
	require(textui_action_menu_present("DbgObj", 0));
	require(streq(screen.title, "DbgObj"));
	require(!textui_action_menu_present("not a command family", 0));
	eq(messages, 0);
	eq(player->noscore, noscore);
	eq(screen_save_depth, 0);
	require(layout_ok);
	Term_resize(100, 30);
	ok;
}

static int test_legacy_centering_is_unadvertised_but_compatible(void *state)
{
	bool saved_center = OPT(player, center_player);
	bool saved_keyset = OPT(player, rogue_like_commands);
	(void)state;
	for (int i = 0; cmds_all[i].name; i++) {
		for (size_t j = 0; j < cmds_all[i].len; j++) {
			require(cmds_all[i].list[j].hook != do_cmd_center_map);
		}
	}
	for (int page = 0; page < OPT_PAGE_MAX; page++) {
		for (int i = 0; option_page[page][i] != OPT_none; i++) {
			require(option_page[page][i] != OPT_center_player);
		}
	}
	/* Existing preferences still round-trip under the same type and name. */
	OPT(player, center_player) = true;
	require(options_save_custom(&player->opts, OP_INTERFACE));
	OPT(player, center_player) = false;
	require(options_restore_custom(&player->opts, OP_INTERFACE));
	require(OPT(player, center_player));
	OPT(player, center_player) = saved_center;
	require(options_save_custom(&player->opts, OP_INTERFACE));
	for (int mode = 0; mode < 2; mode++) {
		OPT(player, rogue_like_commands) = mode != 0;
		begin();
		test_term.offset_x = test_term.offset_y = 10;
		key(NULL, 0, mode ? '@' : KTRL('L'));
		textui_process_command();
		eq(test_term.offset_x, 0);
		eq(test_term.offset_y, 0);
		eq(step_position, step_count);
		eq(messages, 0);
		require(!screen.active);
	}
	OPT(player, rogue_like_commands) = saved_keyset;
	ok;
}

const char *suite_name = "sdl3/command-screen";
struct test tests[] = {
	{ "legacy centring is hidden but compatible", test_legacy_centering_is_unadvertised_but_compatible },
	{ "Items and Information browse without messages", test_items_and_information_are_quiet },
	{ "every command and keyset selects exactly once", test_every_leaf_selects_its_authoritative_command },
	{ "dispatch checks prerequisites after closing", test_dispatch_checks_once_after_menu_closes },
	{ "nested Escape unwinds one level", test_escape_unwinds_nested_menus_one_level },
	{ "cancel is not an unknown command", test_cancel_is_not_an_unknown_command },
	{ "failed real commands explain once", test_real_failed_commands_explain_once },
	{ "menu selection respects inscriptions", test_selected_command_respects_equipment_inscription },
	{ "pointer selection uses the real controller", test_pointer_hover_and_click_select_same_command },
	{ "compact scrolling and legacy fallback", test_compact_scrolling_and_legacy_fallback },
	{ "offscreen previews are read-only", test_capture_previews_are_read_only },
	{ NULL, NULL }
};
