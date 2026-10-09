/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/render.c
 * \brief Character-cell compositor for the SDL3 frontend.
 *
 *
 */

#include "sdl3/render-internal.h"
#include "sdl3/effects.h"
#include "sdl3/fishing.h"
#include "sdl3/screen.h"

#include "sdl3/inspect-card.h"
#include "sdl3/settings.h"
#include "sdl3/home.h"
#include "sdl3/pause-menu.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"
#include "sdl3/zoom.h"

#define SDL3_DEBUG_GLYPH_SIZE 8
#define SDL3_CARD_FONT_SCALE 400

static void invalidate_zoom_out_texture(struct sdl3_visual *visual)
{
	if (!visual) return;
	if (visual->zoom_out_texture) {
		SDL_DestroyTexture(visual->zoom_out_texture);
		visual->zoom_out_texture = NULL;
	}
	visual->zoom_out_texture_width = 0;
	visual->zoom_out_texture_height = 0;
	visual->zoom_out_source = NULL;
}

static void invalidate_scene_texture(struct sdl3_visual *visual)
{
	if (!visual) return;
	if (visual->scene_texture) SDL_DestroyTexture(visual->scene_texture);
	visual->scene_texture = NULL;
	visual->scene_texture_width = 0;
	visual->scene_texture_height = 0;
	visual->scene_cache_unavailable = false;
	visual->scene_cache_ready = false;
}

static void update_origin(struct sdl3_visual *visual)
{
	visual->origin_x = (visual->output_width -
		visual->cell_width * visual->cols) / 2;
	visual->origin_y = (visual->output_height -
		visual->cell_height * visual->rows) / 2;
}

static void use_fallback_layout(struct sdl3_visual *visual)
{
	visual->cell_width = SDL_max(1, visual->output_width / visual->cols);
	visual->cell_height = SDL_max(1, visual->output_height / visual->rows);
	update_origin(visual);
}

static void use_font_layout(struct sdl3_visual *visual)
{
	int available_width = visual->output_width / visual->cols;
	int available_height = visual->output_height / visual->rows;

	visual->font_fits = true;
	/* Let the logical cells consume the output while keeping the fitted font
	 * itself unscaled.  Extra space becomes even padding around each crisp
	 * glyph instead of one large letterbox at the canvas edges. */
	visual->cell_width = SDL_max(visual->font.cell_width, available_width);
	visual->cell_height = SDL_max(visual->font.cell_height, available_height);
	update_origin(visual);
}

static void fit_layout(struct sdl3_visual *visual)
{
	if (visual->font_loaded && sdl3_font_fit(&visual->font,
			visual->output_width, visual->output_height, visual->cols,
			visual->rows, 100)) {
		use_font_layout(visual);
	} else {
		visual->font_fits = false;
		use_fallback_layout(visual);
	}
}

static bool font_request_matches_loaded(const char *requested,
		const char *loaded)
{
	if (!requested || !requested[0]) return true;
	if (!loaded || !loaded[0]) return false;
	if (my_stricmp(requested, loaded) == 0) return true;
	/* Saved bundled selections are filenames.  Keep explicit paths distinct,
	 * even when two directories happen to contain the same filename. */
	if (!strchr(requested, '/') && !strchr(requested, '\\')) {
		return my_stricmp(requested, sdl3_font_filename(loaded)) == 0;
	}
	return false;
}

static bool rebuild_map_font(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const char *font_path, int zoom_percent)
{
	struct sdl3_font replacement;
	char selected_path[SDL3_FONT_PATH_CAPACITY];
	int scaled_width;
	int scaled_height;
	bool used_fallback;

	zoom_percent = sdl3_zoom_clamp(zoom_percent);
	if (!visual->font_loaded) return false;
	if (!font_path || !font_path[0]) font_path = visual->font.path;
	if (zoom_percent == SDL3_ZOOM_DEFAULT &&
			font_request_matches_loaded(font_path, visual->font.path)) {
		if (visual->map_font_loaded) sdl3_font_free(&visual->map_font);
		sdl3_monster_art_pool_free(&visual->map_art);
		my_strcpy(visual->map_font_path, visual->font.path,
			sizeof(visual->map_font_path));
		visual->map_font_loaded = false;
		visual->map_zoom_percent = zoom_percent;
		invalidate_zoom_out_texture(visual);
		return true;
	}
	scaled_width = visual->output_width * zoom_percent / 100;
	scaled_height = visual->output_height * zoom_percent / 100;
	if (!sdl3_font_init_lazy_fallback(&replacement, renderer, font_path,
			scaled_width, scaled_height, visual->output_width,
			visual->output_height, visual->cols, visual->rows, 100,
			&used_fallback)) {
		return false;
	}
	if (used_fallback) {
		SDL_Log("SDL3 map font: x%.2f is below the native minimum at this "
			"layout; using safe nearest-neighbour downsampling",
			zoom_percent / 100.0);
	}
	my_strcpy(selected_path, replacement.path, sizeof(selected_path));
	if (visual->map_font_loaded) sdl3_font_free(&visual->map_font);
	sdl3_monster_art_pool_free(&visual->map_art);
	my_strcpy(visual->map_font_path, selected_path,
		sizeof(visual->map_font_path));
	visual->map_zoom_percent = zoom_percent;
	/* Sharing the fitted interface face at normal zoom avoids a duplicate
	 * font cache.  A distinct map face, or any enlarged map, keeps its own
	 * lazily populated cache. */
	if (zoom_percent == SDL3_ZOOM_DEFAULT &&
			my_stricmp(selected_path, visual->font.path) == 0) {
		sdl3_font_free(&replacement);
		visual->map_font_loaded = false;
		invalidate_zoom_out_texture(visual);
		return true;
	}
	visual->map_font = replacement;
	visual->map_font_loaded = true;
	invalidate_zoom_out_texture(visual);
	return true;
}

bool sdl3_visual_ensure_card_font(struct sdl3_visual *visual,
		SDL_Renderer *renderer)
{
	struct sdl3_font replacement;

	if (!visual->font_loaded) return false;
	if (!sdl3_font_init_lazy(&replacement, renderer, visual->font.path,
			visual->output_width * SDL3_CARD_FONT_SCALE / 100,
			visual->output_height * SDL3_CARD_FONT_SCALE / 100,
			visual->cols, visual->rows, 100)) {
		return false;
	}
	if (visual->card_font_loaded) sdl3_font_free(&visual->card_font);
	visual->card_font = replacement;
	visual->card_font_loaded = true;
	return true;
}

bool sdl3_visual_resize(struct sdl3_visual *visual, SDL_Renderer *renderer)
{
	int old_cell_width;
	int old_cell_height;
	int width;
	int height;

	if (!visual || !renderer ||
			!SDL_GetRenderOutputSize(renderer, &width, &height)) {
		return false;
	}
	if (width <= 0 || height <= 0) return false;
	if (width == visual->output_width && height == visual->output_height) {
		return true;
	}

	old_cell_width = visual->cell_width;
	old_cell_height = visual->cell_height;
	visual->output_width = width;
	visual->output_height = height;
	fit_layout(visual);
	if (!rebuild_map_font(visual, renderer, visual->map_font_path,
			visual->map_zoom_percent)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not refit the SDL3 map font after resize: %s",
			SDL_GetError());
	}
	if (visual->card_font_loaded &&
			!sdl3_visual_ensure_card_font(visual, renderer)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not refit the SDL3 examine-card font after resize: %s",
			SDL_GetError());
	}
	if (visual->cell_width != old_cell_width ||
			visual->cell_height != old_cell_height) {
		sdl3_visual_invalidate_grid_cache(visual);
	}
	return true;
}

bool sdl3_visual_set_grid_size(struct sdl3_visual *visual,
		SDL_Renderer *renderer, int cols, int rows)
{
	if (!visual || !renderer || cols <= 0 || rows <= 0) return false;
	if (visual->cols == cols && visual->rows == rows) return true;
	visual->cols = cols;
	visual->rows = rows;
	fit_layout(visual);
	if (!rebuild_map_font(visual, renderer, visual->map_font_path,
			visual->map_zoom_percent)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not refit the SDL3 map font after layout change: %s",
			SDL_GetError());
	}
	if (visual->card_font_loaded &&
			!sdl3_visual_ensure_card_font(visual, renderer)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not refit the SDL3 examine-card font after layout change: %s",
			SDL_GetError());
	}
	sdl3_visual_invalidate_grid_cache(visual);
	return true;
}

bool sdl3_visual_init(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const char *font_path, const char *map_font_path, int cols, int rows,
		int map_zoom_percent)
{
	if (!visual || !renderer || cols <= 0 || rows <= 0) return false;
	SDL_memset(visual, 0, sizeof(*visual));
	visual->cols = cols;
	visual->rows = rows;
	visual->map_zoom_percent = SDL3_ZOOM_DEFAULT;
	if (!SDL_GetRenderOutputSize(renderer, &visual->output_width,
			&visual->output_height)) {
		return false;
	}

	visual->font_loaded = sdl3_font_init(&visual->font, renderer, font_path,
		visual->output_width, visual->output_height, cols, rows, 100);
	visual->font_fits = visual->font_loaded;
	if (visual->font_fits) {
		use_font_layout(visual);
	} else {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Using SDL debug glyphs until a usable font is available");
		use_fallback_layout(visual);
	}
	if (!rebuild_map_font(visual, renderer, map_font_path,
			map_zoom_percent)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not prepare the requested SDL3 map zoom: %s",
			SDL_GetError());
		visual->map_zoom_percent = SDL3_ZOOM_DEFAULT;
	}
	/* Portrait textures are loaded lazily, only while Hybrid is selected. */
	return true;
}

void sdl3_visual_free(struct sdl3_visual *visual)
{
	if (!visual) return;
	invalidate_zoom_out_texture(visual);
	invalidate_scene_texture(visual);
	if (visual->grid_texture) SDL_DestroyTexture(visual->grid_texture);
	sdl3_monster_art_cache_free(&visual->monster_art);
	sdl3_monster_art_cache_free(&visual->shopkeeper_art);
	sdl3_monster_art_cache_free(&visual->home_art);
	sdl3_monster_art_pool_free(&visual->map_art);
	sdl3_tiles_free(&visual->tiles);
	if (visual->card_font_loaded) sdl3_font_free(&visual->card_font);
	if (visual->map_font_loaded) sdl3_font_free(&visual->map_font);
	if (visual->font_loaded) sdl3_font_free(&visual->font);
	SDL_memset(visual, 0, sizeof(*visual));
}

