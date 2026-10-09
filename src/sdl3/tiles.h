/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/tiles.h
 * \brief Data-driven, nearest-neighbour map tile atlases for SDL3.
 */

#ifndef INCLUDED_SDL3_TILES_H
#define INCLUDED_SDL3_TILES_H

#include "angband.h"

#include <SDL3/SDL.h>

#define SDL3_TILE_KEY_CAPACITY 128
#define SDL3_TILE_ATLAS_CAPACITY 16
#define SDL3_TILE_ENTRY_CAPACITY 4096

enum sdl3_tile_namespace {
	SDL3_TILE_FEATURE = 0,
	SDL3_TILE_MONSTER,
	SDL3_TILE_OBJECT,
	SDL3_TILE_ACTOR
};

struct sdl3_tile_atlas {
	SDL_Texture *texture;
	char id[32];
	int width;
	int height;
	int cell_width;
	int cell_height;
};

struct sdl3_tile_entry {
	char key[SDL3_TILE_KEY_CAPACITY];
	int atlas;
	int col;
	int row;
};

struct sdl3_tile_registry {
	struct sdl3_tile_atlas atlases[SDL3_TILE_ATLAS_CAPACITY];
	struct sdl3_tile_entry entries[SDL3_TILE_ENTRY_CAPACITY];
	int atlas_count;
	int entry_count;
	bool loaded;
};

struct sdl3_map_art_subject;
struct sdl3_cell;

bool sdl3_tile_make_key(char *destination, size_t capacity,
		enum sdl3_tile_namespace name_space, const char *stable_id);
bool sdl3_tiles_init(struct sdl3_tile_registry *registry,
		SDL_Renderer *renderer, const char *manifest);
void sdl3_tiles_free(struct sdl3_tile_registry *registry);
bool sdl3_tiles_draw(struct sdl3_tile_registry *registry,
		SDL_Renderer *renderer, const char *key, int world_col, int world_row,
		const SDL_FRect *target, uint8_t brightness);
/* Shared by stationary map cells and moving combat subjects.  False means
 * the caller must keep the ordinary glyph (including stale/hidden subjects). */
bool sdl3_tiles_draw_subject(struct sdl3_tile_registry *registry,
		SDL_Renderer *renderer, const struct sdl3_map_art_subject *subject,
		const struct sdl3_cell *cell, int world_col, int world_row,
		const SDL_FRect *target, uint8_t brightness);

#endif /* INCLUDED_SDL3_TILES_H */
