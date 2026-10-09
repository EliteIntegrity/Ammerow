/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/tiles.c
 * \brief Data-driven, nearest-neighbour map tile atlases for SDL3.
 */

#include "sdl3/tiles.h"

#include "datafile.h"
#include "init.h"
#include "sdl3/map-view.h"

#include <SDL3_image/SDL_image.h>

#include <errno.h>
#include <limits.h>

static const char *namespace_name(enum sdl3_tile_namespace name_space)
{
	switch (name_space) {
	case SDL3_TILE_FEATURE: return "feature";
	case SDL3_TILE_MONSTER: return "monster";
	case SDL3_TILE_OBJECT: return "object";
	case SDL3_TILE_ACTOR: return "actor";
	default: return NULL;
	}
}

static char *trim(char *text)
{
	char *end;

	while (*text && isspace((unsigned char)*text)) text++;
	end = text + strlen(text);
	while (end > text && isspace((unsigned char)*(end - 1))) end--;
	*end = '\0';
	return text;
}

/** Parse one bounded base-10 integer and reject all trailing characters. */
static bool parse_int(const char *text, int *value)
{
	char *end;
	long parsed;

	if (!text || !text[0] || !value) return false;
	errno = 0;
	parsed = strtol(text, &end, 10);
	if (errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX ||
			end == text || *end) {
		return false;
	}
	*value = (int)parsed;
	return true;
}

static bool parse_coordinates(char *text, int *col, int *row)
{
	char *comma;

	if (!text || !(comma = strchr(text, ','))) return false;
	*comma = '\0';
	return parse_int(trim(text), col) && parse_int(trim(comma + 1), row);
}

/** Remove and return the next colon-delimited field from a mutable line. */
static char *take_field(char **cursor)
{
	char *field;
	char *colon;

	if (!cursor || !*cursor || !(colon = strchr(*cursor, ':'))) return NULL;
	field = *cursor;
	*colon = '\0';
	*cursor = colon + 1;
	return trim(field);
}

static bool parse_atlas_spec(char *text, char **id, char **image_name,
		char **index_name, int *cell_width, int *cell_height)
{
	char *cursor;
	char *width_text;
	char *height_text;

	if (!text || !prefix(text, "atlas:")) return false;
	cursor = text + 6;
	*id = take_field(&cursor);
	*image_name = take_field(&cursor);
	*index_name = take_field(&cursor);
	width_text = take_field(&cursor);
	height_text = trim(cursor);
	return *id && (*id)[0] && *image_name && (*image_name)[0] &&
		*index_name && (*index_name)[0] && width_text && width_text[0] &&
		height_text[0] && parse_int(width_text, cell_width) &&
		parse_int(height_text, cell_height);
}

static bool normalize_id(char *destination, size_t capacity,
		const char *source)
{
	size_t used = 0;
	bool separator = false;

	if (!destination || !capacity || !source || !source[0]) return false;
	while (*source) {
		unsigned char c = (unsigned char)*source++;

		if (isalnum(c)) {
			if (separator && used > 0) {
				if (used + 1 >= capacity) return false;
				destination[used++] = '-';
			}
			if (used + 1 >= capacity) return false;
			destination[used++] = (char)tolower(c);
			separator = false;
		} else {
			separator = used > 0;
		}
	}
	if (!used) return false;
	destination[used] = '\0';
	return datafile_art_id_is_valid(destination);
}

bool sdl3_tile_make_key(char *destination, size_t capacity,
		enum sdl3_tile_namespace name_space, const char *stable_id)
{
	char normalized[SDL3_TILE_KEY_CAPACITY];
	const char *prefix = namespace_name(name_space);
	size_t required;

	if (!destination || !capacity) return false;
	destination[0] = '\0';
	if (!prefix || !normalize_id(normalized, sizeof(normalized), stable_id)) {
		return false;
	}
	required = strlen(prefix) + 1 + strlen(normalized) + 1;
	if (required > capacity) return false;
	strnfmt(destination, capacity, "%s:%s", prefix, normalized);
	return true;
}

static bool leaf_name_is_safe(const char *name)
{
	return name && name[0] && path_filename_index(name) == 0 &&
		!streq(name, ".") && !streq(name, "..") && !strstr(name, "..");
}

static bool namespace_from_name(const char *name,
		enum sdl3_tile_namespace *name_space)
{
	int i;

	if (!name || !name_space) return false;
	for (i = SDL3_TILE_FEATURE; i <= SDL3_TILE_ACTOR; i++) {
		if (streq(name, namespace_name((enum sdl3_tile_namespace)i))) {
			*name_space = (enum sdl3_tile_namespace)i;
			return true;
		}
	}
	return false;
}

static bool entry_key_exists(const struct sdl3_tile_registry *registry,
		const char *key)
{
	int i;

	for (i = 0; i < registry->entry_count; i++) {
		if (streq(registry->entries[i].key, key)) return true;
	}
	return false;
}

