/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-help.c
 * \brief In-game help, retaining inherited links and search controls.
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies. Other copyrights may also apply.
 */
#include "angband.h"
#include "init.h"
#include "ui-help.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-screen.h"
#include "ui-term.h"
#include <wctype.h>

struct help_document {
	textblock *body;
	char title[128];
	char links[26][256];
	size_t tag_offset;
	bool menu;
};

static bool heading_rule(const char *line)
{
	size_t length = strlen(line);
	return length >= 3 && (line[0] == '=' || line[0] == '-') &&
		strspn(line, line[0] == '=' ? "=" : "-") == length;
}

/** Read the small RST subset used by the field guide once. Keep tables
 * indented and aligned; the existing textblock engine handles wrapping. */
static bool help_load(struct help_document *doc, const char *name,
		const char *what)
{
	char filename[1024], path[1024], buf[1024];
	char **lines = NULL, *tag;
	size_t count = 0, tagged = SIZE_MAX;
	bool skip = false;
	ang_file *file;

	memset(doc, 0, sizeof(*doc));
	my_strcpy(filename, name, sizeof(filename));
	tag = strchr(filename, '#');
	if (tag) *tag++ = '\0';
	if (what) my_strcpy(path, filename, sizeof(path));
	else path_build(path, sizeof(path), ANGBAND_DIR_HELP, filename);
	file = file_open(path, MODE_READ, FTYPE_TEXT);
	if (!file) return false;
	my_strcpy(doc->title, what ? what : filename, sizeof(doc->title));
	while (file_getl(file, buf, sizeof(buf))) {
		if (skip) {
			if (contains_only_spaces(buf)) skip = false;
			continue;
		}
		if (prefix(buf, ".. ")) {
			if (prefix(buf, ".. menu:: [") && strlen(buf) > 14 && buf[12] == ']') {
				int key = buf[11] - 'a';
				if (key >= 0 && key < 26) {
					my_strcpy(doc->links[key], buf + 14, sizeof(doc->links[key]));
					doc->menu = true;
				}
			} else if (tag && prefix(buf, ".. _") && suffix(buf, ":")) {
				buf[strlen(buf) - 1] = '\0';
				if (streq(buf + 4, tag)) tagged = count;
			}
			skip = true;
			continue;
		}
		if (!count && contains_only_spaces(buf)) continue;
		lines = mem_realloc(lines, (count + 1) * sizeof(*lines));
		lines[count++] = string_make(buf);
	}
	file_close(file);
	doc->body = textblock_new();
	for (size_t i = 0; i < count; i++) {
		bool heading = i + 1 < count && heading_rule(lines[i + 1]);
		uint8_t color = COLOUR_WHITE;
		if (i == tagged) {
			const wchar_t *text = textblock_text(doc->body);
			doc->tag_offset = text ? wcslen(text) : 0;
		}
		if (!i && heading && !what) {
			my_strcpy(doc->title, lines[i], sizeof(doc->title));
			continue;
		}
		if (heading_rule(lines[i])) {
			if (i == 1 && !what) continue;
			textblock_append(doc->body, "\n");
			continue;
		}
		if (heading || (lines[i][0] && lines[i][0] != ' ' &&
				i + 1 < count && prefix(lines[i + 1], "  "))) color = COLOUR_L_BLUE;
		if (prefix(lines[i], "    (") && strlen(lines[i]) > 6 &&
				lines[i][6] == ')') color = COLOUR_L_GREEN;
		textblock_append_c(doc->body, color, "%s\n", lines[i]);
	}
	for (size_t i = 0; i < count; i++) string_free(lines[i]);
	mem_free(lines);
	return true;
}

static bool help_match(const wchar_t *text, const wchar_t *needle, bool sensitive)
{
	if (!needle[0]) return false;
	for (size_t i = 0; needle[i]; i++) {
		if (!text[i] || (sensitive ? text[i] != needle[i] :
				towlower(text[i]) != towlower(needle[i]))) return false;
	}
	return true;
}

/** Snapshot only the visible page so frontend capacity never truncates a
 * long document. The terminal fallback receives the same styled text. */
static void help_present(struct help_document *doc, const size_t *starts,
		const size_t *lengths, int count, int line, int width, int page_rows,
		const char *highlight, bool sensitive)
{
	struct ui_screen screen = { 0 };
	const wchar_t *text = textblock_text(doc->body);
	const uint8_t *attrs = textblock_attrs(doc->body);
	textblock *page = textblock_new();
	wchar_t needle[80];
	size_t *page_starts = NULL, *page_lengths = NULL;
	char subtitle[128];
	int left = MAX(0, (Term->wid - width) / 2);
	int top = Term->screen_hook ? 7 : 2;
	int shown = MIN(page_rows, MAX(0, count - line));
	const char *help = doc->menu ?
		"Letter: open | Arrows: scroll | Space/PgUp: page | /: search | Esc: back" :
		"Arrows: scroll | Space/PgUp: page | /: search | Esc: back | ?: close guide";

	text_mbstowcs(needle, highlight, N_ELEMENTS(needle));
	needle[N_ELEMENTS(needle) - 1] = L'\0';
	Term_clear();
	for (int i = 0; i < shown; i++) {
		size_t start = starts[line + i], end = start + lengths[line + i];
		size_t highlighted_until = 0;
		for (size_t j = start; j < end; j++) {
			uint8_t attr = attrs[j];
			if (help_match(text + j, needle, sensitive))
				highlighted_until = j + wcslen(needle);
			if (j < highlighted_until) attr = COLOUR_YELLOW;
			textblock_append_pict(page, attr, text[j]);
			Term_putch(left + (int)(j - start), top + i, attr, text[j]);
		}
		textblock_append(page, "\n");
	}
	strnfmt(subtitle, sizeof(subtitle), "Field Guide | lines %d-%d of %d%s",
		count ? line + 1 : 0, line + shown, count,
		sensitive ? " | case-sensitive search" : "");
	prt(doc->title, 0, left);
	prt(help, Term->hgt - 1, 0);
	if (Term->screen_hook) {
		screen.kind = UI_SCREEN_HELP;
		screen.title = doc->title;
		screen.subtitle = subtitle;
		screen.help = help;
		screen.cursor = -1;
		screen.content_col = left;
		screen.content_row = top;
		screen.content_cols = width;
		screen.content_rows = page_rows;
		screen.document_text = textblock_text(page);
		screen.document_attrs = textblock_attrs(page);
		screen.document_length = screen.document_text ? wcslen(screen.document_text) : 0;
		screen.document_line_count = (int)textblock_calculate_lines(page,
			&page_starts, &page_lengths, (size_t)width + 1);
		screen.document_line_starts = page_starts;
		screen.document_line_lengths = page_lengths;
		screen.document_cols = width;
		Term->screen_hook(&screen);
	}
	mem_free(page_starts);
	mem_free(page_lengths);
	textblock_free(page);
}

