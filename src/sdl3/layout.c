/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/layout.c
 * \brief Full-size playfield and message overlay layout for the SDL3 frontend.
 *
 *
 */

#include "sdl3/layout.h"

struct sdl3_interface_density_preset {
	const char *name;
	int cols;
	int rows;
};

static const struct sdl3_interface_density_preset density_presets[] = {
	{ "Large (80 x 24)", 80, 24 },
	{ "Comfortable (100 x 30)", 100, 30 },
	{ "Standard (120 x 36)", 120, 36 },
	{ "Compact (140 x 42)", 140, 42 }
};

/* Selection order and labels share one authority; enum values are persisted
 * and must not be renumbered. The obsolete Left value remains migration-only. */
static const struct {
	enum sdl3_dock_placement placement;
	const char *name;
} message_positions[] = {
	{ SDL3_DOCK_TOP, "Top" },
	{ SDL3_DOCK_TOP_RIGHT, "Top right" },
	{ SDL3_DOCK_BOTTOM, "Bottom" }
};

void sdl3_layout_init(struct sdl3_layout *layout)
{
	sdl3_layout_configure(layout, true, SDL3_LAYOUT_DEFAULT_DOCK_ROWS);
}

struct sdl3_hud_insets sdl3_layout_hud_insets(int sidebar, int map_col,
		int map_row, bool stats_visible, bool messages_visible,
		enum sdl3_dock_placement placement, int message_rows)
{
	struct sdl3_hud_insets result = { 0 };

	/* One interface cell also clears the panels' padded backplates. */
	if (stats_visible) {
		if (sidebar == SIDEBAR_LEFT) result.left = MAX(0, map_col) + 1;
		if (sidebar == SIDEBAR_TOP) result.top = MAX(0, map_row) + 1;
	}
	if (messages_visible && message_rows > 0) {
		int rows = MIN(SDL3_LAYOUT_MAX_DOCK_ROWS, message_rows) + 1;
		if (placement == SDL3_DOCK_TOP || placement == SDL3_DOCK_TOP_RIGHT)
			result.top += rows;
		else
			result.bottom = rows;
	}
	return result;
}

void sdl3_layout_configure(struct sdl3_layout *layout, bool dock_visible,
		int dock_rows)
{
	sdl3_layout_configure_placed(layout, dock_visible, SDL3_DOCK_BOTTOM,
		dock_rows, SDL3_LAYOUT_DEFAULT_DOCK_COLS);
}

void sdl3_layout_configure_placed(struct sdl3_layout *layout,
		bool dock_visible, enum sdl3_dock_placement placement, int dock_rows,
		int dock_cols)
{
	sdl3_layout_configure_sized(layout, dock_visible, placement, dock_rows,
		dock_cols, SDL3_LAYOUT_LARGE_COLS, SDL3_LAYOUT_LARGE_ROWS);
}

void sdl3_layout_configure_sized(struct sdl3_layout *layout,
		bool dock_visible, enum sdl3_dock_placement placement, int dock_rows,
		int dock_cols, int main_cols, int main_rows)
{
	if (!layout) return;
	main_cols = MAX(SDL3_LAYOUT_LARGE_COLS, main_cols);
	main_rows = MAX(SDL3_LAYOUT_LARGE_ROWS, main_rows);
	dock_rows = MAX(SDL3_LAYOUT_MIN_DOCK_ROWS,
		MIN(SDL3_LAYOUT_MAX_DOCK_ROWS, dock_rows));
	dock_cols = MAX(SDL3_LAYOUT_MIN_DOCK_COLS,
		MIN(SDL3_LAYOUT_MAX_DOCK_COLS, dock_cols));
	if (placement < 0 || placement >= SDL3_DOCK_PLACEMENT_COUNT) {
		placement = SDL3_DOCK_BOTTOM;
	}
	memset(layout, 0, sizeof(*layout));
	layout->panes[0] = (struct sdl3_pane_layout) {
		SDL3_LAYOUT_MAIN_TERM, 0, 0, main_cols, main_rows
	};
	layout->separator_col = -1;
	layout->separator_row = -1;
	layout->cols = main_cols;
	layout->rows = main_rows;
	if (placement == SDL3_DOCK_LEFT) {
		layout->panes[1] = (struct sdl3_pane_layout) {
			SDL3_LAYOUT_MESSAGE_TERM, 0, 0, dock_cols, main_rows
		};
	} else if (placement == SDL3_DOCK_TOP || placement == SDL3_DOCK_TOP_RIGHT) {
		layout->panes[1] = (struct sdl3_pane_layout) {
			SDL3_LAYOUT_MESSAGE_TERM, 0, 0, main_cols, dock_rows
		};
	} else {
		layout->panes[1] = (struct sdl3_pane_layout) {
			SDL3_LAYOUT_MESSAGE_TERM, 0, main_rows - dock_rows,
			main_cols, dock_rows
		};
	}
	(void)dock_visible;
}

const char *sdl3_interface_density_name(enum sdl3_interface_density density)
{
	if (density < 0 || density >= SDL3_INTERFACE_DENSITY_COUNT) {
		density = SDL3_INTERFACE_DENSITY_DEFAULT;
	}
	return density_presets[density].name;
}

