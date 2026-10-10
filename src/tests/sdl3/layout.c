/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/layout.c */
/* Exercise the platform-independent SDL3 character layout and cell grid. */

#include "unit-test.h"

#include "sdl3/layout.h"
#include "sdl3/term.h"

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

static int test_default_layout(void *state)
{
	struct sdl3_layout layout;
	const struct sdl3_pane_layout *main_pane;
	const struct sdl3_pane_layout *messages;
	(void)state;

	sdl3_layout_init(&layout);
	eq(layout.cols, 80);
	eq(layout.rows, 24);
	eq(layout.separator_col, -1);
	eq(layout.separator_row, -1);
	main_pane = sdl3_layout_pane(&layout, SDL3_LAYOUT_MAIN_TERM);
	messages = sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM);
	require(main_pane != NULL);
	require(messages != NULL);
	eq(main_pane->cols, 80);
	eq(main_pane->rows, 24);
	eq(messages->col, 0);
	eq(messages->row, 18);
	eq(messages->cols, 80);
	eq(messages->rows, 6);
	require(sdl3_layout_pane(&layout, 7) == NULL);
	ok;
}

static int test_main_cell_mapping(void *state)
{
	struct sdl3_layout layout;
	int col = -1;
	int row = -1;
	(void)state;

	sdl3_layout_init(&layout);
	require(sdl3_layout_main_cell(&layout, 0, 0, &col, &row));
	eq(col, 0);
	eq(row, 0);
	require(sdl3_layout_main_cell(&layout, 79, 23, &col, &row));
	eq(col, 79);
	eq(row, 23);
	require(!sdl3_layout_main_cell(&layout, 80, 23, &col, &row));
	require(sdl3_layout_main_cell(&layout, 79, 24 - 1, &col, &row));
	eq(col, 79);
	eq(row, 23);
	require(!sdl3_layout_main_cell(&layout, 0, 24, &col, &row));
	ok;
}

static int test_configurable_layout(void *state)
{
	struct sdl3_layout layout;
	const struct sdl3_pane_layout *dock;
	(void)state;

	sdl3_layout_configure(&layout, false, 9);
	eq(layout.cols, 80);
	eq(layout.rows, 24);
	eq(layout.separator_col, -1);
	eq(layout.separator_row, -1);
	dock = sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM);
	require(dock != NULL);
	eq(dock->row, 15);
	eq(dock->rows, 9);

	sdl3_layout_configure(&layout, true, 1);
	eq(layout.rows, 24);
	eq(layout.separator_row, -1);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->row, 21);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->rows,
		SDL3_LAYOUT_MIN_DOCK_ROWS);
	sdl3_layout_configure(&layout, true, 99);
	eq(layout.rows, 24);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->row, 12);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->rows,
		SDL3_LAYOUT_MAX_DOCK_ROWS);
	ok;
}

