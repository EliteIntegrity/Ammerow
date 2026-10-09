/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/inspect-card.c
 * \brief Renderer-owned examine card shared by creatures, items and terrain.
 *
 *
 */

#include "sdl3/inspect-card.h"

#include "sdl3/monster-card-layout.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"

static void set_draw_color(SDL_Renderer *renderer, SDL_Color color)
{
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

static void draw_text(struct sdl3_visual *visual, const char *text, int col,
		int row, int maximum, SDL_Color color)
{
	int i;

	if (!visual->font_fits || !text || maximum <= 0) return;
	for (i = 0; text[i] && i < maximum; i++) {
		uint8_t codepoint = (uint8_t)text[i];
		float x = (float)(visual->origin_x +
			(col + i) * visual->cell_width);
		float y = (float)(visual->origin_y + row * visual->cell_height);

		if (codepoint < SDL3_FONT_FIRST_GLYPH ||
				codepoint > SDL3_FONT_LAST_GLYPH) {
			codepoint = '?';
		}
		sdl3_font_draw(&visual->font, codepoint, color, x, y,
			(float)visual->cell_width, (float)visual->cell_height);
	}
}

static int draw_wrapped_text(struct sdl3_visual *visual, const char *text,
		int col, int row, int cols, int rows, SDL_Color color)
{
	const char *cursor = text;
	int used = 0;

	while (cursor && *cursor && used < rows) {
		char line[256];
		int length = 0;

		while (*cursor == ' ') cursor++;
		while (*cursor && length < cols) {
			const char *word = cursor;
			int word_length = 0;

			while (word[word_length] && word[word_length] != ' ') {
				word_length++;
			}
			if (length && length + 1 + word_length > cols) break;
			if (length) line[length++] = ' ';
			while (word_length && length < cols) {
				line[length++] = *cursor++;
				word_length--;
			}
			while (*cursor && *cursor != ' ') cursor++;
			while (*cursor == ' ') cursor++;
			if (length >= cols) break;
		}
		if (!length && *cursor) line[length++] = *cursor++;
		line[length] = '\0';
		draw_text(visual, line, col, row + used, cols, color);
		used++;
	}
	return used;
}

void sdl3_inspect_card_draw(const struct sdl3_inspect_card *card,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view)
{
	SDL_Color glyph_color;
	SDL_FRect panel;
	SDL_FRect shadow;
	SDL_FRect accent;
	SDL_FRect portrait;
	struct sdl3_monster_card_layout layout;
	struct sdl3_font *portrait_font;
	int text_col;
	int text_cols;
	int detail_row;
	int detail_end;
	int flavor_row;
	int flavor_rows;
	int pending_details = 0;
	int i;
	const char *details[4];

	if (!card || !card->active || !renderer || !visual || !theme ||
			!map_view || !map_view->active || !visual->font_fits) {
		return;
	}
	if (!sdl3_monster_card_layout_compute(&layout, map_view->col,
			map_view->row, map_view->cols, map_view->rows, card->cursor_col,
			visual->cell_width, visual->cell_height, card->big)) {
		return;
	}
	panel = (SDL_FRect) {
		(float)(visual->origin_x + layout.panel_col * visual->cell_width),
		(float)(visual->origin_y + layout.panel_row * visual->cell_height),
		(float)(layout.panel_cols * visual->cell_width),
		(float)(layout.panel_rows * visual->cell_height)
	};
	shadow = panel;
	shadow.x += 7.0f;
	shadow.y += 7.0f;
	accent = panel;
	accent.w = 3.0f;

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	set_draw_color(renderer, (SDL_Color){ 0, 0, 0, 190 });
	SDL_RenderFillRect(renderer, &shadow);
	set_draw_color(renderer, (SDL_Color){
		theme->panel.r, theme->panel.g, theme->panel.b, 248
	});
	SDL_RenderFillRect(renderer, &panel);
	set_draw_color(renderer, theme->panel_border);
	SDL_RenderRect(renderer, &panel);
	set_draw_color(renderer, theme->accent);
	SDL_RenderFillRect(renderer, &accent);

	text_col = layout.text_col;
	text_cols = layout.text_cols;
	draw_text(visual, card->name, text_col, layout.panel_row + 1, text_cols,
		card->emphasis ? sdl3_theme_color(theme, COLOUR_VIOLET) :
		theme->title);
	draw_text(visual, card->subtitle, text_col, layout.panel_row + 2, text_cols,
		theme->muted);
	draw_text(visual, card->status, text_col, layout.panel_row + 3, text_cols,
		theme->accent);

	portrait = (SDL_FRect) {
		(float)(visual->origin_x + text_col * visual->cell_width),
		(float)(visual->origin_y + layout.portrait_row * visual->cell_height),
		(float)(text_cols * visual->cell_width),
		(float)(layout.portrait_rows * visual->cell_height)
	};
	set_draw_color(renderer, (SDL_Color){
		theme->canvas.r, theme->canvas.g, theme->canvas.b, 205
	});
	SDL_RenderFillRect(renderer, &portrait);
	portrait_font = visual->card_font_loaded ? &visual->card_font :
		&visual->font;
	glyph_color = sdl3_theme_color(theme, card->attr);
	if (!sdl3_monster_art_draw(&visual->monster_art, renderer,
			visual->font.path, card->asset_key, &portrait)) {
		sdl3_font_draw(portrait_font, (uint32_t)card->glyph, glyph_color,
			portrait.x, portrait.y, portrait.w, portrait.h);
	}

	/* Facts outrank flavor when the map panel is short.  Each populated lore
	 * section gets at least one row; taller panels donate a second wrapped row
	 * before the remaining space is used for the original description. */
	detail_row = layout.detail_row;
	detail_end = layout.detail_end;
	draw_text(visual, card->details[0], text_col, detail_row++, text_cols,
		theme->text);
	draw_text(visual, card->details[1], text_col, detail_row++, text_cols,
		theme->muted);
	details[0] = card->details[2];
	details[1] = card->details[3];
	details[2] = card->details[4];
	details[3] = card->details[5];
	for (i = 0; i < 4; i++) {
		if (details[i][0]) pending_details++;
	}
	for (i = 0; i < 4 && detail_row < detail_end; i++) {
		int available;
		int rows;
		int used;

		if (!details[i][0]) continue;
		available = detail_end - detail_row;
		rows = 1;
		if (available > pending_details) rows = 2;
		used = draw_wrapped_text(visual, details[i], text_col, detail_row,
			text_cols, rows, i == 0 ? theme->text : theme->muted);
		detail_row += SDL_max(1, used);
		pending_details--;
	}
	flavor_row = detail_row + 1;
	flavor_rows = detail_end - flavor_row;
	if (flavor_rows > 0) {
		draw_wrapped_text(visual, card->flavor, text_col, flavor_row,
			text_cols, flavor_rows, theme->text);
	}
	draw_text(visual, card->footer, text_col,
		layout.panel_row + layout.panel_rows - 1, text_cols, theme->muted);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}
