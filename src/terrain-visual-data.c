/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file terrain-visual-data.c
 * \brief Parser and immutable registry for ASCII terrain visual recipes.
 */

#include "angband.h"
#include "cave.h"
#include "datafile.h"
#include "game-world.h"
#include "terrain-visual-data.h"

enum terrain_visual_field {
	TERRAIN_VISUAL_FIELD_BASE = 0x01,
	TERRAIN_VISUAL_FIELD_TILE = 0x02,
	TERRAIN_VISUAL_FIELD_REQUIRED = TERRAIN_VISUAL_FIELD_BASE
};

struct terrain_visual_parse_recipe {
	struct terrain_visual_recipe recipe;
	uint8_t fields;
	struct terrain_visual_parse_recipe *next;
};

struct terrain_visual_parse_state {
	char *profile;
	char *semantic_materials[TERRAIN_VISUAL_SEMANTIC_COUNT];
	struct terrain_visual_parse_recipe *recipes;
	struct terrain_visual_parse_recipe *current;
	int count;
};

static struct terrain_visual_recipe *terrain_visual_recipes;
static int terrain_visual_recipe_count;
static char *terrain_visual_semantic_materials[
	TERRAIN_VISUAL_SEMANTIC_COUNT];

static bool same_id(const char *a, const char *b)
{
	return a && b && streq(a, b);
}

static enum parser_error parse_visual_profile(struct parser *p)
{
	struct terrain_visual_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");

	if (!datafile_art_id_is_valid(id)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	string_free(state->profile);
	state->profile = string_make(id);
	state->current = NULL;
	return PARSE_ERROR_NONE;
}

static bool semantic_from_name(const char *name,
		enum terrain_visual_semantic *semantic)
{
	static const char *const names[] = { "water", "road", "wood" };
	int i;

	for (i = 0; i < (int)N_ELEMENTS(names); i++) {
		if (streq(name, names[i])) {
			*semantic = (enum terrain_visual_semantic)i;
			return true;
		}
	}
	return false;
}

static enum parser_error parse_visual_semantic(struct parser *p)
{
	struct terrain_visual_parse_state *state = parser_priv(p);
	const char *name = parser_getsym(p, "semantic");
	const char *material = parser_getsym(p, "material");
	enum terrain_visual_semantic semantic;