static int test_left_and_top_overlay_layout(void *state)
{
	struct sdl3_layout layout;
	const struct sdl3_pane_layout *main_pane;
	const struct sdl3_pane_layout *dock;
	int request;
	int col = -1;
	int row = -1;
	(void)state;

	sdl3_layout_configure_placed(&layout, true, SDL3_DOCK_LEFT, 6, 32);
	main_pane = sdl3_layout_pane(&layout, SDL3_LAYOUT_MAIN_TERM);
	dock = sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM);
	require(main_pane != NULL);
	require(dock != NULL);
	eq(layout.cols, 80);
	eq(layout.rows, 24);
	eq(layout.separator_col, -1);
	eq(layout.separator_row, -1);
	eq(dock->col, 0);
	eq(dock->row, 0);
	eq(dock->cols, 32);
	eq(dock->rows, 24);
	require(sdl3_layout_main_cell(&layout, 79, 23, &col, &row));
	eq(col, 79);
	eq(row, 23);
	require(sdl3_layout_main_cell(&layout, 31, 0, &col, &row));
	eq(col, 31);
	eq(row, 0);
	require(!sdl3_layout_main_cell(&layout, 80, 23, &col, &row));

	sdl3_layout_configure_placed(&layout, false, SDL3_DOCK_LEFT, 6, 99);
	eq(layout.cols, 80);
	eq(layout.rows, 24);
	eq(layout.separator_col, -1);
	dock = sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM);
	eq(dock->cols, SDL3_LAYOUT_MAX_DOCK_COLS);

	for (request = -32; request <= 64; request++) {
		int expected_cols = request;
		int visible;

		if (expected_cols < SDL3_LAYOUT_MIN_DOCK_COLS) {
			expected_cols = SDL3_LAYOUT_MIN_DOCK_COLS;
		}
		if (expected_cols > SDL3_LAYOUT_MAX_DOCK_COLS) {
			expected_cols = SDL3_LAYOUT_MAX_DOCK_COLS;
		}
		for (visible = 0; visible <= 1; visible++) {
			sdl3_layout_configure_placed(&layout, visible != 0,
				SDL3_DOCK_LEFT, 6, request);
			dock = sdl3_layout_pane(&layout,
				SDL3_LAYOUT_MESSAGE_TERM);
			require(dock != NULL);
			eq(dock->col, 0);
			eq(dock->rows, 24);
			eq(dock->cols, expected_cols);
			eq(layout.cols, 80);
			eq(layout.rows, 24);
			eq(layout.separator_col, -1);
			eq(layout.separator_row, -1);
		}
	}
	sdl3_layout_configure_placed(&layout, true, SDL3_DOCK_TOP, 5, 32);
	dock = sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM);
	eq(layout.cols, 80);
	eq(layout.rows, 24);
	eq(dock->col, 0);
	eq(dock->row, 0);
	eq(dock->cols, 80);
	eq(dock->rows, 5);
	require(streq(sdl3_dock_placement_name(SDL3_DOCK_TOP), "Top"));
	require(streq(sdl3_dock_placement_name(SDL3_DOCK_LEFT), "Left"));
	require(streq(sdl3_dock_placement_name(SDL3_DOCK_BOTTOM), "Bottom"));
	ok;
}

static int test_interface_density_presets(void *state)
{
	static const int expected_cols[] = { 80, 100, 120, 140 };
	static const int expected_rows[] = { 24, 30, 36, 42 };
	struct sdl3_layout layout;
	int i;
	(void)state;

	for (i = 0; i < SDL3_INTERFACE_DENSITY_COUNT; i++) {
		int cols = 0;
		int rows = 0;

		sdl3_interface_density_dimensions(
			(enum sdl3_interface_density)i, &cols, &rows);
		eq(cols, expected_cols[i]);
		eq(rows, expected_rows[i]);
		require(sdl3_interface_density_name(
			(enum sdl3_interface_density)i)[0] != '\0');
	}
	eq(sdl3_interface_density_change(SDL3_INTERFACE_LARGE, -1),
		SDL3_INTERFACE_COMPACT);
	eq(sdl3_interface_density_change(SDL3_INTERFACE_LARGE, 1),
		SDL3_INTERFACE_COMFORTABLE);
	eq(sdl3_interface_density_change(SDL3_INTERFACE_COMPACT, 1),
		SDL3_INTERFACE_LARGE);
	/* Repeated activation can always reach every preset, including after
	 * either endpoint. Zero leaves the choice alone; invalid input recovers. */
	for (i = 0; i < SDL3_INTERFACE_DENSITY_COUNT; i++) {
		enum sdl3_interface_density forward = i, backward = i;
		for (int step = 1; step <= SDL3_INTERFACE_DENSITY_COUNT * 2; step++) {
			forward = sdl3_interface_density_change(forward, 1);
			backward = sdl3_interface_density_change(backward, -1);
			eq(forward, (i + step) % SDL3_INTERFACE_DENSITY_COUNT);
			eq(backward, (i + SDL3_INTERFACE_DENSITY_COUNT * 2 - step) %
				SDL3_INTERFACE_DENSITY_COUNT);
		}
		eq(sdl3_interface_density_change(i, 0), i);
	}
	eq(sdl3_interface_density_change(SDL3_INTERFACE_DENSITY_COUNT, 0),
		SDL3_INTERFACE_DENSITY_DEFAULT);
	eq(sdl3_interface_density_change(-1, 1),
		sdl3_interface_density_change(SDL3_INTERFACE_DENSITY_DEFAULT, 1));

	sdl3_layout_configure_sized(&layout, true, SDL3_DOCK_BOTTOM, 4, 32,
		120, 36);
	eq(layout.cols, 120);
	eq(layout.rows, 36);
	eq(layout.separator_row, -1);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MAIN_TERM)->cols, 120);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MAIN_TERM)->rows, 36);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->row, 32);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->cols, 120);

	sdl3_layout_configure_sized(&layout, true, SDL3_DOCK_LEFT, 6, 32,
		140, 42);
	eq(layout.cols, 140);
	eq(layout.rows, 42);
	eq(layout.separator_col, -1);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->col, 0);
	eq(sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM)->rows, 42);
	ok;
}

