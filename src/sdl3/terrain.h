/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/terrain.h
 * \brief Deterministic, presentation-only ASCII terrain variation.
 *
 *
 */

#ifndef INCLUDED_SDL3_TERRAIN_H
#define INCLUDED_SDL3_TERRAIN_H

#include "angband.h"
#include "cave.h"
#include "terrain-visual-data.h"

enum sdl3_terrain_style {
	/* Numeric order is part of the versioned frontend-config contract. */
	SDL3_TERRAIN_CLASSIC = 0,
	SDL3_TERRAIN_NATURAL,
	SDL3_TERRAIN_STYLE_COUNT
};

enum sdl3_tile_glyph_policy {
	SDL3_TILE_GLYPH_AUTO = 0, /* Top-down source glyph/attr match. */
	SDL3_TILE_GLYPH_REPLACE,  /* Semantic terrain owns this cave cell. */
	SDL3_TILE_GLYPH_PRESERVE  /* Player, cursor or precise navigation marker. */
};

struct sdl3_terrain_variant {
	wchar_t codepoint;
	uint8_t foreground;
	uint8_t lighting;
	uint8_t style;
	const struct terrain_visual_recipe *recipe;
	const char *tile_id;
	const char *tile_overlay_key;
	enum sdl3_tile_glyph_policy tile_glyph_policy;
	/** Zero uses the theme default; otherwise 0..255 remembered brightness. */
	uint8_t remembered_brightness;
	bool in_view;
	bool present;
	bool active;
};

struct sdl3_terrain_overlay {
	struct sdl3_terrain_variant *cells;
	int cols;
	int rows;
};

uint32_t sdl3_terrain_level_key(int32_t level_turn, int depth, int width,
		int height);
const struct terrain_visual_recipe *sdl3_terrain_recipe_for_grid(
		struct chunk *c, struct loc grid, int feature);
const char *sdl3_terrain_style_name(enum sdl3_terrain_style style);
bool sdl3_terrain_style_from_name(const char *name,
		enum sdl3_terrain_style *style);
enum sdl3_terrain_style sdl3_terrain_style_change(
		enum sdl3_terrain_style style, int delta);
bool sdl3_terrain_variant(const struct terrain_visual_recipe *recipe,
		enum grid_light_level lighting, wchar_t codepoint, uint8_t foreground,
		enum sdl3_terrain_style style, struct sdl3_terrain_variant *variant);

bool sdl3_terrain_overlay_resize(struct sdl3_terrain_overlay *overlay,
		int cols, int rows);
void sdl3_terrain_overlay_free(struct sdl3_terrain_overlay *overlay);
void sdl3_terrain_overlay_clear(struct sdl3_terrain_overlay *overlay);
bool sdl3_terrain_overlay_set(struct sdl3_terrain_overlay *overlay, int col,
		int row, const struct sdl3_terrain_variant *variant);
const struct sdl3_terrain_variant *sdl3_terrain_overlay_get(
		const struct sdl3_terrain_overlay *overlay, int col, int row);

#endif /* INCLUDED_SDL3_TERRAIN_H */