void sdl3_visual_invalidate_grid_cache(struct sdl3_visual *visual)
{
	if (!visual) return;
	invalidate_zoom_out_texture(visual);
	invalidate_scene_texture(visual);
	if (visual->grid_texture) SDL_DestroyTexture(visual->grid_texture);
	visual->grid_texture = NULL;
	visual->grid_texture_width = 0;
	visual->grid_texture_height = 0;
	visual->grid_cache_unavailable = false;
	visual->last_cell_updates = 0;
}

bool sdl3_visual_recreate_renderer_resources(struct sdl3_visual *visual,
		SDL_Renderer *renderer)
{
	struct sdl3_visual replacement;
	char font_path[SDL3_FONT_PATH_CAPACITY];
	char map_font_path[SDL3_FONT_PATH_CAPACITY];
	Uint64 frames_presented;
	Uint64 cell_updates_total;
	Uint64 present_wait_ns_total;
	Uint64 present_wait_ns_max;

	if (!visual || !renderer || visual->cols <= 0 || visual->rows <= 0) {
		return false;
	}
	font_path[0] = '\0';
	map_font_path[0] = '\0';
	if (visual->font_loaded) {
		SDL_strlcpy(font_path, visual->font.path, sizeof(font_path));
	}
	SDL_strlcpy(map_font_path, visual->map_font_path,
		sizeof(map_font_path));
	frames_presented = visual->frames_presented;
	cell_updates_total = visual->cell_updates_total;
	present_wait_ns_total = visual->present_wait_ns_total;
	present_wait_ns_max = visual->present_wait_ns_max;
	if (!sdl3_visual_init(&replacement, renderer,
			font_path[0] ? font_path : NULL,
			map_font_path[0] ? map_font_path : NULL, visual->cols,
			visual->rows, visual->map_zoom_percent)) {
		return false;
	}
	replacement.frames_presented = frames_presented;
	replacement.cell_updates_total = cell_updates_total;
	replacement.present_wait_ns_total = present_wait_ns_total;
	replacement.present_wait_ns_max = present_wait_ns_max;
	sdl3_visual_free(visual);
	*visual = replacement;
	return true;
}

bool sdl3_visual_set_font(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const char *font_path)
{
	struct sdl3_font replacement;

	if (!visual || !renderer || !font_path || !font_path[0]) return false;
	if (!sdl3_font_init(&replacement, renderer, font_path,
			visual->output_width, visual->output_height, visual->cols,
			visual->rows, 100)) {
		return false;
	}
	if (visual->font_loaded) sdl3_font_free(&visual->font);
	visual->font = replacement;
	visual->font_loaded = true;
	use_font_layout(visual);
	if (!rebuild_map_font(visual, renderer, visual->map_font_path,
			visual->map_zoom_percent)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not refit the SDL3 map font after an interface font change: %s",
			SDL_GetError());
	}
	if (visual->card_font_loaded &&
			!sdl3_visual_ensure_card_font(visual, renderer)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not rebuild the SDL3 examine-card font after a font change: %s",
			SDL_GetError());
	}
	sdl3_visual_invalidate_grid_cache(visual);
	return true;
}

bool sdl3_visual_set_map_font(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const char *font_path)
{
	if (!visual || !renderer || !font_path || !font_path[0]) return false;
	if (!rebuild_map_font(visual, renderer, font_path,
			visual->map_zoom_percent)) {
		return false;
	}
	sdl3_visual_invalidate_grid_cache(visual);
	return true;
}

bool sdl3_visual_set_map_zoom(struct sdl3_visual *visual,
		SDL_Renderer *renderer, int zoom_percent)
{
	if (!visual || !renderer) return false;
	zoom_percent = sdl3_zoom_clamp(zoom_percent);
	if (zoom_percent == visual->map_zoom_percent) return true;
	return rebuild_map_font(visual, renderer, visual->map_font_path,
		zoom_percent);
}

static void draw_debug_glyph(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_cell *cell, float cell_x,
		float cell_y, SDL_Color foreground)
{
	char text[2];
	float x;
	float y;

	if (visual->cell_width < SDL3_DEBUG_GLYPH_SIZE ||
			visual->cell_height < SDL3_DEBUG_GLYPH_SIZE ||
			cell->codepoint < 32 || cell->codepoint > 126) {
		return;
	}
	text[0] = (char)cell->codepoint;
	text[1] = '\0';
	x = cell_x +
		(visual->cell_width - SDL3_DEBUG_GLYPH_SIZE) / 2.0f;
	y = cell_y +
		(visual->cell_height - SDL3_DEBUG_GLYPH_SIZE) / 2.0f;
	SDL_SetRenderDrawColor(renderer, foreground.r, foreground.g,
		foreground.b, foreground.a);
	SDL_RenderDebugText(renderer, x, y, text);
}

static bool cell_uses_map_font(const struct sdl3_visual *visual,
		const struct sdl3_map_view *map_view, int col, int row)
{
	return visual && map_view && map_view->active &&
		map_view->zoom_percent == SDL3_ZOOM_DEFAULT &&
		visual->map_font_loaded && col >= map_view->col &&
		col < map_view->col + map_view->cols && row >= map_view->row &&
		row < map_view->row + map_view->rows;
}

static const struct sdl3_terrain_variant *presented_terrain_variant(
		const struct sdl3_map_view *map_view, int col, int row,
		bool source_coordinates)
{
	if (!map_view || !map_view->active || !map_view->terrain) return NULL;
	if (map_view->terrain_source_coordinates) {
		/* Expanded zoom-out metadata belongs to its dedicated source grid.
		 * Never reinterpret terminal UI or dock coordinates as source cells. */
		if (!source_coordinates) return NULL;
		if (col < map_view->source_col ||
				col >= map_view->source_col + map_view->source_cols ||
				row < map_view->source_row ||
				row >= map_view->source_row + map_view->source_rows) {
			return NULL;
		}
	} else if (col < map_view->col || col >= map_view->col + map_view->cols ||
			row < map_view->row || row >= map_view->row + map_view->rows) {
		return NULL;
	}
	return sdl3_terrain_overlay_get(map_view->terrain, col, row);
}

static bool presented_map_coordinate(const struct sdl3_map_view *map_view,
		int col, int row)
{
	return map_view && map_view->active && col >= map_view->col &&
		col < map_view->col + map_view->cols && row >= map_view->row &&
		row < map_view->row + map_view->rows;
}

static const struct sdl3_cell *presented_map_cell(
		const struct sdl3_cell *cell, int col, int row,
		const struct sdl3_map_view *map_view, struct sdl3_cell *replacement,
		const struct sdl3_terrain_variant **presented_variant,
		bool source_coordinates)
{
	const struct sdl3_terrain_variant *variant =
		presented_terrain_variant(map_view, col, row, source_coordinates);

	if (presented_variant) *presented_variant = variant;
	if (!cell || !replacement || !variant || !variant->active) return cell;
	*replacement = *cell;
	replacement->codepoint = variant->codepoint;
	replacement->foreground = variant->foreground;
	return replacement;
}

static SDL_Color terrain_surface_color(SDL_Color background,
		SDL_Color foreground, int weight, int total)
{
	SDL_Color result = {
		(uint8_t)((background.r * (total - weight) + foreground.r * weight) /
			total),
		(uint8_t)((background.g * (total - weight) + foreground.g * weight) /
			total),
		(uint8_t)((background.b * (total - weight) + foreground.b * weight) /
			total),
		255
	};

	return result;
}

static SDL_Color terrain_recipe_color(struct terrain_visual_rgb rgb,
		const struct sdl3_terrain_variant *variant,
		const struct sdl3_theme *theme)
{
	SDL_Color color = { rgb.r, rgb.g, rgb.b, SDL_ALPHA_OPAQUE };

	if (!variant) return color;
	if (!variant->in_view) {
		color = variant->remembered_brightness ?
			sdl3_dim_world_color(color, variant->remembered_brightness) :
			sdl3_theme_remembered_world_color(theme, color);
	} else if (variant->lighting == LIGHTING_TORCH) {
		const SDL_Color torch = { 255, 196, 118, 255 };

		color = terrain_surface_color(color, torch, 2, 16);
	}
	return color;
}

static SDL_Color terrain_foreground_color(SDL_Color fallback,
		const struct sdl3_terrain_variant *variant,
		const struct sdl3_theme *theme)
{
	if (!variant) return fallback;
	if (!variant->active || !variant->recipe) {
		return variant->in_view ? fallback :
			(variant->remembered_brightness ?
			 sdl3_dim_world_color(fallback,
				variant->remembered_brightness) :
			 sdl3_theme_remembered_world_color(theme, fallback));
	}
	return terrain_recipe_color(variant->recipe->base, variant, theme);
}

static void draw_cell(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const struct sdl3_cell *cell, int col, int row, int origin_x,
		int origin_y, const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view)
{
	struct sdl3_cell replacement;
	const struct sdl3_terrain_variant *variant;
	bool map_coordinate = presented_map_coordinate(map_view, col, row);

	cell = presented_map_cell(cell, col, row, map_view, &replacement,
		&variant, false);
	SDL_Color background = map_coordinate ?
		sdl3_world_color(theme, cell->background) :
		sdl3_theme_color(theme, cell->background);
	SDL_Color foreground = map_coordinate ?
		sdl3_world_color(theme, cell->foreground) :
		sdl3_theme_color(theme, cell->foreground);
	struct sdl3_font *font = cell_uses_map_font(visual, map_view, col, row) ?
		&visual->map_font : (visual->font_fits ? &visual->font : NULL);
	SDL_FRect rect = {
		(float)(origin_x + col * visual->cell_width),
		(float)(origin_y + row * visual->cell_height),
		(float)visual->cell_width,
		(float)visual->cell_height
	};

	foreground = terrain_foreground_color(foreground, variant, theme);

	SDL_SetRenderDrawColor(renderer, background.r, background.g,
		background.b, background.a);
	SDL_RenderFillRect(renderer, &rect);
	if (font) {
		sdl3_font_draw(font, (uint32_t)cell->codepoint, foreground,
			rect.x, rect.y, rect.w, rect.h);
	} else {
		draw_debug_glyph(visual, renderer, cell, rect.x, rect.y, foreground);
	}
}