static int test_message_overlay_contract(void *state)
{
	struct sdl3_layout top;
	struct sdl3_layout bottom;
	const struct sdl3_pane_layout *top_messages;
	const struct sdl3_pane_layout *bottom_messages;
	(void)state;

	sdl3_layout_configure_sized(&top, true, SDL3_DOCK_TOP, 5, 32, 120, 36);
	sdl3_layout_configure_sized(&bottom, true, SDL3_DOCK_BOTTOM, 5, 32,
		120, 36);
	top_messages = sdl3_layout_pane(&top, SDL3_LAYOUT_MESSAGE_TERM);
	bottom_messages = sdl3_layout_pane(&bottom, SDL3_LAYOUT_MESSAGE_TERM);
	require(top_messages != NULL);
	require(bottom_messages != NULL);
	eq(top_messages->col, 0);
	eq(top_messages->row, 0);
	eq(top_messages->cols, 120);
	eq(top_messages->rows, 5);
	eq(bottom_messages->col, 0);
	eq(bottom_messages->row, 31);
	eq(bottom_messages->cols, 120);
	eq(bottom_messages->rows, 5);
	eq(sdl3_layout_pane(&top, SDL3_LAYOUT_MAIN_TERM)->cols, 120);
	eq(sdl3_layout_pane(&top, SDL3_LAYOUT_MAIN_TERM)->rows, 36);
	eq(sdl3_layout_pane(&bottom, SDL3_LAYOUT_MAIN_TERM)->cols, 120);
	eq(sdl3_layout_pane(&bottom, SDL3_LAYOUT_MAIN_TERM)->rows, 36);
	require(streq(sdl3_dock_placement_name(SDL3_DOCK_TOP), "Top"));
	require(streq(sdl3_dock_placement_name(SDL3_DOCK_BOTTOM), "Bottom"));
	ok;
}

