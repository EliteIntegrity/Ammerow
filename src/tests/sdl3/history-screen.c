/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/history-screen.c */
/* Exercise the shared semantic message and character history adapter. */

#include "unit-test.h"

#include "player-history.h"
#include "game-event.h"
#include "sdl3/screen-model.h"
#include "ui-display.h"
#include "ui-history-screen.h"
#include "ui-term.h"

static struct sdl3_screen captured_screen;

static void capture_screen(const struct ui_screen *screen)
{
	sdl3_screen_capture(&captured_screen, screen);
}

int setup_tests(void **state)
{
	(void)state;
	messages_init();
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	messages_free();
	return 0;
}

static int test_message_history_snapshot(void *state)
{
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	message_color_define(MSG_HIT, COLOUR_L_RED);
	message_add("The tunnel is quiet.", MSG_GENERIC);
	message_add("An orc hits you.", MSG_HIT);
	message_add("An orc hits you.", MSG_HIT);
	test_term.wid = 100;
	test_term.hgt = 30;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	sdl3_screen_clear(&captured_screen);
	ui_history_screen_present_messages(0, 0, "orc", messages_num(), 20);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_HISTORY);
	require(streq(captured_screen.title, "Message History"));
	eq(captured_screen.row_count, 2);
	require(strstr(captured_screen.rows[0].label, "quiet") != NULL);
	require(strstr(captured_screen.rows[1].label, "orc hits") != NULL);
	require(strstr(captured_screen.rows[1].label, "<2x>") != NULL);
	require(streq(captured_screen.rows[1].prefix, "MATCH"));
	eq(captured_screen.rows[1].attr, COLOUR_L_RED);
	require(strstr(captured_screen.help, "next match") != NULL);
	ok;
}

static int test_character_history_snapshot(void *state)
{
	struct history_info entries[2];
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	memset(entries, 0, sizeof(entries));
	entries[0].turn = 40;
	entries[0].dlev = 0;
	my_strcpy(entries[0].event, "Entered the settlement.",
		sizeof(entries[0].event));
	entries[1].turn = 875;
	entries[1].dlev = 3;
	my_strcpy(entries[1].event, "Left a singular blade behind.",
		sizeof(entries[1].event));
	hist_on(entries[1].type, HIST_ARTIFACT_LOST);
	test_term.wid = 80;
	test_term.hgt = 24;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	ui_history_screen_present_character(entries, 2, 0, 14, NULL);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_HISTORY);
	require(streq(captured_screen.title, "Character History"));
	eq(captured_screen.row_count, 2);
	require(streq(captured_screen.rows[0].prefix, "Turn 40"));
	require(streq(captured_screen.rows[0].detail, "Surface"));
	require(strstr(captured_screen.rows[1].label, "(LOST)") != NULL);
	require(streq(captured_screen.rows[1].detail, "150 ft"));
	eq(captured_screen.rows[1].attr, COLOUR_L_RED);
	require(strstr(captured_screen.subtitle, "1-2 of 2") != NULL);
	ok;
}

static bool message_row_is(term *t, int row, const wchar_t *expected)
{
	size_t length = wcslen(expected);
	if (wcsncmp(t->scr->c[row], expected, length) != 0) return false;
	for (size_t col = length; col < (size_t)t->wid; col++) {
		if (t->scr->c[row][col] != L' ') return false;
	}
	return true;
}

static int test_message_overlay_wrap_and_resize(void *state)
{
	term terminal = { 0 };
	term *previous = Term;
	uint32_t flags[ANGBAND_TERM_MAX] = { 0 };
	(void)state;
	messages_free();
	messages_init();
	term_init(&terminal, 20, 5, 32);
	angband_term[1] = &terminal;
	Term_activate(&terminal);
	flags[1] = PW_MESSAGE;
	subwindows_set_flags(flags, ANGBAND_TERM_MAX);
	message_add("Earlier.", MSG_GENERIC);
	message_add("Alpha beta gamma delta epsilon.", MSG_HIT);
	event_signal(EVENT_STATE);
	require(message_row_is(&terminal, 0, L""));
	require(message_row_is(&terminal, 1, L""));
	require(message_row_is(&terminal, 2, L"Earlier."));
	require(message_row_is(&terminal, 3, L"Alpha beta gamma"));
	require(message_row_is(&terminal, 4, L"delta epsilon."));
	eq(terminal.scr->a[3][0], COLOUR_RED);
	message_add("Alpha beta gamma delta epsilon.", MSG_HIT);
	event_signal(EVENT_STATE);
	require(message_row_is(&terminal, 4, L"delta epsilon. <2x>"));
	/* Widening reconstructs the message, and clears the obsolete wrapped row. */
	Term_resize(60, 5);
	event_signal(EVENT_STATE);
	require(message_row_is(&terminal, 2, L""));
	require(message_row_is(&terminal, 3, L"Earlier."));
	require(message_row_is(&terminal, 4, L"Alpha beta gamma delta epsilon. <2x>"));
	eq(terminal.scr->a[4][0], message_color(0));
	/* A long unbroken word and an older partial message stay within bounds. */
	Term_resize(8, 3);
	message_add("ABCDEFGHIJKLMNOPQ", MSG_GENERIC);
	event_signal(EVENT_STATE);
	require(message_row_is(&terminal, 0, L"ABCDEFGH"));
	require(message_row_is(&terminal, 1, L"IJKLMNOP"));
	require(message_row_is(&terminal, 2, L"Q"));
	message_add("Done.", MSG_GENERIC);
	event_signal(EVENT_STATE);
	require(message_row_is(&terminal, 0, L"IJKLMNOP"));
	require(message_row_is(&terminal, 1, L"Q"));
	require(message_row_is(&terminal, 2, L"Done."));
	flags[1] = 0;
	subwindows_set_flags(flags, ANGBAND_TERM_MAX);
	angband_term[1] = NULL;
	Term_activate(previous);
	term_nuke(&terminal);
	messages_free();
	messages_init();
	ok;
}

const char *suite_name = "sdl3/history-screen";
struct test tests[] = {
	{ "message overlay wraps and reflows after resizing", test_message_overlay_wrap_and_resize },
	{ "message history snapshot", test_message_history_snapshot },
	{ "character history snapshot", test_character_history_snapshot },
	{ NULL, NULL }
};