static bool ensure_grid_texture(struct sdl3_visual *visual,
		SDL_Renderer *renderer, bool *rebuilt)
{
	int width = visual->cell_width * visual->cols;
	int height = visual->cell_height * visual->rows;

	*rebuilt = false;
	if (visual->grid_cache_unavailable) return false;
	if (visual->grid_texture && visual->grid_texture_width == width &&
			visual->grid_texture_height == height) {
		return true;
	}
	sdl3_visual_invalidate_grid_cache(visual);
	visual->grid_texture = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
	if (!visual->grid_texture) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not create the SDL3 dirty-cell cache; using full redraws: %s",
			SDL_GetError());
		visual->grid_cache_unavailable = true;
		return false;
	}
	if (!SDL_SetTextureBlendMode(visual->grid_texture, SDL_BLENDMODE_NONE)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not disable blending for the SDL3 grid cache: %s",
			SDL_GetError());
	}
	if (!SDL_SetTextureScaleMode(visual->grid_texture,
			SDL_SCALEMODE_NEAREST)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not select nearest scaling for the SDL3 grid cache: %s",
			SDL_GetError());
	}
	visual->grid_texture_width = width;
	visual->grid_texture_height = height;
	*rebuilt = true;
	return true;
}

static bool update_grid_texture(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_grid *grid,
		const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view)
{
	SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
	bool rebuilt;
	int updates = 0;
	int col;
	int row;

	if (!ensure_grid_texture(visual, renderer, &rebuilt)) return false;
	if (!SDL_SetRenderTarget(renderer, visual->grid_texture)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not target the SDL3 dirty-cell cache; using full redraws: %s",
			SDL_GetError());
		sdl3_visual_invalidate_grid_cache(visual);
		visual->grid_cache_unavailable = true;
		return false;
	}
	for (row = 0; row < grid->rows; row++) {
		for (col = 0; col < grid->cols; col++) {
			const struct sdl3_cell *cell = sdl3_grid_cell(grid, col, row);

			if (!rebuilt && !cell->dirty) continue;
			draw_cell(visual, renderer, cell, col, row, 0, 0, theme,
				map_view);
			updates++;
		}
	}
	if (!SDL_SetRenderTarget(renderer, previous_target)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not restore the SDL3 render target: %s",
			SDL_GetError());
		return false;
	}
	visual->last_cell_updates = updates;
	return true;
}

static void draw_grid_direct(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_grid *grid,
		const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view)
{
	int col;
	int row;

	for (row = 0; row < grid->rows; row++) {
		for (col = 0; col < grid->cols; col++) {
			draw_cell(visual, renderer, sdl3_grid_cell(grid, col, row), col,
				row, visual->origin_x, visual->origin_y, theme, map_view);
		}
	}
	visual->last_cell_updates = grid->cols * grid->rows;
}

static struct sdl3_font *map_camera_font(struct sdl3_visual *visual)
{
	if (!visual) return NULL;
	if (visual->map_font_loaded) return &visual->map_font;
	return visual->font_loaded ? &visual->font : NULL;
}

static bool map_view_uses_camera(const struct sdl3_visual *visual,
		const struct sdl3_map_view *map_view)
{
	const struct sdl3_grid *source = map_view ? map_view->source : NULL;

	return visual && map_view && map_view->active && source &&
		(visual->map_font_loaded || visual->font_loaded) &&
		map_view->col >= 0 && map_view->row >= 0 && map_view->cols > 0 &&
		map_view->rows > 0 && map_view->source_col >= 0 &&
		map_view->source_row >= 0 && map_view->source_cols > 0 &&
		map_view->source_rows > 0 &&
		map_view->source_col + map_view->source_cols <= source->cols &&
		map_view->source_row + map_view->source_rows <= source->rows;
}

static SDL_FRect map_viewport(const struct sdl3_visual *visual,
		const struct sdl3_map_view *map_view)
{
	if (map_view->col == 0 && map_view->row == 0 &&
			map_view->cols == visual->cols &&
			map_view->rows == visual->rows) {
		return (SDL_FRect) {
			0.0f, 0.0f, (float)visual->output_width,
			(float)visual->output_height
		};
	}
	return (SDL_FRect) {
		(float)(visual->origin_x + map_view->col * visual->cell_width),
		(float)(visual->origin_y + map_view->row * visual->cell_height),
		(float)(map_view->cols * visual->cell_width),
		(float)(map_view->rows * visual->cell_height)
	};
}

struct sdl3_map_camera {
	SDL_FRect viewport;
	float cell_width;
	float cell_height;
	float center_col;
	float center_row;
};

static struct sdl3_map_camera map_camera(const struct sdl3_visual *visual,
		const struct sdl3_map_view *map_view)
{
	struct sdl3_map_camera result;

	result.viewport = map_viewport(visual, map_view);
	if (map_view->tile_mode) {
		int side;

		/* Detail is independent of the camera: changing 32/64px art must
		 * not move the world, cursor or mouse hit targets. */
		side = sdl3_zoom_pixel_art_size(visual->cell_height,
			map_view->zoom_percent, 32);
		result.cell_width = (float)side;
		result.cell_height = (float)side;
	} else {
		result.cell_width = (float)SDL_max(1,
			(visual->cell_width * map_view->zoom_percent + 50) / 100);
		result.cell_height = (float)SDL_max(1,
			(visual->cell_height * map_view->zoom_percent + 50) / 100);
	}
	result.center_col = sdl3_zoom_camera_center(map_view->source_col,
		map_view->source_cols, map_view->focus_col, result.viewport.w,
		result.cell_width);
	result.center_row = sdl3_zoom_camera_center(map_view->source_row,
		map_view->source_rows, map_view->focus_row, result.viewport.h,
		result.cell_height);
	if (map_view->hud_stats_visible) {
		/* Use the shared camera so drawing, effects and mouse hit-testing
		 * agree. The world can extend behind the HUD; its focus cannot. */
		if (map_view->sidebar_mode == SIDEBAR_LEFT) {
			float inset = visual->origin_x +
				(map_view->term_col + 1) * visual->cell_width - result.viewport.x;
			result.center_col = sdl3_zoom_safe_center(result.center_col,
				map_view->focus_col, result.viewport.w, result.cell_width,
				inset, 0.0f);
		} else if (map_view->sidebar_mode == SIDEBAR_TOP) {
			float inset = visual->origin_y +
				(map_view->term_row + 1) * visual->cell_height - result.viewport.y;
			result.center_row = sdl3_zoom_safe_center(result.center_row,
				map_view->focus_row, result.viewport.h, result.cell_height,
				inset, 0.0f);
		}
	}
	return result;
}

static SDL_FRect map_cell_rect(const struct sdl3_map_camera *camera, int col,
		int row)
{
	return (SDL_FRect) {
		camera->viewport.x + camera->viewport.w * 0.5f +
			(col - camera->center_col) * camera->cell_width,
		camera->viewport.y + camera->viewport.h * 0.5f +
			(row - camera->center_row) * camera->cell_height,
		camera->cell_width,
		camera->cell_height
	};
}

static const struct sdl3_map_art_subject *tile_subject_for_cell(
		const struct sdl3_map_view *map_view, int col, int row, int *index)
{
	if (!map_view || !index) return NULL;
	while (*index < map_view->art_subject_count) {
		const struct sdl3_map_art_subject *subject =
			&map_view->art_subjects[*index];

		if (subject->row < row ||
				(subject->row == row && subject->col < col)) {
			(*index)++;
			continue;
		}
		if (subject->row == row && subject->col == col) return subject;
		break;
	}
	return NULL;
}

static uint8_t map_tile_brightness(const struct sdl3_terrain_variant *variant,
		const struct sdl3_theme *theme)
{
	return variant && !variant->in_view ?
		(variant->remembered_brightness ? variant->remembered_brightness :
		 theme->remembered_world_brightness) : 255;
}

/** Use the same plate and visibility guard for stationary and lunging art. */
static bool draw_map_subject_art(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_map_view *map_view,
		const struct sdl3_map_art_subject *subject,
		const struct sdl3_cell *cell, const SDL_FRect *rect)
{
	if (map_view->tile_mode || !visual->map_font_loaded ||
			!sdl3_monster_art_map_enabled(map_view->zoom_percent) ||
			!subject || !cell || cell->codepoint != subject->glyph ||
			cell->foreground != subject->attr) {
		return false;
	}
	return sdl3_monster_art_pool_draw(&visual->map_art, renderer,
		visual->map_font.path, subject->asset_key, rect);
}

static bool combat_effect_cell(const struct sdl3_map_view *map_view,
		struct loc dungeon, int *col, int *row)
{
	if (!map_view || !map_view->active || !map_view->source || !col || !row) {
		return false;
	}
	*col = map_view->source_col + dungeon.x - map_view->dungeon_col;
	*row = map_view->source_row + dungeon.y - map_view->dungeon_row;
	return *col >= map_view->source_col &&
		*col < map_view->source_col + map_view->source_cols &&
		*row >= map_view->source_row &&
		*row < map_view->source_row + map_view->source_rows;
}