enum sdl3_interface_density sdl3_interface_density_change(
		enum sdl3_interface_density density, int delta)
{
	int changed = density;

	if (changed < 0 || changed >= SDL3_INTERFACE_DENSITY_COUNT) {
		changed = SDL3_INTERFACE_DENSITY_DEFAULT;
	}
	/* Presets cycle for mouse activation as well as keyboard navigation. */
	if (delta > 0) changed = (changed + 1) % SDL3_INTERFACE_DENSITY_COUNT;
	if (delta < 0) changed = (changed + SDL3_INTERFACE_DENSITY_COUNT - 1) %
		SDL3_INTERFACE_DENSITY_COUNT;
	return (enum sdl3_interface_density)changed;
}

void sdl3_interface_density_dimensions(enum sdl3_interface_density density,
		int *cols, int *rows)
{
	if (density < 0 || density >= SDL3_INTERFACE_DENSITY_COUNT) {
		density = SDL3_INTERFACE_DENSITY_DEFAULT;
	}
	if (cols) *cols = density_presets[density].cols;
	if (rows) *rows = density_presets[density].rows;
}

const struct sdl3_pane_layout *sdl3_layout_pane(
		const struct sdl3_layout *layout, int term_index)
{
	int i;

	if (!layout) return NULL;
	for (i = 0; i < SDL3_LAYOUT_PANE_COUNT; i++) {
		if (layout->panes[i].term_index == term_index) {
			return &layout->panes[i];
		}
	}
	return NULL;
}

bool sdl3_layout_main_cell(const struct sdl3_layout *layout, int composite_col,
		int composite_row, int *term_col, int *term_row)
{
	const struct sdl3_pane_layout *main_pane = sdl3_layout_pane(layout,
		SDL3_LAYOUT_MAIN_TERM);

	if (!main_pane || !term_col || !term_row ||
			composite_col < main_pane->col ||
			composite_col >= main_pane->col + main_pane->cols ||
			composite_row < main_pane->row ||
			composite_row >= main_pane->row + main_pane->rows) {
		return false;
	}
	*term_col = composite_col - main_pane->col;
	*term_row = composite_row - main_pane->row;
	return true;
}

const char *sdl3_dock_placement_name(enum sdl3_dock_placement placement)
{
	for (size_t i = 0; i < N_ELEMENTS(message_positions); i++) {
		if (message_positions[i].placement == placement)
			return message_positions[i].name;
	}
	return placement == SDL3_DOCK_LEFT ? "Left" : "Bottom";
}

enum sdl3_dock_placement sdl3_dock_placement_change(
		enum sdl3_dock_placement placement, int delta)
{
	int count = (int)N_ELEMENTS(message_positions);
	int index;

	for (index = 0; index < count; index++) {
		if (message_positions[index].placement == placement) break;
	}
	if (index == count) index = count - 1; /* Invalid/legacy => Bottom. */
	index = (index + (delta < 0 ? -1 : delta > 0 ? 1 : 0) + count) % count;
	return message_positions[index].placement;
}

/* Position the whole text block, not each line, inside its available area.
 * Wrapping uses the width of the available area for that placement.
 * content_end_col is exclusive, including any leading indentation. */
void sdl3_layout_message_offset(enum sdl3_dock_placement placement,
		int cols, int rows, int content_end_col, int history_rows,
		int *col, int *row)
{
	if (col) *col = placement == SDL3_DOCK_TOP_RIGHT ?
		MAX(0, cols - MAX(0, content_end_col)) : 0;
	if (row) *row = placement == SDL3_DOCK_TOP ||
		placement == SDL3_DOCK_TOP_RIGHT ? 0 : MAX(0, rows - history_rows);
}

struct sdl3_cell_bounds sdl3_layout_message_area(int cols, int rows,
		int sidebar, bool stats_visible, int reserved_bottom_rows,
		enum sdl3_dock_placement placement)
{
	struct sdl3_cell_bounds area = { 0, 0, MAX(1, cols), MAX(1, rows) };
	bool top = placement == SDL3_DOCK_TOP || placement == SDL3_DOCK_TOP_RIGHT;
	if (top && stats_visible && sidebar == SIDEBAR_LEFT)
		area.col = MIN(area.cols - 1, col_map[sidebar] + 1);
	if (stats_visible && sidebar == SIDEBAR_TOP)
		area.row = MIN(area.rows - 1, row_top_map[sidebar] + 1);
	area.cols -= area.col;
	area.rows = MAX(1, area.rows - area.row - MAX(0, reserved_bottom_rows));
	return area;
}

int sdl3_layout_sidebar_end(int rows, int reserved_bottom_rows,
		bool messages_visible, enum sdl3_dock_placement placement,
		int message_rows)
{
	int end = rows - MAX(0, reserved_bottom_rows);
	if (messages_visible && placement != SDL3_DOCK_TOP &&
			placement != SDL3_DOCK_TOP_RIGHT)
		end -= MIN(SDL3_LAYOUT_MAX_DOCK_ROWS, MAX(0, message_rows));
	return MAX(2, end - 1);
}
