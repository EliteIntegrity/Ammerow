/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-art.c
 * \brief Font-aware coloured-ASCII rendering from compiled art plates.
 *
 * Source PNGs are development inputs only. The runtime reads deterministic
 * FROGART3 plates from lib/art and chooses glyphs after measuring the active
 * font.
 */

#include "angband.h"
#include "datafile.h"
#include "init.h"
#include "sdl3/monster-art.h"

#include <math.h>

#define MONSTER_ART_MAGIC "FROGART3"
#define MONSTER_ART_MIN_CELL_WIDTH 4
#define MONSTER_ART_MIN_CELL_HEIGHT 9
#define MONSTER_ART_APPROX_GLYPH_ASPECT 55
#define MONSTER_ART_RAMP_STEPS 12
#define MONSTER_ART_COVER_SOLID 128

struct monster_art_density {
	char glyph;
	Uint64 ink;
};

struct monster_art_look {
	bool tone_by_coverage;
	int background_coverage;
	int low_percentile;
	int high_percentile;
	bool solid_only;
	int gamma_percent;
	int floor_step;
	int floor_percent;
	bool drop_empty;
};

static const struct monster_art_look page_look = {
	true, 24, 3, 97, false, 100, 1, 0, false
};
static const struct monster_art_look map_look = {
	false, 76, 30, 97, true, 75, 2, 60, true
};