static void draw_combat_effect(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view,
		const struct sdl3_combat_effect *effect)
{
	struct sdl3_map_camera camera;
	SDL_FRect source_rect;
	SDL_FRect target_rect;
	SDL_Rect clip;
	int source_col = 0;
	int source_row = 0;
	int target_col;
	int target_row;
	int phase;
	struct sdl3_font *map_font = map_camera_font(visual);

	if (!visual || !renderer || !theme || !map_view || !effect ||
			!effect->active || !map_font ||
			!combat_effect_cell(map_view, effect->target, &target_col,
				&target_row)) {
		return;
	}
	camera = map_camera(visual, map_view);
	target_rect = map_cell_rect(&camera, target_col, target_row);
	clip = (SDL_Rect) {
		(int)camera.viewport.x, (int)camera.viewport.y,
		(int)camera.viewport.w, (int)camera.viewport.h
	};
	SDL_SetRenderClipRect(renderer, &clip);
	if (effect->kind == SDL3_EFFECT_DAMAGE) {
		Uint64 now = SDL_GetTicks();
		Uint64 elapsed = now > effect->started_ms ?
			now - effect->started_ms : 0;
		float progress = MIN(1.0f,
			(float)elapsed / (float)effect->duration_ms);
		SDL_Color color = effect->target_is_player ?
			(SDL_Color){ 255, 64, 64, 255 } :
			(SDL_Color){ 255, 184, 64, 255 };
		char number[24];
		float glyph_width = target_rect.w * 0.82f;
		float glyph_height = target_rect.h * 1.20f;
		float text_width;
		float x;
		float y;
		int i;

		if (progress < 0.38f) {
			SDL_FRect flash = target_rect;
			float pulse = 1.0f - progress / 0.38f;
			int inset = MAX(1, (int)MIN(flash.w, flash.h) / 12);

			flash.x += inset;
			flash.y += inset;
			flash.w -= inset * 2;
			flash.h -= inset * 2;
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
				(Uint8)(150.0f * pulse));
			SDL_RenderFillRect(renderer, &flash);
			SDL_SetRenderDrawColor(renderer, 255, 240, 220,
				(Uint8)(230.0f * pulse));
			SDL_RenderRect(renderer, &flash);
		}
		strnfmt(number, sizeof(number), "-%d", effect->amount);
		text_width = strlen(number) * glyph_width;
		x = target_rect.x + (target_rect.w - text_width) * 0.5f +
			((int)(effect->serial % 3) - 1) * target_rect.w * 0.18f;
		y = target_rect.y - target_rect.h * (0.20f + progress * 1.45f);
		color.a = (Uint8)(255.0f * (1.0f - progress));
		for (i = 0; number[i]; i++) {
			SDL_Color shadow = { 0, 0, 0, color.a };

			sdl3_font_draw(map_font, (uint32_t)number[i], shadow,
				x + i * glyph_width + 2.0f, y + 2.0f,
				glyph_width, glyph_height);
			sdl3_font_draw(map_font, (uint32_t)number[i], color,
				x + i * glyph_width, y, glyph_width, glyph_height);
		}
		SDL_SetRenderClipRect(renderer, NULL);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
		return;
	}
	if (!combat_effect_cell(map_view, effect->source, &source_col,
			&source_row)) {
		SDL_SetRenderClipRect(renderer, NULL);
		return;
	}
	source_rect = map_cell_rect(&camera, source_col, source_row);
	if (effect->kind == SDL3_EFFECT_MELEE) {
		static const int travel_percent[8] = {
			10, 28, 48, 62, 54, 38, 22, 8
		};
		const struct sdl3_cell *cell = sdl3_grid_cell(map_view->source,
			source_col, source_row);
		SDL_Color impact = effect->hit ? theme->accent : theme->muted;
		int travel;
		float dx;
		float dy;

		phase = sdl3_effect_phase(effect, SDL_GetTicks(), 8);
		travel = travel_percent[phase];
		dx = (target_rect.x - source_rect.x) * travel / 100.0f;
		dy = (target_rect.y - source_rect.y) * travel / 100.0f;

		if (cell) {
			SDL_Color background = sdl3_world_color(theme, cell->background);
			SDL_Color foreground = sdl3_world_color(theme, cell->foreground);
			SDL_FRect moving_rect = source_rect;
			bool subject_drawn = false;
			int subject_index = 0;
			const struct sdl3_map_art_subject *subject =
				tile_subject_for_cell(map_view, source_col, source_row,
					&subject_index);

			SDL_SetRenderDrawColor(renderer, background.r, background.g,
				background.b, background.a);
			SDL_RenderFillRect(renderer, &source_rect);
			moving_rect.x += dx;
			moving_rect.y += dy;
			if (map_view->tile_mode) {
				const struct sdl3_terrain_variant *variant =
					presented_terrain_variant(map_view, source_col, source_row, true);

				subject_drawn = sdl3_tiles_draw_subject(&visual->tiles,
					renderer, subject, cell, effect->source.x, effect->source.y,
					&moving_rect, map_tile_brightness(variant, theme));
			} else {
				subject_drawn = draw_map_subject_art(visual, renderer,
					map_view, subject, cell, &moving_rect);
			}
			if (!subject_drawn) {
				sdl3_font_draw(map_font, (uint32_t)cell->codepoint,
					foreground, moving_rect.x, moving_rect.y,
					moving_rect.w, moving_rect.h);
			}
		}
		if (phase >= 1 && phase <= 5) {
			SDL_FRect pulse = target_rect;
			int inset = MAX(1, (int)MIN(pulse.w, pulse.h) / 10);

			pulse.x += inset;
			pulse.y += inset;
			pulse.w -= inset * 2;
			pulse.h -= inset * 2;
			impact.a = effect->hit ? 220 : 130;
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			SDL_SetRenderDrawColor(renderer, impact.r, impact.g, impact.b,
				impact.a);
			SDL_RenderRect(renderer, &pulse);
			sdl3_font_draw(map_font,
				effect->hit ? (uint32_t)'*' : (uint32_t)'/', impact,
				target_rect.x, target_rect.y, target_rect.w, target_rect.h);
		}
	} else {
		static const int travel_percent[8] = {
			10, 23, 36, 49, 62, 75, 88, 100
		};
		SDL_Color color = sdl3_world_color(theme, effect->attr);
		float sx = source_rect.x + source_rect.w * 0.5f;
		float sy = source_rect.y + source_rect.h * 0.5f;
		float tx = target_rect.x + target_rect.w * 0.5f;
		float ty = target_rect.y + target_rect.h * 0.5f;
		int travel;
		float x;
		float y;
		float glyph_width;
		float glyph_height;

		phase = sdl3_effect_phase(effect, SDL_GetTicks(), 8);
		travel = travel_percent[phase];
		x = sx + (tx - sx) * travel / 100.0f;
		y = sy + (ty - sy) * travel / 100.0f;

		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 120);
		SDL_RenderLine(renderer, sx, sy, x, y);
		glyph_width = target_rect.w * 1.30f;
		glyph_height = target_rect.h * 1.18f;
		sdl3_font_draw(map_font,
			effect->glyph ? (uint32_t)effect->glyph : (uint32_t)'*', color,
			x - glyph_width * 0.5f, y - glyph_height * 0.5f,
			glyph_width, glyph_height);
	}
	SDL_SetRenderClipRect(renderer, NULL);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void draw_combat_effects(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view,
		const struct sdl3_combat_effects *effects)
{
	int i;

	if (!effects || !effects->active) return;
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		draw_combat_effect(visual, renderer, theme, map_view,
			&effects->items[i]);
	}
}

static void draw_player_vitality(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_map_view *map_view)
{
	struct sdl3_map_camera camera;
	SDL_FRect rect;
	SDL_Rect clip;
	SDL_Color color;
	int col;
	int row;
	int warning;
	int hp;
	int severity;
	struct sdl3_font *map_font = map_camera_font(visual);

	if (!visual || !renderer || !map_view ||
			!map_view->player_vitality_visible ||
			!map_font || map_view->player_hp <= 0) return;
	warning = map_view->player_warning_hp;
	if (warning <= 0 || map_view->player_hp > warning ||
			!combat_effect_cell(map_view,
				loc(map_view->player_dungeon_col,
					map_view->player_dungeon_row), &col, &row)) return;
	hp = MIN(warning, MAX(0, map_view->player_hp));
	severity = 255 - hp * 255 / MAX(1, warning);
	color = (SDL_Color) {
		255,
		(Uint8)(245 - severity * 205 / 255),
		(Uint8)(245 - severity * 205 / 255),
		255
	};
	camera = map_camera(visual, map_view);
	rect = map_cell_rect(&camera, col, row);
	clip = (SDL_Rect) {
		(int)camera.viewport.x, (int)camera.viewport.y,
		(int)camera.viewport.w, (int)camera.viewport.h
	};
	SDL_SetRenderClipRect(renderer, &clip);
	sdl3_font_draw(map_font, (uint32_t)'@', color, rect.x, rect.y,
		rect.w, rect.h);
	SDL_SetRenderClipRect(renderer, NULL);
}

static uint32_t weather_particle_hash(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7feb352dU;
	value ^= value >> 15;
	value *= 0x846ca68bU;
	value ^= value >> 16;
	return value;
}

static bool weather_ground_rect(const struct sdl3_map_view *map_view,
		const struct sdl3_map_camera *camera, uint32_t seed, int particle,
		SDL_FRect *rect)
{
	int visible_cols;
	int visible_rows;
	int first_col;
	int first_row;
	int last_col;
	int last_row;
	int attempt;

	if (!map_view || !map_view->source || !camera || !rect ||
			map_view->source_cols <= 0 || map_view->source_rows <= 0) {
		return false;
	}
	visible_cols = SDL_max(1,
		(int)(camera->viewport.w / camera->cell_width) + 3);
	visible_rows = SDL_max(1,
		(int)(camera->viewport.h / camera->cell_height) + 3);
	first_col = SDL_max(map_view->source_col,
		(int)camera->center_col - visible_cols / 2 - 1);
	first_row = SDL_max(map_view->source_row,
		(int)camera->center_row - visible_rows / 2 - 1);
	last_col = SDL_min(map_view->source_col + map_view->source_cols - 1,
		first_col + visible_cols - 1);
	last_row = SDL_min(map_view->source_row + map_view->source_rows - 1,
		first_row + visible_rows - 1);
	if (last_col < first_col || last_row < first_row) return false;

	for (attempt = 0; attempt < 12; attempt++) {
		uint32_t choice = weather_particle_hash(seed ^
			(uint32_t)particle * 0x045d9f3bU ^
			(uint32_t)attempt * 0x27d4eb2dU);
		int col = first_col + (int)(choice %
			(uint32_t)(last_col - first_col + 1));
		int row = first_row + (int)((choice >> 16) %
			(uint32_t)(last_row - first_row + 1));
		const struct sdl3_cell *cell = sdl3_grid_cell(map_view->source, col,
			row);

		/* Town ground uses Angband's authoritative floor dot.  Splashes and
		 * brief settling marks are overlays only; the stored glyph is untouched. */
		if (cell && cell->codepoint == L'.') {
			*rect = map_cell_rect(camera, col, row);
			return true;
		}
	}
	return false;
}

