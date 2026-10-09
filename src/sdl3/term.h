/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/term.h
 * \brief Character-cell storage for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_TERM_H
#define INCLUDED_SDL3_TERM_H

#include "angband.h"

struct sdl3_cell {
	wchar_t codepoint;
	uint8_t foreground;
	uint8_t background;
	bool dirty;
};

struct sdl3_grid {
	struct sdl3_cell *cells;
	int cols;
	int rows;
	bool dirty;
};

struct sdl3_cell_bounds {
	int col;
	int row;
	int cols;
	int rows;
};

bool sdl3_grid_init(struct sdl3_grid *grid, int cols, int rows);
bool sdl3_grid_resize(struct sdl3_grid *grid, int cols, int rows);
void sdl3_grid_free(struct sdl3_grid *grid);
void sdl3_grid_clear(struct sdl3_grid *grid);
void sdl3_grid_fill(struct sdl3_grid *grid, int col, int row, int cols,
		int rows, wchar_t codepoint, int attr);
void sdl3_grid_wipe(struct sdl3_grid *grid, int col, int row, int n);
void sdl3_grid_put_text(struct sdl3_grid *grid, int col, int row, int n,
		int attr, const wchar_t *text);
void sdl3_grid_put_cell(struct sdl3_grid *grid, int col, int row, int attr,
		wchar_t codepoint);
const struct sdl3_cell *sdl3_grid_cell(const struct sdl3_grid *grid,
		int col, int row);
bool sdl3_grid_content_bounds(const struct sdl3_grid *grid, int col, int row,
		int cols, int rows, struct sdl3_cell_bounds *bounds);
void sdl3_grid_mark_dirty(struct sdl3_grid *grid);
void sdl3_grid_mark_all_dirty(struct sdl3_grid *grid);
void sdl3_grid_mark_clean(struct sdl3_grid *grid);

#endif /* INCLUDED_SDL3_TERM_H */
