/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-nearby-screen.c
 * \brief Shared semantic presentation for nearby and symbol-reference lists.
 */

#include "angband.h"

#include "ui-nearby-screen.h"
#include "ui-screen.h"
#include "ui-term.h"

void ui_nearby_screen_format_offset(char *buffer, size_t length, int dy,
		int dx)
{
	if (!buffer || !length) return;
	if (!dy && !dx) {
		my_strcpy(buffer, "underfoot", length);
	} else if (!dy) {
		strnfmt(buffer, length, "%d %s", abs(dx), dx < 0 ? "W" : "E");
	} else if (!dx) {
		strnfmt(buffer, length, "%d %s", abs(dy), dy < 0 ? "N" : "S");
	} else {
		strnfmt(buffer, length, "%d %s  %d %s", abs(dy),
			dy < 0 ? "N" : "S", abs(dx), dx < 0 ? "W" : "E");
	}
}

void ui_nearby_screen_present(const char *title, const char *subtitle,
		const char *help, const struct ui_nearby_screen_entry *entries,
		int entry_count, int cursor)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[UI_NEARBY_SCREEN_CAPACITY] = { 0 };
	int count;
	int i;

	if (!Term || !Term->screen_hook) return;
	count = entries ? MIN(MAX(0, entry_count), UI_NEARBY_SCREEN_CAPACITY) : 0;
	for (i = 0; i < count; i++) {
		rows[i].label = entries[i].label;
		rows[i].prefix = entries[i].prefix;
		rows[i].detail = entries[i].detail;
		rows[i].glyph = entries[i].glyph;
		rows[i].attr = entries[i].text_attr;
		rows[i].enabled = true;
		rows[i].glyph_attr = entries[i].glyph_attr;
		rows[i].has_glyph_attr = entries[i].has_glyph_attr;
	}
	screen.kind = UI_SCREEN_NEARBY;
	screen.title = title;
	screen.subtitle = subtitle;
	screen.help = help;
	screen.content_col = 4;
	screen.content_row = 7;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = cursor >= 0 && cursor < count ? cursor : -1;
	screen.row_count = count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}
