/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/render.h
 * \brief Character-cell compositor for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_RENDER_H
#define INCLUDED_SDL3_RENDER_H

#include "sdl3/presenter.h"

struct sdl3_visual;

bool sdl3_visual_init(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const char *font_path, const char *map_font_path, int cols, int rows,
		int map_zoom_percent);
void sdl3_visual_free(struct sdl3_visual *visual);
bool sdl3_visual_resize(struct sdl3_visual *visual, SDL_Renderer *renderer);
bool sdl3_visual_set_grid_size(struct sdl3_visual *visual,
		SDL_Renderer *renderer, int cols, int rows);
bool sdl3_visual_set_font(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const char *font_path);
bool sdl3_visual_set_map_font(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const char *font_path);
bool sdl3_visual_set_map_zoom(struct sdl3_visual *visual,
		SDL_Renderer *renderer, int zoom_percent);
void sdl3_visual_invalidate_grid_cache(struct sdl3_visual *visual);
bool sdl3_visual_recreate_renderer_resources(struct sdl3_visual *visual,
		SDL_Renderer *renderer);
void sdl3_visual_render(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const struct sdl3_presentation_frame *frame);
bool sdl3_visual_render_overlay_frame(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_overlay_frame *frame);
bool sdl3_visual_point_to_cell(const struct sdl3_visual *visual, float x,
		float y, const struct sdl3_map_view *map_view, int *col, int *row);

#endif /* INCLUDED_SDL3_RENDER_H */
