/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-art.h
 * \brief Font-aware coloured-ASCII rendering from compiled art plates.
 */

#ifndef INCLUDED_SDL3_MONSTER_ART_H
#define INCLUDED_SDL3_MONSTER_ART_H

#include "sdl3/font.h"

#define SDL3_MONSTER_ART_ID_CAPACITY 128
#define SDL3_MONSTER_ART_KEY_CAPACITY 160
#define SDL3_MONSTER_ART_MAX_COLS 96
#define SDL3_MONSTER_ART_MAX_ROWS 96
#define SDL3_MONSTER_ART_MAX_CELLS \
	(SDL3_MONSTER_ART_MAX_COLS * SDL3_MONSTER_ART_MAX_ROWS)
#define SDL3_MONSTER_ART_RAMP_CAPACITY 16
#define SDL3_MONSTER_ART_POOL_CAPACITY 16
#define SDL3_MONSTER_ART_MAP_MIN_ZOOM 400
#define SDL3_MONSTER_ART_PLATE_HEADER_SIZE 16

struct sdl3_monster_art_plate {
	int cols;
	int rows;
	int source_width;
	int source_height;
	uint8_t coverage[SDL3_MONSTER_ART_MAX_CELLS];
	SDL_Color colors[SDL3_MONSTER_ART_MAX_CELLS];
};

struct sdl3_monster_art_cell {
	uint32_t codepoint;
	SDL_Color color;
	bool visible;
};

struct sdl3_monster_art {
	int cols;
	int rows;
	struct sdl3_monster_art_cell cells[SDL3_MONSTER_ART_MAX_CELLS];
};

struct sdl3_monster_art_cache {
	struct sdl3_font font;
	struct sdl3_monster_art art;
	char asset_id[SDL3_MONSTER_ART_KEY_CAPACITY];
	char font_path[SDL3_FONT_PATH_CAPACITY];
	int target_width;
	int target_height;
	bool map_style;
	bool font_loaded;
	bool ready;
	bool failed;
};

struct sdl3_monster_art_pool_entry {
	struct sdl3_monster_art_cache *cache;
	Uint64 last_used;
};

struct sdl3_monster_art_pool {
	struct sdl3_monster_art_pool_entry entries[
		SDL3_MONSTER_ART_POOL_CAPACITY];
	Uint64 use_clock;
};

bool sdl3_monster_art_id_valid(const char *id);
void sdl3_monster_art_make_key(char *destination, size_t capacity,
		const char *art_id, const char *base_art_id);
bool sdl3_monster_art_plate_decode(const uint8_t *bytes, size_t size,
		struct sdl3_monster_art_plate *plate);
bool sdl3_monster_art_plate_render(
		const struct sdl3_monster_art_plate *plate, const char *ramp,
		int ramp_count, int cols, int rows, bool map_style,
		struct sdl3_monster_art *art);
void sdl3_monster_art_cache_init(struct sdl3_monster_art_cache *cache);
void sdl3_monster_art_cache_free(struct sdl3_monster_art_cache *cache);
bool sdl3_monster_art_draw(struct sdl3_monster_art_cache *cache,
		SDL_Renderer *renderer, const char *font_path, const char *asset_id,
		const SDL_FRect *target);
bool sdl3_monster_art_draw_shopkeeper(struct sdl3_monster_art_cache *cache,
		SDL_Renderer *renderer, const char *font_path, const char *asset_key,
		const SDL_FRect *target);
bool sdl3_monster_art_map_enabled(int zoom_percent);
void sdl3_monster_art_pool_init(struct sdl3_monster_art_pool *pool);
void sdl3_monster_art_pool_free(struct sdl3_monster_art_pool *pool);
bool sdl3_monster_art_pool_draw(struct sdl3_monster_art_pool *pool,
		SDL_Renderer *renderer, const char *font_path, const char *asset_id,
		const SDL_FRect *target);

#endif /* INCLUDED_SDL3_MONSTER_ART_H */
