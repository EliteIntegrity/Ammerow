/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/menu-layout.h
 * \brief Shared cell-space geometry for keyboard and pointer menus.
 *
 * Renderers and pointer input use the same description, so changing a menu's
 * rhythm cannot silently leave its mouse targets behind.
 */

#ifndef INCLUDED_SDL3_MENU_LAYOUT_H
#define INCLUDED_SDL3_MENU_LAYOUT_H

struct sdl3_menu_layout {
	int col;
	int row;
	int width;
	int count;
	int step;
	int item_height;
};

static inline int sdl3_menu_item_row(const struct sdl3_menu_layout *layout,
		int item)
{
	if (!layout || item < 0 || item >= layout->count) return -1;
	return layout->row + item * layout->step;
}

static inline int sdl3_menu_item_at(const struct sdl3_menu_layout *layout,
		int col, int row)
{
	int item;
	int item_row;

	if (!layout || layout->width <= 0 || layout->count <= 0 ||
			layout->step <= 0 || layout->item_height <= 0 ||
			col < layout->col || col >= layout->col + layout->width ||
			row < layout->row) {
		return -1;
	}
	item = (row - layout->row) / layout->step;
	item_row = sdl3_menu_item_row(layout, item);
	if (item < 0 || item >= layout->count || row >= item_row +
			layout->item_height) {
		return -1;
	}
	return item;
}

#endif /* INCLUDED_SDL3_MENU_LAYOUT_H */