static int weather_particle_count(const struct sdl3_weather_state *weather)
{
	if (!weather) return 0;
	if (weather->kind == SDL3_WEATHER_KIND_SNOW) {
		static const int snow_counts[] = { 0, 18, 30, 44 };

		return snow_counts[SDL_min((int)weather->intensity,
			SDL3_WEATHER_HEAVY)];
	}
	{
		static const int rain_counts[] = { 0, 28, 48, 72 };

		return rain_counts[SDL_min((int)weather->intensity,
			SDL3_WEATHER_HEAVY)];
	}
}

static void draw_weather(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const struct sdl3_map_view *map_view,
		const struct sdl3_weather_state *weather)
{
	struct sdl3_map_camera camera;
	SDL_Rect old_clip;
	SDL_Rect clip;
	bool had_clip;
	int count;
	int i;

	if (!visual || !renderer || !map_view || !map_view->active || !weather ||
			!weather->visible || weather->kind == SDL3_WEATHER_KIND_NONE) {
		return;
	}
	camera = map_camera(visual, map_view);
	clip = (SDL_Rect) {
		(int)camera.viewport.x, (int)camera.viewport.y,
		(int)camera.viewport.w, (int)camera.viewport.h
	};
	had_clip = SDL_RenderClipEnabled(renderer);
	SDL_GetRenderClipRect(renderer, &old_clip);
	SDL_SetRenderClipRect(renderer, &clip);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	count = weather_particle_count(weather);

	for (i = 0; i < count; i++) {
		uint32_t seed = weather_particle_hash(weather->seed ^
			(uint32_t)i * 0x9e3779b9U);
		SDL_FRect ground;
		bool has_ground = weather_ground_rect(map_view, &camera,
			weather->seed, i, &ground);
		float impact_x = has_ground ? ground.x + ground.w *
			(0.25f + (float)(seed & 255U) / 510.0f) :
			camera.viewport.x + (float)(seed & 0xffffU) / 65535.0f *
			camera.viewport.w;
		float impact_y = has_ground ? ground.y + ground.h * 0.72f :
			camera.viewport.y + camera.viewport.h;
		uint32_t speed = weather->kind == SDL3_WEATHER_KIND_RAIN ?
			(37U + (seed >> 24) % 17U) : (11U + (seed >> 24) % 9U);
		uint32_t phase = ((seed >> 8) + weather->frame * speed) % 1000U;
		uint32_t fall_limit = weather->kind == SDL3_WEATHER_KIND_RAIN ?
			900U : 950U;

		if (phase < fall_limit) {
			float progress = (float)phase / (float)fall_limit;
			float y = camera.viewport.y - 8.0f + progress *
				(impact_y - camera.viewport.y + 8.0f);

			if (weather->kind == SDL3_WEATHER_KIND_RAIN) {
				float length = 4.0f + 1.5f * weather->intensity;
				float wind = (impact_y - y) * 0.08f;
				float x = impact_x - wind;
				uint8_t alpha = (uint8_t)(85 + weather->intensity * 20);

				SDL_SetRenderDrawColor(renderer, 146, 190, 222, alpha);
				SDL_RenderLine(renderer, x, y, x - length * 0.45f,
					y + length);
			} else {
				int drift_phase = (int)((weather->frame + i * 13U) % 24U);
				float drift = (float)(drift_phase - 12) * 0.22f;
				float x = impact_x + drift;
				float radius = weather->intensity == SDL3_WEATHER_HEAVY ?
					1.5f : 1.0f;

				SDL_SetRenderDrawColor(renderer, 226, 235, 242, 175);
				SDL_RenderLine(renderer, x - radius, y, x + radius, y);
				SDL_RenderLine(renderer, x, y - radius, x, y + radius);
			}
		} else if (has_ground) {
			float settle = (float)(phase - fall_limit) /
				(float)(1000U - fall_limit);

			if (weather->kind == SDL3_WEATHER_KIND_RAIN) {
				float width = SDL_max(2.0f,
					ground.w * (0.18f + settle * 0.16f));
				uint8_t alpha = (uint8_t)(150.0f * (1.0f - settle));

				SDL_SetRenderDrawColor(renderer, 164, 205, 232, alpha);
				/* Two kicked-up arms and a short ripple read as a tiny ASCII
				 * \\_/ splash without replacing the underlying floor dot. */
				SDL_RenderLine(renderer, impact_x - width, impact_y - 1.0f,
					impact_x - width * 0.35f, impact_y);
				SDL_RenderLine(renderer, impact_x + width * 0.35f, impact_y,
					impact_x + width, impact_y - 1.0f);
				SDL_RenderLine(renderer, impact_x - width * 0.45f,
					impact_y + 1.0f, impact_x + width * 0.45f,
					impact_y + 1.0f);
			} else {
				uint8_t alpha = (uint8_t)(120.0f * (1.0f - settle));

				SDL_SetRenderDrawColor(renderer, 232, 239, 244, alpha);
				SDL_RenderLine(renderer, impact_x - 1.0f, impact_y,
					impact_x + 1.0f, impact_y);
			}
		}
	}

	if (weather->kind == SDL3_WEATHER_KIND_RAIN && weather->flash_ticks) {
		static const uint8_t flash_alpha[SDL3_WEATHER_FLASH_TICKS + 1] = {
			0, 14, 48, 22, 72
		};
		SDL_FRect flash = camera.viewport;
		uint8_t alpha = flash_alpha[SDL_min((int)weather->flash_ticks,
			(int)SDL3_WEATHER_FLASH_TICKS)];

		SDL_SetRenderDrawColor(renderer, 205, 220, 255, alpha);
		SDL_RenderFillRect(renderer, &flash);
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	SDL_SetRenderClipRect(renderer, had_clip ? &old_clip : NULL);
}

static void draw_store_art(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_store_art *store_art)
{
	SDL_FRect panel;
	SDL_FRect portrait;
	SDL_Color canvas;
	int content_cols;
	float inset_x;
	float inset_y;

	if (!visual || !renderer || !store_art || !store_art->active ||
			!visual->font_loaded) {
		return;
	}
	content_cols = sdl3_store_art_content_cols(visual->cols);
	if (content_cols <= 0 || content_cols >= visual->cols) return;
	canvas = theme ? theme->canvas :
		(SDL_Color){ 0, 0, 0, SDL_ALPHA_OPAQUE };
	panel = (SDL_FRect) {
		(float)(visual->origin_x +
			(content_cols + 1) * visual->cell_width),
		(float)visual->origin_y,
		(float)((visual->cols - content_cols - 1) * visual->cell_width),
		(float)(visual->rows * visual->cell_height)
	};
	SDL_SetRenderDrawColor(renderer, canvas.r, canvas.g, canvas.b, canvas.a);
	SDL_RenderFillRect(renderer, &panel);

	inset_x = SDL_max(2.0f, visual->cell_width * 0.5f);
	inset_y = SDL_max(2.0f, visual->cell_height * 0.75f);
	portrait = (SDL_FRect) {
		panel.x + inset_x, panel.y + inset_y,
		panel.w - inset_x * 2.0f, panel.h - inset_y * 2.0f
	};
	if (portrait.w <= 0.0f || portrait.h <= 0.0f) return;
	sdl3_monster_art_draw_shopkeeper(&visual->shopkeeper_art, renderer,
		visual->font.path, store_art->asset_key, &portrait);
}

static bool ensure_zoom_out_texture(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_map_view *map_view,
		const struct sdl3_map_camera *camera, bool *rebuilt)
{
	int width = (int)camera->cell_width * map_view->source_cols;
	int height = (int)camera->cell_height * map_view->source_rows;

	*rebuilt = false;
	if (width <= 0 || height <= 0) return false;
	if (visual->zoom_out_texture &&
			visual->zoom_out_source == map_view->source &&
			visual->zoom_out_texture_width == width &&
			visual->zoom_out_texture_height == height) {
		return true;
	}
	invalidate_zoom_out_texture(visual);
	visual->zoom_out_texture = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, width, height);
	if (!visual->zoom_out_texture) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not create the SDL3 zoom-out cache; using direct draws: %s",
			SDL_GetError());
		return false;
	}
	SDL_SetTextureBlendMode(visual->zoom_out_texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(visual->zoom_out_texture, SDL_SCALEMODE_NEAREST);
	visual->zoom_out_texture_width = width;
	visual->zoom_out_texture_height = height;
	visual->zoom_out_source = map_view->source;
	*rebuilt = true;
	return true;
}

static bool update_zoom_out_texture(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view,
		const struct sdl3_map_camera *camera, int *updates)
{
	SDL_Texture *previous_target = SDL_GetRenderTarget(renderer);
	struct sdl3_font *font = map_camera_font(visual);
	bool rebuilt;
	int col;
	int row;

	*updates = 0;
	if (!font || !ensure_zoom_out_texture(visual, renderer, map_view, camera,
			&rebuilt)) {
		return false;
	}
	if (!SDL_SetRenderTarget(renderer, visual->zoom_out_texture)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not target the SDL3 zoom-out cache; using direct draws: %s",
			SDL_GetError());
		invalidate_zoom_out_texture(visual);
		return false;
	}
	for (row = map_view->source_row;
			row < map_view->source_row + map_view->source_rows; row++) {
		for (col = map_view->source_col;
				col < map_view->source_col + map_view->source_cols; col++) {
			const struct sdl3_cell *source_cell =
				sdl3_grid_cell(map_view->source, col, row);
			struct sdl3_cell replacement;
			const struct sdl3_terrain_variant *variant;
			const struct sdl3_cell *cell = presented_map_cell(source_cell,
				col, row, map_view, &replacement, &variant, true);
			SDL_FRect rect;
			SDL_Color background;
			SDL_Color foreground;

			if (!cell || (!rebuilt && !source_cell->dirty)) continue;
			rect = (SDL_FRect) {
				(float)((col - map_view->source_col) * camera->cell_width),
				(float)((row - map_view->source_row) * camera->cell_height),
				camera->cell_width, camera->cell_height
			};
			background = sdl3_world_color(theme, cell->background);
			foreground = terrain_foreground_color(
				sdl3_world_color(theme, cell->foreground), variant, theme);
			SDL_SetRenderDrawColor(renderer, background.r, background.g,
				background.b, background.a);
			SDL_RenderFillRect(renderer, &rect);
			sdl3_font_draw(font, (uint32_t)cell->codepoint,
				foreground, rect.x, rect.y, rect.w, rect.h);
			(*updates)++;
		}
	}
	if (!SDL_SetRenderTarget(renderer, previous_target)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not restore the SDL3 render target after updating the zoom-out "
			"cache: %s", SDL_GetError());
		invalidate_zoom_out_texture(visual);
		return false;
	}
	return true;
}

