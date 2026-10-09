/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/pause-menu-model.c
 * \brief Platform-independent state for the SDL3 in-game Escape menu.
 */

#include "sdl3/pause-menu-model.h"

#include <string.h>

struct sdl3_menu_layout sdl3_pause_root_layout(int cols, int rows)
{
	int width = cols - 4 < SDL3_PAUSE_PANEL_COLS ?
		cols - 4 : SDL3_PAUSE_PANEL_COLS;
	int height = rows - 2 < SDL3_PAUSE_PANEL_ROWS ?
		rows - 2 : SDL3_PAUSE_PANEL_ROWS;
	struct sdl3_menu_layout layout = { 0 };

	if (width < 40 || height < SDL3_PAUSE_PANEL_ROWS) return layout;
	layout = (struct sdl3_menu_layout) {
		(cols - width) / 2 + 2, (rows - height) / 2 + 4,
		width - 4, SDL3_PAUSE_ROOT_ROW_COUNT, 2, 1
	};
	return layout;
}

/* Each side-by-side choice is a separate one-row target.  Both the renderer
 * and pointer selection use these exact bounds, including the gap. */
struct sdl3_menu_layout sdl3_pause_confirm_choice_layout(int cols, int rows,
		bool abandon)
{
	int width = cols - 4 < SDL3_PAUSE_CONFIRM_COLS ?
		cols - 4 : SDL3_PAUSE_CONFIRM_COLS;
	int height = rows - 2 < SDL3_PAUSE_CONFIRM_ROWS ?
		rows - 2 : SDL3_PAUSE_CONFIRM_ROWS;
	struct sdl3_menu_layout layout = { 0 };

	if (width < 44 || height < SDL3_PAUSE_CONFIRM_ROWS) return layout;
	layout = (struct sdl3_menu_layout) {
		(cols - width) / 2 + (abandon ? width / 2 : 3),
		(rows - height) / 2 + 7, width / 2 - 4, 1, 1, 1
	};
	return layout;
}

bool sdl3_pause_menu_select_at(struct sdl3_pause_menu *menu, int cols,
		int rows, int col, int row)
{
	if (!menu || !menu->visible) return false;
	if (menu->page == SDL3_PAUSE_ROOT) {
		struct sdl3_menu_layout layout = sdl3_pause_root_layout(cols, rows);
		int item = sdl3_menu_item_at(&layout, col, row);

		if (item < 0) return false;
		menu->selected_row = item;
		return true;
	}
	if (menu->page == SDL3_PAUSE_CONFIRM_ABANDON) {
		for (int i = 0; i < 2; i++) {
			struct sdl3_menu_layout layout =
				sdl3_pause_confirm_choice_layout(cols, rows, i == 1);

			if (sdl3_menu_item_at(&layout, col, row) < 0) continue;
			menu->abandon_confirmed = i == 1;
			return true;
		}
	}
	return false;
}

void sdl3_pause_menu_init(struct sdl3_pause_menu *menu)
{
	if (!menu) return;
	memset(menu, 0, sizeof(*menu));
}

void sdl3_pause_menu_open(struct sdl3_pause_menu *menu)
{
	if (!menu) return;
	menu->visible = true;
	menu->page = SDL3_PAUSE_ROOT;
	menu->selected_row = SDL3_PAUSE_BACK_TO_GAME;
	menu->abandon_confirmed = false;
	menu->action = SDL3_PAUSE_ACTION_NONE;
}

void sdl3_pause_menu_move(struct sdl3_pause_menu *menu, int delta)
{
	if (!menu || !delta) return;
	if (menu->page == SDL3_PAUSE_ROOT) {
		menu->selected_row += delta < 0 ? -1 : 1;
		if (menu->selected_row < 0) {
			menu->selected_row = SDL3_PAUSE_ROOT_ROW_COUNT - 1;
		}
		if (menu->selected_row >= SDL3_PAUSE_ROOT_ROW_COUNT) {
			menu->selected_row = 0;
		}
	} else if (menu->page == SDL3_PAUSE_CONFIRM_ABANDON) {
		menu->abandon_confirmed = !menu->abandon_confirmed;
	}
}

void sdl3_pause_menu_activate(struct sdl3_pause_menu *menu)
{
	if (!menu) return;
	if (menu->page == SDL3_PAUSE_CONFIRM_ABANDON) {
		if (menu->abandon_confirmed) {
			menu->action = SDL3_PAUSE_ACTION_ABANDON;
		} else {
			menu->page = SDL3_PAUSE_ROOT;
		}
		return;
	}
	if (menu->page == SDL3_PAUSE_HELP) return;
	switch (menu->selected_row) {
	case SDL3_PAUSE_BACK_TO_GAME:
		menu->action = SDL3_PAUSE_ACTION_BACK;
		break;
	case SDL3_PAUSE_SETTINGS:
		menu->action = SDL3_PAUSE_ACTION_SETTINGS;
		break;
	case SDL3_PAUSE_HELP_CONTROLS:
		menu->page = SDL3_PAUSE_HELP;
		break;
	case SDL3_PAUSE_SAVE_QUIT:
		menu->action = SDL3_PAUSE_ACTION_SAVE_QUIT;
		break;
	case SDL3_PAUSE_ABANDON:
		menu->page = SDL3_PAUSE_CONFIRM_ABANDON;
		menu->abandon_confirmed = false;
		break;
	default:
		break;
	}
}

void sdl3_pause_menu_back(struct sdl3_pause_menu *menu)
{
	if (!menu) return;
	if (menu->page == SDL3_PAUSE_ROOT) {
		menu->action = SDL3_PAUSE_ACTION_BACK;
	} else {
		menu->page = SDL3_PAUSE_ROOT;
		menu->abandon_confirmed = false;
	}
}
