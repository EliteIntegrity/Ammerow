/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-lore-screen.c
 * \brief SDL3 renderer for knowledge-safe creature recall.
 *
 *
 */

#include "angband.h"

#include "sdl3/monster-lore-screen.h"
#include "sdl3/render-internal.h"
#include "sdl3/screen-model.h"
#include "sdl3/screen.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

#define LORE_PORTRAIT_MIN_COLS 20
#define LORE_PORTRAIT_MIN_ROWS 12

static void draw_portrait(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int col, int cols)
{
	char asset_id[SDL3_MONSTER_ART_ID_CAPACITY];
	SDL_FRect target;
	struct sdl3_font *glyph_font;
	int top = screen->content_row;
	int rows = MAX(0, visual->rows - top - 4);
	int art_rows;

	if (cols < LORE_PORTRAIT_MIN_COLS || rows < LORE_PORTRAIT_MIN_ROWS) return;
	if (!visual->card_font_loaded &&
			!sdl3_visual_ensure_card_font(visual, renderer)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not prepare the creature-recall glyph font: %s",
			SDL_GetError());
	}
	glyph_font = visual->card_font_loaded ? &visual->card_font : &visual->font;
	sdl3_ui_draw_panel(renderer, visual, theme, col, top, cols, rows, false);
	sdl3_ui_draw_text(visual, screen->subject_unique ? "SINGULAR RECORD" :
		"FIELD PORTRAIT", col + 2, top + 1, cols - 4,
		screen->subject_unique ? sdl3_theme_color(theme, COLOUR_VIOLET) :
		theme->accent);
	sdl3_ui_draw_text(visual, screen->subject_name, col + 2, top + 2,
		cols - 4, theme->text);
	sdl3_ui_draw_text(visual, screen->subject_base, col + 2, top + 3,
		cols - 4, theme->muted);
	art_rows = rows - 6;
	target = (SDL_FRect) {
		(float)(visual->origin_x + (col + 2) * visual->cell_width),
		(float)(visual->origin_y + (top + 5) * visual->cell_height),
		(float)((cols - 4) * visual->cell_width),
		(float)(art_rows * visual->cell_height)
	};
	sdl3_monster_art_make_key(asset_id, sizeof(asset_id),
		screen->subject_art_id, screen->subject_base_art_id);
	if (!sdl3_monster_art_draw(&visual->monster_art, renderer,
			visual->font.path, asset_id, &target)) {
		sdl3_font_draw(glyph_font, (uint32_t)screen->subject_glyph,
			sdl3_theme_color(theme, screen->subject_attr), target.x, target.y,
			target.w, target.h);
		sdl3_ui_draw_text(visual, "NO PORTRAIT RECORDED", col + 2,
			top + rows - 2, cols - 4, theme->muted);
	}
}

bool sdl3_monster_lore_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	int document_width;
	int portrait_col;
	int portrait_cols;

	if (!screen || screen->kind != UI_SCREEN_MONSTER_LORE || !renderer ||
			!visual || !theme) return false;
	document_width = MIN(screen->document_cols, width);
	portrait_col = left + document_width + 4;
	portrait_cols = left + width - portrait_col;
	sdl3_screen_draw_document(screen, visual, theme, left, document_width);
	draw_portrait(screen, renderer, visual, theme, portrait_col, portrait_cols);
	return true;
}

#undef LORE_PORTRAIT_MIN_COLS
#undef LORE_PORTRAIT_MIN_ROWS
