/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/history-screen.c */
/* Exercise the shared semantic message and character history adapter. */

#include "unit-test.h"

#include "player-history.h"
#include "sdl3/screen-model.h"
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

const char *suite_name = "sdl3/history-screen";
struct test tests[] = {
	{ "message history snapshot", test_message_history_snapshot },
	{ "character history snapshot", test_character_history_snapshot },
	{ NULL, NULL }
};
