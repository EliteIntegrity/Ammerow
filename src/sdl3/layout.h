/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/layout.h
 * \brief Full-size playfield and message overlay layout for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_LAYOUT_H
#define INCLUDED_SDL3_LAYOUT_H

#include "angband.h"
#include "ui-term.h"

#define SDL3_LAYOUT_MAIN_TERM 0
#define SDL3_LAYOUT_MESSAGE_TERM 1
#define SDL3_LAYOUT_PANE_COUNT 2
#define SDL3_LAYOUT_DEFAULT_DOCK_ROWS 6
#define SDL3_LAYOUT_MIN_DOCK_ROWS 3
#define SDL3_LAYOUT_MAX_DOCK_ROWS 12
#define SDL3_LAYOUT_DEFAULT_DOCK_COLS 32
#define SDL3_LAYOUT_MIN_DOCK_COLS 20
#define SDL3_LAYOUT_MAX_DOCK_COLS 48
#define SDL3_LAYOUT_LARGE_COLS 80
#define SDL3_LAYOUT_LARGE_ROWS 24
#define SDL3_LAYOUT_CAVE_STATUS_ROWS 3

enum sdl3_interface_density {
	SDL3_INTERFACE_LARGE = 0,
	SDL3_INTERFACE_COMFORTABLE,
	SDL3_INTERFACE_STANDARD,
	SDL3_INTERFACE_COMPACT,
	SDL3_INTERFACE_DENSITY_COUNT
};

#define SDL3_INTERFACE_DENSITY_DEFAULT SDL3_INTERFACE_STANDARD

enum sdl3_dock_placement {
	SDL3_DOCK_BOTTOM = 0,
	/* Reserved for settings migration and the Ctrl+2 input signal.  Runtime
	 * message placement normalizes it to bottom; player stats are independent. */
	SDL3_DOCK_LEFT,
	SDL3_DOCK_TOP,
	SDL3_DOCK_TOP_RIGHT,
	SDL3_DOCK_PLACEMENT_COUNT
};

struct sdl3_pane_layout {
	int term_index;
	int col;
	int row;
	int cols;
	int rows;
};

struct sdl3_layout {
	struct sdl3_pane_layout panes[SDL3_LAYOUT_PANE_COUNT];
	int separator_col;
	int separator_row;
	int cols;
	int rows;
};

void sdl3_layout_init(struct sdl3_layout *layout);
void sdl3_layout_configure(struct sdl3_layout *layout, bool dock_visible,
		int dock_rows);
void sdl3_layout_configure_placed(struct sdl3_layout *layout,
		bool dock_visible, enum sdl3_dock_placement placement, int dock_rows,
		int dock_cols);
void sdl3_layout_configure_sized(struct sdl3_layout *layout,
		bool dock_visible, enum sdl3_dock_placement placement, int dock_rows,
		int dock_cols, int main_cols, int main_rows);
const char *sdl3_interface_density_name(enum sdl3_interface_density density);
enum sdl3_interface_density sdl3_interface_density_change(
		enum sdl3_interface_density density, int delta);
void sdl3_interface_density_dimensions(enum sdl3_interface_density density,
		int *cols, int *rows);
const struct sdl3_pane_layout *sdl3_layout_pane(
		const struct sdl3_layout *layout, int term_index);
bool sdl3_layout_main_cell(const struct sdl3_layout *layout, int composite_col,
		int composite_row, int *term_col, int *term_row);
const char *sdl3_dock_placement_name(enum sdl3_dock_placement placement);
enum sdl3_dock_placement sdl3_dock_placement_change(
		enum sdl3_dock_placement placement, int delta);
void sdl3_layout_message_offset(enum sdl3_dock_placement placement,
		int cols, int rows, int content_end_col, int history_rows,
		int *col, int *row);

#endif /* INCLUDED_SDL3_LAYOUT_H */
