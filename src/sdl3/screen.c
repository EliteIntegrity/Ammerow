/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/screen.c
 * \brief Reusable renderer for semantic full-screen SDL3 interfaces.
 */

#include "angband.h"

#include "sdl3/dungeon-map-screen.h"
#include "sdl3/monster-lore-screen.h"
#include "sdl3/render-internal.h"
#include "sdl3/run-summary-screen.h"
#include "sdl3/screen.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"
#include "sdl3/world-map-screen.h"

static void draw_text_ellipsized(struct sdl3_visual *visual,
		const char *source, int col, int row, int width, SDL_Color color)
{
	char text[SDL3_SCREEN_LABEL_CAPACITY];
	int length;

	if (!source || width <= 0) return;
	length = (int)strlen(source);
	if (length <= width || width <= 3) {
		sdl3_ui_draw_text(visual, source, col, row, width, color);
		return;
	}
	length = MIN(width - 3, (int)sizeof(text) - 4);
	memcpy(text, source, (size_t)length);
	memcpy(text + length, "...", 4);
	sdl3_ui_draw_text(visual, text, col, row, width, color);
}

static void draw_tabs(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme,
		const struct sdl3_screen_layout *layout)
{
	int col = layout->left;
	int i;

	for (i = 0; i < screen->tab_count &&
			col < layout->left + layout->width; i++) {
		const struct sdl3_screen_tab *tab = &screen->tabs[i];
		int label_width = MIN((int)strlen(tab->label) + 4,
			layout->left + layout->width - col);

		if (label_width <= 0) break;
		if (tab->active) {
			sdl3_ui_fill_cells(renderer, visual, col, layout->tab_row,
				label_width, 1, theme->selection);
		}
		sdl3_ui_draw_text(visual, tab->active ? "[" : " ", col,
			layout->tab_row, 1, theme->accent);
		sdl3_ui_draw_text(visual, tab->label, col + 1, layout->tab_row,
			label_width - 2, tab->active ? theme->text : theme->muted);
		sdl3_ui_draw_text(visual, tab->active ? "]" : " ",
			col + label_width - 1, layout->tab_row, 1, theme->accent);
		col += label_width + 1;
	}
}

