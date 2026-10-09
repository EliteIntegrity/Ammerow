/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/zoom.c */
/* Stress the platform-independent zoom camera over extreme geometries. */

#include "unit-test.h"

#include "sdl3/zoom.h"

#include <math.h>

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

static int test_camera_geometry_torture(void *state)
{
	static const int first_cells[] = { -20, 0, 13 };
	static const int cell_counts[] = { 1, 2, 3, 5, 22, 66, 200 };
	static const float viewports[] = { 1.0f, 31.0f, 640.0f, 2112.0f,
		8192.0f };
	static const float cell_sizes[] = { 1.0f, 7.0f, 16.0f, 32.0f, 64.0f,
		256.0f };
	int first_index;
	int count_index;
	int viewport_index;
	int size_index;
	(void)state;

	for (first_index = 0; first_index < (int)N_ELEMENTS(first_cells);
			first_index++) {
		for (count_index = 0; count_index < (int)N_ELEMENTS(cell_counts);
				count_index++) {
			int first = first_cells[first_index];
			int count = cell_counts[count_index];
			int focuses[] = {
				first - 3, first, first + count / 2,
				first + count - 1, first + count + 3
			};

			for (viewport_index = 0;
					viewport_index < (int)N_ELEMENTS(viewports);
					viewport_index++) {
				for (size_index = 0;
						size_index < (int)N_ELEMENTS(cell_sizes);
						size_index++) {
					int focus_index;

					for (focus_index = 0;
							focus_index < (int)N_ELEMENTS(focuses);
							focus_index++) {
						float viewport = viewports[viewport_index];
						float cell_size = cell_sizes[size_index];
						float center = sdl3_zoom_camera_center(first, count,
							focuses[focus_index], viewport, cell_size);
						float half_cells = viewport / (cell_size * 2.0f);
						int cell;

						require(isfinite(center));
						if (half_cells * 2.0f >= count) {
							require(fabsf(center - (first + count * 0.5f)) <
								0.001f);
						} else {
							require(center >= first + half_cells - 0.001f);
							require(center <= first + count - half_cells +
								0.001f);
						}
						for (cell = first; cell < first + count; cell++) {
							const float viewport_start = 37.0f;
							float pixel = viewport_start + viewport * 0.5f +
								(cell - center + 0.5f) * cell_size;

							if (pixel < viewport_start ||
									pixel >= viewport_start + viewport) {
								continue;
							}
							eq(sdl3_zoom_cell_from_pixel(pixel, viewport_start,
								viewport, cell_size, center), cell);
						}
					}
				}
			}
		}
	}
	ok;
}

static int test_panel_shift_torture(void *state)
{
	static const int zoom_levels[] = {
		125, 150, 200, 250, 300, 400, 500, 600, 800
	};
	static const int player_cells[] = { 10, 100, 1000 };
	const int first = 13;
	const int count = 66;
	const float viewport = 2112.0f;
	int zoom_index;
	int player_index;
	(void)state;

	for (zoom_index = 0; zoom_index < (int)N_ELEMENTS(zoom_levels);
			zoom_index++) {
		int margin = sdl3_zoom_panel_margin(count, zoom_levels[zoom_index]);
		float cell_size = 32.0f * zoom_levels[zoom_index] / 100.0f;

		for (player_index = 0;
				player_index < (int)N_ELEMENTS(player_cells);
				player_index++) {
			int player_cell = player_cells[player_index];
			int old_offset = player_cell - (count - margin);
			int new_offset = player_cell - count / 2;
			float old_center = sdl3_zoom_camera_center(first, count,
				first + player_cell - old_offset, viewport, cell_size);
			float new_center = sdl3_zoom_camera_center(first, count,
				first + player_cell - new_offset, viewport, cell_size);
			int delta;

			for (delta = -12; delta <= 12; delta++) {
				float old_relative = first + player_cell + delta -
					old_offset - old_center;
				float new_relative = first + player_cell + delta -
					new_offset - new_center;

				require(fabsf(old_relative - new_relative) < 0.001f);
			}
		}
	}
	ok;
}

static int test_panel_margin_torture(void *state)
{
	static const int zoom_levels[] = {
		125, 150, 200, 250, 300, 400, 500, 600, 800
	};
	int cells;
	int zoom_index;
	(void)state;

	for (cells = 1; cells <= 300; cells++) {
		for (zoom_index = 0; zoom_index < (int)N_ELEMENTS(zoom_levels);
				zoom_index++) {
			int margin = sdl3_zoom_panel_margin(cells,
				zoom_levels[zoom_index]);

			require(margin >= 0);
			require(margin <= cells / 2);
			if (cells >= 6) require(margin >= 3);
		}
	}
	ok;
}

