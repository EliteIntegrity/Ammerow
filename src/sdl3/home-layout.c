/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home-layout.c
 * \brief Shared responsive geometry for the SDL3 Home screen.
 */

#include "sdl3/home-layout.h"
#include "sdl3/home-model.h"

#include <string.h>

static int int_min(int left, int right)
{
	return left < right ? left : right;
}

static int int_max(int left, int right)
{
	return left > right ? left : right;
}

bool sdl3_home_layout_init(int cols, int rows,
		struct sdl3_home_layout *layout)
{
	int content_width;

	if (!layout || cols < 64 || rows < 22) return false;
	memset(layout, 0, sizeof(*layout));
	layout->cols = cols;
	layout->rows = rows;
	layout->hero_visible = cols >= 120 && rows >= 28;
	if (layout->hero_visible) {
		int left_region = cols * 55 / 100;

		content_width = int_min(72, left_region - 8);
		layout->title_col = int_max(4,
			(left_region - content_width) / 2);
		layout->hero_col = left_region + 1;
		layout->hero_row = 1;
		layout->hero_cols = cols - layout->hero_col - 3;
		layout->hero_rows = rows - 4;
	} else {
		content_width = int_min(88, cols - 8);
		layout->title_col = int_max(4, (cols - content_width) / 2);
	}
	layout->title_row = 1;
	layout->menu_col = layout->title_col;
	layout->menu_width = content_width;
	layout->run_card_row = rows < 24 ? 6 : 7;
	layout->run_card_rows = rows >= 30 ? 4 : 3;
	layout->menu_row = layout->run_card_row + layout->run_card_rows +
		(rows >= 30 ? 2 : 1);
	/* Derive spacing from the complete menu, including the description and
	 * footer. Adding an entry must never silently place it underneath either. */
	layout->menu_spacing = int_max(1, int_min(2,
		(rows - 6 - layout->menu_row) / (SDL3_HOME_ROOT_ROW_COUNT - 2)));
	layout->detail_row = layout->menu_row +
		(SDL3_HOME_ROOT_ROW_COUNT - 2) * layout->menu_spacing + 2;
	layout->footer_row = rows - 2;
	/* The extra Memorial row must not put its detail beneath the footer fill. */
	layout->detail_row = int_min(layout->detail_row, layout->footer_row - 2);
	return true;
}
