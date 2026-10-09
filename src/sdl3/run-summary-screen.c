/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/run-summary-screen.c
 * \brief Native renderer for the single authoritative end-of-run summary.
 */

#include "angband.h"

#include "sdl3/render-internal.h"
#include "sdl3/run-summary-screen.h"
#include "sdl3/screen-model.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

static void draw_art(const struct sdl3_screen *screen,
		struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int left, int top, int width, int maximum_rows)
{
	int i;

	for (i = 0; i < screen->document_line_count && i < maximum_rows; i++) {
		size_t start = screen->document_line_starts[i];
		size_t length = MIN(screen->document_line_lengths[i], (size_t)width);
		size_t j;

		for (j = 0; j < length; j++) {
			size_t index = start + j;
			wchar_t glyph;

			if (index >= screen->document_length) break;
			glyph = screen->document_text[index];
			if (glyph < 32 || glyph == L' ') continue;
			sdl3_font_draw(&visual->font, (uint32_t)glyph,
				sdl3_theme_color(theme, screen->document_attrs[index]),
				(float)(visual->origin_x +
					(left + (int)j) * visual->cell_width),
				(float)(visual->origin_y + (top + i) * visual->cell_height),
				(float)visual->cell_width, (float)visual->cell_height);
		}
	}
}

static void draw_actions(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int top, int width,
		int last_row)
{
	int i;

	for (i = 0; i < screen->row_count && top + i <= last_row; i++) {
		const struct sdl3_screen_row *action = &screen->rows[i];
		char tag[4] = { ' ', ' ', ' ', '\0' };
		SDL_Color color = action->attr == COLOUR_L_RED ?
			sdl3_theme_color(theme, COLOUR_L_RED) : theme->text;

		if (i == screen->cursor) {
			sdl3_ui_fill_cells(renderer, visual, left, top + i, width, 1,
				theme->selection);
			sdl3_ui_draw_text(visual, ">", left + 1, top + i, 1,
				theme->accent);
		}
		if (action->tag) {
			tag[0] = action->tag;
			tag[1] = ')';
		}
		sdl3_ui_draw_text(visual, tag, left + 3, top + i, 3,
			theme->accent);
		sdl3_ui_draw_text(visual, action->label, left + 7, top + i,
			MAX(1, width - 8), color);
	}
}

static void draw_record(const struct sdl3_screen *screen,
		struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int left, int top, int width, int last_row)
{
	const char *source = screen->context;
	int row = top;

	sdl3_ui_draw_text(visual, screen->context_title, left, row++, width,
		theme->accent);
	row++;
	while (*source && row <= last_row) {
		char line[192];
		size_t length = 0;
		size_t draw_length;
		SDL_Color color;

		if (*source == '\n') {
			source++;
			row++;
			continue;
		}
		while (source[length] && source[length] != '\n' &&
				length < (size_t)width && length + 1 < sizeof(line)) {
			length++;
		}
		draw_length = length;
		if (source[length] && source[length] != '\n' && length > 1) {
			size_t break_at = length;

			while (break_at > length / 2 && source[break_at] != ' ') {
				break_at--;
			}
			if (break_at > length / 2) draw_length = break_at;
		}
		memcpy(line, source, draw_length);
		line[draw_length] = '\0';
		color = streq(line, "Last moments") ? theme->accent : theme->text;
		sdl3_ui_draw_text(visual, line, left, row++, width, color);
		source += draw_length;
		while (*source == ' ') source++;
		if (*source == '\n') source++;
	}
}

bool sdl3_run_summary_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	int top = screen ? screen->content_row : 0;
	int last_row;
	int left_width;
	int record_left;
	int record_width;
	int available_rows;
	int art_rows;
	int action_top;
	int action_rows;

	if (!screen || screen->kind != UI_SCREEN_RUN_SUMMARY || !renderer ||
			!visual || !theme || width < 32) {
		return false;
	}
	last_row = visual->rows - 3;
	available_rows = MAX(1, last_row - top + 1);
	left_width = MIN(36, MAX(26, width / 3));
	record_left = left + left_width + 3;
	record_width = left + width - record_left;
	if (record_width < 24) {
		left_width = MAX(18, width - 27);
		record_left = left + left_width + 2;
		record_width = left + width - record_left;
	}
	action_rows = MIN(screen->row_count, available_rows);
	art_rows = MIN(screen->document_line_count,
		MAX(0, available_rows - action_rows - 1));
	draw_art(screen, visual, theme, left, top, left_width, art_rows);
	action_top = MAX(top,
		MIN(last_row - action_rows + 1, top + art_rows + 1));
	draw_actions(screen, renderer, visual, theme, left, action_top,
		left_width, last_row);
	draw_record(screen, visual, theme, record_left, top, record_width,
		last_row);
	return true;
}
