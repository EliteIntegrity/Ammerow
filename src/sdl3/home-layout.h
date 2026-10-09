/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home-layout.h
 * \brief Shared responsive geometry for the SDL3 Home screen.
 */

#ifndef INCLUDED_SDL3_HOME_LAYOUT_H
#define INCLUDED_SDL3_HOME_LAYOUT_H

#include <stdbool.h>

struct sdl3_home_layout {
	int cols;
	int rows;
	int title_col;
	int title_row;
	int menu_col;
	int menu_row;
	int menu_width;
	int menu_spacing;
	int run_card_row;
	int run_card_rows;
	int detail_row;
	int footer_row;
	bool hero_visible;
	int hero_col;
	int hero_row;
	int hero_cols;
	int hero_rows;
};

bool sdl3_home_layout_init(int cols, int rows,
		struct sdl3_home_layout *layout);

#endif /* INCLUDED_SDL3_HOME_LAYOUT_H */