static int test_top_right_messages(void *state)
{
	struct sdl3_layout layout;
	struct sdl3_grid history;
	struct sdl3_cell_bounds bounds;
	int col, row;
	(void)state;

	/* Persisted values cannot change when another choice is introduced. */
	eq(SDL3_DOCK_BOTTOM, 0);
	eq(SDL3_DOCK_LEFT, 1);
	eq(SDL3_DOCK_TOP, 2);
	eq(SDL3_DOCK_TOP_RIGHT, 3);
	require(streq(sdl3_dock_placement_name(SDL3_DOCK_TOP_RIGHT), "Top right"));
	eq(sdl3_dock_placement_change(SDL3_DOCK_TOP, 1), SDL3_DOCK_TOP_RIGHT);
	eq(sdl3_dock_placement_change(SDL3_DOCK_TOP_RIGHT, 1), SDL3_DOCK_BOTTOM);
	eq(sdl3_dock_placement_change(SDL3_DOCK_BOTTOM, 1), SDL3_DOCK_TOP);
	eq(sdl3_dock_placement_change(SDL3_DOCK_TOP, -1), SDL3_DOCK_BOTTOM);
	eq(sdl3_dock_placement_change(SDL3_DOCK_BOTTOM, -1), SDL3_DOCK_TOP_RIGHT);
	eq(sdl3_dock_placement_change(SDL3_DOCK_TOP_RIGHT, -1), SDL3_DOCK_TOP);
	eq(sdl3_dock_placement_change(SDL3_DOCK_TOP_RIGHT, 0), SDL3_DOCK_TOP_RIGHT);
	eq(sdl3_dock_placement_change(SDL3_DOCK_LEFT, 0), SDL3_DOCK_BOTTOM);
	eq(sdl3_dock_placement_change((enum sdl3_dock_placement)99, 0), SDL3_DOCK_BOTTOM);

	for (int density = 0; density < SDL3_INTERFACE_DENSITY_COUNT; density++) {
		int cols, rows;
		const struct sdl3_pane_layout *messages;
		sdl3_interface_density_dimensions(density, &cols, &rows);
		sdl3_layout_configure_sized(&layout, true, SDL3_DOCK_TOP_RIGHT,
			6, 32, cols, rows);
		messages = sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM);
		/* History retains the same text capacity; the map is never resized. */
		eq(messages->col, 0);
		eq(messages->row, 0);
		eq(messages->cols, cols);
		eq(messages->rows, 6);
		eq(layout.cols, cols);
		eq(layout.rows, rows);
		for (int width = 1; width <= cols; width++) {
			sdl3_layout_message_offset(SDL3_DOCK_TOP_RIGHT, cols, rows,
				width, 6, &col, &row);
			eq(col + width, cols);
			eq(row, 0);
		}
		sdl3_layout_message_offset(SDL3_DOCK_TOP, cols, rows, 20, 6, &col, &row);
		eq(col, 0);
		eq(row, 0);
		sdl3_layout_message_offset(SDL3_DOCK_BOTTOM, cols, rows, 20, 6, &col, &row);
		eq(col, 0);
		eq(row, rows - 6);
	}

	/* Use real content bounds, including indentation and unequal line lengths.
	 * A common offset preserves left alignment and moves text/backplate alike. */
	require(sdl3_grid_init(&history, 80, 6));
	require(!sdl3_grid_content_bounds(&history, 0, 0, 80, 6, &bounds));
	sdl3_grid_put_text(&history, 2, 4, 12, COLOUR_WHITE, L"A long line.");
	sdl3_grid_put_text(&history, 2, 5, 6, COLOUR_YELLOW, L"Short.");
	require(sdl3_grid_content_bounds(&history, 0, 0, 80, 6, &bounds));
	sdl3_layout_message_offset(SDL3_DOCK_TOP_RIGHT, 80, 24,
		bounds.col + bounds.cols, history.rows, &col, &row);
	eq(col, 66);
	eq(row, 0);
	eq(col + bounds.col + bounds.cols, 80);
	sdl3_grid_free(&history);
	/* Defensive clamping when an old buffer is larger than a resized view. */
	sdl3_layout_message_offset(SDL3_DOCK_TOP_RIGHT, 40, 24, 80, 6, &col, &row);
	eq(col, 0);
	eq(row, 0);
	ok;
}

