/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/presenter.c
 * \brief Presentation and compositor ownership for the SDL3 frontend.
 */

#include "sdl3/presenter.h"

#include "sdl3/render-internal.h"

#include <string.h>

struct sdl3_presenter {
	struct sdl3_visual visual;
};

struct sdl3_presenter *sdl3_presenter_create(SDL_Renderer *renderer,
		const char *font_path, const char *map_font_path, int cols, int rows,
		int map_zoom_percent)
{
	struct sdl3_presenter *presenter = SDL_calloc(1, sizeof(*presenter));

	if (!presenter) return NULL;
	if (!sdl3_visual_init(&presenter->visual, renderer, font_path,
			map_font_path, cols, rows, map_zoom_percent)) {
		SDL_free(presenter);
		return NULL;
	}
	return presenter;
}

void sdl3_presenter_destroy(struct sdl3_presenter *presenter)
{
	if (!presenter) return;
	sdl3_visual_free(&presenter->visual);
	SDL_free(presenter);
}

void sdl3_presenter_get_info(const struct sdl3_presenter *presenter,
		struct sdl3_presenter_info *info)
{
	const struct sdl3_visual *visual;

	if (!info) return;
	memset(info, 0, sizeof(*info));
	info->interface_font_path = "";
	info->map_font_path = "";
	if (!presenter) return;
	visual = &presenter->visual;
	if (visual->font_loaded) info->interface_font_path = visual->font.path;
	info->map_font_path = visual->map_font_path;
	info->interface_font_loaded = visual->font_loaded;
	info->map_font_loaded = visual->map_font_loaded;
	info->grid_cache_active = visual->grid_texture != NULL;
	info->interface_font_point_size = visual->font.point_size;
	info->map_font_point_size = visual->map_font_loaded ?
		visual->map_font.point_size : visual->font.point_size;
	info->output_width = visual->output_width;
	info->output_height = visual->output_height;
	info->origin_x = visual->origin_x;
	info->origin_y = visual->origin_y;
	info->cell_width = visual->cell_width;
	info->cell_height = visual->cell_height;
	info->last_cell_updates = visual->last_cell_updates;
	info->frames_presented = visual->frames_presented;
	info->cell_updates_total = visual->cell_updates_total;
	info->present_wait_ns_total = visual->present_wait_ns_total;
	info->present_wait_ns_max = visual->present_wait_ns_max;
}

bool sdl3_presenter_resize(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer)
{
	return presenter && sdl3_visual_resize(&presenter->visual, renderer);
}

bool sdl3_presenter_set_grid_size(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, int cols, int rows)
{
	return presenter && sdl3_visual_set_grid_size(&presenter->visual,
		renderer, cols, rows);
}

bool sdl3_presenter_set_font(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const char *font_path)
{
	return presenter && sdl3_visual_set_font(&presenter->visual, renderer,
		font_path);
}

bool sdl3_presenter_set_map_font(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const char *font_path)
{
	return presenter && sdl3_visual_set_map_font(&presenter->visual,
		renderer, font_path);
}

bool sdl3_presenter_set_map_zoom(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, int zoom_percent)
{
	return presenter && sdl3_visual_set_map_zoom(&presenter->visual,
		renderer, zoom_percent);
}

void sdl3_presenter_invalidate_grid_cache(struct sdl3_presenter *presenter)
{
	if (presenter) sdl3_visual_invalidate_grid_cache(&presenter->visual);
}

bool sdl3_presenter_recreate_renderer_resources(
		struct sdl3_presenter *presenter, SDL_Renderer *renderer)
{
	return presenter && sdl3_visual_recreate_renderer_resources(
		&presenter->visual, renderer);
}

void sdl3_presenter_render(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const struct sdl3_presentation_frame *frame)
{
	if (presenter) sdl3_visual_render(&presenter->visual, renderer, frame);
}

bool sdl3_presenter_render_overlay(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const struct sdl3_overlay_frame *frame)
{
	return presenter && sdl3_visual_render_overlay_frame(&presenter->visual,
		renderer, frame);
}

bool sdl3_presenter_point_to_cell(const struct sdl3_presenter *presenter,
		float x, float y, const struct sdl3_map_view *map_view, int *col,
		int *row)
{
	return presenter && sdl3_visual_point_to_cell(&presenter->visual, x, y,
		map_view, col, row);
}

bool sdl3_presenter_prime_map_glyphs(struct sdl3_presenter *presenter)
{
	static const char glyphs[] = "#.@+%$abcdefghijklmnopqrstuvwxyz";
	SDL_Color color = { 255, 255, 255, 255 };
	struct sdl3_visual *visual;
	int i;

	if (!presenter) return false;
	visual = &presenter->visual;
	if (!visual->map_font_loaded) return true;
	for (i = 0; glyphs[i]; i++) {
		if (!sdl3_font_draw(&visual->map_font, (uint8_t)glyphs[i], color,
				0.0f, 0.0f, (float)visual->map_font.cell_width,
				(float)visual->map_font.cell_height)) {
			return false;
		}
	}
	return true;
}
