/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/presenter.h
 * \brief Presentation and compositor ownership for the SDL3 frontend.
 *
 * The frontend coordinator supplies immutable frame inputs through this
 * interface.  Fonts, glyph caches, textures, and compositor statistics remain
 * private to the presenter.
 */

#ifndef INCLUDED_SDL3_PRESENTER_H
#define INCLUDED_SDL3_PRESENTER_H

#include "sdl3/font.h"
#include "sdl3/layout.h"
#include "sdl3/map-view.h"
#include "sdl3/store-art.h"
#include "sdl3/terrain.h"
#include "sdl3/term.h"
#include "sdl3/weather.h"

struct sdl3_settings_overlay;
struct sdl3_home_screen;
struct sdl3_pause_menu;
struct sdl3_inspect_card;
struct sdl3_screen;
struct sdl3_theme;
struct sdl3_audio_settings;
struct player_options;
struct sdl3_combat_effects;
struct world_fishing_runtime;

/** Everything required for one complete presentation pass. */
struct sdl3_presentation_frame {
	struct sdl3_grid *grid;
	struct sdl3_grid *dock_grid;
	const struct sdl3_theme *theme;
	const struct sdl3_home_screen *home;
	const struct sdl3_pause_menu *pause_menu;
	const struct sdl3_settings_overlay *settings;
	const struct player_options *game_options;
	bool playing;
	const struct sdl3_screen *screen;
	const struct sdl3_screen *screen_parent;
	const char *interface_font_name;
	const char *map_font_name;
	enum sdl3_interface_density interface_density;
	const struct sdl3_map_view *map_view;
	const struct sdl3_weather_state *weather;
	const struct sdl3_combat_effects *combat_effects;
	const struct world_fishing_runtime *fishing;
	const struct sdl3_store_art *store_art;
	const struct sdl3_inspect_card *inspect_card;
	bool fullscreen;
	bool cursor_visible;
	int cursor_col;
	int cursor_row;
	bool dock_active;
	bool dock_visible;
	bool hud_stats_visible;
	bool big_stat_cards;
	bool animated_combat;
	bool edge_warnings;
	bool tile_mode;
	int hybrid_tile_size;
	enum sdl3_dock_placement dock_placement;
	int dock_rows;
	int dock_cols;
	const struct sdl3_audio_settings *audio;
	enum sdl3_terrain_style terrain_style;
	bool visible_weather;
};

struct sdl3_overlay_frame {
	const struct sdl3_theme *theme;
	const struct sdl3_grid *grid;
	const struct sdl3_grid *dock_grid;
	const struct sdl3_map_view *map_view;
	const struct sdl3_weather_state *weather;
	const struct sdl3_combat_effects *combat_effects;
	const struct world_fishing_runtime *fishing;
	const struct sdl3_inspect_card *inspect_card;
	bool cursor_visible;
	int cursor_col;
	int cursor_row;
	bool dock_active;
	bool hud_stats_visible;
	enum sdl3_dock_placement dock_placement;
};

/** A read-only snapshot for diagnostics and persisted font selection. */
struct sdl3_presenter_info {
	const char *interface_font_path;
	const char *map_font_path;
	bool interface_font_loaded;
	bool map_font_loaded;
	bool grid_cache_active;
	int interface_font_point_size;
	int map_font_point_size;
	int output_width;
	int output_height;
	int origin_x;
	int origin_y;
	int cell_width;
	int cell_height;
	int last_cell_updates;
	Uint64 frames_presented;
	Uint64 cell_updates_total;
	Uint64 present_wait_ns_total;
	Uint64 present_wait_ns_max;
};

struct sdl3_presenter;

struct sdl3_presenter *sdl3_presenter_create(SDL_Renderer *renderer,
		const char *font_path, const char *map_font_path, int cols, int rows,
		int map_zoom_percent);
void sdl3_presenter_destroy(struct sdl3_presenter *presenter);
void sdl3_presenter_get_info(const struct sdl3_presenter *presenter,
		struct sdl3_presenter_info *info);
bool sdl3_presenter_resize(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer);
bool sdl3_presenter_set_grid_size(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, int cols, int rows);
bool sdl3_presenter_set_font(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const char *font_path);
bool sdl3_presenter_set_map_font(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const char *font_path);
bool sdl3_presenter_set_map_zoom(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, int zoom_percent);
void sdl3_presenter_invalidate_grid_cache(struct sdl3_presenter *presenter);
bool sdl3_presenter_recreate_renderer_resources(
		struct sdl3_presenter *presenter, SDL_Renderer *renderer);
void sdl3_presenter_render(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const struct sdl3_presentation_frame *frame);
bool sdl3_presenter_render_overlay(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, const struct sdl3_overlay_frame *frame);
bool sdl3_presenter_point_to_cell(const struct sdl3_presenter *presenter,
		float x, float y, const struct sdl3_map_view *map_view, int *col,
		int *row);
bool sdl3_presenter_prime_map_glyphs(struct sdl3_presenter *presenter);

#endif /* INCLUDED_SDL3_PRESENTER_H */
