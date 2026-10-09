/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/term.c
 * \brief Character-cell storage for the SDL3 frontend.
 *
 *
 */

#include "sdl3/term.h"

static struct sdl3_cell *grid_cell(struct sdl3_grid *grid, int col, int row)
{
	if (!grid || !grid->cells || col < 0 || col >= grid->cols || row < 0 ||
			row >= grid->rows) {
		return NULL;
	}

	return &grid->cells[row * grid->cols + col];
}

static uint8_t background_from_attr(int attr)
{
	switch (attr / MULT_BG) {
	case BG_SAME:
		return attr % MAX_COLORS;
	case BG_DARK:
		return COLOUR_SHADE;
	case BG_BLACK:
	default:
		return COLOUR_DARK;
	}
}

static bool set_cell(struct sdl3_cell *cell, wchar_t codepoint, int attr)
{
	uint8_t foreground = attr % MAX_COLORS;
	uint8_t background = background_from_attr(attr);

	if (cell->codepoint == codepoint && cell->foreground == foreground &&
			cell->background == background) {
		return false;
	}
	cell->codepoint = codepoint;
	cell->foreground = foreground;
	cell->background = background;
	cell->dirty = true;
	return true;
}

bool sdl3_grid_init(struct sdl3_grid *grid, int cols, int rows)
{
	if (!grid || cols <= 0 || rows <= 0) return false;

	memset(grid, 0, sizeof(*grid));
	grid->cells = mem_zalloc((size_t)cols * rows * sizeof(*grid->cells));
	if (!grid->cells) return false;

	grid->cols = cols;
	grid->rows = rows;
	sdl3_grid_clear(grid);
	return true;
}

bool sdl3_grid_resize(struct sdl3_grid *grid, int cols, int rows)
{
	struct sdl3_grid replacement;
	int copy_cols;
	int copy_rows;
	int col;
	int row;

	if (!grid || !grid->cells || cols <= 0 || rows <= 0) return false;
	if (grid->cols == cols && grid->rows == rows) return true;
	if (!sdl3_grid_init(&replacement, cols, rows)) return false;
	copy_cols = MIN(grid->cols, replacement.cols);
	copy_rows = MIN(grid->rows, replacement.rows);
	for (row = 0; row < copy_rows; row++) {
		for (col = 0; col < copy_cols; col++) {
			*grid_cell(&replacement, col, row) =
				*grid_cell(grid, col, row);
		}
	}
	mem_free(grid->cells);
	*grid = replacement;
	sdl3_grid_mark_all_dirty(grid);
	return true;
}

void sdl3_grid_free(struct sdl3_grid *grid)
{
	if (!grid) return;

	mem_free(grid->cells);
	memset(grid, 0, sizeof(*grid));
}

void sdl3_grid_clear(struct sdl3_grid *grid)
{
	if (!grid || !grid->cells) return;
	sdl3_grid_fill(grid, 0, 0, grid->cols, grid->rows, L' ', COLOUR_WHITE);
}

void sdl3_grid_fill(struct sdl3_grid *grid, int col, int row, int cols,
		int rows, wchar_t codepoint, int attr)
{
	bool changed = false;
	int x;
	int y;

	if (!grid || !grid->cells || cols <= 0 || rows <= 0) return;

	for (y = 0; y < rows; y++) {
		for (x = 0; x < cols; x++) {
			struct sdl3_cell *cell = grid_cell(grid, col + x, row + y);

			if (cell && set_cell(cell, codepoint, attr)) {
				changed = true;
			}
		}
	}
	if (changed) grid->dirty = true;
}

void sdl3_grid_wipe(struct sdl3_grid *grid, int col, int row, int n)
{
	if (!grid || !grid->cells || n <= 0) return;
	sdl3_grid_fill(grid, col, row, n, 1, L' ', COLOUR_WHITE);
}

void sdl3_grid_put_text(struct sdl3_grid *grid, int col, int row, int n,
		int attr, const wchar_t *text)
{
	bool changed = false;
	int i;

	if (!grid || !grid->cells || !text || n <= 0) return;

	for (i = 0; i < n; i++) {
		struct sdl3_cell *cell = grid_cell(grid, col + i, row);

		if (cell && set_cell(cell, text[i], attr)) changed = true;
	}
	if (changed) grid->dirty = true;
}

void sdl3_grid_put_cell(struct sdl3_grid *grid, int col, int row, int attr,
		wchar_t codepoint)
{
	struct sdl3_cell *cell = grid_cell(grid, col, row);

	if (cell && set_cell(cell, codepoint, attr)) grid->dirty = true;
}

const struct sdl3_cell *sdl3_grid_cell(const struct sdl3_grid *grid,
		int col, int row)
{
	if (!grid || !grid->cells || col < 0 || col >= grid->cols || row < 0 ||
			row >= grid->rows) {
		return NULL;
	}

	return &grid->cells[row * grid->cols + col];
}

bool sdl3_grid_content_bounds(const struct sdl3_grid *grid, int col, int row,
		int cols, int rows, struct sdl3_cell_bounds *bounds)
{
	int first_col;
	int first_row;
	int last_col;
	int last_row;
	int x;
	int y;
	bool found = false;

	if (!grid || !grid->cells || !bounds || cols <= 0 || rows <= 0) {
		return false;
	}
	first_col = MAX(0, col);
	first_row = MAX(0, row);
	last_col = MIN(grid->cols, col + cols);
	last_row = MIN(grid->rows, row + rows);
	if (first_col >= last_col || first_row >= last_row) return false;
	for (y = first_row; y < last_row; y++) {
		for (x = first_col; x < last_col; x++) {
			const struct sdl3_cell *cell = sdl3_grid_cell(grid, x, y);

			if (!cell || !cell->codepoint || cell->codepoint == L' ') continue;
			if (!found) {
				*bounds = (struct sdl3_cell_bounds) { x, y, 1, 1 };
				found = true;
			} else {
				int right = MAX(bounds->col + bounds->cols, x + 1);
				int bottom = MAX(bounds->row + bounds->rows, y + 1);

				bounds->col = MIN(bounds->col, x);
				bounds->row = MIN(bounds->row, y);
				bounds->cols = right - bounds->col;
				bounds->rows = bottom - bounds->row;
			}
		}
	}
	return found;
}

void sdl3_grid_mark_dirty(struct sdl3_grid *grid)
{
	if (grid) grid->dirty = true;
}

void sdl3_grid_mark_all_dirty(struct sdl3_grid *grid)
{
	int col;
	int row;

	if (!grid || !grid->cells) return;
	for (row = 0; row < grid->rows; row++) {
		for (col = 0; col < grid->cols; col++) {
			grid_cell(grid, col, row)->dirty = true;
		}
	}
	grid->dirty = true;
}

void sdl3_grid_mark_clean(struct sdl3_grid *grid)
{
	int col;
	int row;

	if (!grid || !grid->cells) return;

	for (row = 0; row < grid->rows; row++) {
		for (col = 0; col < grid->cols; col++) {
			grid_cell(grid, col, row)->dirty = false;
		}
	}
	grid->dirty = false;
}
