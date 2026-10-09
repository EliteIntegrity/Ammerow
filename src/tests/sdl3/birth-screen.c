/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/birth-screen.c */
/* Exercise the semantic character-creation choice adapter. */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include "init.h"
#include "game-event.h"
#include "game-input.h"
#include "game-world.h"
#include "option.h"
#include "player.h"
#include "player-birth.h"
#include "sdl3/screen-model.h"
#include "ui-birth.h"
#include "ui-birth-screen.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-prefs.h"
#include "ui-term.h"

static struct sdl3_screen captured_screen;
static bool saw_options, saw_option_change, saw_points, saw_scrolled_option;
static bool original_option;
static int script_position;
static const uint32_t birth_keys[] = {
	'=', ARROW_UP, ARROW_DOWN, 't', 't', ESCAPE,
	KC_ENTER, KC_ENTER, KC_ENTER,
	ESCAPE, KC_ENTER, KC_ENTER,
	'*', KC_ENTER, KC_ENTER, KC_ENTER
};
static const uint32_t *input_keys = birth_keys;
static int input_count = N_ELEMENTS(birth_keys);

static void print_log(const char *message)
{
	printf("%s\n", message);
}

static void capture_screen(const struct ui_screen *screen)
{
	sdl3_screen_capture(&captured_screen, screen);
	if (screen && screen->kind == UI_SCREEN_OPTIONS) {
		saw_options = true;
		if (screen->row_offset > 0 && screen->cursor == screen->row_count - 1)
			saw_scrolled_option = true;
		if (player->opts.opt[option_page[OPT_PAGE_BIRTH][0]] != original_option)
			saw_option_change = true;
	}
	if (screen && screen->kind == UI_SCREEN_BIRTH_STATS) saw_points = true;
}

static errr scripted_input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		if (script_position >= input_count)
			quit("Birth regression exhausted its scripted input");
		Term_keypress(input_keys[script_position++], 0);
	}
	return 0;
}

int setup_tests(void **state)
{
	char test_user[1024];
	*state = NULL;
	plog_aux = print_log;
	set_file_paths();
	/* A real birth writes generated-artifact evidence. Keep the fixture out
	 * of the ordinary player directories, including on interrupted tests. */
	path_build(test_user, sizeof(test_user), ANGBAND_DIR_USER, "birth-screen-test");
	if (!dir_create(test_user)) return 1;
	string_free(ANGBAND_DIR_USER);
	string_free(ANGBAND_DIR_ARCHIVE);
	string_free(ANGBAND_DIR_SAVE);
	ANGBAND_DIR_USER = string_make(test_user);
	ANGBAND_DIR_ARCHIVE = string_make(test_user);
	ANGBAND_DIR_SAVE = string_make(test_user);
	init_angband();
	textui_prefs_init();
	return player_make_simple(NULL, NULL, "Birth Screen Tester") ? 0 : 1;
}

int teardown_tests(void *state)
{
	(void)state;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static void present(struct menu *menu, enum ui_birth_screen_stage stage,
		const char *const *items, int count, const char *hint)
{
	term test_term = { 0 };
	term *previous_term = Term;

	test_term.wid = 120;
	test_term.hgt = 36;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	sdl3_screen_clear(&captured_screen);
	ui_birth_screen_present(menu, stage, items, count, hint);
	Term = previous_term;
}

static int test_race_choice_has_owned_context(void *state)
{
	const char *items[64] = { 0 };
	struct player_race *race;
	struct menu menu = { 0 };
	int count = 0;
	(void)state;

	for (race = races; race && race->ridx < (int)N_ELEMENTS(items);
			race = race->next) {
		items[race->ridx] = race->name;
		count++;
	}
	menu.count = count;
	menu.cursor = 0;
	menu.selections = all_letters_nohjkl;
	present(&menu, UI_BIRTH_SCREEN_RACE, items, count,
		"Origin affects stats and skills.");

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_CHARACTER_CREATION);
	require(streq(captured_screen.title, "Choose your origin"));
	require(streq(captured_screen.context_title, items[0]));
	require(strstr(captured_screen.context, "Stat modifiers") != NULL);
	require(strstr(captured_screen.context, "Combat") != NULL);
	eq(captured_screen.row_count, count);
	eq(captured_screen.rows[0].tag, all_letters_nohjkl[0]);
	ok;
}

