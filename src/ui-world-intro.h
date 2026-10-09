/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef UI_WORLD_INTRO_H
#define UI_WORLD_INTRO_H

#include "h-basic.h"
#include "z-textblock.h"

struct player_race;

struct ui_world_intro {
	int tab;
	int origin; /**< People-list selection: origins, then the class-title guide. */
	int offset;
};

/** Cell geometry shared by drawing and pointer selection. */
struct ui_world_intro_layout {
	int left, width, tab_row, top, visible_rows;
	int list_width, list_first, text_col, text_width;
	int tab_count, tab_columns;
};

enum ui_world_intro_action {
	UI_INTRO_PREVIOUS_TAB, UI_INTRO_NEXT_TAB,
	UI_INTRO_UP, UI_INTRO_DOWN,
	UI_INTRO_PAGE_UP, UI_INTRO_PAGE_DOWN,
	UI_INTRO_FIRST_LINE, UI_INTRO_LAST_LINE
};

int ui_world_intro_origin_count(void);
const struct player_race *ui_world_intro_origin_at(int index);
int ui_world_intro_people_count(void);
const char *ui_world_intro_people_label(int index);
bool ui_world_intro_has_origins(const struct ui_world_intro *state);
bool ui_world_intro_measure(const struct ui_world_intro *state,
		int cols, int rows, struct ui_world_intro_layout *layout);
textblock *ui_world_intro_document(const struct ui_world_intro *state);
int ui_world_intro_max_offset(const struct ui_world_intro *state,
		const struct ui_world_intro_layout *layout);
void ui_world_intro_scroll(struct ui_world_intro *state, int delta,
		const struct ui_world_intro_layout *layout);
void ui_world_intro_handle(struct ui_world_intro *state,
		enum ui_world_intro_action action, int cols, int rows);
bool ui_world_intro_select_at(struct ui_world_intro *state,
		int cols, int rows, int col, int row);

#endif
