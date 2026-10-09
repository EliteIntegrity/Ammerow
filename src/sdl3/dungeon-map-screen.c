/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/dungeon-map-screen.c
 * \brief SDL3 renderer for Angband's knowledge-safe compressed level map.
 *
 *
 */

#include "angband.h"

#include "sdl3/dungeon-map-screen.h"
#include "sdl3/render-internal.h"
#include "sdl3/screen-model.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

bool sdl3_dungeon_map_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	struct sdl3_font *font;
	int map_cols;
	int map_rows;
	int panel_cols;
	int panel_rows;
	int panel_left;
	int panel_top;
	int region_top = 6;
	int region_rows;
	int row, col;

	if (!screen || screen->kind != UI_SCREEN_DUNGEON_MAP || !renderer ||
			!visual || !theme) {
		return false;
	}
	map_cols = MIN(screen->dungeon_map_cols, MAX(0, width - 2));
	map_rows = MIN(screen->dungeon_map_rows,
		MAX(0, visual->rows - region_top - 5));
	if (map_cols <= 0 || map_rows <= 0) return true;
	panel_cols = map_cols + 2;
	panel_rows = map_rows + 2;
	region_rows = MAX(panel_rows, visual->rows - region_top - 3);
	panel_left = left + MAX(0, (width - panel_cols) / 2);
	panel_top = region_top + MAX(0, (region_rows - panel_rows) / 2);
	sdl3_ui_draw_panel(renderer, visual, theme, panel_left, panel_top,
		panel_cols, panel_rows, false);
	font = visual->map_font_loaded ? &visual->map_font : &visual->font;
	for (row = 0; row < map_rows; row++) {
		for (col = 0; col < map_cols; col++) {
			const struct sdl3_screen_map_cell *cell =
				&screen->dungeon_map_cells[
					row * screen->dungeon_map_cols + col];
			bool player_cell = col == screen->dungeon_player_col &&
				row == screen->dungeon_player_row;
			SDL_Color color;

			if (player_cell) {
				sdl3_ui_fill_cells(renderer, visual, panel_left + col + 1,
					panel_top + row + 1, 1, 1, theme->selection);
			}
			if (!cell->glyph || cell->glyph == L' ') continue;
			color = player_cell ? theme->title :
				sdl3_theme_color(theme, cell->attr);
			sdl3_font_draw(font, (uint32_t)cell->glyph, color,
				(float)(visual->origin_x +
					(panel_left + col + 1) * visual->cell_width),
				(float)(visual->origin_y +
					(panel_top + row + 1) * visual->cell_height),
				(float)visual->cell_width, (float)visual->cell_height);
		}
	}
	return true;
}