static void draw_rows(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme,
		const struct sdl3_screen_layout *layout)
{
	int i;

	if (layout->visible_rows <= 0 || layout->label_width <= 4) return;
	for (i = 0; i < layout->visible_rows; i++) {
		int index = layout->first_row + i;
		int row_number = screen->content_row + i;
		const struct sdl3_screen_row *row = &screen->rows[index];
		SDL_Color text_color = row->enabled ? theme->text : theme->muted;
		char tag[4] = { ' ', ' ', ' ', '\0' };
		bool dossier = screen->kind == UI_SCREEN_CHARACTER_DOSSIER;
		bool history = screen->kind == UI_SCREEN_HISTORY;
		bool selected = row->enabled && index == screen->cursor;
		if (row->enabled && (screen->kind == UI_SCREEN_ABILITIES ||
				screen->kind == UI_SCREEN_CHARACTER_CREATION ||
				screen->kind == UI_SCREEN_BIRTH_STATS ||
				screen->kind == UI_SCREEN_KNOWLEDGE || history ||
				screen->kind == UI_SCREEN_NEARBY)) {
			text_color = sdl3_theme_color(theme, row->attr);
		}
		if (screen->kind == UI_SCREEN_BIRTH_STATS) {
			if (selected) {
				sdl3_ui_fill_cells(renderer, visual, layout->left, row_number,
					layout->list_width, 1, theme->selection);
			}
			sdl3_ui_draw_text(visual, row->label, layout->left + 1,
				row_number, layout->list_width - 2, text_color);
			continue;
		}
		if (history) {
			int detail_width = row->detail[0] ? layout->detail_width : 0;
			int label_end = layout->left + layout->list_width -
				(detail_width ? detail_width + 1 : 2);

			if (row->prefix[0] && layout->list_width >= 36) {
				int prefix_width = MIN(16, MAX(10,
					layout->list_width / 4));
				int label_col = layout->left + prefix_width + 3;

				sdl3_ui_draw_text(visual, row->prefix, layout->left + 2,
					row_number, prefix_width, theme->muted);
				sdl3_ui_draw_text(visual, row->label, label_col, row_number,
					MAX(1, label_end - label_col), text_color);
			} else {
				sdl3_ui_draw_text(visual, row->label, layout->left + 2,
					row_number, MAX(1, label_end - layout->left - 2),
					text_color);
			}
			if (detail_width) {
				sdl3_ui_draw_text(visual, row->detail,
					layout->left + layout->list_width - detail_width,
					row_number, detail_width - 1, theme->muted);
			}
			continue;
		}

		if (dossier) {
			if (!row->enabled) {
				if (row->label[0]) {
					sdl3_ui_draw_text(visual, row->label, layout->left,
						row_number, layout->list_width, theme->accent);
				}
				continue;
			}
			if (selected) {
				sdl3_ui_fill_cells(renderer, visual, layout->left, row_number,
					layout->list_width, 1, theme->selection);
				sdl3_ui_draw_text(visual, ">", layout->left + 1, row_number, 1,
					theme->accent);
			}
			if (row->prefix[0]) {
				int label_width = MIN(28,
					MAX(18, layout->list_width / 3));
				int status_width = MIN(24,
					MAX(16, layout->list_width / 4));
				int status_col = layout->left + label_width + 3;
				int source_col = status_col + status_width + 2;
				int source_width = layout->left + layout->list_width -
					source_col;

				sdl3_ui_draw_text(visual, row->label,
					layout->left + (selected ? 3 : 2), row_number,
					MAX(1, label_width - (selected ? 3 : 2)),
					selected ? theme->text : theme->muted);
				sdl3_ui_draw_text(visual, row->prefix, status_col,
					row_number, status_width,
					sdl3_theme_color(theme, row->attr));
				if (source_width > 0) {
					sdl3_ui_draw_text(visual, row->detail, source_col,
						row_number, source_width, theme->text);
				}
				continue;
			}
			sdl3_ui_draw_text(visual, row->label,
				layout->left + (selected ? 3 : 2), row_number,
				MAX(1, layout->label_width - (selected ? 3 : 2)),
				selected ? theme->text : theme->muted);
			if (layout->detail_width && row->detail[0]) {
				sdl3_ui_draw_text(visual, row->detail,
					layout->left + layout->list_width - layout->detail_width,
					row_number, layout->detail_width - 1,
					sdl3_theme_color(theme, row->attr));
			}
			continue;
		}

		if (index == screen->cursor) {
			sdl3_ui_fill_cells(renderer, visual, layout->left, row_number,
				layout->list_width, 1, theme->selection);
			sdl3_ui_draw_text(visual, ">", layout->left + 1, row_number, 1,
				theme->accent);
		}
		if (row->tag) {
			tag[0] = row->tag;
			tag[1] = ')';
		}
		sdl3_ui_draw_text(visual, tag, layout->left + 3, row_number, 3,
			row->enabled ? theme->accent : theme->muted);
		if (row->glyph && row->glyph >= 32) {
			sdl3_font_draw(&visual->font, (uint32_t)row->glyph,
				sdl3_theme_color(theme, row->has_glyph_attr ?
					row->glyph_attr : row->attr),
				(float)(visual->origin_x +
				(layout->left + 6) * visual->cell_width),
				(float)(visual->origin_y + row_number * visual->cell_height),
				(float)visual->cell_width, (float)visual->cell_height);
		}
		if (screen->kind == UI_SCREEN_STORE) {
			if (layout->prefix_width && row->prefix[0]) {
				sdl3_ui_draw_text(visual, row->prefix, layout->left + 8,
					row_number, layout->prefix_width, theme->muted);
			}
			draw_text_ellipsized(visual, row->label,
				layout->left + layout->label_col, row_number,
				layout->label_width, text_color);
			if (layout->detail_width && row->detail[0]) {
				int detail_length = MIN((int)strlen(row->detail),
					layout->detail_width - 2);
				int detail_col = layout->left + layout->list_width - 2 -
					detail_length;

				sdl3_ui_draw_text(visual, row->detail, detail_col, row_number,
					detail_length, theme->muted);
			}
			continue;
		}
		if (row->prefix[0] && layout->list_width >= 56) {
			sdl3_ui_draw_text(visual, row->prefix, layout->left + 8,
				row_number, 15, theme->muted);
			sdl3_ui_draw_text(visual, row->label, layout->left + 23,
				row_number, MAX(1, layout->label_width - 15), text_color);
		} else {
			sdl3_ui_draw_text(visual, row->label, layout->left + 8,
				row_number, layout->label_width, text_color);
		}
		if (layout->detail_width && row->detail[0]) {
			sdl3_ui_draw_text(visual, row->detail,
				layout->left + layout->list_width - layout->detail_width,
				row_number, layout->detail_width - 1, theme->muted);
		}
	}
}

