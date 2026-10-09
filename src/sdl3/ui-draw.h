/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/ui-draw.h
 * \brief Shared character-cell drawing primitives for SDL3 interfaces.
 */

#ifndef INCLUDED_SDL3_UI_DRAW_H
#define INCLUDED_SDL3_UI_DRAW_H

#include <SDL3/SDL.h>

struct sdl3_theme;
struct sdl3_visual;
struct sdl3_cell_bounds;

void sdl3_ui_set_color(SDL_Renderer *renderer, SDL_Color color);
void sdl3_ui_draw_text(struct sdl3_visual *visual, const char *text, int col,
		int row, int maximum, SDL_Color color);
/** Draw map glyphs with the selected map font, falling back to the UI font. */
void sdl3_ui_draw_map_text(struct sdl3_visual *visual, const char *text,
		int col, int row, int maximum, SDL_Color color);
void sdl3_ui_fill_output(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, SDL_Color color);
void sdl3_ui_fill_cells(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, int col, int row, int cols, int rows,
		SDL_Color color);
void sdl3_ui_draw_backplate(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, const struct sdl3_theme *theme,
		const struct sdl3_cell_bounds *bounds, int offset_col, int offset_row);
void sdl3_ui_draw_selection(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int col, int row, int cols);
void sdl3_ui_draw_panel(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int col, int row, int cols, int rows, bool shade_output);

#endif /* INCLUDED_SDL3_UI_DRAW_H */
