/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/settings-model.c */
/* Exercise platform-independent full-screen settings navigation. */

#include "unit-test.h"

#include "sdl3/settings-model.h"

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

static int test_open_and_toggle(void *state)
{
	struct sdl3_settings_overlay overlay;
	(void)state;

	sdl3_settings_init(&overlay);
	require(!overlay.visible);
	eq(overlay.page, SDL3_SETTINGS_HUB);
	eq(overlay.selected_hub_row, 0);
	sdl3_settings_open(&overlay);
	require(overlay.visible);
	eq(overlay.page, SDL3_SETTINGS_HUB);
	sdl3_settings_toggle(&overlay);
	require(!overlay.visible);
	sdl3_settings_toggle(&overlay);
	require(overlay.visible);
	eq(overlay.page, SDL3_SETTINGS_HUB);
	ok;
}

static int test_hub_navigation(void *state)
{
	static const int first_rows[] = {
		SDL3_SETTINGS_THEME,
		SDL3_SETTINGS_INTERFACE_FONT,
		SDL3_SETTINGS_AUDIO_ENABLED,
		SDL3_SETTINGS_FULLSCREEN
	};
	struct sdl3_settings_overlay overlay;
	int i;
	(void)state;

	for (i = 0; i < 4; i++) {
		sdl3_settings_init(&overlay);
		sdl3_settings_open(&overlay);
		while (overlay.selected_hub_row != i) {
			sdl3_settings_move(&overlay, 1);
		}
		require(sdl3_settings_activate(&overlay));
		eq(overlay.page, SDL3_SETTINGS_APPEARANCE + i);
		eq(overlay.selected_row, first_rows[i]);
		require(!sdl3_settings_activate(&overlay));
		require(sdl3_settings_back(&overlay));
		eq(overlay.page, SDL3_SETTINGS_HUB);
		eq(overlay.selected_hub_row, i);
		require(!sdl3_settings_back(&overlay));
	}
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_hub_row, 2);
	ok;
}

static int test_hub_contains_only_settings(void *state)
{
	struct sdl3_settings_overlay overlay;
	(void)state;

	sdl3_settings_init(&overlay);
	sdl3_settings_open(&overlay);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_hub_row, SDL3_SETTINGS_HUB_DISPLAY);
	require(sdl3_settings_activate(&overlay));
	eq(overlay.page, SDL3_SETTINGS_DISPLAY);
	ok;
}

static int test_category_navigation(void *state)
{
	struct sdl3_settings_overlay overlay;
	(void)state;

	sdl3_settings_init(&overlay);
	sdl3_settings_open(&overlay);
	require(sdl3_settings_activate(&overlay));
	eq(overlay.selected_row, SDL3_SETTINGS_THEME);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_MAP_FONT);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_TILE_MODE);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_HYBRID_DETAIL);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_TERRAIN);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_THEME);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_row, SDL3_SETTINGS_TERRAIN);

	require(sdl3_settings_back(&overlay));
	sdl3_settings_move(&overlay, 1);
	require(sdl3_settings_activate(&overlay));
	eq(overlay.page, SDL3_SETTINGS_INTERFACE);
	eq(overlay.selected_row, SDL3_SETTINGS_INTERFACE_FONT);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_INTERFACE_DENSITY);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_BIG_STAT_CARDS);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_row, SDL3_SETTINGS_INTERFACE_DENSITY);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_row, SDL3_SETTINGS_INTERFACE_FONT);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_row, SDL3_SETTINGS_HUD_STATS);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_INTERFACE_FONT);
	ok;
}

static int test_page_rows_are_shared_with_pointer_layout(void *state)
{
	enum sdl3_settings_row row;
	(void)state;

	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_APPEARANCE), 5);
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_APPEARANCE, 3, &row));
	eq(row, SDL3_SETTINGS_HYBRID_DETAIL);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_INTERFACE), 7);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_SOUND), 9);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_DISPLAY), 4);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_HUB), 0);
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_INTERFACE, 2, &row));
	eq(row, SDL3_SETTINGS_BIG_STAT_CARDS);
	require(!sdl3_settings_page_row_at(SDL3_SETTINGS_INTERFACE, 7, &row));
	require(!sdl3_settings_page_row_at(SDL3_SETTINGS_DISPLAY, 0, NULL));
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_DISPLAY, 2, &row));
	eq(row, SDL3_SETTINGS_ANIMATED_COMBAT);
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_DISPLAY, 3, &row));
	eq(row, SDL3_SETTINGS_WEATHER);
	ok;
}

static int test_all_pages_fit_and_hit_test(void *state)
{
	static const int cols[] = { 80, 100, 120, 140 };
	static const int rows[] = { 24, 30, 36, 42 };
	(void)state;
	for (int size = 0; size < 4; size++) {
		for (int page = SDL3_SETTINGS_HUB; page <= SDL3_SETTINGS_DISPLAY; page++) {
			struct sdl3_settings_overlay overlay;
			struct sdl3_settings_layout layout = sdl3_settings_layout(page,
				cols[size], rows[size]);
			struct sdl3_menu_layout *menu = &layout.menu;
			int last = sdl3_menu_item_row(menu, menu->count - 1);
			sdl3_settings_init(&overlay);
			overlay.page = page;
			require(menu->col >= 0 && menu->col + menu->width <= cols[size]);
			require(menu->row > 5);
			require(last + menu->item_height < layout.footer_row);
			if (layout.note_rows) {
				require(layout.note_row > last + menu->item_height);
				require(layout.note_row + layout.note_rows < layout.footer_row);
			}
			for (int i = 0; i < menu->count; i++) {
				int row = sdl3_menu_item_row(menu, i);
				require(sdl3_settings_select_at(&overlay, cols[size], rows[size],
					menu->col + 4, row));
				if (page == SDL3_SETTINGS_HUB) {
					eq(overlay.selected_hub_row, i);
				} else {
					enum sdl3_settings_row expected;
					require(sdl3_settings_page_row_at(page, i, &expected));
					eq(overlay.selected_row, expected);
				}
				if (menu->step > menu->item_height)
					require(!sdl3_settings_select_at(&overlay, cols[size], rows[size],
						menu->col + 4, row + menu->item_height));
			}
			require(!sdl3_settings_select_at(&overlay, cols[size], rows[size],
				menu->col + 4, layout.footer_row));
			require(!sdl3_settings_select_at(&overlay, cols[size], rows[size],
				menu->col - 1, menu->row));
		}
	}
	ok;
}

const char *suite_name = "sdl3/settings-model";
struct test tests[] = {
	{ "all settings pages fit and hit-test at every density", test_all_pages_fit_and_hit_test },
	{ "open and toggle", test_open_and_toggle },
	{ "hub navigation", test_hub_navigation },
	{ "hub contains only settings", test_hub_contains_only_settings },
	{ "category navigation", test_category_navigation },
	{ "page rows are shared with pointer layout",
		test_page_rows_are_shared_with_pointer_layout },
	{ NULL, NULL },
};
