/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-knowledge-screen.c
 * \brief Semantic presentation adapter for the knowledge gateway.
 */

#include "angband.h"

#include "ui-knowledge-screen.h"
#include "ui-menu.h"
#include "ui-screen.h"

#define UI_KNOWLEDGE_SCREEN_CAPACITY 32

static const char *knowledge_label(int index, int store_first, int store_count,
		const char *fallback, char *buffer, size_t length)
{
	static const char *const lore_labels[] = {
		"Objects",
		"Runes",
		"Artifacts",
		"Item qualities",
		"Creatures",
		"Terrain",
		"Traps",
		"Shapechange effects"
	};
	int records_first = store_first + store_count;

	if (index >= 0 && index < (int)N_ELEMENTS(lore_labels)) {
		return lore_labels[index];
	}
	if (index >= store_first && index < records_first) {
		const char *source = fallback ? fallback : "Shop";

		if (prefix(source, "Display ")) source += strlen("Display ");
		my_strcpy(buffer, source, length);
		if (buffer[0]) {
			buffer[0] = (char)toupper((unsigned char)buffer[0]);
		}
		return buffer;
	}
	switch (index - records_first) {
	case 0: return "Hall of records";
	case 1: return "Character history";
	case 2: return "Equipment comparison";
	default:
		break;
	}
	my_strcpy(buffer, fallback ? fallback : "Knowledge", length);
	if (prefix(buffer, "Display ")) {
		memmove(buffer, buffer + strlen("Display "),
			strlen(buffer + strlen("Display ")) + 1);
	}
	if (buffer[0]) buffer[0] = (char)toupper((unsigned char)buffer[0]);
	return buffer;
}

static const char *knowledge_context(int index, int store_first,
		int store_count, bool enabled)
{
	static const char *const lore_context[] = {
		"Browse the kinds of objects this character has identified.",
		"Review the runes whose properties this character has discovered.",
		"Recall named and singular objects learned during this run.",
		"Review the special qualities learned from identified equipment.",
		"Browse creatures this character has encountered or studied.",
		"Review known terrain and the symbols used to represent it.",
		"Review known hazards and the symbols used to represent them.",
		"Review forms this character knows how to assume."
	};
	int records_first = store_first + store_count;

	if (!enabled) {
		return "Nothing has been learned in this category yet.";
	}
	if (index >= 0 && index < (int)N_ELEMENTS(lore_context)) {
		return lore_context[index];
	}
	if (index >= store_first && index < records_first) {
		return "Review the kinds of goods associated with this shop.";
	}
	switch (index - records_first) {
	case 0:
		return "Open the recorded score ledger from earlier expeditions.";
	case 1:
		return "Read the significant events recorded during this run.";
	case 2:
		return "Compare the known properties of wearable equipment.";
	default:
		return "Open this part of the character's accumulated knowledge.";
	}
}

static uint8_t knowledge_attr(int index, int store_first, int store_count)
{
	if (index < store_first) return COLOUR_L_BLUE;
	if (index < store_first + store_count) return COLOUR_ORANGE;
	return COLOUR_L_GREEN;
}

void ui_knowledge_screen_present(struct menu *menu, int store_first,
		int store_count)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[UI_KNOWLEDGE_SCREEN_CAPACITY] = { 0 };
	char labels[UI_KNOWLEDGE_SCREEN_CAPACITY][160] = { { 0 } };
	menu_action *actions;
	int count;
	int cursor;
	int visible;
	int offset;
	int i;

	if (!Term || !Term->screen_hook || !menu) return;
	actions = menu_priv(menu);
	if (!actions) return;
	count = MIN(MAX(0, menu->count), UI_KNOWLEDGE_SCREEN_CAPACITY);
	store_first = MIN(MAX(0, store_first), count);
	store_count = MIN(MAX(0, store_count), count - store_first);
	cursor = count > 0 ? MIN(MAX(0, menu->cursor), count - 1) : -1;
	visible = MAX(1, Term->hgt - 8 - 3);
	offset = MIN(MAX(0, menu->top), MAX(0, count - visible));
	if (cursor >= 0 && cursor < offset) offset = cursor;
	if (cursor >= offset + visible) offset = cursor - visible + 1;

	for (i = 0; i < count; i++) {
		rows[i].label = knowledge_label(i, store_first, store_count,
			actions[i].name, labels[i], sizeof(labels[i]));
		rows[i].prefix = i < store_first ? "Lore" :
			(i < store_first + store_count ? "Shops" : "Records");
		rows[i].tag = menu->selections && menu->selections[i] ?
			menu->selections[i] : actions[i].tag;
		rows[i].attr = knowledge_attr(i, store_first, store_count);
		rows[i].enabled = !(actions[i].flags & MN_ACT_GRAYED);
	}

	screen.kind = UI_SCREEN_KNOWLEDGE;
	screen.title = "Known World";
	screen.subtitle =
		"What this character has learned, remembered, and recorded";
	screen.help =
		"Up/Down or 8/2 browses   Enter opens   Letters open entries   1-9 opens shops   Escape returns";
	screen.context_title = cursor >= 0 ? rows[cursor].label : "Known World";
	screen.context = cursor >= 0 ?
		knowledge_context(cursor, store_first, store_count,
			rows[cursor].enabled) :
		"This character has not accumulated any knowledge yet.";
	screen.content_col = 4;
	screen.content_row = 8;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = visible;
	screen.cursor = cursor;
	screen.row_offset = offset;
	screen.row_count = count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}