static int draw_zoomed_subject_art(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view,
		const struct sdl3_map_camera *camera)
{
	int updates = 0;
	int i;

	if (map_view->tile_mode ||
			!sdl3_monster_art_map_enabled(map_view->zoom_percent) ||
			!visual->map_font_loaded) {
		return 0;
	}
	for (i = 0; i < map_view->art_subject_count; i++) {
		const struct sdl3_map_art_subject *subject =
			&map_view->art_subjects[i];
		const struct sdl3_cell *cell;
		SDL_FRect rect;
		SDL_Color background;
		SDL_Color foreground;

		if (subject->col < map_view->source_col ||
				subject->col >= map_view->source_col + map_view->source_cols ||
				subject->row < map_view->source_row ||
				subject->row >= map_view->source_row + map_view->source_rows) {
			continue;
		}
		rect = map_cell_rect(camera, subject->col, subject->row);
		if (rect.x + rect.w <= camera->viewport.x ||
				rect.x >= camera->viewport.x + camera->viewport.w ||
				rect.y + rect.h <= camera->viewport.y ||
				rect.y >= camera->viewport.y + camera->viewport.h) {
			continue;
		}
		cell = sdl3_grid_cell(map_view->source, subject->col, subject->row);
		if (!cell || cell->codepoint != subject->glyph ||
				cell->foreground != subject->attr) {
			continue;
		}
		background = sdl3_world_color(theme, cell->background);
		foreground = sdl3_world_color(theme, cell->foreground);
		SDL_SetRenderDrawColor(renderer, background.r, background.g,
			background.b, background.a);
		SDL_RenderFillRect(renderer, &rect);
		if (!draw_map_subject_art(visual, renderer, map_view, subject, cell,
				&rect)) {
			sdl3_font_draw(&visual->map_font, (uint32_t)cell->codepoint,
				foreground, rect.x, rect.y, rect.w, rect.h);
		}
		updates++;
	}
	return updates;
}

static int map_world_coordinate(int source, int source_first,
		int dungeon_first, int tile_size, bool expanded)
{
	return dungeon_first + (source - source_first) /
		(expanded ? 1 : SDL_max(1, tile_size));
}

static bool draw_map_tile_cell(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view,
		const struct sdl3_cell *source_cell, const struct sdl3_cell *cell,
		const struct sdl3_terrain_variant *variant,
		const struct sdl3_map_art_subject *subject, int col, int row,
		const SDL_FRect *rect, struct sdl3_font *font)
{
	SDL_Color background;
	SDL_Color foreground;
	char feature_key[SDL3_TILE_KEY_CAPACITY] = "";
	int world_col;
	int world_row;
	uint8_t brightness;
	bool terrain_drawn = false;
	bool subject_drawn = false;
	bool terrain_only;

	if (!visual || !renderer || !theme || !map_view || !source_cell || !cell ||
			!rect || !font) {
		return false;
	}
	brightness = map_tile_brightness(variant, theme);
	world_col = map_world_coordinate(col, map_view->source_col,
		map_view->dungeon_col, map_view->tile_width,
		map_view->expanded_source);
	world_row = map_world_coordinate(row, map_view->source_row,
		map_view->dungeon_row, map_view->tile_height,
		map_view->expanded_source);
	background = sdl3_world_color(theme, cell->background);
	foreground = terrain_foreground_color(
		sdl3_world_color(theme, cell->foreground), variant, theme);
	SDL_SetRenderDrawColor(renderer, background.r, background.g, background.b,
		background.a);
	SDL_RenderFillRect(renderer, rect);
	if (variant && variant->tile_id && sdl3_tile_make_key(feature_key,
			sizeof(feature_key), SDL3_TILE_FEATURE, variant->tile_id)) {
		terrain_drawn = sdl3_tiles_draw(&visual->tiles, renderer, feature_key,
			world_col, world_row, rect, brightness);
	}
	terrain_only = variant && (variant->tile_glyph_policy == SDL3_TILE_GLYPH_REPLACE ||
		(source_cell->codepoint == variant->codepoint &&
		 source_cell->foreground == variant->foreground));
	if (variant && variant->tile_overlay_key) {
		/* Missing fixture art must retain its glyph even if the base drew. */
		terrain_drawn = sdl3_tiles_draw(&visual->tiles, renderer,
			variant->tile_overlay_key, world_col, world_row, rect, brightness);
	}
	subject_drawn = sdl3_tiles_draw_subject(&visual->tiles, renderer, subject,
		source_cell, world_col, world_row, rect, brightness);
	if (!subject_drawn && ((variant && variant->tile_glyph_policy == SDL3_TILE_GLYPH_PRESERVE) ||
			!(terrain_only && terrain_drawn))) {
		sdl3_font_draw(font, (uint32_t)cell->codepoint, foreground,
			rect->x, rect->y, rect->w, rect->h);
	}
	return true;
}

static int draw_scaled_map(struct sdl3_visual *visual,
		SDL_Renderer *renderer,
		const struct sdl3_theme *theme, const struct sdl3_map_view *map_view,
		SDL_Color canvas)
{
	struct sdl3_map_camera camera = map_camera(visual, map_view);
	struct sdl3_font *font = map_camera_font(visual);
	SDL_Rect clip = {
		(int)camera.viewport.x, (int)camera.viewport.y,
		(int)camera.viewport.w, (int)camera.viewport.h
	};
	int updates = 0;
	bool cached = false;
	int col;
	int row;
	int subject_index = 0;

	if (map_view->expanded_source && !map_view->tile_mode) {
		cached = update_zoom_out_texture(visual, renderer, theme, map_view,
			&camera, &updates);
	}
	SDL_SetRenderClipRect(renderer, &clip);
	SDL_SetRenderDrawColor(renderer, canvas.r, canvas.g, canvas.b, canvas.a);
	SDL_RenderFillRect(renderer, &camera.viewport);
	if (cached) {
		SDL_FRect target = {
			camera.viewport.x + camera.viewport.w * 0.5f +
				(map_view->source_col - camera.center_col) *
				camera.cell_width,
			camera.viewport.y + camera.viewport.h * 0.5f +
				(map_view->source_row - camera.center_row) *
				camera.cell_height,
			(float)visual->zoom_out_texture_width,
			(float)visual->zoom_out_texture_height
		};

		SDL_RenderTexture(renderer, visual->zoom_out_texture, NULL, &target);
	} else {
		for (row = map_view->source_row;
				row < map_view->source_row + map_view->source_rows; row++) {
			for (col = map_view->source_col;
					col < map_view->source_col + map_view->source_cols;
					col++) {
				const struct sdl3_cell *source_cell =
					sdl3_grid_cell(map_view->source, col, row);
				struct sdl3_cell replacement;
				const struct sdl3_terrain_variant *variant;
				const struct sdl3_cell *cell = presented_map_cell(source_cell,
					col, row, map_view, &replacement, &variant, true);
				const struct sdl3_map_art_subject *subject =
					tile_subject_for_cell(map_view, col, row, &subject_index);
				SDL_FRect rect = map_cell_rect(&camera, col, row);
				SDL_Color background;
				SDL_Color foreground;

				if (rect.x + rect.w <= camera.viewport.x ||
						rect.x >= camera.viewport.x + camera.viewport.w ||
						rect.y + rect.h <= camera.viewport.y ||
						rect.y >= camera.viewport.y + camera.viewport.h) {
					continue;
				}
				if (!cell) continue;
				if (map_view->tile_mode && draw_map_tile_cell(visual, renderer,
						theme, map_view, source_cell, cell, variant, subject, col,
						row, &rect, font)) {
					updates++;
					continue;
				}
				background = sdl3_world_color(theme, cell->background);
				foreground = terrain_foreground_color(
					sdl3_world_color(theme, cell->foreground), variant, theme);
				SDL_SetRenderDrawColor(renderer, background.r, background.g,
					background.b, background.a);
				SDL_RenderFillRect(renderer, &rect);
				sdl3_font_draw(font, (uint32_t)cell->codepoint,
					foreground, rect.x, rect.y, rect.w, rect.h);
				updates++;
			}
		}
	}
	updates += draw_zoomed_subject_art(visual, renderer, theme, map_view,
		&camera);
	SDL_SetRenderClipRect(renderer, NULL);
	if (map_view->expanded_source) sdl3_grid_mark_clean(map_view->source);
	return updates;
}

static void draw_transparent_cell(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_cell *cell, int target_col, int target_row)
{
	SDL_FRect rect;
	SDL_Color foreground;
	SDL_Color shadow = { 0, 0, 0, 224 };

	if (!visual || !renderer || !theme || !cell || cell->codepoint == L' ' ||
			target_col < 0 || target_col >= visual->cols || target_row < 0 ||
			target_row >= visual->rows) {
		return;
	}
	rect = (SDL_FRect) {
		(float)(visual->origin_x + target_col * visual->cell_width),
		(float)(visual->origin_y + target_row * visual->cell_height),
		(float)visual->cell_width, (float)visual->cell_height
	};
	foreground = sdl3_theme_color(theme, cell->foreground);
	if (visual->font_fits) {
		/* A one-pixel ink shadow preserves legibility without introducing a
		 * panel, border, blur, or opaque block over the world. */
		sdl3_font_draw(&visual->font, (uint32_t)cell->codepoint, shadow,
			rect.x + 1.0f, rect.y + 1.0f, rect.w, rect.h);
		sdl3_font_draw(&visual->font, (uint32_t)cell->codepoint, foreground,
			rect.x, rect.y, rect.w, rect.h);
	} else {
		draw_debug_glyph(visual, renderer, cell, rect.x, rect.y, foreground);
	}
	visual->last_cell_updates++;
}

