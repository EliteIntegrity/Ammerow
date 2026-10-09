/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/nearby-screen.c */
/* Exercise the shared semantic nearby-information adapter. */

#include "unit-test.h"

#include "sdl3/screen-model.h"
#include "ui-nearby-screen.h"
#include "ui-term.h"
#include "z-color.h"

static struct sdl3_screen captured_screen;

static void capture_screen(const struct ui_screen *screen)
{
	sdl3_screen_capture(&captured_screen, screen);
}

int setup_tests(void **state)
{
	(void)state;
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	return 0;
}

static int test_nearby_snapshot_preserves_row_semantics(void *state)
{
	struct ui_nearby_screen_entry entries[] = {
		{
			.label = "two cave skitters",
			.prefix = "IN SIGHT",
			.detail = "1 asleep",
			.glyph = L'c',
			.text_attr = COLOUR_RED,
			.glyph_attr = COLOUR_L_BLUE,
			.has_glyph_attr = true
		},
		{
			.label = "a ration",
			.prefix = "SENSED",
			.detail = "3 N  2 E",
			.glyph = L',',
			.text_attr = COLOUR_WHITE,
			.glyph_attr = COLOUR_L_UMBER,
			.has_glyph_attr = true
		}
	};
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	test_term.wid = 100;
	test_term.hgt = 30;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	sdl3_screen_clear(&captured_screen);
	ui_nearby_screen_present("Nearby Creatures",
		"2 in sight | 1 sensed", "Escape returns", entries,
		N_ELEMENTS(entries), 1);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_NEARBY);
	require(streq(captured_screen.title, "Nearby Creatures"));
	eq(captured_screen.row_count, 2);
	eq(captured_screen.cursor, 1);
	require(streq(captured_screen.rows[0].prefix, "IN SIGHT"));
	require(captured_screen.rows[0].glyph == L'c');
	eq(captured_screen.rows[0].attr, COLOUR_RED);
	eq(captured_screen.rows[0].glyph_attr, COLOUR_L_BLUE);
	require(captured_screen.rows[0].has_glyph_attr);
	require(strstr(captured_screen.rows[1].detail, "3 N") != NULL);
	ok;
}

static int test_nearby_empty_snapshot(void *state)
{
	term test_term = { 0 };
	term *previous_term = Term;
	(void)state;

	test_term.wid = 80;
	test_term.hgt = 24;
	test_term.screen_hook = capture_screen;
	Term = &test_term;
	ui_nearby_screen_present("Nearby Objects", "Nothing nearby.",
		"Press any key", NULL, 4, 9);
	Term = previous_term;

	require(captured_screen.active);
	eq(captured_screen.kind, UI_SCREEN_NEARBY);
	eq(captured_screen.row_count, 0);
	eq(captured_screen.cursor, -1);
	require(streq(captured_screen.subtitle, "Nothing nearby."));
	ok;
}

static int test_nearby_formats_relative_locations(void *state)
{
	char location[32];
	(void)state;

	ui_nearby_screen_format_offset(location, sizeof(location), 0, 0);
	require(streq(location, "underfoot"));
	ui_nearby_screen_format_offset(location, sizeof(location), -3, 0);
	require(streq(location, "3 N"));
	ui_nearby_screen_format_offset(location, sizeof(location), 0, 4);
	require(streq(location, "4 E"));
	ui_nearby_screen_format_offset(location, sizeof(location), 2, -5);
	require(streq(location, "2 S  5 W"));
	ok;
}

const char *suite_name = "sdl3/nearby-screen";
struct test tests[] = {
	{ "nearby snapshot preserves row semantics",
		test_nearby_snapshot_preserves_row_semantics },
	{ "nearby empty snapshot", test_nearby_empty_snapshot },
	{ "nearby formats relative locations",
		test_nearby_formats_relative_locations },
	{ NULL, NULL }
};
