/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/pause-menu-model.h
 * \brief Platform-independent state for the SDL3 in-game Escape menu.
 */

#ifndef INCLUDED_SDL3_PAUSE_MENU_MODEL_H
#define INCLUDED_SDL3_PAUSE_MENU_MODEL_H

#include <stdbool.h>
#include "sdl3/menu-layout.h"

enum {
	SDL3_PAUSE_PANEL_COLS = 58,
	SDL3_PAUSE_PANEL_ROWS = 16,
	SDL3_PAUSE_CONFIRM_COLS = 62,
	SDL3_PAUSE_CONFIRM_ROWS = 12
};

enum sdl3_pause_page {
	SDL3_PAUSE_ROOT = 0,
	SDL3_PAUSE_HELP,
	SDL3_PAUSE_CONFIRM_ABANDON
};

enum sdl3_pause_action {
	SDL3_PAUSE_ACTION_NONE = 0,
	SDL3_PAUSE_ACTION_BACK,
	SDL3_PAUSE_ACTION_SETTINGS,
	SDL3_PAUSE_ACTION_SAVE_QUIT,
	SDL3_PAUSE_ACTION_ABANDON
};

enum sdl3_pause_root_row {
	SDL3_PAUSE_BACK_TO_GAME = 0,
	SDL3_PAUSE_SETTINGS,
	SDL3_PAUSE_HELP_CONTROLS,
	SDL3_PAUSE_SAVE_QUIT,
	SDL3_PAUSE_ABANDON,
	SDL3_PAUSE_ROOT_ROW_COUNT
};

struct sdl3_pause_menu {
	bool visible;
	enum sdl3_pause_page page;
	int selected_row;
	bool abandon_confirmed;
	enum sdl3_pause_action action;
};

void sdl3_pause_menu_init(struct sdl3_pause_menu *menu);
void sdl3_pause_menu_open(struct sdl3_pause_menu *menu);
void sdl3_pause_menu_move(struct sdl3_pause_menu *menu, int delta);
void sdl3_pause_menu_activate(struct sdl3_pause_menu *menu);
void sdl3_pause_menu_back(struct sdl3_pause_menu *menu);
struct sdl3_menu_layout sdl3_pause_root_layout(int cols, int rows);
struct sdl3_menu_layout sdl3_pause_confirm_choice_layout(int cols, int rows,
		bool abandon);
bool sdl3_pause_menu_select_at(struct sdl3_pause_menu *menu, int cols,
		int rows, int col, int row);

#endif /* INCLUDED_SDL3_PAUSE_MENU_MODEL_H */