static bool help_view(const char *name, const char *what, int line, int mode,
		bool preview)
{
	struct help_document doc;
	size_t *starts = NULL, *lengths = NULL;
	char highlight[80] = "", finder[80] = "";
	bool sensitive = false, first = true, done = false, back = true;
	if (!help_load(&doc, name, what)) {
		if (!preview) {
			msg("Cannot open '%s'.", name);
			event_signal(EVENT_MESSAGE_FLUSH);
		}
		return !preview;
	}
	while (!done) {
		struct keypress key;
		int width = MAX(1, MIN(100, Term->wid - (Term->screen_hook ? 8 : 0)));
		int page_rows = MAX(1, Term->hgt - (Term->screen_hook ? 10 : 4));
		int count = (int)textblock_calculate_lines(doc.body, &starts, &lengths, width);
		if (first && doc.tag_offset) {
			line = 0;
			while (line + 1 < count && starts[line + 1] <= doc.tag_offset) line++;
		}
		first = false;
		line = MAX(0, MIN(line, count - page_rows));
		help_present(&doc, starts, lengths, count, line, width, page_rows,
			highlight, sensitive);
		if (preview) break;
		key = inkey();
		/* Prompts and recursive pages must not hide behind the overlay. */
		if (Term->screen_hook) Term->screen_hook(NULL);
		if (key.code < 256 && isalpha((unsigned char)key.code)) {
			int index = tolower((unsigned char)key.code) - 'a';
			if (index >= 0 && index < 26 && doc.links[index][0]) {
				if (!show_file(doc.links[index], NULL, 0, mode)) {
					done = true; back = false;
				}
				continue;
			}
		}
		switch (key.code) {
		case ESCAPE: done = true; break;
		case '?': done = true; back = false; break;
		case '!': sensitive = !sensitive; break;
		case ARROW_UP: case 'k': case '8': case '=': line--; break;
		case ARROW_DOWN: case 'j': case '2': case KC_ENTER: line++; break;
		case KC_PGUP: case '9': case '-': line -= page_rows; break;
		case KC_PGDOWN: case '3': case ' ': line += page_rows; break;
		case '+': line += MAX(1, page_rows / 2); break;
		case '_': line -= MAX(1, page_rows / 2); break;
		case KC_HOME: case '7': line = 0; break;
		case KC_END: case '1': line = count; break;
		case '&':
			prt("Highlight: ", Term->hgt - 1, 0);
			(void)askfor_aux(highlight, sizeof(highlight), NULL);
			break;
		case '/': {
			wchar_t needle[80];
			bool found = false;
			prt("Find: ", Term->hgt - 1, 0);
			if (!askfor_aux(finder, sizeof(finder), NULL) || !finder[0]) break;
			my_strcpy(highlight, finder, sizeof(highlight));
			text_mbstowcs(needle, finder, N_ELEMENTS(needle));
			needle[N_ELEMENTS(needle) - 1] = L'\0';
			for (int i = line + 1; i < count && !found; i++) {
				for (size_t j = starts[i]; j < starts[i] + lengths[i]; j++) {
					if (help_match(textblock_text(doc.body) + j, needle, sensitive)) {
						line = i; found = true; break;
					}
				}
			}
			if (!found) bell();
			break;
		}
		case '#': {
			char number[80] = "1";
			prt("Go to line: ", Term->hgt - 1, 0);
			if (askfor_aux(number, sizeof(number), NULL)) line = MAX(0, atoi(number) - 1);
			break;
		}
		case '%': {
			char file[80] = "index.txt";
			prt("Go to file: ", Term->hgt - 1, 0);
			if (askfor_aux(file, sizeof(file), NULL) && !show_file(file, NULL, 0, mode)) {
				done = true; back = false;
			}
			break;
		}
		default: break;
		}
	}
	mem_free(starts);
	mem_free(lengths);
	textblock_free(doc.body);
	return back;
}

bool ui_help_preview(const char *name, int line)
{
	return help_view(name, NULL, line, 0, true);
}

bool show_file(const char *name, const char *what, int line, int mode)
{
	return help_view(name, what, line, mode, false);
}

void do_cmd_help(void)
{
	screen_save();
	(void)show_file(OPT(player, rogue_like_commands) ? "r_index.txt" :
		"index.txt", NULL, 0, 0);
	screen_load();
}