static uint16_t read_u16(const uint8_t *bytes)
{
	return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

bool sdl3_monster_art_id_valid(const char *id)
{
	return datafile_art_id_is_valid(id);
}

void sdl3_monster_art_make_key(char *destination, size_t capacity,
		const char *art_id, const char *base_art_id)
{
	if (!destination || !capacity) return;
	destination[0] = '\0';
	if (!sdl3_monster_art_id_valid(art_id)) return;
	if (base_art_id && base_art_id[0]) {
		if (!sdl3_monster_art_id_valid(base_art_id)) return;
		strnfmt(destination, capacity, "%s|%s", art_id, base_art_id);
	} else {
		my_strcpy(destination, art_id, capacity);
	}
}

bool sdl3_monster_art_plate_decode(const uint8_t *bytes, size_t size,
		struct sdl3_monster_art_plate *plate)
{
	int cells;
	int i;
	size_t expected;

	if (!bytes || !plate || size < SDL3_MONSTER_ART_PLATE_HEADER_SIZE ||
			memcmp(bytes, MONSTER_ART_MAGIC, 8) != 0) {
		return false;
	}
	SDL_memset(plate, 0, sizeof(*plate));
	plate->cols = read_u16(bytes + 8);
	plate->rows = read_u16(bytes + 10);
	plate->source_width = read_u16(bytes + 12);
	plate->source_height = read_u16(bytes + 14);
	if (plate->cols < 1 || plate->cols > SDL3_MONSTER_ART_MAX_COLS ||
			plate->rows < 1 || plate->rows > SDL3_MONSTER_ART_MAX_ROWS ||
			plate->source_width < 1 || plate->source_height < 1) {
		return false;
	}
	cells = plate->cols * plate->rows;
	expected = SDL3_MONSTER_ART_PLATE_HEADER_SIZE + (size_t)cells * 4;
	if (size != expected) return false;
	for (i = 0; i < cells; i++) {
		const uint8_t *source = bytes + SDL3_MONSTER_ART_PLATE_HEADER_SIZE +
			(size_t)i * 4;

		plate->coverage[i] = source[0];
		plate->colors[i] = (SDL_Color) {
			source[1], source[2], source[3], SDL_ALPHA_OPAQUE
		};
	}
	return true;
}

static uint8_t color_luma(SDL_Color color)
{
	return (uint8_t)((54 * color.r + 183 * color.g + 19 * color.b + 128) /
		256);
}

bool sdl3_monster_art_plate_render(
		const struct sdl3_monster_art_plate *plate, const char *ramp,
		int ramp_count, int cols, int rows, bool map_style,
		struct sdl3_monster_art *art)
{
	const struct monster_art_look *look = map_style ? &map_look : &page_look;
	uint8_t coverage[SDL3_MONSTER_ART_MAX_CELLS];
	uint8_t tone[SDL3_MONSTER_ART_MAX_CELLS];
	int histogram[256];
	int counted = 0;
	int inked = 0;
	int low = 0;
	int high = 255;
	int cells;
	int col;
	int row;
	int i;
	int span;
	int floor_index;

	if (!plate || !ramp || ramp_count < 2 || !art || cols < 1 ||
			rows < 1 || cols > SDL3_MONSTER_ART_MAX_COLS ||
			rows > SDL3_MONSTER_ART_MAX_ROWS || plate->cols < 1 ||
			plate->rows < 1) {
		return false;
	}
	SDL_memset(art, 0, sizeof(*art));
	SDL_memset(coverage, 0, sizeof(coverage));
	SDL_memset(tone, 0, sizeof(tone));
	SDL_memset(histogram, 0, sizeof(histogram));
	art->cols = cols;
	art->rows = rows;
	cells = cols * rows;

	for (row = 0; row < rows; row++) {
		int top = row * plate->rows / rows;
		int bottom = (row + 1) * plate->rows / rows;

		if (bottom <= top) bottom = top + 1;
		for (col = 0; col < cols; col++) {
			int at = row * cols + col;
			int left = col * plate->cols / cols;
			int right = (col + 1) * plate->cols / cols;
			Uint64 coverage_sum = 0;
			Uint64 red_sum = 0;
			Uint64 green_sum = 0;
			Uint64 blue_sum = 0;
			int samples = 0;
			int x;
			int y;
			int average_coverage;
			SDL_Color color;
			int value;

			if (right <= left) right = left + 1;
			for (y = top; y < bottom; y++) {
				for (x = left; x < right; x++) {
					int source = y * plate->cols + x;
					uint8_t amount = plate->coverage[source];

					coverage_sum += amount;
					red_sum += plate->colors[source].r * amount;
					green_sum += plate->colors[source].g * amount;
					blue_sum += plate->colors[source].b * amount;
					samples++;
				}
			}
			if (!samples || !coverage_sum) continue;
			average_coverage = (int)(coverage_sum / (Uint64)samples);
			if (average_coverage < look->background_coverage) continue;
			color = (SDL_Color) {
				(uint8_t)(red_sum / coverage_sum),
				(uint8_t)(green_sum / coverage_sum),
				(uint8_t)(blue_sum / coverage_sum), SDL_ALPHA_OPAQUE
			};
			value = color_luma(color);
			if (look->tone_by_coverage) {
				value = (average_coverage * ((3 * value + 255) / 4) + 127) /
					255;
			}
			coverage[at] = (uint8_t)average_coverage;
			tone[at] = (uint8_t)SDL_clamp(value, 0, 255);
			art->cells[at].visible = true;
			art->cells[at].color = color;
			inked++;
			if (!look->solid_only ||
					average_coverage >= MONSTER_ART_COVER_SOLID) {
				histogram[tone[at]]++;
				counted++;
			}
		}
	}
	if (!inked) return false;
	if (counted >= 8) {
		int cut = counted * look->low_percentile / 100;
		int accumulated = 0;

		for (i = 0; i < 255; i++) {
			accumulated += histogram[i];
			if (accumulated > cut) break;
		}
		low = i;
		cut = counted * (100 - look->high_percentile) / 100;
		accumulated = 0;
		for (i = 255; i > 0; i--) {
			accumulated += histogram[i];
			if (accumulated > cut) break;
		}
		high = i;
	}
	if (high <= low) {
		low = 0;
		high = 255;
	}
	span = high - low;
	floor_index = 1 + look->floor_percent * (ramp_count - 2) / 100;
	floor_index = SDL_clamp(floor_index, look->floor_step, ramp_count - 1);
	for (i = 0; i < cells; i++) {
		int normalized;
		int ramp_index;

		if (!art->cells[i].visible) continue;
		normalized = ((int)tone[i] - low) * 1000 / span;
		normalized = SDL_clamp(normalized, 0, 1000);
		if (look->gamma_percent != 100 && normalized > 0) {
			double gamma = (double)look->gamma_percent / 100.0;

			normalized = (int)(1000.0 *
				pow((double)normalized / 1000.0, gamma) + 0.5);
		}
		ramp_index = (normalized * (ramp_count - 1) + 500) / 1000;
		if (coverage[i] >= MONSTER_ART_COVER_SOLID &&
				ramp_index < floor_index) {
			ramp_index = floor_index;
		}
		if (ramp_index < 1) {
			if (look->drop_empty) {
				art->cells[i].visible = false;
				continue;
			}
			ramp_index = 1;
		}
		ramp_index = SDL_min(ramp_index, ramp_count - 1);
		art->cells[i].codepoint = (uint8_t)ramp[ramp_index];
	}
	return true;
}

static void reset_cache(struct sdl3_monster_art_cache *cache)
{
	if (cache->font_loaded) sdl3_font_free(&cache->font);
	SDL_memset(cache, 0, sizeof(*cache));
}

void sdl3_monster_art_cache_init(struct sdl3_monster_art_cache *cache)
{
	if (cache) SDL_memset(cache, 0, sizeof(*cache));
}

void sdl3_monster_art_cache_free(struct sdl3_monster_art_cache *cache)
{
	if (cache) reset_cache(cache);
}

static bool resolve_plate_in_collection(char *path, size_t capacity,
		const char *collection, const char *kind, const char *asset_id)
{
	char filename[SDL3_MONSTER_ART_ID_CAPACITY + 8];
	char directory[SDL3_FONT_PATH_CAPACITY];
	char art_root[SDL3_FONT_PATH_CAPACITY];
	char collection_root[SDL3_FONT_PATH_CAPACITY];

	if (!sdl3_monster_art_id_valid(asset_id)) return false;
	strnfmt(filename, sizeof(filename), "%s.art", asset_id);
	path_build(art_root, sizeof(art_root), "lib", "art");
	path_build(collection_root, sizeof(collection_root), art_root, collection);
	path_build(directory, sizeof(directory), collection_root, kind);
	path_build(path, capacity, directory, filename);
	if (file_exists(path)) return true;
	if (ANGBAND_DIR_FONTS) {
		char relative[64];

		strnfmt(relative, sizeof(relative), "../art/%s/%s", collection,
			kind);
		path_build(directory, sizeof(directory), ANGBAND_DIR_FONTS, relative);
		path_build(path, capacity, directory, filename);
		if (file_exists(path)) return true;
	}
	path[0] = '\0';
	return false;
}

static bool resolve_key_in_collection(char *path, size_t capacity,
		const char *collection, const char *exact_kind,
		const char *fallback_kind, const char *asset_key)
{
	char key[SDL3_MONSTER_ART_ID_CAPACITY];
	char *fallback_id;

	if (!asset_key || !asset_key[0]) return false;
	my_strcpy(key, asset_key, sizeof(key));
	fallback_id = strchr(key, '|');
	if (fallback_id) *fallback_id++ = '\0';
	if (resolve_plate_in_collection(path, capacity, collection, exact_kind,
			key)) return true;
	if (fallback_id && resolve_plate_in_collection(path, capacity, collection,
			fallback_kind, fallback_id)) {
		return true;
	}
	path[0] = '\0';
	return false;
}

static bool resolve_asset_path(char *path, size_t capacity,
		const char *asset_key)
{
	static const char shopkeeper_prefix[] = "shopkeeper:";
	static const char item_kind_prefix[] = "item-kind:";
	static const char item_unidentified_prefix[] = "item-unidentified:";
	static const char terrain_prefix[] = "terrain:";

	if (!asset_key) return false;
	if (prefix(asset_key, shopkeeper_prefix)) {
		return resolve_key_in_collection(path, capacity, "shopkeepers",
			"owners", "stores", asset_key + sizeof(shopkeeper_prefix) - 1);
	}
	if (prefix(asset_key, item_kind_prefix)) {
		return resolve_key_in_collection(path, capacity, "items", "kinds",
			"kinds", asset_key + sizeof(item_kind_prefix) - 1);
	}
	if (prefix(asset_key, item_unidentified_prefix)) {
		return resolve_key_in_collection(path, capacity, "items",
			"unidentified", "unidentified",
			asset_key + sizeof(item_unidentified_prefix) - 1);
	}
	if (prefix(asset_key, terrain_prefix)) {
		return resolve_key_in_collection(path, capacity, "terrain",
			"features", "features", asset_key + sizeof(terrain_prefix) - 1);
	}
	return resolve_key_in_collection(path, capacity, "monsters", "races",
		"bases", asset_key);
}

static bool read_plate_file(const char *path,
		struct sdl3_monster_art_plate *plate)
{
	FILE *file;
	uint8_t header[SDL3_MONSTER_ART_PLATE_HEADER_SIZE];
	uint8_t bytes[SDL3_MONSTER_ART_PLATE_HEADER_SIZE +
		SDL3_MONSTER_ART_MAX_CELLS * 4];
	size_t expected;
	size_t count;
	int cols;
	int rows;

#if defined(_MSC_VER)
	if (fopen_s(&file, path, "rb") != 0) file = NULL;
#else
	file = fopen(path, "rb");
#endif
	if (!file) return false;
	count = fread(header, 1, sizeof(header), file);
	if (count != sizeof(header) || memcmp(header, MONSTER_ART_MAGIC, 8) != 0) {
		fclose(file);
		return false;
	}
	cols = read_u16(header + 8);
	rows = read_u16(header + 10);
	if (cols < 1 || cols > SDL3_MONSTER_ART_MAX_COLS || rows < 1 ||
			rows > SDL3_MONSTER_ART_MAX_ROWS) {
		fclose(file);
		return false;
	}
	expected = sizeof(header) + (size_t)cols * rows * 4;
	memcpy(bytes, header, sizeof(header));
	count = fread(bytes + sizeof(header), 1, expected - sizeof(header), file);
	if (count != expected - sizeof(header) || fgetc(file) != EOF) {
		fclose(file);
		return false;
	}
	fclose(file);
	return sdl3_monster_art_plate_decode(bytes, expected, plate);
}

static void sort_densities(struct monster_art_density *entries, int count)
{
	int i;

	for (i = 1; i < count; i++) {
		struct monster_art_density value = entries[i];
		int position = i;

		while (position > 0 && (entries[position - 1].ink > value.ink ||
				(entries[position - 1].ink == value.ink &&
				entries[position - 1].glyph > value.glyph))) {
			entries[position] = entries[position - 1];
			position--;
		}
		entries[position] = value;
	}
}

static bool build_density_ramp(struct sdl3_font *font, char *ramp,
		int capacity, int *ramp_count)
{
	static const char candidates[] =
		".'`^\",:;Il!i><~+-_?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao"
		"*#MW&8%B@$";
	struct monster_art_density entries[N_ELEMENTS(candidates) - 1];
	int count = 0;
	int i;

	if (!font || !font->handle || !ramp || capacity < MONSTER_ART_RAMP_STEPS ||
			!ramp_count) {
		return false;
	}
	for (i = 0; candidates[i] && count < (int)N_ELEMENTS(entries); i++) {
		SDL_Surface *surface = TTF_RenderGlyph_Blended(font->handle,
			(uint8_t)candidates[i], (SDL_Color) { 255, 255, 255, 255 });
		SDL_Surface *rgba;
		Uint64 ink = 0;
		int x;
		int y;

		if (!surface) continue;
		rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
		SDL_DestroySurface(surface);
		if (!rgba) continue;
		if (SDL_MUSTLOCK(rgba) && !SDL_LockSurface(rgba)) {
			SDL_DestroySurface(rgba);
			continue;
		}
		for (y = 0; y < rgba->h; y++) {
			const uint8_t *source = (const uint8_t *)rgba->pixels +
				y * rgba->pitch;

			for (x = 0; x < rgba->w; x++) ink += source[x * 4 + 3];
		}
		if (SDL_MUSTLOCK(rgba)) SDL_UnlockSurface(rgba);
		SDL_DestroySurface(rgba);
		entries[count].glyph = candidates[i];
		entries[count].ink = ink;
		count++;
	}
	if (count < 2) return false;
	sort_densities(entries, count);
	ramp[0] = ' ';
	*ramp_count = SDL_min(MONSTER_ART_RAMP_STEPS, count + 1);
	for (i = 1; i < *ramp_count; i++) {
		int source = (i - 1) * (count - 1) /
			SDL_max(1, *ramp_count - 2);

		ramp[i] = entries[source].glyph;
	}
	return true;
}

static bool prepare_cache(struct sdl3_monster_art_cache *cache,
		SDL_Renderer *renderer, const char *font_path, const char *asset_id,
		int target_width, int target_height, bool map_style)
{
	static const int column_rungs[] = { 96, 80, 64, 48, 32, 24, 16, 8, 4 };
	struct sdl3_monster_art_plate plate;
	char plate_path[SDL3_FONT_PATH_CAPACITY];
	char ramp[SDL3_MONSTER_ART_RAMP_CAPACITY];
	int ramp_count = 0;
	int i;

	reset_cache(cache);
	my_strcpy(cache->asset_id, asset_id, sizeof(cache->asset_id));
	my_strcpy(cache->font_path, font_path, sizeof(cache->font_path));
	cache->target_width = target_width;
	cache->target_height = target_height;
	cache->map_style = map_style;
	if (!resolve_asset_path(plate_path, sizeof(plate_path), asset_id) ||
			!read_plate_file(plate_path, &plate)) {
		cache->failed = true;
		return false;
	}
	for (i = 0; i < (int)N_ELEMENTS(column_rungs); i++) {
		int cols = SDL_min(column_rungs[i], plate.cols);
		int rows = (plate.rows * cols * MONSTER_ART_APPROX_GLYPH_ASPECT +
			plate.cols * 50) / (plate.cols * 100);

		rows = SDL_clamp(rows, 1, SDL3_MONSTER_ART_MAX_ROWS);
		if (cols * MONSTER_ART_MIN_CELL_WIDTH > target_width) continue;
		if (rows * MONSTER_ART_MIN_CELL_HEIGHT > target_height) continue;
		if (!sdl3_font_init_lazy(&cache->font, renderer, font_path,
				target_width, target_height, cols, rows, 100)) {
			continue;
		}
		cache->font_loaded = true;
		if (!build_density_ramp(&cache->font, ramp, (int)sizeof(ramp),
				&ramp_count) || !sdl3_monster_art_plate_render(&plate, ramp,
				ramp_count, cols, rows, map_style, &cache->art)) {
			sdl3_font_free(&cache->font);
			cache->font_loaded = false;
			continue;
		}
		cache->ready = true;
		break;
	}
	if (!cache->ready) {
		if (cache->font_loaded) {
			sdl3_font_free(&cache->font);
			cache->font_loaded = false;
		}
		cache->failed = true;
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not prepare SDL3 art plate '%s'", asset_id);
		return false;
	}
	SDL_Log("SDL3 art plate: %s, %d by %d ASCII cells", plate_path,
		cache->art.cols, cache->art.rows);
	return true;
}

static bool draw_with_style(struct sdl3_monster_art_cache *cache,
		SDL_Renderer *renderer, const char *font_path, const char *asset_id,
		const SDL_FRect *target, bool map_style)
{
	int target_width;
	int target_height;
	float origin_x;
	float origin_y;
	int col;
	int row;

	if (!cache || !renderer || !font_path || !font_path[0] || !asset_id ||
			!asset_id[0] || !target || target->w <= 0.0f || target->h <= 0.0f) {
		return false;
	}
	target_width = (int)target->w;
	target_height = (int)target->h;
	if (my_stricmp(cache->asset_id, asset_id) != 0 ||
			my_stricmp(cache->font_path, font_path) != 0 ||
			cache->target_width != target_width ||
			cache->target_height != target_height ||
			cache->map_style != map_style) {
		prepare_cache(cache, renderer, font_path, asset_id, target_width,
			target_height, map_style);
	}
	if (!cache->ready || !cache->font_loaded) return false;
	origin_x = target->x + (target->w -
		cache->art.cols * cache->font.cell_width) * 0.5f;
	origin_y = target->y + (target->h -
		cache->art.rows * cache->font.cell_height) * 0.5f;
	for (row = 0; row < cache->art.rows; row++) {
		for (col = 0; col < cache->art.cols; col++) {
			const struct sdl3_monster_art_cell *cell =
				&cache->art.cells[row * cache->art.cols + col];

			if (!cell->visible) continue;
			sdl3_font_draw(&cache->font, cell->codepoint, cell->color,
				origin_x + col * cache->font.cell_width,
				origin_y + row * cache->font.cell_height,
				(float)cache->font.cell_width,
				(float)cache->font.cell_height);
		}
	}
	return true;
}

bool sdl3_monster_art_draw(struct sdl3_monster_art_cache *cache,
		SDL_Renderer *renderer, const char *font_path, const char *asset_id,
		const SDL_FRect *target)
{
	return draw_with_style(cache, renderer, font_path, asset_id, target,
		false);
}

bool sdl3_monster_art_draw_shopkeeper(struct sdl3_monster_art_cache *cache,
		SDL_Renderer *renderer, const char *font_path, const char *asset_key,
		const SDL_FRect *target)
{
	char prefixed_key[SDL3_MONSTER_ART_KEY_CAPACITY];

	if (!asset_key || !asset_key[0]) return false;
	strnfmt(prefixed_key, sizeof(prefixed_key), "shopkeeper:%s", asset_key);
	return draw_with_style(cache, renderer, font_path, prefixed_key, target,
		false);
}

bool sdl3_monster_art_map_enabled(int zoom_percent)
{
	return zoom_percent >= SDL3_MONSTER_ART_MAP_MIN_ZOOM;
}

void sdl3_monster_art_pool_init(struct sdl3_monster_art_pool *pool)
{
	if (pool) SDL_memset(pool, 0, sizeof(*pool));
}

void sdl3_monster_art_pool_free(struct sdl3_monster_art_pool *pool)
{
	int i;

	if (!pool) return;
	for (i = 0; i < SDL3_MONSTER_ART_POOL_CAPACITY; i++) {
		if (!pool->entries[i].cache) continue;
		sdl3_monster_art_cache_free(pool->entries[i].cache);
		mem_free(pool->entries[i].cache);
	}
	SDL_memset(pool, 0, sizeof(*pool));
}

static bool pool_entry_matches(
		const struct sdl3_monster_art_pool_entry *entry,
		const char *font_path, const char *asset_id, int target_width,
		int target_height)
{
	return entry && entry->cache && entry->cache->asset_id[0] &&
		my_stricmp(entry->cache->asset_id, asset_id) == 0 &&
		my_stricmp(entry->cache->font_path, font_path) == 0 &&
		entry->cache->target_width == target_width &&
		entry->cache->target_height == target_height && entry->cache->map_style;
}

static struct sdl3_monster_art_pool_entry *pool_entry_for(
		struct sdl3_monster_art_pool *pool, const char *font_path,
		const char *asset_id, int target_width, int target_height)
{
	struct sdl3_monster_art_pool_entry *replacement = NULL;
	int i;

	for (i = 0; i < SDL3_MONSTER_ART_POOL_CAPACITY; i++) {
		struct sdl3_monster_art_pool_entry *entry = &pool->entries[i];

		if (pool_entry_matches(entry, font_path, asset_id, target_width,
				target_height)) {
			return entry;
		}
		if (!entry->cache || !entry->cache->asset_id[0]) {
			if (!replacement) replacement = entry;
		} else if (!replacement || (replacement->cache &&
				replacement->cache->asset_id[0] &&
				entry->last_used < replacement->last_used)) {
			replacement = entry;
		}
	}
	if (!replacement) return NULL;
	if (!replacement->cache) {
		replacement->cache = mem_zalloc(sizeof(*replacement->cache));
		sdl3_monster_art_cache_init(replacement->cache);
	} else if (replacement->cache->asset_id[0]) {
		sdl3_monster_art_cache_free(replacement->cache);
	}
	return replacement;
}

bool sdl3_monster_art_pool_draw(struct sdl3_monster_art_pool *pool,
		SDL_Renderer *renderer, const char *font_path, const char *asset_id,
		const SDL_FRect *target)
{
	struct sdl3_monster_art_pool_entry *entry;
	int target_width;
	int target_height;

	if (!pool || !renderer || !font_path || !font_path[0] || !asset_id ||
			!asset_id[0] || !target || target->w <= 0.0f ||
			target->h <= 0.0f) {
		return false;
	}
	target_width = (int)target->w;
	target_height = (int)target->h;
	entry = pool_entry_for(pool, font_path, asset_id, target_width,
		target_height);
	if (!entry) return false;
	entry->last_used = ++pool->use_clock;
	return draw_with_style(entry->cache, renderer, font_path, asset_id,
		target, true);
}

#undef MONSTER_ART_MAGIC
#undef MONSTER_ART_MIN_CELL_WIDTH
#undef MONSTER_ART_MIN_CELL_HEIGHT
#undef MONSTER_ART_APPROX_GLYPH_ASPECT
#undef MONSTER_ART_RAMP_STEPS
#undef MONSTER_ART_COVER_SOLID
