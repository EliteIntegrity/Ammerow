/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Exercise the actual book menus and the mechanics their hints describe. */
#include "unit-test.h"
#include "test-utils.h"
#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "game-event.h"
#include "game-input.h"
#include "init.h"
#include "object.h"
#include "option.h"
#include "player-birth.h"
#include "player-spell.h"
#include "player-timed.h"
#include "ui-game.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-prefs.h"
#include "ui-spell.h"

static term test_term;
static char frames[3][8192];
static keycode_t keys[3];
static int step, steps;
static bool learned_reminder;

static errr input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		size_t offset = 0;
		if (step >= steps) quit("Unexpected input in spell-menu regression");
		for (int y = 0; y < Term->hgt; y++) {
			for (int x = 0; x < Term->wid; x++) {
				int attr;
				wchar_t ch;
				Term_what(x, y, &attr, &ch);
				if (offset + 2 < sizeof(frames[step]))
					frames[step][offset++] = ch < 128 ? (char)ch : '?';
			}
			if (offset + 2 < sizeof(frames[step])) frames[step][offset++] = '\n';
		}
		frames[step][offset] = '\0';
		Term_keypress(keys[step++], 0);
	}
	return 0;
}

static void message(game_event_type type, game_event_data *data, void *user)
{
	(void)type;
	(void)user;
	if (strstr(data->message.msg, "Learning does not activate it"))
		learned_reminder = true;
}

static void begin(bool toggle)
{
	Term_flush();
	Term_clear();
	inkey_next = NULL;
	memset(frames, 0, sizeof(frames));
	step = 0;
	steps = toggle ? 3 : 1;
	keys[0] = toggle ? '?' : ESCAPE;
	keys[1] = '?';
	keys[2] = ESCAPE;
}

static bool choose_class(const char *name)
{
	struct player_class *c;
	for (c = classes; c && !streq(c->name, name); c = c->next) {}
	if (!c) return false;
	player_spells_free(player);
	player->class = c;
	player_spells_init(player);
	player->lev = 50;
	player->msp = player->csp = 100;
	player->upkeep->update = player->upkeep->redraw = 0;
	return true;
}

