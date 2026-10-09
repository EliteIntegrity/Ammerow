/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-nearby-screen.h
 * \brief Shared semantic presentation for nearby and symbol-reference lists.
 */

#ifndef INCLUDED_UI_NEARBY_SCREEN_H
#define INCLUDED_UI_NEARBY_SCREEN_H

#include "h-basic.h"

#define UI_NEARBY_SCREEN_CAPACITY 64

struct ui_nearby_screen_entry {
	const char *label;
	const char *prefix;
	const char *detail;
	wchar_t glyph;
	uint8_t text_attr;
	uint8_t glyph_attr;
	bool has_glyph_attr;
};

void ui_nearby_screen_format_offset(char *buffer, size_t length, int dy,
		int dx);
void ui_nearby_screen_present(const char *title, const char *subtitle,
		const char *help, const struct ui_nearby_screen_entry *entries,
		int entry_count, int cursor);

#endif /* INCLUDED_UI_NEARBY_SCREEN_H */
