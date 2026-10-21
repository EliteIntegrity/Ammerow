/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/zoom.c
 * \brief Bounded play-area zoom for the SDL3 character display.
 *
 *
 */

#include "sdl3/zoom.h"

#include <limits.h>
#include <math.h>

static const int zoom_levels[] = {
	50, 67, 80, 100, 125, 150, 200, 250, 300, 400, 500, 600, 800
};

int sdl3_zoom_clamp(int percent)
{
	if (percent < SDL3_ZOOM_MIN) return SDL3_ZOOM_MIN;
	if (percent > SDL3_ZOOM_MAX) return SDL3_ZOOM_MAX;
	return percent;
}

int sdl3_zoom_change(int percent, int direction)
{
	int i;

	percent = sdl3_zoom_clamp(percent);
	if (direction > 0) {
		for (i = 0; i < (int)(sizeof(zoom_levels) /
				sizeof(zoom_levels[0])); i++) {
			if (zoom_levels[i] > percent) return zoom_levels[i];
		}
		return SDL3_ZOOM_MAX;
	}
	if (direction < 0) {
		for (i = (int)(sizeof(zoom_levels) / sizeof(zoom_levels[0])) - 1;
				i >= 0; i--) {
			if (zoom_levels[i] < percent) return zoom_levels[i];
		}
		return SDL3_ZOOM_MIN;
	}
	return percent;
}

int sdl3_zoom_panel_margin(int cell_count, int percent)
{
	int denominator;
	int visible_half;
	int maximum;

	if (cell_count <= 0 || percent <= SDL3_ZOOM_DEFAULT) return 3;
	denominator = percent * 2;
	/* Include half a zoomed cell so the focused glyph's center, rather than
	 * merely its leading edge, remains inside the camera's unclamped range
	 * when Angband changes its backing panel. */
	visible_half = (cell_count * 100 + percent + denominator - 1) /
		denominator;
	if (visible_half < 3) visible_half = 3;
	maximum = cell_count / 2;
	return visible_half < maximum ? visible_half : maximum;
}

int sdl3_zoom_visible_cells(int viewport_cells, int percent)
{
	int result;

	if (viewport_cells <= 0) return 0;
	percent = sdl3_zoom_clamp(percent);
	result = (viewport_cells * 100 + percent - 1) / percent;
	/* A guard cell at each edge absorbs integer cell-size rounding and keeps
	 * the camera independent from the underlying terminal panel.  At normal
	 * zoom those guards are what permit per-tile player following. */
	if (percent <= SDL3_ZOOM_DEFAULT) result += 2;
	return result;
}

int sdl3_zoom_square_size(int base_width, int base_height, int percent)
{
	double side;
	if (base_width < 1) base_width = 1;
	if (base_height < 1) base_height = 1;
	side = sqrt((double)base_width * base_height) * sdl3_zoom_clamp(percent) / 100.0;
	if (side >= INT_MAX) return INT_MAX;
	return side < 1.0 ? 1 : (int)(side + 0.5);
}

int sdl3_zoom_square_visible_cells(int viewport_cells, int base_axis_size,
		int square_size)
{
	double span;
	if (viewport_cells <= 0 || base_axis_size <= 0 || square_size <= 0) return 0;
	/* Cover the physical square-cell viewport, not the old terminal row count.
	 * Guards cover fractional camera alignment. */
	span = ceil((double)viewport_cells * base_axis_size / square_size) + 2;
	return span >= INT_MAX ? INT_MAX : (int)span;
}

int sdl3_zoom_pixel_art_size(int base_cell_height, int percent,
		int native_art_size)
{
	long long scaled;

	if (base_cell_height < 1) base_cell_height = 1;
	percent = sdl3_zoom_clamp(percent);
	scaled = ((long long)base_cell_height * percent + 50) / 100;
	if (scaled < 1) scaled = 1;
	/* Magnified pixel art stays on a whole native-cell multiple. Shrinking is
	 * allowed to remain continuous so zoom-out can still show a whole level. */
	if (native_art_size > 0 && scaled >= native_art_size) {
		long long multiple = (scaled + native_art_size / 2) / native_art_size;

		if (multiple < 1) multiple = 1;
		scaled = multiple * native_art_size;
	}
	return scaled > INT_MAX ? INT_MAX : (int)scaled;
}