static bool load_index(struct sdl3_tile_registry *registry, int atlas_index,
		const char *filename)
{
	char path[1024];
	char line[512];
	ang_file *file;
	enum sdl3_tile_namespace name_space = SDL3_TILE_FEATURE;
	bool have_namespace = false;
	int line_number = 0;

	path_build(path, sizeof(path), ANGBAND_DIR_TILES, filename);
	file = file_open(path, MODE_READ, FTYPE_TEXT);
	if (!file) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not open tile index %s", path);
		return false;
	}
	while (file_getl(file, line, sizeof(line))) {
		struct sdl3_tile_entry *entry;
		char *text = trim(line);
		char *arrow;
		char *id;
		char *coordinates;
		char normalized[SDL3_TILE_KEY_CAPACITY];
		char key[SDL3_TILE_KEY_CAPACITY];
		int col;
		int row;

		line_number++;
		if (!text[0] || text[0] == '#') continue;
		if (prefix(text, "domain:")) {
			have_namespace = namespace_from_name(trim(text + 7), &name_space);
			if (!have_namespace) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Invalid tile domain in %s line %d", path, line_number);
				file_close(file);
				return false;
			}
			continue;
		}
		if (!have_namespace || !(arrow = strstr(text, "->"))) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Malformed tile entry in %s line %d", path, line_number);
			file_close(file);
			return false;
		}
		*arrow = '\0';
		id = trim(text);
		coordinates = trim(arrow + 2);
		if (!normalize_id(normalized, sizeof(normalized), id) ||
				!sdl3_tile_make_key(key, sizeof(key), name_space, normalized) ||
				!parse_coordinates(coordinates, &col, &row) ||
				col < 0 || row < 0) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid tile entry in %s line %d", path, line_number);
			file_close(file);
			return false;
		}
		if (entry_key_exists(registry, key) ||
				registry->entry_count >= SDL3_TILE_ENTRY_CAPACITY) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Duplicate or excessive tile key %s in %s line %d", key,
				path, line_number);
			file_close(file);
			return false;
		}
		entry = &registry->entries[registry->entry_count++];
		memset(entry, 0, sizeof(*entry));
		my_strcpy(entry->key, key, sizeof(entry->key));
		entry->atlas = atlas_index;
		entry->col = col;
		entry->row = row;
	}
	file_close(file);
	return true;
}

static bool load_atlas(struct sdl3_tile_registry *registry,
		SDL_Renderer *renderer, const char *id, const char *image_name,
		const char *index_name, int cell_width, int cell_height)
{
	struct sdl3_tile_atlas *atlas;
	SDL_Surface *surface;
	char path[1024];
	int atlas_index;
	int first_entry;
	int i;

	if (registry->atlas_count >= SDL3_TILE_ATLAS_CAPACITY ||
			!datafile_art_id_is_valid(id) || !leaf_name_is_safe(image_name) ||
			!leaf_name_is_safe(index_name) || cell_width <= 0 || cell_height <= 0) {
		return false;
	}
	path_build(path, sizeof(path), ANGBAND_DIR_TILES, image_name);
	surface = IMG_Load(path);
	if (!surface) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not load tile atlas %s: %s", path, SDL_GetError());
		return false;
	}
	if (surface->w <= 0 || surface->h <= 0 || surface->w % cell_width ||
			surface->h % cell_height) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Tile atlas %s is not divisible into %dx%d cells", path,
			cell_width, cell_height);
		SDL_DestroySurface(surface);
		return false;
	}
	atlas_index = registry->atlas_count;
	atlas = &registry->atlases[registry->atlas_count++];
	memset(atlas, 0, sizeof(*atlas));
	my_strcpy(atlas->id, id, sizeof(atlas->id));
	atlas->width = surface->w;
	atlas->height = surface->h;
	atlas->cell_width = cell_width;
	atlas->cell_height = cell_height;
	atlas->texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_DestroySurface(surface);
	if (!atlas->texture) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not upload tile atlas %s: %s", path, SDL_GetError());
		return false;
	}
	SDL_SetTextureScaleMode(atlas->texture, SDL_SCALEMODE_NEAREST);
	SDL_SetTextureBlendMode(atlas->texture, SDL_BLENDMODE_BLEND);
	first_entry = registry->entry_count;
	if (!load_index(registry, atlas_index, index_name)) return false;
	for (i = first_entry; i < registry->entry_count; i++) {
		const struct sdl3_tile_entry *entry = &registry->entries[i];

		if ((entry->col + 1) * cell_width > atlas->width ||
				(entry->row + 1) * cell_height > atlas->height) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Tile key %s lies outside atlas %s", entry->key, path);
			return false;
		}
	}
	return true;
}

void sdl3_tiles_free(struct sdl3_tile_registry *registry)
{
	int i;

	if (!registry) return;
	for (i = 0; i < registry->atlas_count; i++) {
		if (registry->atlases[i].texture) {
			SDL_DestroyTexture(registry->atlases[i].texture);
		}
	}
	memset(registry, 0, sizeof(*registry));
}