static int test_dirty_cells(void *state)
{
	struct sdl3_grid grid;
	const wchar_t text[] = L"AB";
	const struct sdl3_cell *cell;
	(void)state;

	require(sdl3_grid_init(&grid, 4, 3));
	require(grid.dirty);
	sdl3_grid_mark_clean(&grid);
	require(!grid.dirty);

	sdl3_grid_put_text(&grid, 1, 1, 2, COLOUR_L_BLUE, text);
	require(grid.dirty);
	cell = sdl3_grid_cell(&grid, 1, 1);
	require(cell != NULL);
	eq(cell->codepoint, L'A');
	eq(cell->foreground, COLOUR_L_BLUE);
	require(cell->dirty);
	require(!sdl3_grid_cell(&grid, 0, 0)->dirty);

	sdl3_grid_mark_clean(&grid);
	sdl3_grid_put_text(&grid, 1, 1, 2, COLOUR_L_BLUE, text);
	require(!grid.dirty);
	sdl3_grid_fill(&grid, 0, 2, 4, 1, L' ',
		COLOUR_SLATE + MULT_BG * BG_DARK);
	require(grid.dirty);
	eq(sdl3_grid_cell(&grid, 0, 2)->background, COLOUR_SHADE);
	require(!sdl3_grid_cell(&grid, 0, 0)->dirty);
	require(sdl3_grid_resize(&grid, 5, 4));
	eq(grid.cols, 5);
	eq(grid.rows, 4);
	eq(sdl3_grid_cell(&grid, 1, 1)->codepoint, L'A');
	eq(sdl3_grid_cell(&grid, 4, 3)->codepoint, L' ');
	require(sdl3_grid_cell(&grid, 4, 3)->dirty);
	require(sdl3_grid_resize(&grid, 2, 2));
	eq(grid.cols, 2);
	eq(grid.rows, 2);
	eq(sdl3_grid_cell(&grid, 1, 1)->codepoint, L'A');

	sdl3_grid_free(&grid);
	ok;
}

static int test_grid_content_bounds(void *state)
{
	struct sdl3_grid grid;
	struct sdl3_cell_bounds bounds;
	(void)state;

	require(sdl3_grid_init(&grid, 8, 5));
	require(!sdl3_grid_content_bounds(&grid, 0, 0, 8, 5, &bounds));
	sdl3_grid_put_cell(&grid, 2, 1, COLOUR_WHITE, L'A');
	sdl3_grid_put_cell(&grid, 6, 3, COLOUR_WHITE, L'Z');
	require(sdl3_grid_content_bounds(&grid, 0, 0, 8, 5, &bounds));
	eq(bounds.col, 2);
	eq(bounds.row, 1);
	eq(bounds.cols, 5);
	eq(bounds.rows, 3);
	require(sdl3_grid_content_bounds(&grid, 4, 2, 8, 8, &bounds));
	eq(bounds.col, 6);
	eq(bounds.row, 3);
	eq(bounds.cols, 1);
	eq(bounds.rows, 1);
	sdl3_grid_free(&grid);
	ok;
}

static int test_layout_configuration_torture(void *state)
{
	struct sdl3_layout layout;
	int placement;
	int request;
	int visible;
	(void)state;

	for (placement = SDL3_DOCK_BOTTOM;
			placement < SDL3_DOCK_PLACEMENT_COUNT; placement++) {
	for (visible = 0; visible <= 1; visible++) {
		for (request = -32; request <= 32; request++) {
			const struct sdl3_pane_layout *main_pane;
			const struct sdl3_pane_layout *dock;
			int expected_rows = request;
			int col;
			int row;

			if (expected_rows < SDL3_LAYOUT_MIN_DOCK_ROWS) {
				expected_rows = SDL3_LAYOUT_MIN_DOCK_ROWS;
			}
			if (expected_rows > SDL3_LAYOUT_MAX_DOCK_ROWS) {
				expected_rows = SDL3_LAYOUT_MAX_DOCK_ROWS;
			}
			sdl3_layout_configure_sized(&layout, visible != 0,
				(enum sdl3_dock_placement)placement, request, request,
				80, 24);
			main_pane = sdl3_layout_pane(&layout, SDL3_LAYOUT_MAIN_TERM);
			dock = sdl3_layout_pane(&layout, SDL3_LAYOUT_MESSAGE_TERM);
			require(main_pane != NULL);
			require(dock != NULL);
			eq(main_pane->col, 0);
			eq(main_pane->row, 0);
			eq(main_pane->cols, 80);
			eq(main_pane->rows, 24);
			eq(layout.cols, 80);
			eq(layout.rows, 24);
			eq(layout.separator_col, -1);
			eq(layout.separator_row, -1);
			if (placement == SDL3_DOCK_LEFT) {
				int expected_cols = MAX(SDL3_LAYOUT_MIN_DOCK_COLS,
					MIN(SDL3_LAYOUT_MAX_DOCK_COLS, request));

				eq(dock->col, 0);
				eq(dock->row, 0);
				eq(dock->cols, expected_cols);
				eq(dock->rows, 24);
			} else {
				eq(dock->col, 0);
				eq(dock->row, placement == SDL3_DOCK_TOP ||
					placement == SDL3_DOCK_TOP_RIGHT ? 0 :
					24 - expected_rows);
				eq(dock->cols, 80);
				eq(dock->rows, expected_rows);
			}
			for (row = -1; row <= layout.rows; row++) {
				for (col = -1; col <= layout.cols; col++) {
					int term_col = -1;
					int term_row = -1;
					bool expected = col >= 0 && col < 80 &&
						row >= 0 && row < 24;

					require(sdl3_layout_main_cell(&layout, col, row,
						&term_col, &term_row) == expected);
					if (expected) {
						eq(term_col, col);
						eq(term_row, row);
					}
				}
			}
		}
	}
	}
	ok;
}

