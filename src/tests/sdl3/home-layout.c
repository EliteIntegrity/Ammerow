/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/home-layout.c */
/* Exercise shared responsive Home geometry. */

#include "unit-test.h"

#include "sdl3/home-layout.h"
#include "sdl3/home-model.h"

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

static int test_compact_layout(void *state)
{
	struct sdl3_home_layout layout;
	(void)state;

	require(sdl3_home_layout_init(80, 24, &layout));
	require(layout.detail_row < layout.footer_row - 1);
	eq(layout.menu_col, 4);
	eq(layout.menu_width, 72);
	require(!layout.hero_visible);
	eq(layout.footer_row, 22);
	ok;
}

static int test_wide_layout_reserves_hero(void *state)
{
	struct sdl3_home_layout layout;
	(void)state;

	require(sdl3_home_layout_init(160, 45, &layout));
	require(layout.hero_visible);
	eq(layout.menu_width, 72);
	eq(layout.menu_col, 8);
	eq(layout.hero_col, 89);
	eq(layout.hero_cols, 68);
	require(layout.menu_col + layout.menu_width < layout.hero_col);
	ok;
}

static int test_invalid_layout(void *state)
{
	struct sdl3_home_layout layout;
	(void)state;

	require(!sdl3_home_layout_init(63, 24, &layout));
	require(!sdl3_home_layout_init(80, 21, &layout));
	require(!sdl3_home_layout_init(80, 24, NULL));
	ok;
}

static int test_root_rows_clear_footer(void *state)
{
	struct sdl3_home_layout layout;
	(void)state;
	for (int rows = 22; rows <= 60; rows++) {
		int last_menu_row;
		require(sdl3_home_layout_init(80, rows, &layout));
		last_menu_row = layout.menu_row +
			(SDL3_HOME_ROOT_ROW_COUNT - 2) * layout.menu_spacing;
		require(last_menu_row < layout.detail_row);
		require(layout.detail_row < layout.footer_row - 1);
	}
	ok;
}

const char *suite_name = "sdl3/home-layout";
struct test tests[] = {
	{ "compact layout", test_compact_layout },
	{ "wide layout reserves hero", test_wide_layout_reserves_hero },
	{ "invalid layout", test_invalid_layout },
	{ "root rows clear footer", test_root_rows_clear_footer },
	{ NULL, NULL },
};
