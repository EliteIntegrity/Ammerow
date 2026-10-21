/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef UI_SPELUNKING_LIST_H
#define UI_SPELUNKING_LIST_H

#include "h-basic.h"
struct player;

struct ui_spelunking_list_entry {
	char label[160];
	char offset[64];
	wchar_t glyph;
	uint8_t attr;
	bool detected;
	int dx, dy, distance;
};

/** Read-only, knowledge-filtered entries; no native cave or discovery writes. */
size_t textui_spelunking_collect_nearby(const struct player *p, bool objects,
	struct ui_spelunking_list_entry *entries, size_t capacity);
/** Present the first page without input, using the live controller's layout. */
bool textui_spelunking_list_present(const struct player *p, bool objects);
/** Interactive main-screen list, using the shared semantic presentation. */
void textui_spelunking_list(const struct player *p, bool objects);

#endif
