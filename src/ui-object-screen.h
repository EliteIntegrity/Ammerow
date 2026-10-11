/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-object-screen.h
 * \brief Semantic presentation adapter for item-selection menus.
 */

#ifndef INCLUDED_UI_OBJECT_SCREEN_H
#define INCLUDED_UI_OBJECT_SCREEN_H

#include "ui-object.h"

struct menu;

struct ui_item_details_layout {
	int panel_col, panel_row, panel_cols, panel_rows;
	int text_col, text_row, text_cols, text_rows;
	int art_col, art_row, art_cols, art_rows;
	int heading_cols;
};

void ui_item_details_measure(int cols, int rows,
		struct ui_item_details_layout *layout);
/* Emits full, knowledge-filtered text and a knowledge-safe art identity. */
int ui_item_details_present(const struct object *obj, int offset);
void ui_item_details_show(struct object *obj);
void ui_object_actions_present(struct menu *menu);

struct ui_object_screen_item {
	const struct object *object;
	const char *fallback_name;
	const char *equipment_label;
	char tag;
};

/**
 * Step through item-selector tabs in their visual left-to-right order.
 * available_work is a bitmask of USE_* and SHOW_THROWING values.
 */
int ui_object_screen_step_tab(int current_work, int direction,
		int available_work);

void ui_object_screen_present(struct menu *menu,
		const struct ui_object_screen_item *items, int item_count,
		olist_detail_t detail_mode, int item_mode, bool allow_all,
		const char *prompt, const char *header);

#endif /* INCLUDED_UI_OBJECT_SCREEN_H */