	if (!semantic_from_name(name, &semantic) ||
			!datafile_art_id_is_valid(material)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->semantic_materials[semantic]) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->semantic_materials[semantic] = string_make(material);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_visual_material(struct parser *p)
{
	struct terrain_visual_parse_state *state = parser_priv(p);
	struct terrain_visual_parse_recipe *cursor;
	struct terrain_visual_parse_recipe *record;
	const char *id = parser_getsym(p, "id");

	if (!state->profile) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!datafile_art_id_is_valid(id)) return PARSE_ERROR_INVALID_VALUE;
	for (cursor = state->recipes; cursor; cursor = cursor->next) {
		if (same_id(cursor->recipe.profile, state->profile) &&
				same_id(cursor->recipe.material, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	record = mem_zalloc(sizeof(*record));
	record->recipe.profile = string_make(state->profile);
	record->recipe.material = string_make(id);
	record->next = state->recipes;
	state->recipes = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_rgb(struct parser *p,
		struct terrain_visual_rgb *rgb, uint8_t field)
{
	struct terrain_visual_parse_state *state = parser_priv(p);
	unsigned int red = parser_getuint(p, "red");
	unsigned int green = parser_getuint(p, "green");
	unsigned int blue = parser_getuint(p, "blue");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & field) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (red > 255 || green > 255 || blue > 255) {
		return PARSE_ERROR_OUT_OF_BOUNDS;
	}
	rgb->r = (uint8_t)red;
	rgb->g = (uint8_t)green;
	rgb->b = (uint8_t)blue;
	state->current->fields |= field;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_visual_base(struct parser *p)
{
	struct terrain_visual_parse_state *state = parser_priv(p);

	return state->current ? parse_rgb(p, &state->current->recipe.base,
		TERRAIN_VISUAL_FIELD_BASE) : PARSE_ERROR_MISSING_RECORD_HEADER;
}

static enum parser_error parse_visual_tile(struct parser *p)
{
	struct terrain_visual_parse_state *state = parser_priv(p);
	const char *id;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & TERRAIN_VISUAL_FIELD_TILE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	id = parser_getsym(p, "id");
	if (!datafile_art_id_is_valid(id)) return PARSE_ERROR_INVALID_VALUE;
	state->current->recipe.tile_id = string_make(id);
	state->current->fields |= TERRAIN_VISUAL_FIELD_TILE;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_terrain_visuals(void)
{
	struct parser *p = parser_new();
	struct terrain_visual_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "semantic sym semantic sym material", parse_visual_semantic);
	parser_reg(p, "profile sym id", parse_visual_profile);
	parser_reg(p, "material sym id", parse_visual_material);
	parser_reg(p, "base uint red uint green uint blue", parse_visual_base);
	parser_reg(p, "tile sym id", parse_visual_tile);
	return p;
}

static errr run_parse_terrain_visuals(struct parser *p)
{
	return parse_file_quit_not_found(p, "terrain_visuals");
}

static void free_parse_state(struct terrain_visual_parse_state *state)
{
	struct terrain_visual_parse_recipe *record;

	if (!state) return;
	record = state->recipes;
	while (record) {
		struct terrain_visual_parse_recipe *next = record->next;

		string_free((char *)record->recipe.profile);
		string_free((char *)record->recipe.material);
		string_free((char *)record->recipe.tile_id);
		mem_free(record);
		record = next;
	}
	string_free(state->profile);
	for (int i = 0; i < TERRAIN_VISUAL_SEMANTIC_COUNT; i++) {
		string_free(state->semantic_materials[i]);
	}
	mem_free(state);
}

static bool parse_state_has_default_material(
		const struct terrain_visual_parse_state *state, const char *material)
{
	const struct terrain_visual_parse_recipe *record;

	for (record = state->recipes; record; record = record->next) {
		if (same_id(record->recipe.profile, "default") &&
				same_id(record->recipe.material, material)) {
			return true;
		}
	}
	return false;
}

static void cleanup_terrain_visuals(void)
{
	int i;

	for (i = 0; i < terrain_visual_recipe_count; i++) {
		string_free((char *)terrain_visual_recipes[i].profile);
		string_free((char *)terrain_visual_recipes[i].material);
		string_free((char *)terrain_visual_recipes[i].tile_id);
	}
	mem_free(terrain_visual_recipes);
	terrain_visual_recipes = NULL;
	terrain_visual_recipe_count = 0;
	for (i = 0; i < TERRAIN_VISUAL_SEMANTIC_COUNT; i++) {
		string_free(terrain_visual_semantic_materials[i]);
		terrain_visual_semantic_materials[i] = NULL;
	}
}

static errr finish_parse_terrain_visuals(struct parser *p)
{
	struct terrain_visual_parse_state *state = parser_priv(p);
	struct terrain_visual_parse_recipe *record;
	struct terrain_visual_parse_recipe *next;
	int index;

	if (!state->count) {
		free_parse_state(state);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (index = 0; index < TERRAIN_VISUAL_SEMANTIC_COUNT; index++) {
		if (!state->semantic_materials[index] ||
				!parse_state_has_default_material(state,
					state->semantic_materials[index])) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	for (record = state->recipes; record; record = record->next) {
		if ((record->fields & TERRAIN_VISUAL_FIELD_REQUIRED) !=
				TERRAIN_VISUAL_FIELD_REQUIRED ||
				(!same_id(record->recipe.profile, "default") &&
				!parse_state_has_default_material(state,
					record->recipe.material))) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	cleanup_terrain_visuals();
	terrain_visual_recipes = mem_zalloc((size_t)state->count *
		sizeof(*terrain_visual_recipes));
	index = state->count - 1;
	for (record = state->recipes; record; record = next, index--) {
		next = record->next;
		terrain_visual_recipes[index] = record->recipe;
		mem_free(record);
	}
	terrain_visual_recipe_count = state->count;
	string_free(state->profile);
	for (index = 0; index < TERRAIN_VISUAL_SEMANTIC_COUNT; index++) {
		terrain_visual_semantic_materials[index] =
			state->semantic_materials[index];
	}
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser terrain_visual_parser = {
	"terrain visuals",
	init_parse_terrain_visuals,
	run_parse_terrain_visuals,
	finish_parse_terrain_visuals,
	cleanup_terrain_visuals
};

const struct terrain_visual_recipe *terrain_visual_recipe_for(
		const char *profile, const char *material)
{
	const struct terrain_visual_recipe *fallback = NULL;
	int i;

	if (!material) return NULL;
	for (i = 0; i < terrain_visual_recipe_count; i++) {
		const struct terrain_visual_recipe *recipe =
			&terrain_visual_recipes[i];

		if (!same_id(recipe->material, material)) continue;
		if (profile && same_id(recipe->profile, profile)) return recipe;
		if (same_id(recipe->profile, "default")) fallback = recipe;
	}
	return fallback;
}

const char *terrain_visual_material_for_semantic(
		enum terrain_visual_semantic semantic)
{
	if (semantic < 0 || semantic >= TERRAIN_VISUAL_SEMANTIC_COUNT) return NULL;
	return terrain_visual_semantic_materials[semantic];
}

bool terrain_visual_material_is_known(const char *material)
{
	return terrain_visual_recipe_for("default", material) != NULL;
}

bool terrain_visual_profile_is_known(const char *profile)
{
	int i;

	if (!profile) return false;
	for (i = 0; i < terrain_visual_recipe_count; i++) {
		if (same_id(terrain_visual_recipes[i].profile, profile)) return true;
	}
	return false;
}

bool terrain_visual_data_validate_features(void)
{
	int i;

	for (i = 0; i < FEAT_MAX; i++) {
		if (f_info[i].visual_material &&
				!terrain_visual_material_is_known(f_info[i].visual_material)) {
			plog_fmt("Terrain feature %s references unknown visual material %s",
				f_info[i].name ? f_info[i].name : "(unnamed)",
				f_info[i].visual_material);
			return false;
		}
	}
	return true;
}

bool terrain_visual_data_validate_world(void)
{
	const struct world_site *site;

	for (site = world_sites; site; site = site->next) {
		if (site->visual_profile &&
				!terrain_visual_profile_is_known(site->visual_profile)) {
			plog_fmt("World site %s references unknown visual profile %s",
				site->id, site->visual_profile);
			return false;
		}
		if (site->victory_visual_profile &&
				!terrain_visual_profile_is_known(
					site->victory_visual_profile)) {
			plog_fmt("World site %s references unknown victory visual profile %s",
				site->id, site->victory_visual_profile);
			return false;
		}
	}
	return true;
}
