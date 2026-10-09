/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-history-screen.c
 * \brief Semantic presentation adapters for recorded histories.
 */

#include "angband.h"

#include "player-history.h"
#include "ui-history-screen.h"
#include "ui-screen.h"
#include "ui-term.h"

#define UI_HISTORY_SCREEN_CAPACITY 64

void ui_history_screen_present_messages(int first, int horizontal_offset,
		const char *search, int total, int page_size)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[UI_HISTORY_SCREEN_CAPACITY] = { 0 };
	char labels[UI_HISTORY_SCREEN_CAPACITY][160] = { { 0 } };
	char prefixes[UI_HISTORY_SCREEN_CAPACITY][16] = { { 0 } };
	char subtitle[160];
	int count;
	int i;

	if (!Term || !Term->screen_hook) return;
	total = MAX(0, total);
	first = MIN(MAX(0, first), total);
	horizontal_offset = MAX(0, horizontal_offset);
	page_size = MIN(MAX(1, page_size), UI_HISTORY_SCREEN_CAPACITY);
	count = MIN(page_size, total - first);
	for (i = 0; i < count; i++) {
		int age = first + count - i - 1;
		const char *original = message_str((uint16_t)age);
		const char *visible = original;
		uint16_t repeat = message_count((uint16_t)age);

		if ((int)strlen(original) >= horizontal_offset) {
			visible += horizontal_offset;
		} else {
			visible = "";
		}
		if (repeat > 1) {
			strnfmt(labels[i], sizeof(labels[i]), "%s <%ux>", visible,
				(unsigned)repeat);
		} else {
			my_strcpy(labels[i], visible, sizeof(labels[i]));
		}
		if (search && search[0] && my_stristr(visible, search)) {
			my_strcpy(prefixes[i], "MATCH", sizeof(prefixes[i]));
		}
		rows[i].label = labels[i];
		rows[i].prefix = prefixes[i];
		rows[i].attr = message_color((uint16_t)age);
		rows[i].enabled = true;
	}
	if (total > 0) {
		strnfmt(subtitle, sizeof(subtitle),
			"Recorded messages %d-%d of %d   Horizontal offset %d",
			first + 1, first + count, total, horizontal_offset);
	} else {
		my_strcpy(subtitle, "No messages have been recorded.",
			sizeof(subtitle));
	}
	screen.kind = UI_SCREEN_HISTORY;
	screen.title = "Message History";
	screen.subtitle = subtitle;
	screen.help = search && search[0] ?
		"Up/Down scrolls   PgUp/PgDn pages   Left/Right slides   - next match   = new find   Escape returns" :
		"Up/Down scrolls   PgUp/PgDn pages   Left/Right slides   = finds text   Escape returns";
	screen.content_col = 4;
	screen.content_row = 7;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = -1;
	screen.row_count = count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}

void ui_history_screen_present_character(const struct history_info *entries,
		size_t total, size_t first, int page_size,
		const struct player *history_player)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[UI_HISTORY_SCREEN_CAPACITY] = { 0 };
	char labels[UI_HISTORY_SCREEN_CAPACITY][160] = { { 0 } };
	char prefixes[UI_HISTORY_SCREEN_CAPACITY][32] = { { 0 } };
	char details[UI_HISTORY_SCREEN_CAPACITY][24] = { { 0 } };
	char expanded[PLAYER_NAME_LEN + 106];
	char subtitle[160];
	size_t count;
	size_t i;

	if (!Term || !Term->screen_hook) return;
	first = MIN(first, total);
	page_size = MIN(MAX(1, page_size), UI_HISTORY_SCREEN_CAPACITY);
	count = MIN((size_t)page_size, total - first);
	for (i = 0; i < count; i++) {
		const struct history_info *entry = &entries[first + i];
		bool user_note = hist_has(entry->type, HIST_USER_INPUT);
		bool lost = hist_has(entry->type, HIST_ARTIFACT_LOST);
		const char *event = entry->event;

		if (user_note && history_player) {
			event = history_expand_user_input(entry->event, history_player,
				expanded, sizeof(expanded), true);
		}
		strnfmt(labels[i], sizeof(labels[i]), "%s%s", event,
			lost ? " (LOST)" : "");
		strnfmt(prefixes[i], sizeof(prefixes[i]), "Turn %ld",
			(long)entry->turn);
		if (entry->dlev > 0) {
			strnfmt(details[i], sizeof(details[i]), "%d ft",
				entry->dlev * 50);
		} else {
			my_strcpy(details[i], "Surface", sizeof(details[i]));
		}
		rows[i].label = labels[i];
		rows[i].prefix = prefixes[i];
		rows[i].detail = details[i];
		rows[i].attr = lost ? COLOUR_L_RED :
			(user_note ? COLOUR_YELLOW : COLOUR_L_BLUE);
		rows[i].enabled = true;
	}
	if (total > 0) {
		strnfmt(subtitle, sizeof(subtitle), "Recorded events %lu-%lu of %lu",
			(unsigned long)(first + 1), (unsigned long)(first + count),
			(unsigned long)total);
	} else {
		my_strcpy(subtitle, "No events have been recorded for this run.",
			sizeof(subtitle));
	}
	screen.kind = UI_SCREEN_HISTORY;
	screen.title = "Character History";
	screen.subtitle = subtitle;
	screen.help =
		"Up/Down scrolls   PgUp/PgDn or P/N changes page   Escape returns";
	screen.content_col = 4;
	screen.content_row = 7;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = -1;
	screen.row_count = (int)count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}
