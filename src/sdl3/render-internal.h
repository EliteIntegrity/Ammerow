/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/render-internal.h
 * \brief Renderer-private compositor and cache state.
 */

#ifndef INCLUDED_SDL3_RENDER_INTERNAL_H
#define INCLUDED_SDL3_RENDER_INTERNAL_H

#include "sdl3/render.h"

#include "sdl3/monster-art.h"
#include "sdl3/tiles.h"

struct sdl3_visual {
	struct sdl3_font font;
	struct sdl3_font map_font;
	struct sdl3_font card_font;
	char map_font_path[SDL3_FONT_PATH_CAPACITY];
	struct sdl3_monster_art_cache monster_art;
	struct sdl3_monster_art_cache shopkeeper_art;
	struct sdl3_monster_art_cache home_art;
	struct sdl3_monster_art_pool map_art;
	struct sdl3_tile_registry tiles;
	int hybrid_tile_size;
	SDL_Texture *grid_texture;
	SDL_Texture *zoom_out_texture;
	const struct sdl3_grid *zoom_out_source;
	SDL_Texture *scene_texture;
	int output_width;
	int output_height;
	int grid_texture_width;
	int grid_texture_height;
	int zoom_out_texture_width;
	int zoom_out_texture_height;
	int scene_texture_width;
	int scene_texture_height;
	int origin_x;
	int origin_y;
	int cell_width;
	int cell_height;
	int cols;
	int rows;
	int map_zoom_percent;
	bool font_loaded;
	bool map_font_loaded;
	bool card_font_loaded;
	bool font_fits;
	bool grid_cache_unavailable;
	bool scene_cache_unavailable;
	bool scene_cache_ready;
	int last_cell_updates;
	Uint64 frames_presented;
	Uint64 cell_updates_total;
	Uint64 present_wait_ns_total;
	Uint64 present_wait_ns_max;
};

bool sdl3_visual_ensure_card_font(struct sdl3_visual *visual,
		SDL_Renderer *renderer);

#endif /* INCLUDED_SDL3_RENDER_INTERNAL_H */