static void draw_message_overlay(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_grid *grid, enum sdl3_dock_placement placement,
		int reserved_bottom_rows)
{
	SDL_Rect clip;
	struct sdl3_cell_bounds bounds;
	int first_col;
	int first_row;
	int col;
	int row;

	if (!visual || !renderer || !theme || !grid || !grid->cells ||
			grid->cols <= 0 || grid->rows <= 0) {
		return;
	}
	if (!sdl3_grid_content_bounds(grid, 0, 0, grid->cols, grid->rows,
			&bounds)) return;
	sdl3_layout_message_offset(placement, visual->cols,
		MAX(1, visual->rows - reserved_bottom_rows),
		bounds.col + bounds.cols, grid->rows, &first_col, &first_row);
	clip = (SDL_Rect) { 0, 0, visual->output_width,
		visual->origin_y + (visual->rows - reserved_bottom_rows) * visual->cell_height };
	SDL_SetRenderClipRect(renderer, &clip);
	sdl3_ui_draw_backplate(renderer, visual, theme, &bounds, first_col, first_row);
	for (row = 0; row < grid->rows; row++) {
		for (col = 0; col < grid->cols; col++) {
			draw_transparent_cell(visual, renderer, theme,
				sdl3_grid_cell(grid, col, row), first_col + col, first_row + row);
		}
	}
	SDL_SetRenderClipRect(renderer, NULL);
}

static void draw_world_stats(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_grid *grid, const struct sdl3_map_view *map_view,
		bool visible, bool temporarily_closed)
{
	struct sdl3_cell_bounds bounds;
	bool bounds_found;
	int col;
	int row;

	if (!visual || !renderer || !theme || !grid || !grid->cells ||
			!map_view_uses_camera(visual, map_view)) {
		return;
	}
	/* Fishing owns the playfield while active.  Paint over the terminal's
	 * cached left sidebar rather than mutating the player's saved preference;
	 * the next ordinary frame therefore reopens it automatically. */
	if (temporarily_closed && map_view->sidebar_mode == SIDEBAR_LEFT) {
		sdl3_ui_fill_cells(renderer, visual, 0, 1,
			MIN(map_view->term_col, grid->cols), grid->rows - 1,
			theme->canvas);
		return;
	}
	if (!visible) return;
	if (map_view->sidebar_mode == SIDEBAR_LEFT) {
		bounds_found = sdl3_grid_content_bounds(grid, 0, 1,
			MIN(map_view->term_col, grid->cols), grid->rows - 1, &bounds);
		if (bounds_found) {
			sdl3_ui_draw_backplate(renderer, visual, theme, &bounds, 0, 0);
		}
		for (row = 1; row < grid->rows; row++) {
			for (col = 0; col < map_view->term_col && col < grid->cols; col++) {
				draw_transparent_cell(visual, renderer, theme,
					sdl3_grid_cell(grid, col, row), col, row);
			}
		}
	} else if (map_view->sidebar_mode == SIDEBAR_TOP) {
		/* Row 3 is the native cave fallback, replaced by the semantic strip. */
		int rows = map_view->cave_status.active ? 3 : map_view->term_row;
		if (sdl3_grid_content_bounds(grid, 0, 1, grid->cols,
				MIN(rows, grid->rows) - 1, &bounds)) {
			sdl3_ui_draw_backplate(renderer, visual, theme, &bounds, 0, 0);
		}
		for (row = 1; row < rows && row < grid->rows; row++) {
			for (col = 0; col < grid->cols; col++) {
				draw_transparent_cell(visual, renderer, theme,
					sdl3_grid_cell(grid, col, row), col, row);
			}
		}
	}
}

static void draw_cave_status(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_map_view *view)
{
	const struct ui_spelunking_status *status;
	char text[192];
	int row, width;
	if (!view || !view->active || !view->cave_status.active) return;
	status = &view->cave_status;
	row = visual->rows - view->status_rows;
	width = visual->cols - 2;
	sdl3_ui_fill_cells(renderer, visual, 0, row, visual->cols,
		view->status_rows, theme->canvas);
	strnfmt(text, sizeof(text), "HP %d/%d   Stamina %d/%d   Air %d/%d   %s",
		status->hp, status->max_hp, status->stamina, status->max_stamina,
		status->air, status->max_air, status->activity);
	sdl3_ui_draw_text(visual, text, 1, row, width, theme->text);
	sdl3_ui_draw_text(visual, status->location, 1, row + 1, width, theme->accent);
	strnfmt(text, sizeof(text), "%s%s%s", status->effort,
		status->effort[0] && status->can_fish ? "   " : "",
		status->can_fish ? "Ctrl-C: Fish" : "");
	sdl3_ui_draw_text(visual, text, 1, row + 2, width, theme->text);
}

static void draw_prompt_overlay(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_grid *grid, const struct sdl3_map_view *map_view,
		bool cursor_visible)
{
	int col;

	if (!cursor_visible || !grid || !grid->cells ||
			!map_view_uses_camera(visual, map_view)) {
		return;
	}
	/* Row zero is Angband's authoritative active Look/target description.  The
	 * input cursor moves onto the selected map grid, so its row cannot decide
	 * whether the description is visible.  Preserve the line for every active
	 * modal cursor, including alongside a richer monster card. */
	for (col = 0; col < grid->cols; col++) {
		draw_transparent_cell(visual, renderer, theme,
			sdl3_grid_cell(grid, col, 0), col, 0);
	}
}

static bool ensure_scene_texture(struct sdl3_visual *visual,
		SDL_Renderer *renderer)
{
	if (!visual || !renderer || visual->scene_cache_unavailable) return false;
	if (visual->scene_texture &&
			visual->scene_texture_width == visual->output_width &&
			visual->scene_texture_height == visual->output_height) {
		return true;
	}
	if (visual->scene_texture) SDL_DestroyTexture(visual->scene_texture);
	visual->scene_texture = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
		visual->output_width, visual->output_height);
	if (!visual->scene_texture) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not create the SDL3 scene cache; animated overlays will "
			"use full redraws: %s", SDL_GetError());
		visual->scene_texture_width = 0;
		visual->scene_texture_height = 0;
		visual->scene_cache_unavailable = true;
		visual->scene_cache_ready = false;
		return false;
	}
	SDL_SetTextureBlendMode(visual->scene_texture, SDL_BLENDMODE_NONE);
	SDL_SetTextureScaleMode(visual->scene_texture, SDL_SCALEMODE_NEAREST);
	visual->scene_texture_width = visual->output_width;
	visual->scene_texture_height = visual->output_height;
	visual->scene_cache_ready = false;
	return true;
}

static void draw_base_scene(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_grid *grid,
		const struct sdl3_theme *theme, const struct sdl3_map_view *map_view,
		SDL_Color canvas, bool grid_cache_ready)
{
	SDL_FRect grid_rect = {
		(float)visual->origin_x, (float)visual->origin_y,
		(float)(visual->cell_width * visual->cols),
		(float)(visual->cell_height * visual->rows)
	};

	SDL_SetRenderDrawColor(renderer, canvas.r, canvas.g, canvas.b, canvas.a);
	SDL_RenderClear(renderer);
	if (!grid_cache_ready || !SDL_RenderTexture(renderer,
			visual->grid_texture, NULL, &grid_rect)) {
		draw_grid_direct(visual, renderer, grid, theme, map_view);
	}
	if (map_view_uses_camera(visual, map_view)) {
		visual->last_cell_updates += draw_scaled_map(visual, renderer,
			theme, map_view, canvas);
	}
}

static void draw_foreground_overlays(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view,
		const struct sdl3_inspect_card *inspect_card, bool cursor_visible,
		int cursor_col, int cursor_row)
{
	if (inspect_card && inspect_card->active && !visual->card_font_loaded &&
			!sdl3_visual_ensure_card_font(visual, renderer)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not prepare the SDL3 examine-card font: %s",
			SDL_GetError());
	}
	sdl3_inspect_card_draw(inspect_card, renderer, visual, theme, map_view);
	if (cursor_visible) {
		SDL_Color cursor = sdl3_world_color(theme, COLOUR_YELLOW);
		bool scaled_cursor = map_view_uses_camera(visual, map_view) &&
			map_view->cursor_col >= map_view->source_col &&
			map_view->cursor_col <
				map_view->source_col + map_view->source_cols &&
			map_view->cursor_row >= map_view->source_row &&
			map_view->cursor_row <
				map_view->source_row + map_view->source_rows;
		bool ordinary_cursor = !scaled_cursor && cursor_col >= 0 &&
			cursor_col < visual->cols && cursor_row >= 0 &&
			cursor_row < visual->rows;
		struct sdl3_map_camera camera = { 0 };
		SDL_FRect rect;

		if (!scaled_cursor && !ordinary_cursor) return;
		if (scaled_cursor) {
			camera = map_camera(visual, map_view);
			rect = map_cell_rect(&camera, map_view->cursor_col,
				map_view->cursor_row);
		} else {
			rect = (SDL_FRect) {
				(float)(visual->origin_x + cursor_col * visual->cell_width),
				(float)(visual->origin_y + cursor_row * visual->cell_height),
				(float)visual->cell_width,
				(float)visual->cell_height
			};
		}

		if (scaled_cursor) {
			SDL_Rect clip = {
				(int)camera.viewport.x, (int)camera.viewport.y,
				(int)camera.viewport.w, (int)camera.viewport.h
			};

			SDL_SetRenderClipRect(renderer, &clip);
		}
		SDL_SetRenderDrawColor(renderer, cursor.r, cursor.g, cursor.b,
			cursor.a);
		/* A single physical pixel disappears at giant zoom. */
		{
			int thickness = SDL_max(2, SDL_min(4,
				(int)SDL_min(rect.w, rect.h) / 12));
			int i;

			for (i = 0; i < thickness && rect.w > 2.0f && rect.h > 2.0f;
					i++) {
				SDL_RenderRect(renderer, &rect);
				rect.x += 1.0f;
				rect.y += 1.0f;
				rect.w -= 2.0f;
				rect.h -= 2.0f;
			}
		}
		if (scaled_cursor) SDL_SetRenderClipRect(renderer, NULL);
	}
}

static void record_present(struct sdl3_visual *visual, Uint64 started)
{
	Uint64 elapsed = SDL_GetTicksNS() - started;

	visual->frames_presented++;
	visual->cell_updates_total += (Uint64)visual->last_cell_updates;
	visual->present_wait_ns_total += elapsed;
	if (elapsed > visual->present_wait_ns_max) {
		visual->present_wait_ns_max = elapsed;
	}
}

