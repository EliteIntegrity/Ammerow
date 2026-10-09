/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/ui-draw.c
 * \brief Shared character-cell drawing primitives for SDL3 interfaces.
 */

#include "angband.h"

#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

void sdl3_ui_set_color(SDL_Renderer *renderer, SDL_Color color)
{
	if (!renderer) return;
	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void sdl3_ui_draw_text(struct sdl3_visual *visual, const char *text, int col,
		int row, int maximum, SDL_Color color)
{
	int i;

	if (!visual || !visual->font_fits || !text || maximum <= 0) return;
	for (i = 0; text[i] && i < maximum; i++) {
		sdl3_font_draw(&visual->font, (uint8_t)text[i], color,
			(float)(visual->origin_x + (col + i) * visual->cell_width),
			(float)(visual->origin_y + row * visual->cell_height),
			(float)visual->cell_width, (float)visual->cell_height);
	}
}

void sdl3_ui_draw_map_text(struct sdl3_visual *visual, const char *text,
		int col, int row, int maximum, SDL_Color color)
{
	struct sdl3_font *font;
	int i;

	if (!visual || !visual->font_fits || !text || maximum <= 0) return;
	font = visual->map_font_loaded ? &visual->map_font : &visual->font;
	for (i = 0; text[i] && i < maximum; i++) {
		sdl3_font_draw(font, (uint8_t)text[i], color,
			(float)(visual->origin_x + (col + i) * visual->cell_width),
			(float)(visual->origin_y + row * visual->cell_height),
			(float)visual->cell_width, (float)visual->cell_height);
	}
}

void sdl3_ui_fill_output(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, SDL_Color color)
{
	SDL_FRect rect;

	if (!renderer || !visual) return;
	rect = (SDL_FRect) {
		0.0f, 0.0f, (float)visual->output_width,
		(float)visual->output_height
	};
	sdl3_ui_set_color(renderer, color);
	SDL_RenderFillRect(renderer, &rect);
}

void sdl3_ui_fill_cells(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, int col, int row, int cols, int rows,
		SDL_Color color)
{
	SDL_FRect rect;

	if (!renderer || !visual || cols <= 0 || rows <= 0) return;
	rect = (SDL_FRect) {
		(float)(visual->origin_x + col * visual->cell_width),
		(float)(visual->origin_y + row * visual->cell_height),
		(float)(cols * visual->cell_width),
		(float)(rows * visual->cell_height)
	};
	sdl3_ui_set_color(renderer, color);
	SDL_RenderFillRect(renderer, &rect);
}

void sdl3_ui_draw_backplate(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, const struct sdl3_theme *theme,
		const struct sdl3_cell_bounds *bounds, int offset_col, int offset_row)
{
	SDL_Color color;
	SDL_FRect horizontal;
	SDL_FRect vertical;
	float grid_left;
	float grid_top;
	float grid_right;
	float grid_bottom;
	float padding_x;
	float padding_y;
	float radius;
	float left;
	float top;
	float right;
	float bottom;

	if (!renderer || !visual || !theme || !bounds || bounds->cols <= 0 ||
			bounds->rows <= 0) {
		return;
	}
	grid_left = (float)visual->origin_x;
	grid_top = (float)visual->origin_y;
	grid_right = grid_left + visual->cols * visual->cell_width;
	grid_bottom = grid_top + visual->rows * visual->cell_height;
	padding_x = (float)SDL_max(3, visual->cell_width / 3);
	padding_y = (float)SDL_max(2, visual->cell_height / 8);
	left = (float)(visual->origin_x +
		(bounds->col + offset_col) * visual->cell_width) - padding_x;
	top = (float)(visual->origin_y +
		(bounds->row + offset_row) * visual->cell_height) - padding_y;
	right = (float)(visual->origin_x +
		(bounds->col + offset_col + bounds->cols) * visual->cell_width) +
		padding_x;
	bottom = (float)(visual->origin_y +
		(bounds->row + offset_row + bounds->rows) * visual->cell_height) +
		padding_y;
	left = SDL_max(grid_left, left);
	top = SDL_max(grid_top, top);
	right = SDL_min(grid_right, right);
	bottom = SDL_min(grid_bottom, bottom);
	if (right <= left || bottom <= top) return;
	radius = SDL_min(6.0f, SDL_min((right - left) / 4.0f,
		(bottom - top) / 4.0f));
	horizontal = (SDL_FRect) {
		left + radius, top, right - left - 2.0f * radius, bottom - top
	};
	vertical = (SDL_FRect) {
		left, top + radius, right - left, bottom - top - 2.0f * radius
	};
	color = theme->canvas;
	color.a = 168;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	sdl3_ui_set_color(renderer, color);
	SDL_RenderFillRect(renderer, &horizontal);
	SDL_RenderFillRect(renderer, &vertical);
}

void sdl3_ui_draw_selection(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int col, int row, int cols)
{
	if (!theme) return;
	sdl3_ui_fill_cells(renderer, visual, col, row, cols, 1,
		theme->selection);
}

void sdl3_ui_draw_panel(SDL_Renderer *renderer,
		const struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int col, int row, int cols, int rows, bool shade_output)
{
	SDL_FRect shadow;
	SDL_FRect panel;
	SDL_FRect accent;

	if (!renderer || !visual || !theme || cols <= 0 || rows <= 0) return;
	shadow = (SDL_FRect) {
		(float)(visual->origin_x + col * visual->cell_width + 6),
		(float)(visual->origin_y + row * visual->cell_height + 6),
		(float)(cols * visual->cell_width),
		(float)(rows * visual->cell_height)
	};
	panel = (SDL_FRect) {
		(float)(visual->origin_x + col * visual->cell_width),
		(float)(visual->origin_y + row * visual->cell_height),
		(float)(cols * visual->cell_width),
		(float)(rows * visual->cell_height)
	};
	accent = panel;
	accent.w = 3.0f;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	if (shade_output) {
		sdl3_ui_fill_output(renderer, visual, (SDL_Color){ 0, 0, 0, 150 });
	}
	sdl3_ui_set_color(renderer, (SDL_Color){ 0, 0, 0, 205 });
	SDL_RenderFillRect(renderer, &shadow);
	sdl3_ui_set_color(renderer, theme->panel);
	SDL_RenderFillRect(renderer, &panel);
	sdl3_ui_set_color(renderer, theme->panel_border);
	SDL_RenderRect(renderer, &panel);
	sdl3_ui_set_color(renderer, theme->accent);
	SDL_RenderFillRect(renderer, &accent);
}
