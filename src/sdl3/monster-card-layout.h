/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-card-layout.h
 * \brief Platform-independent geometry for SDL3 monster stat cards.
 */

#ifndef INCLUDED_SDL3_MONSTER_CARD_LAYOUT_H
#define INCLUDED_SDL3_MONSTER_CARD_LAYOUT_H

#include <stdbool.h>

struct sdl3_monster_card_layout {
	int panel_col;
	int panel_row;
	int panel_cols;
	int panel_rows;
	int text_col;
	int text_cols;
	int portrait_row;
	int portrait_rows;
	int detail_row;
	int detail_end;
};

bool sdl3_monster_card_layout_compute(
		struct sdl3_monster_card_layout *layout,
		int view_col, int view_row, int view_cols, int view_rows,
		int cursor_col, int cell_width, int cell_height, bool big);

#endif /* INCLUDED_SDL3_MONSTER_CARD_LAYOUT_H */
