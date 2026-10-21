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
		SDL3_SETTINGS_FULLSCREEN,
		SDL3_SETTINGS_EXPLORE
	};
	struct sdl3_settings_overlay overlay;
	int i;
	(void)state;

	for (i = 0; i < SDL3_SETTINGS_HUB_ROW_COUNT; i++) {
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
	eq(overlay.selected_hub_row, SDL3_SETTINGS_HUB_ROW_COUNT - 2);
	ok;
}

static int test_hub_contains_only_settings(void *state)
{
	struct sdl3_settings_overlay overlay;
	(void)state;

	sdl3_settings_init(&overlay);
	sdl3_settings_open(&overlay);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_hub_row, SDL3_SETTINGS_HUB_CONTROLS);
	require(sdl3_settings_activate(&overlay));
	eq(overlay.page, SDL3_SETTINGS_CONTROLS);
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
	eq(overlay.selected_row, SDL3_SETTINGS_ASCII_GRID);
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

	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_APPEARANCE, false), 6);
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_APPEARANCE, false, 3, &row));
	eq(row, SDL3_SETTINGS_HYBRID_DETAIL);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_INTERFACE, false), 7);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_SOUND, false), 9);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_DISPLAY, false), 5);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_CONTROLS, false), 7);
	eq(sdl3_settings_page_row_count(SDL3_SETTINGS_HUB, false), 0);
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_INTERFACE, false, 2, &row));
	eq(row, SDL3_SETTINGS_BIG_STAT_CARDS);
	require(!sdl3_settings_page_row_at(SDL3_SETTINGS_INTERFACE, false, 7, &row));
	require(!sdl3_settings_page_row_at(SDL3_SETTINGS_DISPLAY, false, 0, NULL));
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_DISPLAY, false, 2, &row));
	eq(row, SDL3_SETTINGS_ANIMATED_COMBAT);
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_DISPLAY, false, 3, &row));
	eq(row, SDL3_SETTINGS_WEATHER);
	require(sdl3_settings_page_row_at(SDL3_SETTINGS_DISPLAY, false, 4, &row));
	eq(row, SDL3_SETTINGS_EDGE_WARNINGS);
	ok;
}

static int test_all_pages_fit_and_hit_test(void *state)
{
	static const int cols[] = { 80, 100, 120, 140 };
	static const int rows[] = { 24, 30, 36, 42 };
	(void)state;
	for (int size = 0; size < 4; size++) {
		for (int page = SDL3_SETTINGS_HUB; page < SDL3_SETTINGS_PAGE_COUNT; page++) {
			struct sdl3_settings_overlay overlay;
			struct sdl3_settings_layout layout = sdl3_settings_layout(page, false,
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
					require(sdl3_settings_page_row_at(page, false, i, &expected));
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
static int test_enhanced_square_row(void *state)
{
	struct sdl3_settings_overlay overlay;
	(void)state;
	sdl3_settings_init(&overlay);
	overlay.page = SDL3_SETTINGS_APPEARANCE;
	overlay.selected_row = SDL3_SETTINGS_ASCII_GRID;
	sdl3_settings_set_enhanced_available(&overlay, true);
	eq(sdl3_settings_page_row_count(overlay.page, true), 7);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_ASCII_ENHANCED);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_TERRAIN);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_row, SDL3_SETTINGS_ASCII_ENHANCED);
	for (int size = 0; size < 4; size++) {
		int cols = 80 + size * 20, rows = 24 + size * 6;
		struct sdl3_settings_layout layout = sdl3_settings_layout(
			overlay.page, true, cols, rows);
		require(layout.note_row + layout.note_rows < layout.footer_row);
		for (int i = 0; i < layout.menu.count; i++) {
			enum sdl3_settings_row expected;
			require(sdl3_settings_page_row_at(overlay.page, true, i, &expected));
			require(sdl3_settings_select_at(&overlay, cols, rows,
				layout.menu.col + 4, sdl3_menu_item_row(&layout.menu, i)));
			eq(overlay.selected_row, expected);
		}
	}
	overlay.selected_row = SDL3_SETTINGS_ASCII_ENHANCED;
	sdl3_settings_set_enhanced_available(&overlay, false);
	eq(overlay.selected_row, SDL3_SETTINGS_ASCII_GRID);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_TERRAIN);
	eq(sdl3_settings_page_row_count(overlay.page, false), 6);
	ok;
}

static int test_ascii_floors_row(void *state)
{
	struct sdl3_settings_overlay overlay;
	(void)state;
	sdl3_settings_init(&overlay);
	overlay.page = SDL3_SETTINGS_APPEARANCE;
	overlay.selected_row = SDL3_SETTINGS_TILE_MODE;
	sdl3_settings_set_floors_available(&overlay, true);
	eq(sdl3_settings_appearance_mode(&overlay), SDL3_SETTINGS_APPEARANCE_FULL);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_ASCII_FLOORS);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_HYBRID_DETAIL);
	sdl3_settings_move(&overlay, -1);
	eq(overlay.selected_row, SDL3_SETTINGS_ASCII_FLOORS);
	for (int size = 0; size < 4; size++) {
		int cols = 80 + size * 20, rows = 24 + size * 6;
		struct sdl3_settings_layout layout = sdl3_settings_layout(
			overlay.page, SDL3_SETTINGS_APPEARANCE_FULL, cols, rows);
		eq(layout.menu.count, 7);
		require(layout.note_row + layout.note_rows < layout.footer_row);
		for (int i = 0; i < layout.menu.count; i++) {
			enum sdl3_settings_row expected;
			require(sdl3_settings_page_row_at(overlay.page,
				SDL3_SETTINGS_APPEARANCE_FULL, i, &expected));
			require(sdl3_settings_select_at(&overlay, cols, rows,
				layout.menu.col + 4, sdl3_menu_item_row(&layout.menu, i)));
			eq(overlay.selected_row, expected);
		}
	}
	overlay.selected_row = SDL3_SETTINGS_ASCII_FLOORS;
	sdl3_settings_set_floors_available(&overlay, false);
	eq(overlay.selected_row, SDL3_SETTINGS_TILE_MODE);
	sdl3_settings_move(&overlay, 1);
	eq(overlay.selected_row, SDL3_SETTINGS_HYBRID_DETAIL);
	ok;
}

struct test tests[] = {
	{ "conditional ASCII floors navigation and layout", test_ascii_floors_row },
	{ "conditional Enhanced Square navigation and layout", test_enhanced_square_row },
	{ "all settings pages fit and hit-test at every density", test_all_pages_fit_and_hit_test },
	{ "open and toggle", test_open_and_toggle },
	{ "hub navigation", test_hub_navigation },
	{ "hub contains only settings", test_hub_contains_only_settings },
	{ "category navigation", test_category_navigation },
	{ "page rows are shared with pointer layout",
		test_page_rows_are_shared_with_pointer_layout },
	{ NULL, NULL },
};