static int test_ammerow_origin_lore(void *state)
{
	const char *items[64] = { 0 };
	struct player_race *race;
	struct menu menu = { 0 };
	int count = 0;
	(void)state;

	for (race = races; race && race->ridx < (int)N_ELEMENTS(items);
			race = race->next) {
		items[race->ridx] = race->name;
		count++;
	}
	race = player_name2race("Burrowhand");
	notnull(race);
	menu.count = count;
	menu.cursor = race->ridx;
	menu.selections = all_letters_nohjkl;
	present(&menu, UI_BIRTH_SCREEN_RACE, items, count,
		"Origin affects stats and skills.");
	require(strstr(captured_screen.context, "drainage galleries") != NULL);
	ok;
}

static int test_ammerow_random_names_are_viable(void *state)
{
	char name[32];
	int i;
	(void)state;

	for (i = 0; i < 100; i++) {
		size_t length = player_random_name(name, sizeof(name));

		require(length >= 4 && length <= 8);
		eq(strlen(name), length);
	}
	ok;
}

static int test_name_acceptance_defaults_only_blank_names(void *state)
{
	static const struct {
		const char *initial;
		uint32_t keys[8];
		int count;
		bool accepted, random;
		const char *expected;
	} cases[] = {
		{ "", { KC_ENTER }, 1, true, true, NULL },
		{ "Mara", { KC_BACKSPACE, KC_ENTER }, 2, true, true, NULL },
		{ "Mara", { ' ', ' ', KC_ENTER }, 3, true, true, NULL },
		{ "Mara", { KC_ENTER }, 1, true, false, "Mara" },
		{ "Mara", { 'E', 'l', 'i', KC_ENTER }, 4, true, false, "Eli" },
		{ "Mara", { KC_BACKSPACE, ESCAPE }, 2, false, false, "Mara" },
		{ "", { ESCAPE }, 1, false, false, "" },
		{ "Mara", { '*', KC_ENTER }, 2, true, true, NULL }
	};
	term test_term;
	term *previous = Term, *previous_screen = term_screen;
	term *previous_main = angband_term[0];
	char saved_name[PLAYER_NAME_LEN];
	bool correct = true;
	(void)state;

	my_strcpy(saved_name, player->full_name, sizeof(saved_name));
	term_init(&test_term, 120, 24, 128);
	test_term.xtra_hook = scripted_input;
	term_screen = angband_term[0] = &test_term;
	Term_activate(&test_term);
	for (size_t i = 0; i < N_ELEMENTS(cases); i++) {
		char name[PLAYER_NAME_LEN], expected[PLAYER_NAME_LEN];
		struct randomizer_state before, after, expected_state;
		bool accepted;

		my_strcpy(player->full_name, cases[i].initial, sizeof(player->full_name));
		Rand_state_export(&before);
		if (cases[i].random) {
			player_random_name(expected, sizeof(expected));
		} else {
			my_strcpy(expected, cases[i].expected, sizeof(expected));
		}
		Rand_state_export(&expected_state);
		Rand_state_import(&before);
		input_keys = cases[i].keys;
		input_count = cases[i].count;
		script_position = 0;
		accepted = get_character_name(name, sizeof(name));
		Rand_state_export(&after);
		/* Cancellation and an explicit name must not consume a random name. */
		if (accepted != cases[i].accepted || !streq(name, expected) ||
				script_position != input_count ||
				after.algorithm != expected_state.algorithm ||
				memcmp(after.words, expected_state.words, sizeof(after.words))) {
			printf("Name case %zu: expected '%s', got '%s'\n", i, expected, name);
			correct = false;
		}
	}
	input_keys = birth_keys;
	input_count = N_ELEMENTS(birth_keys);
	my_strcpy(player->full_name, saved_name, sizeof(player->full_name));
	Term_activate(previous);
	term_screen = previous_screen;
	angband_term[0] = previous_main;
	term_nuke(&test_term);
	require(correct);
	ok;
}

static int test_class_choice_combines_selected_race(void *state)
{
	const char *items[64] = { 0 };
	struct player_class *class;
	struct menu menu = { 0 };
	int count = 0;
	(void)state;

	for (class = classes; class && class->cidx < (int)N_ELEMENTS(items);
			class = class->next) {
		items[class->cidx] = class->name;
		count++;
	}
	menu.count = count;
	menu.cursor = MIN(1, count - 1);
	menu.selections = all_letters_nohjkl;
	present(&menu, UI_BIRTH_SCREEN_CLASS, items, count,
		"Class affects stats and skills.");

	require(streq(captured_screen.title, "Choose your class"));
	require(streq(captured_screen.context_title, items[menu.cursor]));
	require(strstr(captured_screen.context, "Stat modifiers") != NULL);
	require(strstr(captured_screen.context, "Hit die") != NULL);
	ok;
}