static void draw_context(const struct sdl3_screen *screen,
		struct sdl3_visual *visual, const struct sdl3_theme *theme,
		const struct sdl3_screen_layout *layout)
{
	const char *source = screen->context;
	int row = screen->kind == UI_SCREEN_BIRTH_STATS && layout->width < 100 ?
		screen->content_row + screen->row_count + 2 : screen->content_row;
	int last_row = visual->rows - 3;

	if (!source[0] || layout->context_width <= 0) return;
	sdl3_ui_draw_text(visual, screen->context_title, layout->context_col, row,
		layout->context_width, theme->accent);
	if (screen->context_title[0]) {
		row += screen->kind == UI_SCREEN_BIRTH_STATS && layout->width < 100 ? 1 : 2;
	}
	while (*source && row <= last_row) {
		char line[192];
		size_t length = 0;
		size_t draw_length;

		while (source[length] && source[length] != '\n' &&
				length < (size_t)layout->context_width &&
				length + 1 < sizeof(line)) {
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
		sdl3_ui_draw_text(visual, line, layout->context_col, row,
			layout->context_width, theme->text);
		row++;
		source += draw_length;
		while (*source == ' ') source++;
		if (*source == '\n') source++;
	}
}

static void draw_memorial(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme,
		const struct sdl3_screen_layout *layout)
{
	int last_row = visual->rows - 3;
	int i;

	for (i = 0; i < screen->row_count; i++) {
		const struct sdl3_screen_row *entry = &screen->rows[i];
		int row = screen->content_row + i * 3;
		SDL_Color heading = entry->enabled ?
			sdl3_theme_color(theme, entry->attr) : theme->muted;

		if (row + 2 > last_row) break;
		if (i == screen->cursor) {
			sdl3_ui_fill_cells(renderer, visual, layout->left, row,
				layout->width, 3, theme->selection);
		}
		sdl3_ui_draw_text(visual, entry->label, layout->left + 2, row,
			MAX(1, layout->width - 4), heading);
		sdl3_ui_draw_text(visual, entry->prefix, layout->left + 4, row + 1,
			MAX(1, layout->width - 6), theme->accent);
		sdl3_ui_draw_text(visual, entry->detail, layout->left + 4, row + 2,
			MAX(1, layout->width - 6), theme->text);
	}
}

static void draw_store_intro(const struct sdl3_screen *screen,
		struct sdl3_visual *visual, const struct sdl3_theme *theme,
		const struct sdl3_screen_layout *layout)
{
	const char *source = screen->context;
	int row = 5;

	while (*source && row <= 6) {
		char line[192];
		size_t length = MIN(strlen(source), (size_t)layout->width);
		size_t draw_length = length;

		if (source[length] && length > 1) {
			size_t break_at = length;

			while (break_at > length / 2 && source[break_at] != ' ') {
				break_at--;
			}
			if (break_at > length / 2) draw_length = break_at;
		}
		memcpy(line, source, draw_length);
		line[draw_length] = '\0';
		sdl3_ui_draw_text(visual, line, layout->left, row, layout->width,
			theme->text);
		source += draw_length;
		while (*source == ' ') source++;
		row++;
	}
}

void sdl3_screen_draw_document(const struct sdl3_screen *screen,
		struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int left, int width)
{
	int visible = MIN(screen->content_rows,
		MAX(0, visual->rows - screen->content_row - 3));
	for (int i = 0; i < visible; i++) {
		int line = screen->document_line_offset + i;
		size_t start, length;
		if (line >= screen->document_line_count) break;
		start = screen->document_line_starts[line];
		length = MIN(screen->document_line_lengths[line], (size_t)width);
		for (size_t j = 0; j < length; j++) {
			size_t index = start + j;
			wchar_t glyph;
			if (index >= screen->document_length) break;
			glyph = screen->document_text[index];
			if (glyph < 32) continue;
			sdl3_font_draw(&visual->font, (uint32_t)glyph,
				sdl3_theme_color(theme, screen->document_attrs[index]),
				(float)(visual->origin_x + (left + (int)j) * visual->cell_width),
				(float)(visual->origin_y + (screen->content_row + i) * visual->cell_height),
				(float)visual->cell_width, (float)visual->cell_height);
		}
	}
}

void sdl3_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	struct sdl3_screen_layout layout;

	if (!screen || !screen->active || !renderer || !visual || !theme) return;
	sdl3_ui_fill_output(renderer, visual,
		(SDL_Color){ 0, 0, 0, SDL_ALPHA_OPAQUE });
	if (!sdl3_screen_measure(screen, visual->cols, visual->rows, &layout)) {
		return;
	}
	sdl3_ui_draw_text(visual, screen->title, layout.left, 2, layout.width,
		theme->title);
	sdl3_ui_draw_text(visual, screen->subtitle, layout.left, 4, layout.width,
		theme->muted);
	draw_tabs(screen, renderer, visual, theme, &layout);
	if (screen->kind == UI_SCREEN_HELP) {
		sdl3_screen_draw_document(screen, visual, theme, layout.left, layout.width);
		sdl3_ui_draw_text(visual, screen->help, layout.left, visual->rows - 2,
			layout.width, theme->muted);
		return;
	}
	if (sdl3_run_summary_screen_draw(screen, renderer, visual, theme,
			layout.left, layout.width)) {
		sdl3_ui_draw_text(visual, screen->help, layout.left, visual->rows - 2,
			layout.width, theme->muted);
		return;
	}
	if (sdl3_monster_lore_screen_draw(screen, renderer, visual, theme,
			layout.left, layout.width)) {
		sdl3_ui_draw_text(visual, screen->help, layout.left, visual->rows - 2,
			layout.width, theme->muted);
		return;
	}
	if (sdl3_dungeon_map_screen_draw(screen, renderer, visual, theme,
			layout.left, layout.width)) {
		sdl3_ui_draw_text(visual, screen->help, layout.left, visual->rows - 2,
			layout.width, theme->muted);
		return;
	}
	if (sdl3_world_map_screen_draw(screen, renderer, visual, theme, layout.left,
			layout.width)) {
		sdl3_ui_draw_text(visual, screen->help, layout.left, visual->rows - 2,
			layout.width, theme->muted);
		return;
	}
	if (screen->kind == UI_SCREEN_MEMORIAL) {
		draw_memorial(screen, renderer, visual, theme, &layout);
		sdl3_ui_draw_text(visual, screen->help, layout.left, visual->rows - 2,
			layout.width, theme->muted);
		return;
	}
	if (screen->kind == UI_SCREEN_STORE && screen->context[0]) {
		draw_store_intro(screen, visual, theme, &layout);
	}
	draw_rows(screen, renderer, visual, theme, &layout);
	if (screen->kind != UI_SCREEN_STORE) {
		draw_context(screen, visual, theme, &layout);
	}
	sdl3_ui_draw_text(visual, screen->help, layout.left, visual->rows - 2,
		layout.width, theme->muted);
}
