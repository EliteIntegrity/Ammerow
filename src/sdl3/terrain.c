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
#include "map-visual-data.h"
#include "trap.h"
#include "world-entry.h"
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

/** Presentation only: never substitute a real feature or change a saved map.
 * The caller supplies map_info()'s exposed/mimicked feature, not hidden rock.
 * Interior permanent blocks are the village's shops; perimeter permanent rock
 * is the safety rim. Granite outcrops (including broken ruins) stay natural. */
const char *sdl3_terrain_full_tile_for_grid(struct chunk *c,
		struct chunk *known, struct loc grid, int exposed_feature)
{
	const struct level *level;
	bool boundary;

	if (!c || !square_in_bounds(c, grid) || exposed_feature == FEAT_NONE)
		return NULL;
	if (exposed_feature == FEAT_MERIDIAN_STONE) return "meridian-marker-stone";
	if (exposed_feature != FEAT_PERM && exposed_feature != FEAT_GRANITE)
		return NULL;
	boundary = !square_in_bounds_fully(c, grid);
	if (boundary && exposed_feature == FEAT_PERM && known &&
			known->width == c->width && known->height == c->height) {
		struct loc inward = loc(MAX(1, MIN(c->width - 2, grid.x)),
			MAX(1, MIN(c->height - 2, grid.y)));
		/* The actual automatic-travel band is one cell inside the rim.
		 * Only an already-known, passable, authored exit gets an opening;
		 * no unseen route or unrelated edge is revealed. The arrows remain. */
		if (world_entry_is_edge_exit_grid(c, inward) &&
				square(known, inward)->feat != FEAT_NONE &&
				square_ispassable(known, inward)) return "travel-opening";
	}
	level = world_chunk_level(c);
	if (!level || level->biome != WORLD_BIOME_VILLAGE) return NULL;
	return exposed_feature == FEAT_PERM && !boundary ?
		"village-masonry" : "village-rock-bank";
}

/** These are ordinary traversable ground recipes, not every passable feature.
 * In particular, water/lava/pits and side-view cave air must not match. */
bool sdl3_terrain_plain_ground(const char *tile_id)
{
	static const char *ids[] = {
		"open-floor", "meridian-floor", "dirt", "patch-of-grass",
		"cavewood-floor", "lake-margin", "pine-wood-floor", "marsh-ground",
		"heath-ground", "meadow-ground", "ruins-floor", "scrub-ground"
	};
	if (!tile_id) return false;
	for (size_t i = 0; i < N_ELEMENTS(ids); i++)
		if (streq(tile_id, ids[i])) return true;
	return false;
}

/** Transparent top-down props need the same quiet ground as nearby cells.
 * Missing prop artwork must still draw its glyph above this underlay. */
const char *sdl3_terrain_underlay(const char *tile_id)
{
	static const char *props[] = {
		"door", "open-door", "broken-door", "up-staircase", "down-staircase",
		"rubble", "passable-rubble", "general-store", "armoury", "weapon-smiths",
		"bookstore", "alchemy-shop", "magic-shop", "black-market", "home",
		"fishing-shop", "relic-broker"
	};
	if (!tile_id) return NULL;
	if (streq(tile_id, "tree") || streq(tile_id, "pine-tree") || streq(tile_id, "brake"))
		return "patch-of-grass";
	for (size_t i = 0; i < N_ELEMENTS(props); i++)
		if (streq(tile_id, props[i])) return "open-floor";
	return NULL;
}

/** Use only the trap already exposed by map_info and its public category name.
 * In particular, a gas vent must not identify which gas, nor a pit its spikes.
 * Webs cover objects in the core display; other traps do not. */
const char *sdl3_terrain_trap_tile(const struct grid_data *data)
{
	const struct map_visual_binding *binding;
	const struct trap *trap;
	char semantic[128];
	size_t i;
	if (!data || data->hallucinate || data->is_player || data->m_idx ||
			!(trap = data->trap) || !trap->kind || !trap->kind->name ||
			!(trf_has(trap->flags, TRF_VISIBLE) ||
			  trf_has(trap->flags, TRF_GLYPH) || trf_has(trap->flags, TRF_WEB)))
		return NULL;
	if (!trf_has(trap->flags, TRF_WEB) && (data->first_kind ||
			data->multiple_objects || data->unseen_object || data->unseen_money))
		return NULL;
	if (strlen(trap->kind->name) >= sizeof(semantic)) return NULL;
	my_strcpy(semantic, trap->kind->name, sizeof(semantic));
	for (i = 0; semantic[i]; i++) if (semantic[i] == ' ') semantic[i] = '-';
	binding = map_visual_for("trap", semantic);
	return binding ? binding->key : NULL;
}

enum sdl3_tile_glyph_policy sdl3_terrain_glyph_policy(int exposed_feature)
{
	/* The sign is art; its number/letter remains the actual game's label. */
	return exposed_feature > FEAT_NONE && exposed_feature < FEAT_MAX &&
		tf_has(f_info[exposed_feature].flags, TF_SHOP) ?
		SDL3_TILE_GLYPH_PRESERVE : SDL3_TILE_GLYPH_AUTO;
}

enum grid_surface_type sdl3_terrain_crossing(const struct grid_data *data)
{
	/* This is an underlay: an actor/item must not erase a known threshold.
	 * Use exposed map metadata, never infer a route from its glyph. */
	return data && data->f_idx > FEAT_NONE && data->f_idx < FEAT_MAX &&
		!data->hallucinate && data->surface >= GRID_SURFACE_EXIT_NORTH &&
		data->surface <= GRID_SURFACE_EXIT_WEST ? data->surface : GRID_SURFACE_NONE;
}

enum sdl3_ascii_terrain sdl3_terrain_ascii_kind(const struct grid_data *data,
		const char *full_tile_id)
{
	/* Do not infer terrain from a glyph: a monster/item can also use # or %.
	 * map_info supplies only the exposed/mimicked feature, not hidden rock. */
	if (!data || data->f_idx <= FEAT_NONE || data->f_idx >= FEAT_MAX ||
			data->hallucinate || data->is_player || data->m_idx ||
			data->first_kind || data->multiple_objects || data->unseen_object ||
			data->unseen_money || data->trap || data->surface != GRID_SURFACE_NONE)
		return SDL3_ASCII_TERRAIN_NORMAL;
	if (full_tile_id && streq(full_tile_id, "travel-opening"))
		return SDL3_ASCII_TERRAIN_OPENING;
	return feat_is_wall(data->f_idx) && !feat_is_passable(data->f_idx) &&
		!tf_has(f_info[data->f_idx].flags, TF_DOOR_ANY) ?
		SDL3_ASCII_TERRAIN_WALL : SDL3_ASCII_TERRAIN_NORMAL;
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
			cell->full_tile_id == variant->full_tile_id &&
			cell->tile_overlay_key == variant->tile_overlay_key &&
			cell->tile_glyph_policy == variant->tile_glyph_policy &&
			cell->ascii_terrain == variant->ascii_terrain &&
			cell->crossing == variant->crossing &&
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