int sdl3_zoom_centered_start(int focus_cell, int cell_count, int limit)
{
	int start;

	if (cell_count <= 0 || limit <= cell_count) return 0;
	start = focus_cell - cell_count / 2;
	if (start < 0) return 0;
	if (start > limit - cell_count) return limit - cell_count;
	return start;
}

int sdl3_zoom_source_cell_for_grid(int grid_cell, int dungeon_first,
		int source_first, int tile_span, bool expanded_source)
{
	int span = expanded_source || tile_span < 1 ? 1 : tile_span;

	return source_first + (grid_cell - dungeon_first) * span;
}

int sdl3_zoom_grid_cell_for_source(int source_cell, int source_first,
		int dungeon_first, int tile_span, bool expanded_source)
{
	int span = expanded_source || tile_span < 1 ? 1 : tile_span;

	return dungeon_first + (source_cell - source_first) / span;
}

float sdl3_zoom_camera_center(int first_cell, int cell_count, int focus_cell,
		float viewport_size, float cell_size)
{
	float half_cells;
	float minimum;
	float maximum;
	float center = focus_cell + 0.5f;

	if (cell_count <= 0 || viewport_size <= 0.0f || cell_size <= 0.0f) {
		return center;
	}
	half_cells = viewport_size / (cell_size * 2.0f);
	minimum = first_cell + half_cells;
	maximum = first_cell + cell_count - half_cells;
	if (minimum > maximum) return first_cell + cell_count * 0.5f;
	if (center < minimum) return minimum;
	if (center > maximum) return maximum;
	return center;
}

int sdl3_zoom_cell_from_pixel(float pixel, float viewport_start,
		float viewport_size, float cell_size, float camera_center)
{
	if (cell_size <= 0.0f) return 0;
	return (int)floorf(camera_center +
		(pixel - viewport_start - viewport_size * 0.5f) / cell_size);
}

float sdl3_zoom_safe_center(float center, int focus_cell, float viewport_size,
		float cell_size, float leading_inset, float trailing_inset)
{
	float position, minimum, maximum;
	if (viewport_size <= 0.0f || cell_size <= 0.0f) return center;
	if (leading_inset < 0.0f) leading_inset = 0.0f;
	if (trailing_inset < 0.0f) trailing_inset = 0.0f;
	minimum = leading_inset + cell_size * 0.5f;
	maximum = viewport_size - trailing_inset - cell_size * 0.5f;
	position = viewport_size * 0.5f + (focus_cell + 0.5f - center) * cell_size;
	/* At extreme zoom a whole cell may not fit. Centre it in the remaining
	 * space rather than oscillating between incompatible edge constraints. */
	if (minimum > maximum) return center +
		(position - (leading_inset + viewport_size - trailing_inset) * 0.5f) / cell_size;
	if (position < minimum) return center - (minimum - position) / cell_size;
	if (position > maximum) return center + (position - maximum) / cell_size;
	return center;
}

float sdl3_zoom_safe_focus_center(float center, int focus_cell,
		float viewport_size, float cell_size, float leading_inset,
		float trailing_inset)
{
	/* Three clear map cells make an edge approach readable. Limit the margin
	 * to a quarter of the usable span so large tiles cannot squeeze the focus
	 * out of the remaining space. The whole map still draws behind the HUD. */
	const float nearby_cells = 3.0f;
	float available, margin;
	if (viewport_size <= 0.0f || cell_size <= 0.0f) return center;
	leading_inset = fmaxf(0.0f, leading_inset);
	trailing_inset = fmaxf(0.0f, trailing_inset);
	available = fmaxf(0.0f, viewport_size - leading_inset - trailing_inset - cell_size);
	margin = fminf(nearby_cells * cell_size, available * 0.25f);
	return sdl3_zoom_safe_center(center, focus_cell, viewport_size, cell_size,
		leading_inset + margin, trailing_inset + margin);
}