bool sdl3_tiles_init(struct sdl3_tile_registry *registry,
		SDL_Renderer *renderer, const char *manifest)
{
	char path[1024];
	char line[1024];
	ang_file *file;
	int line_number = 0;

	if (!registry || !renderer || !manifest || !leaf_name_is_safe(manifest)) return false;
	memset(registry, 0, sizeof(*registry));
	path_build(path, sizeof(path), ANGBAND_DIR_TILES, manifest);
	file = file_open(path, MODE_READ, FTYPE_TEXT);
	if (!file) return false;
	while (file_getl(file, line, sizeof(line))) {
		char *text = trim(line);
		char *id;
		char *image_name;
		char *index_name;
		int cell_width;
		int cell_height;

		line_number++;
		if (!text[0] || text[0] == '#') continue;
		if (!parse_atlas_spec(text, &id, &image_name, &index_name,
				&cell_width, &cell_height) ||
				!load_atlas(registry, renderer, trim(id), trim(image_name),
					trim(index_name), cell_width, cell_height)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Invalid tile manifest entry in %s line %d", path, line_number);
			file_close(file);
			sdl3_tiles_free(registry);
			return false;
		}
	}
	file_close(file);
	registry->loaded = registry->atlas_count > 0 && registry->entry_count > 0;
	if (registry->loaded) {
		SDL_Log("Loaded %d map tiles from %d owned atlases",
			registry->entry_count, registry->atlas_count);
	}
	return registry->loaded;
}

static bool numeric_variant(const char *candidate, const char *base)
{
	const char *suffix;

	if (!prefix(candidate, base)) return false;
	suffix = candidate + strlen(base);
	if (*suffix++ != '-') return false;
	if (!isdigit((unsigned char)*suffix)) return false;
	while (isdigit((unsigned char)*suffix)) suffix++;
	return *suffix == '\0';
}

static const struct sdl3_tile_entry *find_entry(
		const struct sdl3_tile_registry *registry, const char *key,
		int world_col, int world_row)
{
	const struct sdl3_tile_entry *candidates[64];
	int candidate_count = 0;
	int i;
	uint32_t choice;

	if (!registry || !registry->loaded || !key || !key[0]) return NULL;
	for (i = 0; i < registry->entry_count; i++) {
		const struct sdl3_tile_entry *entry = &registry->entries[i];

		if (streq(entry->key, key) || numeric_variant(entry->key, key)) {
			if (candidate_count < (int)N_ELEMENTS(candidates)) {
				candidates[candidate_count++] = entry;
			}
		}
	}
	if (!candidate_count) return NULL;
	choice = (uint32_t)world_col * UINT32_C(0x9e3779b1) ^
		(uint32_t)world_row * UINT32_C(0x85ebca77);
	choice ^= choice >> 16;
	return candidates[choice % (uint32_t)candidate_count];
}

bool sdl3_tiles_draw(struct sdl3_tile_registry *registry,
		SDL_Renderer *renderer, const char *key, int world_col, int world_row,
		const SDL_FRect *target, uint8_t brightness)
{
	const struct sdl3_tile_entry *entry;
	struct sdl3_tile_atlas *atlas;
	SDL_FRect source;
	bool drawn;

	if (!registry || !renderer || !target || target->w <= 0.0f ||
			target->h <= 0.0f) {
		return false;
	}
	entry = find_entry(registry, key, world_col, world_row);
	if (!entry || entry->atlas < 0 || entry->atlas >= registry->atlas_count) {
		return false;
	}
	atlas = &registry->atlases[entry->atlas];
	if (!atlas->texture) return false;
	source = (SDL_FRect) {
		(float)(entry->col * atlas->cell_width),
		(float)(entry->row * atlas->cell_height),
		(float)atlas->cell_width, (float)atlas->cell_height
	};
	SDL_SetTextureColorMod(atlas->texture, brightness, brightness, brightness);
	drawn = SDL_RenderTexture(renderer, atlas->texture, &source, target);
	SDL_SetTextureColorMod(atlas->texture, 255, 255, 255);
	return drawn;
}

bool sdl3_tiles_draw_subject(struct sdl3_tile_registry *registry,
		SDL_Renderer *renderer, const struct sdl3_map_art_subject *subject,
		const struct sdl3_cell *cell, int world_col, int world_row,
		const SDL_FRect *target, uint8_t brightness)
{
	if (!subject || !cell || cell->codepoint != subject->glyph ||
			cell->foreground != subject->attr) return false;
	if (subject->tile_key[0] && sdl3_tiles_draw(registry, renderer,
			subject->tile_key, world_col, world_row, target, brightness)) {
		return true;
	}
	return subject->tile_fallback_key[0] && sdl3_tiles_draw(registry, renderer,
		subject->tile_fallback_key, world_col, world_row, target, brightness);
}
