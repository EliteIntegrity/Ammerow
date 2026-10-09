/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-card-layout.c
 * \brief Platform-independent geometry for SDL3 monster stat cards.
 */

#include "sdl3/monster-card-layout.h"

#include <string.h>

#define MONSTER_CARD_MIN_COLS 24
#define MONSTER_CARD_MIN_ROWS 19
#define MONSTER_CARD_COMPACT_WIDTH_PERCENT 34
#define MONSTER_CARD_COMPACT_HEIGHT_PERCENT 70
#define MONSTER_CARD_BIG_WIDTH_PERCENT 44
#define MONSTER_CARD_COMPACT_PORTRAIT_ROWS 7
#define MONSTER_CARD_PORTRAIT_TOP 5
#define MONSTER_CARD_DETAIL_RESERVE 9

static int maximum(int a, int b)
{
	return a > b ? a : b;
}

static int minimum(int a, int b)
{
	return a < b ? a : b;
}

bool sdl3_monster_card_layout_compute(
		struct sdl3_monster_card_layout *layout,
		int view_col, int view_row, int view_cols, int view_rows,
		int cursor_col, int cell_width, int cell_height, bool big)
{
	int width_percent = big ? MONSTER_CARD_BIG_WIDTH_PERCENT :
		MONSTER_CARD_COMPACT_WIDTH_PERCENT;
	int card_view_row = view_row;
	int card_view_rows = view_rows;
	int maximum_portrait_rows;
	bool cursor_on_left;

	if (!layout) return false;
	memset(layout, 0, sizeof(*layout));
	if (view_col < 0 || view_row < 0 || view_cols < MONSTER_CARD_MIN_COLS + 8 ||
			view_rows < MONSTER_CARD_MIN_ROWS || cell_width <= 0 ||
			cell_height <= 0) {
		return false;
	}
	/* Terminal row zero belongs to Angband's authoritative Look/target line.
	 * Keep it outside the richer card rather than covering or reconstructing
	 * that core-owned description. */
	if (card_view_row == 0) {
		card_view_row++;
		card_view_rows--;
	}
	if (card_view_rows < MONSTER_CARD_MIN_ROWS) return false;
	layout->panel_cols = maximum(MONSTER_CARD_MIN_COLS,
		(view_cols * width_percent) / 100);
	layout->panel_cols = minimum(layout->panel_cols, view_cols - 8);
	layout->panel_rows = big ? card_view_rows : maximum(MONSTER_CARD_MIN_ROWS,
		(card_view_rows * MONSTER_CARD_COMPACT_HEIGHT_PERCENT) / 100);
	layout->panel_rows = minimum(layout->panel_rows, card_view_rows);
	if (layout->panel_cols < MONSTER_CARD_MIN_COLS ||
			layout->panel_rows < MONSTER_CARD_MIN_ROWS) {
		memset(layout, 0, sizeof(*layout));
		return false;
	}

	cursor_on_left = cursor_col < view_col + view_cols / 2;
	layout->panel_col = cursor_on_left ?
		view_col + view_cols - layout->panel_cols : view_col;
	layout->panel_row = big ? card_view_row :
		card_view_row + (card_view_rows - layout->panel_rows) / 2;
	layout->text_col = layout->panel_col + 2;
	layout->text_cols = layout->panel_cols - 4;
	layout->portrait_row = layout->panel_row + MONSTER_CARD_PORTRAIT_TOP;
	maximum_portrait_rows = layout->panel_rows - MONSTER_CARD_PORTRAIT_TOP -
		MONSTER_CARD_DETAIL_RESERVE;
	maximum_portrait_rows = maximum(MONSTER_CARD_COMPACT_PORTRAIT_ROWS,
		maximum_portrait_rows);
	if (big) {
		/* Most source portraits are near-square.  A square physical drawing
		 * area lets the font-aware art renderer consume the full card width
		 * without stretching glyphs. */
		layout->portrait_rows = (layout->text_cols * cell_width +
			cell_height - 1) / cell_height;
		layout->portrait_rows = maximum(MONSTER_CARD_COMPACT_PORTRAIT_ROWS,
			layout->portrait_rows);
		layout->portrait_rows = minimum(layout->portrait_rows,
			maximum_portrait_rows);
	} else {
		layout->portrait_rows = MONSTER_CARD_COMPACT_PORTRAIT_ROWS;
	}
	layout->detail_row = layout->portrait_row + layout->portrait_rows;
	layout->detail_end = layout->panel_row + layout->panel_rows - 1;
	return true;
}

#undef MONSTER_CARD_MIN_COLS
#undef MONSTER_CARD_MIN_ROWS
#undef MONSTER_CARD_COMPACT_WIDTH_PERCENT
#undef MONSTER_CARD_COMPACT_HEIGHT_PERCENT
#undef MONSTER_CARD_BIG_WIDTH_PERCENT
#undef MONSTER_CARD_COMPACT_PORTRAIT_ROWS
#undef MONSTER_CARD_PORTRAIT_TOP
#undef MONSTER_CARD_DETAIL_RESERVE