static int test_grid_resize_torture(void *state)
{
	struct sdl3_grid grid;
	int i;
	(void)state;

	require(sdl3_grid_init(&grid, 1, 1));
	for (i = 0; i < 500; i++) {
		int cols = 1 + (i * 37) % 161;
		int rows = 1 + (i * 19) % 73;
		wchar_t glyph = (wchar_t)(L'A' + i % 26);

		require(sdl3_grid_resize(&grid, cols, rows));
		eq(grid.cols, cols);
		eq(grid.rows, rows);
		require(sdl3_grid_cell(&grid, 0, 0) != NULL);
		require(sdl3_grid_cell(&grid, cols - 1, rows - 1) != NULL);
		sdl3_grid_put_text(&grid, cols - 1, rows - 1, 1,
			COLOUR_L_GREEN, &glyph);
		eq(sdl3_grid_cell(&grid, cols - 1, rows - 1)->codepoint, glyph);
		require(sdl3_grid_cell(&grid, cols, rows - 1) == NULL);
		require(sdl3_grid_cell(&grid, cols - 1, rows) == NULL);
	}
	sdl3_grid_free(&grid);
	ok;
}

static int test_cave_messages_clear_status(void *unused)
{
	(void)unused;
	for (int density = 0; density < SDL3_INTERFACE_DENSITY_COUNT; density++) {
		int cols, rows, col, row;
		sdl3_interface_density_dimensions(density, &cols, &rows);
		for (int history = SDL3_LAYOUT_MIN_DOCK_ROWS;
				history <= SDL3_LAYOUT_MAX_DOCK_ROWS; history++) {
			for (int placement = 0; placement < SDL3_DOCK_PLACEMENT_COUNT; placement++) {
				sdl3_layout_message_offset(placement, cols,
					rows - SDL3_LAYOUT_CAVE_STATUS_ROWS, cols, history, &col, &row);
				require(row >= 0);
				require(row + history <= rows - SDL3_LAYOUT_CAVE_STATUS_ROWS);
			}
		}
	}
	ok;
}

static int test_camera_hud_insets(void *unused)
{
	struct sdl3_hud_insets inset;
	(void)unused;
	for (int history = SDL3_LAYOUT_MIN_DOCK_ROWS;
			history <= SDL3_LAYOUT_MAX_DOCK_ROWS; history++) {
		for (int placement = 0; placement < SDL3_DOCK_PLACEMENT_COUNT; placement++) {
			inset = sdl3_layout_hud_insets(SIDEBAR_LEFT, 13, 1,
				true, true, placement, history);
			eq(inset.left, 14);
			if (placement == SDL3_DOCK_TOP || placement == SDL3_DOCK_TOP_RIGHT) {
				eq(inset.top, history + 1);
				eq(inset.bottom, 0);
			} else {
				eq(inset.top, 0);
				eq(inset.bottom, history + 1);
			}
		}
	}
	/* Hidden statistics (including fishing) must not retain their clearance. */
	inset = sdl3_layout_hud_insets(SIDEBAR_LEFT, 13, 1,
		false, true, SDL3_DOCK_BOTTOM, 6);
	eq(inset.left, 0);
	eq(inset.bottom, 7);
	inset = sdl3_layout_hud_insets(SIDEBAR_LEFT, 13, 1,
		false, false, SDL3_DOCK_BOTTOM, 6);
	eq(inset.left, 0);
	eq(inset.top, 0);
	eq(inset.bottom, 0);
	/* Top messages sit below top statistics, with no overlapping text. */
	inset = sdl3_layout_hud_insets(SIDEBAR_TOP, 0, 4,
		true, true, SDL3_DOCK_TOP_RIGHT, 6);
	eq(inset.left, 0);
	eq(inset.top, 12);
	inset = sdl3_layout_hud_insets(SIDEBAR_TOP, 0, 4,
		true, false, SDL3_DOCK_TOP, 6);
	eq(inset.top, 5);
	/* Cave status is already outside the map viewport; never counted here. */
	inset = sdl3_layout_hud_insets(SIDEBAR_LEFT, 13, 1,
		true, true, SDL3_DOCK_BOTTOM, 6);
	eq(inset.bottom, 7);
	ok;
}