static int test_roller_choice_changes_explanation(void *state)
{
	const char *items[] = { "Point-based", "Standard roller" };
	struct menu menu = { 0 };
	(void)state;

	menu.count = N_ELEMENTS(items);
	menu.selections = all_letters_nohjkl;
	present(&menu, UI_BIRTH_SCREEN_ROLLER, items, N_ELEMENTS(items),
		"Choose how to generate attributes.");
	require(strstr(captured_screen.context, "Shape your attributes") != NULL);
	menu.cursor = 1;
	present(&menu, UI_BIRTH_SCREEN_ROLLER, items, N_ELEMENTS(items),
		"Choose how to generate attributes.");
	require(strstr(captured_screen.context, "traditional random") != NULL);
	ok;
}

static void begin_direct_capture(term *test_term, term **previous_term)
{
	memset(test_term, 0, sizeof(*test_term));
	test_term->wid = 120;
	test_term->hgt = 36;
	test_term->screen_hook = capture_screen;
	*previous_term = Term;
	Term = test_term;
	sdl3_screen_clear(&captured_screen);
}

static int test_point_allocation_exposes_costs_and_selection(void *state)
{
	int spent[STAT_MAX] = { 1, 2, 3, 4, 5 };
	int increase[STAT_MAX] = { 1, 1, 2, 2, 3 };
	int buysell[STAT_MAX] = { 3, 3, 3, 1, 0 };
	term test_term;
	term *previous_term;
	(void)state;

	begin_direct_capture(&test_term, &previous_term);
	ui_birth_screen_present_points(2, spent, increase, 7, buysell);
	Term = previous_term;
	require(captured_screen.active);
	require(streq(captured_screen.title, "Allocate attributes"));
	require(prefix(captured_screen.subtitle, "Points left: 7 / 22"));
	eq(captured_screen.cursor, 2);
	require(strstr(captured_screen.rows[2].label, "cost 3") != NULL);
	require(strstr(captured_screen.context, "Points remaining 7") != NULL);
	eq(captured_screen.kind, UI_SCREEN_BIRTH_STATS);
	require(strstr(captured_screen.context, "Base range 10-18") != NULL);
	require(strstr(captured_screen.context, "Stamina") != NULL);
	require(strstr(captured_screen.context, "Focus") != NULL);
	for (int width = 80; width <= 160; width += 40) {
		struct sdl3_screen_layout layout;
		require(sdl3_screen_measure(&captured_screen, width, 36, &layout));
		for (int i = 0; i < STAT_MAX; i++) {
			require((int)strlen(captured_screen.rows[i].label) <= layout.list_width - 2);
		}
	}
	begin_direct_capture(&test_term, &previous_term);
	test_term.wid = 80;
	test_term.hgt = 24;
	ui_birth_screen_present_points(2, spent, increase, 7, buysell);
	Term = previous_term;
	eq(captured_screen.content_row, 6);
	require(prefix(captured_screen.subtitle, "Points left: 7 / 22"));
	require(strlen(captured_screen.subtitle) <= 72);
	require(strstr(captured_screen.context, "Points remaining 7/") != NULL);
	require(strstr(captured_screen.context, "Next costs 2") != NULL);
	require(strlen(captured_screen.help) <= 72);
	ok;
}

static int test_point_buy_stops_at_engine_cap(void *state)
{
	(void)state;
	cmdq_push(CMD_RESET_STATS);
	cmd_set_arg_choice(cmdq_peek(), "choice", false);
	cmdq_execute(CTX_BIRTH);
	for (int i = 0; i < 20; i++) {
		cmdq_push(CMD_BUY_STAT);
		cmd_set_arg_choice(cmdq_peek(), "choice", STAT_STR);
		cmdq_execute(CTX_BIRTH);
	}
	eq(player->stat_birth[STAT_STR], 18);
	cmdq_push(CMD_SELL_STAT);
	cmd_set_arg_choice(cmdq_peek(), "choice", STAT_STR);
	cmdq_execute(CTX_BIRTH);
	eq(player->stat_birth[STAT_STR], 17);
	cmdq_push(CMD_BUY_STAT);
	cmd_set_arg_choice(cmdq_peek(), "choice", STAT_STR);
	cmdq_execute(CTX_BIRTH);
	eq(player->stat_birth[STAT_STR], 18);
	ok;
}

