/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/terrain.c
 * \brief Deterministic, presentation-only ASCII terrain variation.
 *
 *
 */

#include "sdl3/terrain.h"
#include "game-world.h"
#include "world-overworld.h"

static uint32_t mix(uint32_t value)
{
	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	value *= UINT32_C(0x846ca68b);
	value ^= value >> 16;
	return value;
}

const struct terrain_visual_recipe *sdl3_terrain_recipe_for_grid(
		struct chunk *c, struct loc grid, int feature)
{
	const struct level *level;
	const struct world_site *site;
	const char *profile = "default";
	const char *material;

	if (!c || !square_in_bounds(c, grid) || feature <= FEAT_NONE ||
			feature >= FEAT_MAX) {
		return NULL;
	}
	level = world_chunk_level(c);
	site = level ? world_site_by_id(level->site_id) : NULL;
	if (site && player && player->total_winner &&
			site->victory_visual_profile) {
		profile = site->victory_visual_profile;
	} else if (site && site->visual_profile) {
		profile = site->visual_profile;
	} else if (level && level->biome != WORLD_BIOME_NONE) {
		/* Compatibility for old world data without authored site profiles. */
		profile = world_overworld_biome_name(level->biome);
	}
	if (sqinfo_has(square(c, grid)->info, SQUARE_WATER)) {
		material = terrain_visual_material_for_semantic(
			TERRAIN_VISUAL_SEMANTIC_WATER);
	} else if (sqinfo_has(square(c, grid)->info, SQUARE_ROAD)) {
		material = terrain_visual_material_for_semantic(
			TERRAIN_VISUAL_SEMANTIC_ROAD);
	} else if (sqinfo_has(square(c, grid)->info, SQUARE_WOOD)) {
		material = terrain_visual_material_for_semantic(
			TERRAIN_VISUAL_SEMANTIC_WOOD);
	} else {
		material = f_info[feature].visual_material;
	}
	return terrain_visual_recipe_for(profile, material);
}

const char *sdl3_terrain_style_name(enum sdl3_terrain_style style)
{
	switch (style) {
	case SDL3_TERRAIN_CLASSIC:
		return "Classic";
	case SDL3_TERRAIN_NATURAL:
	default:
		return "Natural";
	}
}

bool sdl3_terrain_style_from_name(const char *name,
		enum sdl3_terrain_style *style)
{
	int i;

	if (!name || !style) return false;
	for (i = SDL3_TERRAIN_CLASSIC; i < SDL3_TERRAIN_STYLE_COUNT; i++) {
		enum sdl3_terrain_style candidate = (enum sdl3_terrain_style)i;

		if (my_stricmp(name, sdl3_terrain_style_name(candidate)) == 0) {
			*style = candidate;
			return true;
		}
	}
	return false;
}

enum sdl3_terrain_style sdl3_terrain_style_change(
		enum sdl3_terrain_style style, int delta)
{
	int changed = (int)style + (delta < 0 ? -1 : 1);

	if (!delta) return style;
	if (changed < SDL3_TERRAIN_CLASSIC) changed = SDL3_TERRAIN_STYLE_COUNT - 1;
	if (changed >= SDL3_TERRAIN_STYLE_COUNT) changed = SDL3_TERRAIN_CLASSIC;
	return (enum sdl3_terrain_style)changed;
}

uint32_t sdl3_terrain_level_key(int32_t level_turn, int depth, int width,
		int height)
{
	uint32_t value = (uint32_t)level_turn ^
		((uint32_t)depth * UINT32_C(0x9e3779b1)) ^
		((uint32_t)width * UINT32_C(0x85ebca77)) ^
		((uint32_t)height * UINT32_C(0xc2b2ae3d));

	return mix(value);
}

bool sdl3_terrain_variant(const struct terrain_visual_recipe *recipe,
		enum grid_light_level lighting, wchar_t codepoint, uint8_t foreground,
		enum sdl3_terrain_style style, struct sdl3_terrain_variant *variant)
{
	if (!variant) return false;
	memset(variant, 0, sizeof(*variant));
	variant->codepoint = codepoint;
	variant->foreground = foreground;
	variant->lighting = (uint8_t)lighting;
	variant->style = (uint8_t)style;
	variant->recipe = NULL;
	variant->tile_id = NULL;
	variant->remembered_brightness = 0;
	variant->in_view = lighting == LIGHTING_LOS ||
		lighting == LIGHTING_TORCH || lighting == LIGHTING_DARK;
	variant->present = false;
	variant->active = false;
	if (style <= SDL3_TERRAIN_CLASSIC || style >= SDL3_TERRAIN_STYLE_COUNT ||
			!recipe) {
		return false;
	}
	variant->recipe = recipe;
	variant->active = true;
	return true;
}

bool sdl3_terrain_overlay_resize(struct sdl3_terrain_overlay *overlay,
		int cols, int rows)
{
	struct sdl3_terrain_variant *cells;

	if (!overlay || cols <= 0 || rows <= 0) return false;
	if (overlay->cells && overlay->cols == cols && overlay->rows == rows) {
		return true;
	}
	cells = mem_zalloc((size_t)cols * rows * sizeof(*cells));
	if (!cells) return false;
	mem_free(overlay->cells);
	overlay->cells = cells;
	overlay->cols = cols;
	overlay->rows = rows;
	return true;
}

void sdl3_terrain_overlay_free(struct sdl3_terrain_overlay *overlay)
{
	if (!overlay) return;
	mem_free(overlay->cells);
	memset(overlay, 0, sizeof(*overlay));
}

void sdl3_terrain_overlay_clear(struct sdl3_terrain_overlay *overlay)
{
	if (!overlay || !overlay->cells) return;
	memset(overlay->cells, 0,
		(size_t)overlay->cols * overlay->rows * sizeof(*overlay->cells));
}

bool sdl3_terrain_overlay_set(struct sdl3_terrain_overlay *overlay, int col,
		int row, const struct sdl3_terrain_variant *variant)
{
	struct sdl3_terrain_variant *cell;

	if (!overlay || !overlay->cells || col < 0 || col >= overlay->cols ||
			row < 0 || row >= overlay->rows) {
		return false;
	}
	cell = &overlay->cells[row * overlay->cols + col];
	/* Unknown or out-of-map cells carry no presentation metadata. */
	if (!variant || !variant->present) {
		if (!cell->present) return false;
		memset(cell, 0, sizeof(*cell));
		return true;
	}
	if (cell->codepoint == variant->codepoint &&
			cell->foreground == variant->foreground &&
			cell->lighting == variant->lighting &&
			cell->style == variant->style &&
			cell->recipe == variant->recipe &&
			cell->tile_id == variant->tile_id &&
			cell->tile_overlay_key == variant->tile_overlay_key &&
			cell->tile_glyph_policy == variant->tile_glyph_policy &&
			cell->remembered_brightness == variant->remembered_brightness &&
			cell->in_view == variant->in_view &&
			cell->present == variant->present &&
			cell->active == variant->active) {
		return false;
	}
	*cell = *variant;
	return true;
}

const struct sdl3_terrain_variant *sdl3_terrain_overlay_get(
		const struct sdl3_terrain_overlay *overlay, int col, int row)
{
	const struct sdl3_terrain_variant *cell;

	if (!overlay || !overlay->cells || col < 0 || col >= overlay->cols ||
			row < 0 || row >= overlay->rows) {
		return NULL;
	}
	cell = &overlay->cells[row * overlay->cols + col];
	return cell->present ? cell : NULL;
}