static int test_messages_do_not_overlap_stats(void *state)
{
	(void)state;
	for (int density = 0; density < SDL3_INTERFACE_DENSITY_COUNT; density++) {
		int cols, rows;
		sdl3_interface_density_dimensions(density, &cols, &rows);
		for (int sidebar = SIDEBAR_LEFT; sidebar <= SIDEBAR_NONE; sidebar++) {
			for (int visible = 0; visible < 2; visible++) {
				for (int cave = 0; cave < 2; cave++) {
					int reserved = cave ? SDL3_LAYOUT_CAVE_STATUS_ROWS : 0;
					for (int history = 3; history <= 12; history++) {
						for (int placement = 0; placement < SDL3_DOCK_PLACEMENT_COUNT; placement++) {
							int col, row;
							bool top = placement == SDL3_DOCK_TOP || placement == SDL3_DOCK_TOP_RIGHT;
							struct sdl3_cell_bounds area = sdl3_layout_message_area(
								cols, rows, sidebar, visible != 0, reserved, placement);
							int end = sdl3_layout_sidebar_end(rows, reserved, true, placement, history);
							eq(area.col, top && visible && sidebar == SIDEBAR_LEFT ? col_map[sidebar] + 1 : 0);
							eq(area.row, visible && sidebar == SIDEBAR_TOP ? row_top_map[sidebar] + 1 : 0);
							eq(area.col + area.cols, cols);
							eq(area.row + area.rows, rows - reserved);
							require(end > 1);
							eq(end, rows - reserved - 1 - (top ? 0 : history));
							eq(sdl3_layout_sidebar_end(rows, reserved, false, placement, history), rows - reserved - 1);
							sdl3_layout_message_offset(placement, area.cols, area.rows,
								area.cols, history, &col, &row);
							require(col >= 0 && col + area.cols <= area.cols);
							require(row >= 0 && row + history <= area.rows);
							if (!top) {
								eq(col + area.col, 0);
								eq(end + 1, row + area.row);
							}
						}
					}
				}
			}
		}
	}
	ok;
}

const char *suite_name = "sdl3/layout";
struct test tests[] = {
	{ "messages clear statistics at every size and placement", test_messages_do_not_overlap_stats },
	{ "camera clears all HUD placements", test_camera_hud_insets },
	{ "cave messages clear status", test_cave_messages_clear_status },
	{ "default layout", test_default_layout },
	{ "main cell mapping", test_main_cell_mapping },
	{ "configurable layout", test_configurable_layout },
	{ "left and top overlay layout", test_left_and_top_overlay_layout },
	{ "interface density presets", test_interface_density_presets },
	{ "message overlay contract", test_message_overlay_contract },
	{ "top right messages", test_top_right_messages },
	{ "dirty cells", test_dirty_cells },
	{ "grid content bounds", test_grid_content_bounds },
	{ "layout configuration torture", test_layout_configuration_torture },
	{ "grid resize torture", test_grid_resize_torture },
	{ NULL, NULL },
};
