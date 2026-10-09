/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-store-screen.h
 * \brief Semantic presentation adapter shared by ordinary stores and Home.
 */

#ifndef INCLUDED_UI_STORE_SCREEN_H
#define INCLUDED_UI_STORE_SCREEN_H

#include "h-basic.h"

struct menu;
struct object;
struct store;
struct world_larder_state;
struct world_larder_report;

void ui_store_screen_present_donation(const struct store *store,
		const char *name, const struct world_larder_state *larder,
		const struct world_larder_report *report, bool wait_for_ack);

void ui_store_screen_present(struct menu *menu, struct store *store,
		struct object *const *stock, bool inspect_only, int usable_width,
		const char *notice);
void ui_store_screen_present_confirmation(struct store *store,
		const char *title, const char *prompt, int32_t price,
		int usable_width);
void ui_store_screen_present_quantity(struct store *store, const char *title,
		const char *prompt, const char *value, int maximum, int usable_width);
void ui_store_screen_present_actions(struct store *store, const char *title,
		const char *context, const char *const *labels, const char *tags,
		int count, int cursor, int usable_width);

#endif /* INCLUDED_UI_STORE_SCREEN_H */