static int test_expanded_source_geometry(void *state)
{
	int player_cell;
	const int source_cells = sdl3_zoom_visible_cells(66, 100);
	const float viewport = 66.0f * 32.0f;

	(void)state;

	eq(source_cells, 68);
	eq(sdl3_zoom_visible_cells(66, 80), 85);
	eq(sdl3_zoom_visible_cells(66, 67), 101);
	eq(sdl3_zoom_visible_cells(66, 50), 134);
	eq(sdl3_zoom_visible_cells(0, 50), 0);
	eq(sdl3_zoom_centered_start(100, 134, 198), 33);
	eq(sdl3_zoom_centered_start(2, 134, 198), 0);
	eq(sdl3_zoom_centered_start(197, 134, 198), 64);
	eq(sdl3_zoom_centered_start(10, 200, 198), 0);
	/* The two normal-zoom guard cells leave exactly enough source around the
	 * viewport for the camera to follow every interior player step. */
	for (player_cell = 34; player_cell <= 163; player_cell++) {
		int first = sdl3_zoom_centered_start(player_cell, source_cells, 198);
		float center = sdl3_zoom_camera_center(0, source_cells,
			player_cell - first, viewport, 32.0f);

		require(fabsf(first + center - (player_cell + 0.5f)) < 0.001f);
	}
	ok;
}

static int test_source_grid_round_trip(void *state)
{
	static const int tile_spans[] = { 1, 2, 4 };
	int expanded;
	int span_index;
	int grid;

	(void)state;
	/* The expanded camera origin, not the terminal panel origin, owns map-art
	 * placement. This is the regression that previously dropped monsters from
	 * enlarged maps when the two origins diverged. */
	eq(sdl3_zoom_source_cell_for_grid(105, 100, 0, 1, true), 5);
	for (expanded = 0; expanded <= 1; expanded++) {
		for (span_index = 0;
				span_index < (int)N_ELEMENTS(tile_spans); span_index++) {
			for (grid = 90; grid <= 110; grid++) {
				int source = sdl3_zoom_source_cell_for_grid(grid, 87, 13,
					tile_spans[span_index], expanded != 0);

				eq(sdl3_zoom_grid_cell_for_source(source, 13, 87,
					tile_spans[span_index], expanded != 0), grid);
			}
		}
	}
	ok;
}

static int test_pixel_art_uses_square_native_multiples(void *state)
{
	(void)state;

	eq(sdl3_zoom_pixel_art_size(20, 50, 16), 10);
	eq(sdl3_zoom_pixel_art_size(20, 80, 16), 16);
	eq(sdl3_zoom_pixel_art_size(20, 100, 16), 16);
	eq(sdl3_zoom_pixel_art_size(20, 125, 16), 32);
	eq(sdl3_zoom_pixel_art_size(20, 200, 16), 48);
	eq(sdl3_zoom_pixel_art_size(0, 100, 16), 1);
	ok;
}

const char *suite_name = "sdl3/zoom";
static int test_hud_safe_camera(void *state)
{
	int focus, zoom;
	(void)state;
	for (zoom = 50; zoom <= 800; zoom += 25) {
		float cell = 10.0f * zoom / 100.0f;
		for (focus = 0; focus < 200; focus++) {
			float ordinary = sdl3_zoom_camera_center(0, 200, focus, 1200.0f, cell);
			float center = sdl3_zoom_safe_center(ordinary, focus,
				1200.0f, cell, 160.0f, 20.0f);
			float left = 600.0f + (focus - center) * cell;
			require(left >= 159.99f);
			require(left + cell <= 1180.01f);
			eq(sdl3_zoom_cell_from_pixel(left + cell * 0.5f,
				0.0f, 1200.0f, cell, center), focus);
			require(fabsf(center - sdl3_zoom_safe_center(center, focus,
				1200.0f, cell, 160.0f, 20.0f)) < 0.001f);
		}
	}
	require(isfinite(sdl3_zoom_safe_center(0.5f, 0, 100.0f, 200.0f, 60.0f, 0.0f)));
	ok;
}

struct test tests[] = {
	{ "HUD safe camera and mouse round trip", test_hud_safe_camera },
	{ "camera geometry torture", test_camera_geometry_torture },
	{ "panel shift torture", test_panel_shift_torture },
	{ "panel margin torture", test_panel_margin_torture },
	{ "expanded source geometry", test_expanded_source_geometry },
	{ "source/grid round trip", test_source_grid_round_trip },
	{ "pixel-art native multiples", test_pixel_art_uses_square_native_multiples },
	{ NULL, NULL },
};