static int test_roll_and_name_are_semantic(void *state)
{
	term test_term;
	term *previous_term;
	(void)state;

	begin_direct_capture(&test_term, &previous_term);
	do_cmd_roll_stats(NULL);
	do_cmd_roll_stats(NULL);
	ui_birth_screen_present_roll(true);
	require(streq(captured_screen.title, "Review rolled attributes"));
	eq(captured_screen.row_count, STAT_MAX);
	require(strstr(captured_screen.help, "P restores previous") != NULL);
	require(strstr(captured_screen.rows[0].label, "prev") != NULL);
	require(strstr(captured_screen.context, "Base roll range 8-17") != NULL);
	{
		int previous[STAT_MAX];
		require(player_birth_previous_stats(previous));
		do_cmd_prev_stats(NULL);
		for (int i = 0; i < STAT_MAX; i++) eq(player->state.stat_use[i], previous[i]);
	}
	ui_birth_screen_present_name("Mara", 2, false);
	Term = previous_term;
	require(streq(captured_screen.title, "Name your character"));
	require(strstr(captured_screen.rows[0].label, "Ma|ra") != NULL);
	require(strstr(captured_screen.context, "Leave it blank") != NULL);
	ok;
}

static int test_birth_options_and_handler_lifetime(void *state)
{
	term test_term;
	term *previous = Term, *previous_screen = term_screen;
	term *previous_main = angband_term[0];
	bool untouched = true;
	int result;
	(void)state;

	term_init(&test_term, 120, 24, 128);
	test_term.screen_hook = capture_screen;
	test_term.xtra_hook = scripted_input;
	term_screen = angband_term[0] = &test_term;
	Term_activate(&test_term);
	ui_init_birthstate_handlers();
	character_generated = false;
	player->upkeep->playing = false;
	player->ht_birth = 0; /* A genuinely fresh birth, not quickstart. */
	original_option = player->opts.opt[option_page[OPT_PAGE_BIRTH][0]];
	script_position = 0;
	saw_options = saw_option_change = saw_points = saw_scrolled_option = false;
	result = textui_do_birth();
	/* Nothing from the covered legacy character sheet may flash between
	 * accepting this character and the opening story/map. */
	for (int y = 0; y < test_term.hgt; y++) {
		for (int x = 0; x < test_term.wid; x++) {
			int attr;
			wchar_t ch;
			Term_what(x, y, &attr, &ch);
			if (ch != L' ') untouched = false;
		}
	}

	/* Completed birth must remove every point-buy callback, including after
	 * backing out and returning. Use the terminal renderer here so a leaked
	 * legacy callback cannot be concealed by the semantic screen hook. */
	test_term.screen_hook = NULL;
	Term_clear();
	event_signal(EVENT_GOLD);
	event_signal(EVENT_STATS);
	for (int y = 0; y < test_term.hgt; y++) {
		for (int x = 0; x < test_term.wid; x++) {
			int attr;
			wchar_t ch;
			Term_what(x, y, &attr, &ch);
			if (ch != L' ') untouched = false;
		}
	}
	Term_activate(previous);
	term_screen = previous_screen;
	angband_term[0] = previous_main;
	term_nuke(&test_term);
	eq(result, 0);
	eq(script_position, (int)N_ELEMENTS(birth_keys));
	require(saw_options);
	require(saw_option_change);
	require(saw_scrolled_option);
	require(saw_points);
	eq(player->opts.opt[option_page[OPT_PAGE_BIRTH][0]], original_option);
	require(untouched);
	ok;
}

static int test_history_and_confirmation_are_semantic(void *state)
{
	term test_term;
	term *previous_term;
	(void)state;

	begin_direct_capture(&test_term, &previous_term);
	ui_birth_screen_present_history(
		"Raised beside the eastern lake and drawn to the buried worlds.",
		12, true);
	require(streq(captured_screen.title, "Edit your history"));
	require(captured_screen.row_count > 0);
	require(captured_screen.cursor >= 0);
	ui_birth_screen_present_confirm();
	Term = previous_term;
	require(captured_screen.active);
	require(strstr(captured_screen.subtitle, "final review") != NULL);
	require(strstr(captured_screen.context_title, "Ammerow") != NULL);
	eq(captured_screen.cursor, -1);
	ok;
}

const char *suite_name = "sdl3/birth-screen";
struct test tests[] = {
	{ "race choice has owned context", test_race_choice_has_owned_context },
	{ "Ammerow origin lore", test_ammerow_origin_lore },
	{ "Ammerow random names are viable", test_ammerow_random_names_are_viable },
	{ "name acceptance defaults only blank names", test_name_acceptance_defaults_only_blank_names },
	{ "class choice combines selected race", test_class_choice_combines_selected_race },
	{ "roller choice changes explanation", test_roller_choice_changes_explanation },
	{ "point allocation exposes costs and selection", test_point_allocation_exposes_costs_and_selection },
	{ "roll and name are semantic", test_roll_and_name_are_semantic },
	{ "point buy stops at engine cap", test_point_buy_stops_at_engine_cap },
	{ "history and confirmation are semantic", test_history_and_confirmation_are_semantic },
	{ "birth options and handler lifetime", test_birth_options_and_handler_lifetime },
	{ NULL, NULL }
};
