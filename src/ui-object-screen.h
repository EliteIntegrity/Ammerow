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