void sdl3_visual_render(struct sdl3_visual *visual, SDL_Renderer *renderer,
		const struct sdl3_presentation_frame *frame)
{
	struct sdl3_grid *grid = frame ? frame->grid : NULL;
	const struct sdl3_theme *theme = frame ? frame->theme : NULL;
	const struct sdl3_map_view *map_view = frame ? frame->map_view : NULL;
	bool cache_ready;
	bool scene_targeted = false;
	bool scene_copied = false;
	bool animated_overlay_active;
	bool fishing_active;
	Uint64 present_started;

	if (!visual || !renderer || !frame || !grid ||
			(!grid->dirty && !(frame->dock_active && frame->dock_grid &&
			frame->dock_grid->dirty))) {
		return;
	}
	SDL_Color canvas = theme ? theme->canvas :
		(SDL_Color){ 0, 0, 0, SDL_ALPHA_OPAQUE };
	int tile_size = frame->tile_mode ? (frame->hybrid_tile_size == 32 ? 32 : 64) : 0;
	if (visual->hybrid_tile_size != tile_size) {
		char manifest[32];

		sdl3_tiles_free(&visual->tiles);
		visual->hybrid_tile_size = tile_size;
		if (tile_size) {
			strnfmt(manifest, sizeof(manifest), "hybrid-%d.txt", tile_size);
			if (!sdl3_tiles_init(&visual->tiles, renderer, manifest)) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Hybrid %dpx portraits unavailable; using ordinary glyphs", tile_size);
			}
		}
		sdl3_visual_invalidate_grid_cache(visual);
		sdl3_grid_mark_all_dirty(grid);
	}
	cache_ready = update_grid_texture(visual, renderer, grid, theme, map_view);
	if (!cache_ready) SDL_SetRenderTarget(renderer, NULL);
	fishing_active = sdl3_fishing_is_active(frame->fishing);
	animated_overlay_active = (frame->weather && frame->weather->visible) ||
		(frame->combat_effects && frame->combat_effects->active) ||
		fishing_active;
	if (animated_overlay_active && ensure_scene_texture(visual, renderer) &&
			SDL_SetRenderTarget(renderer, visual->scene_texture)) {
		scene_targeted = true;
	}
	if (!animated_overlay_active) visual->scene_cache_ready = false;
	draw_base_scene(visual, renderer, grid, theme, map_view, canvas,
		cache_ready);
	if (scene_targeted && SDL_SetRenderTarget(renderer, NULL)) {
		visual->scene_cache_ready = true;
		scene_copied = SDL_RenderTexture(renderer, visual->scene_texture,
			NULL, NULL);
	}
	if (!scene_copied) {
		visual->scene_cache_ready = false;
		SDL_SetRenderTarget(renderer, NULL);
		if (scene_targeted) {
			draw_base_scene(visual, renderer, grid, theme, map_view, canvas,
				cache_ready);
		}
	}
	draw_weather(visual, renderer, map_view, frame->weather);
	draw_player_vitality(visual, renderer, map_view);
	draw_combat_effects(visual, renderer, theme, map_view,
		frame->combat_effects);
	if (!frame->screen || !frame->screen->active ||
			frame->screen->kind != UI_SCREEN_STORE) {
		draw_store_art(visual, renderer, theme, frame->store_art);
	}
	draw_world_stats(visual, renderer, theme, grid, map_view,
		frame->hud_stats_visible, fishing_active);
	if (frame->dock_active) {
		draw_message_overlay(visual, renderer, theme, frame->dock_grid,
			frame->dock_placement, map_view && map_view->active ? map_view->status_rows : 0);
	}
	draw_prompt_overlay(visual, renderer, theme, grid, map_view,
		frame->cursor_visible);
	sdl3_fishing_draw(frame->fishing, renderer, visual, theme,
		map_view && map_view->active ? map_view->status_rows : 0);
	draw_cave_status(visual, renderer, theme, map_view);
	draw_foreground_overlays(visual, renderer, theme, map_view,
		frame->inspect_card, frame->cursor_visible, frame->cursor_col,
		frame->cursor_row);
	sdl3_screen_draw(frame->screen, renderer, visual, theme);
	if (frame->screen && frame->screen->active &&
			frame->screen->kind == UI_SCREEN_STORE) {
		/* A semantic store reserves its right-hand columns for the existing
		 * shopkeeper portrait.  Draw that portrait after the black semantic canvas
		 * so the stock list and the art remain one composition. */
		draw_store_art(visual, renderer, theme, frame->store_art);
	}
	sdl3_home_draw(frame->home, renderer, visual, theme);
	sdl3_pause_menu_draw(frame->pause_menu, renderer, visual, theme);
	sdl3_settings_draw(frame->settings, renderer, visual, theme,
		frame->interface_font_name, frame->map_font_name,
		frame->interface_density,
		map_view ? map_view->zoom_percent : SDL3_ZOOM_DEFAULT,
		frame->terrain_style,
		frame->visible_weather ? SDL3_WEATHER_AUTO : SDL3_WEATHER_OFF,
		frame->fullscreen, frame->dock_visible, frame->hud_stats_visible,
		frame->big_stat_cards, frame->animated_combat, frame->tile_mode,
		frame->hybrid_tile_size,
		frame->dock_placement,
		frame->dock_rows, frame->dock_cols, frame->audio);

	present_started = SDL_GetTicksNS();
	SDL_RenderPresent(renderer);
	record_present(visual, present_started);
	sdl3_grid_mark_clean(grid);
	if (frame->dock_grid) sdl3_grid_mark_clean(frame->dock_grid);
}

bool sdl3_visual_render_overlay_frame(struct sdl3_visual *visual,
		SDL_Renderer *renderer, const struct sdl3_overlay_frame *frame)
{
	Uint64 present_started;
	bool weather_active = frame && frame->weather && frame->weather->visible;
	bool combat_active = frame && frame->combat_effects &&
		frame->combat_effects->active;
	bool fishing_active = frame &&
		sdl3_fishing_is_active(frame->fishing);

	if (!visual || !renderer || !frame || !frame->theme ||
			!frame->map_view || !frame->map_view->active ||
			(!weather_active && !combat_active && !fishing_active) ||
			!visual->scene_texture ||
			!visual->scene_cache_ready) {
		return false;
	}
	if (!SDL_SetRenderTarget(renderer, NULL) ||
			!SDL_RenderTexture(renderer, visual->scene_texture, NULL, NULL)) {
		visual->scene_cache_ready = false;
		return false;
	}
	visual->last_cell_updates = 0;
	draw_weather(visual, renderer, frame->map_view, frame->weather);
	draw_player_vitality(visual, renderer, frame->map_view);
	draw_combat_effects(visual, renderer, frame->theme, frame->map_view,
		frame->combat_effects);
	draw_world_stats(visual, renderer, frame->theme, frame->grid,
		frame->map_view, frame->hud_stats_visible, fishing_active);
	if (frame->dock_active) {
		draw_message_overlay(visual, renderer, frame->theme, frame->dock_grid,
			frame->dock_placement, frame->map_view->status_rows);
	}
	draw_prompt_overlay(visual, renderer, frame->theme, frame->grid,
		frame->map_view, frame->cursor_visible);
	sdl3_fishing_draw(frame->fishing, renderer, visual, frame->theme,
		frame->map_view->status_rows);
	draw_cave_status(visual, renderer, frame->theme, frame->map_view);
	draw_foreground_overlays(visual, renderer, frame->theme, frame->map_view,
		frame->inspect_card, frame->cursor_visible, frame->cursor_col,
		frame->cursor_row);
	present_started = SDL_GetTicksNS();
	SDL_RenderPresent(renderer);
	record_present(visual, present_started);
	return true;
}

bool sdl3_visual_point_to_cell(const struct sdl3_visual *visual, float x,
		float y, const struct sdl3_map_view *map_view, int *col, int *row)
{
	int candidate_col;
	int candidate_row;

	if (!visual || !col || !row || visual->cell_width <= 0 ||
			visual->cell_height <= 0 || x < visual->origin_x ||
			y < visual->origin_y) {
		return false;
	}
	if (map_view_uses_camera(visual, map_view)) {
		struct sdl3_map_camera camera = map_camera(visual, map_view);

		if (x >= camera.viewport.x &&
				x < camera.viewport.x + camera.viewport.w &&
				y >= camera.viewport.y &&
				y < camera.viewport.y + camera.viewport.h) {
			candidate_col = sdl3_zoom_cell_from_pixel(x, camera.viewport.x,
				camera.viewport.w, camera.cell_width, camera.center_col);
			candidate_row = sdl3_zoom_cell_from_pixel(y, camera.viewport.y,
				camera.viewport.h, camera.cell_height, camera.center_row);
			if (candidate_col < map_view->source_col ||
					candidate_col >=
						map_view->source_col + map_view->source_cols ||
					candidate_row < map_view->source_row ||
					candidate_row >=
						map_view->source_row + map_view->source_rows) {
				return false;
			}
			if (map_view->expanded_source) {
				int dungeon_col = map_view->dungeon_col +
					candidate_col - map_view->source_col;
				int dungeon_row = map_view->dungeon_row +
					candidate_row - map_view->source_row;

				candidate_col = map_view->term_col +
					(dungeon_col - map_view->term_offset_col) *
					map_view->tile_width;
				candidate_row = map_view->term_row +
					(dungeon_row - map_view->term_offset_row) *
					map_view->tile_height;
				/* Angband's mouse event still addresses its authoritative
				 * terminal panel.  Ignore expanded cells outside that panel
				 * rather than sending a command for the wrong dungeon grid. */
				if (candidate_col < map_view->term_col ||
						candidate_col >=
						map_view->term_col + map_view->term_cols ||
						candidate_row < map_view->term_row ||
						candidate_row >=
						map_view->term_row + map_view->term_rows) {
					return false;
				}
			}
			*col = candidate_col;
			*row = candidate_row;
			return true;
		}
	}
	candidate_col = (int)(x - visual->origin_x) / visual->cell_width;
	candidate_row = (int)(y - visual->origin_y) / visual->cell_height;
	if (candidate_col < 0 || candidate_col >= visual->cols ||
			candidate_row < 0 || candidate_row >= visual->rows) {
		return false;
	}
	*col = candidate_col;
	*row = candidate_row;
	return true;
}