int setup_tests(void **state)
{
	char path[1024];
	*state = NULL;
	set_file_paths();
	path_build(path, sizeof(path), ANGBAND_DIR_USER, "spell-menu-test");
	if (!dir_create(path)) return 1;
	string_free(ANGBAND_DIR_USER);
	string_free(ANGBAND_DIR_ARCHIVE);
	string_free(ANGBAND_DIR_SAVE);
	ANGBAND_DIR_USER = string_make(path);
	ANGBAND_DIR_ARCHIVE = string_make(path);
	ANGBAND_DIR_SAVE = string_make(path);
	init_angband();
	textui_prefs_init();
	if (!player_make_simple("Riftlistener", "Ranger", "Magic Tester")) return 1;
	cave = t_build_arena(9, 11);
	player->grid = loc(5, 4);
	term_init(&test_term, 80, 24, 128);
	test_term.xtra_hook = input;
	term_screen = angband_term[0] = &test_term;
	Term_activate(&test_term);
	cmd_init();
	textui_input_init();
	event_add_handler(EVENT_MESSAGE, message, NULL);
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	event_remove_handler(EVENT_MESSAGE, message, NULL);
	term_nuke(&test_term);
	term_screen = angband_term[0] = NULL;
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

static int test_realm_hints_and_cancel(void *state)
{
	const char *names[] = { "Ranger", "Mage", "Priest", "Necromancer" };
	(void)state;
	for (int mode = 0; mode < 2; mode++) {
		Term_resize(mode ? 120 : 80, mode ? 36 : 24);
		player->opts.opt[OPT_rogue_like_commands] = mode != 0;
		for (int i = 0; i < (int)N_ELEMENTS(names); i++) {
			const struct class_book *book;
			const struct class_spell *spell;
			struct object obj = { 0 };
			char expected[100];
			require(choose_class(names[i]));
			book = &player->class->magic.books[0];
			spell = &book->spells[0];
			obj.tval = (uint8_t)book->tval;
			obj.sval = (uint8_t)book->sval;
			player->spell_flags[spell->sidx] = PY_SPELL_LEARNED;
			begin(false);
			eq(textui_get_spell_from_book(player, "cast", &obj, NULL,
				spell_okay_to_cast), -1);
			strnfmt(expected, sizeof(expected), "%s which %s?",
				book->realm->verb, book->realm->spell_noun);
			my_strcap(expected);
			require(strstr(frames[0], expected));
			strnfmt(expected, sizeof(expected), "Active %s, not passive.",
				book->realm->spell_noun);
			require(strstr(frames[0], expected));
			require(strstr(frames[0], "Learn: G   Use: m."));
			strnfmt(expected, sizeof(expected), "Requires level %d; %d Focus",
				spell->slevel, spell->smana);
			require(strstr(frames[0], expected));
			require(strstr(frames[0], "learned, unused"));
			eq(step, steps);
			eq(screen_save_depth, 0);
			eq(player->csp, 100);
			require(!(player->spell_flags[spell->sidx] & PY_SPELL_WORKED));
		}
	}
	ok;
}

static int test_description_toggle_and_study(void *state)
{
	const struct class_book *book;
	struct object obj = { 0 };
	(void)state;
	require(choose_class("Ranger"));
	book = &player->class->magic.books[0];
	obj.tval = (uint8_t)book->tval;
	obj.sval = (uint8_t)book->sval;
	Term_resize(80, 24);
	begin(true);
	eq(textui_get_spell_from_book(player, "study", &obj, NULL,
		spell_okay_to_study), -1);
	require(strstr(frames[0], "Study which practice?"));
	require(strstr(frames[0], "not learned"));
	require(strstr(frames[0], "Restores food once when used"));
	require(strstr(frames[1], "Active practice, not passive"));
	require(!strstr(frames[1], "Restores food once when used"));
	require(strstr(frames[2], "Restores food once when used"));
	eq(step, steps);
	eq(screen_save_depth, 0);
	begin(false);
	textui_book_browse(&obj);
	require(strstr(frames[0], "Browsing practices"));
	require(strstr(frames[0], "Restores food once when used"));
	require(!(player->spell_flags[0] & PY_SPELL_LEARNED));
	ok;
}

static int test_navigation_and_activation_are_separate(void *state)
{
	const struct class_book *book;
	struct object obj = { 0 };
	(void)state;
	require(choose_class("Ranger"));
	book = &player->class->magic.books[0];
	obj.tval = (uint8_t)book->tval;
	obj.sval = (uint8_t)book->sval;
	player->spell_flags[0] = PY_SPELL_LEARNED;
	begin(false);
	steps = 3;
	keys[0] = ARROW_DOWN;
	keys[1] = KC_ENTER;
	keys[2] = ESCAPE;
	eq(textui_get_spell_from_book(player, "cast", &obj, NULL,
		spell_okay_to_cast), -1);
	require(strstr(frames[1], "Not learned: leave this menu and press G"));
	require(strstr(frames[2], "Not learned: leave this menu and press G"));
	eq(step, steps);
	eq(player->csp, 100);
	require(!(player->spell_flags[1] & PY_SPELL_LEARNED));
	/* Both arrows + Enter and a direct letter select a learned second entry. */
	player->spell_flags[1] = PY_SPELL_LEARNED;
	for (int mode = 0; mode < 2; mode++) {
		player->opts.opt[OPT_rogue_like_commands] = mode != 0;
		begin(false);
		steps = 2;
		keys[0] = ARROW_DOWN;
		keys[1] = KC_ENTER;
		eq(textui_get_spell_from_book(player, "cast", &obj, NULL,
			spell_okay_to_cast), 1);
		eq(step, steps);
		begin(false);
		keys[0] = 'b';
		eq(textui_get_spell_from_book(player, "cast", &obj, NULL,
			spell_okay_to_cast), 1);
		eq(step, steps);
	}
	ok;
}

static int test_remove_hunger_is_active_top_up(void *state)
{
	const char *names[] = { "Ranger", "Druid" };
	(void)state;
	for (int n = 0; n < (int)N_ELEMENTS(names); n++) {
		const struct class_spell *spell = NULL;
		int index;
		require(choose_class(names[n]));
		for (index = 0; index < player->class->magic.total_spells; index++) {
			spell = spell_by_index(player, index);
			if (streq(spell->name, "Remove Hunger")) break;
		}
		require(index < player->class->magic.total_spells);
		player->upkeep->new_spells = 1;
		player->timed[TMD_FOOD] = 20 * z_info->food_value;
		learned_reminder = false;
		spell_learn(index);
		require(learned_reminder);
		eq(player->timed[TMD_FOOD], 20 * z_info->food_value);
		eq(player->csp, 100);
		/* Avoid level-up rewards in this deliberately level-boosted fixture. */
		player->spell_flags[index] |= PY_SPELL_WORKED;
		rand_fix(100);
		require(spell_cast(index, 0, NULL));
		rand_unfix();
		eq(player->timed[TMD_FOOD], 50 * z_info->food_value + 1);
		eq(player->csp, 100 - spell->smana);
		player->timed[TMD_FOOD] = 70 * z_info->food_value;
		rand_fix(100);
		require(spell_cast(index, 0, NULL));
		rand_unfix();
		eq(player->timed[TMD_FOOD], 70 * z_info->food_value);
		eq(player->csp, 100 - 2 * spell->smana);
		player->timed[TMD_FOOD] = 20 * z_info->food_value;
		rand_fix(0);
		require(spell_cast(index, 0, NULL));
		rand_unfix();
		eq(player->timed[TMD_FOOD], 20 * z_info->food_value);
		eq(player->csp, 100 - 3 * spell->smana);
	}
	ok;
}

const char *suite_name = "ui/spell-menu";
struct test tests[] = {
	{ "realm hints and cancellation", test_realm_hints_and_cancel },
	{ "description toggle, study and browse", test_description_toggle_and_study },
	{ "inspect unavailable but only activate eligible magic", test_navigation_and_activation_are_separate },
	{ "Remove Hunger is an active top-up", test_remove_hunger_is_active_top_up },
	{ NULL, NULL }
};
