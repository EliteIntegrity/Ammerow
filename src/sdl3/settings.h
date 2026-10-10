/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/settings.h
 * \brief In-game presentation settings overlay for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_SETTINGS_H
#define INCLUDED_SDL3_SETTINGS_H

#include <SDL3/SDL.h>

#include "sdl3/audio.h"
#include "sdl3/layout.h"
#include "sdl3/settings-model.h"
#include "sdl3/terrain.h"
#include "sdl3/weather.h"

struct sdl3_theme;
struct sdl3_visual;

void sdl3_settings_draw(const struct sdl3_settings_overlay *overlay,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, const char *interface_font_name,
		const char *map_font_name,
		enum sdl3_interface_density interface_density,
		int zoom_percent, enum sdl3_terrain_style terrain_style,
		enum sdl3_weather_mode weather_mode, bool fullscreen,
		bool dock_visible, bool hud_stats_visible, bool big_stat_cards,
		bool animated_combat, bool tile_mode, int hybrid_tile_size,
		enum sdl3_dock_placement dock_placement, int dock_rows, int dock_cols,
		const struct sdl3_audio_settings *audio);

#endif /* INCLUDED_SDL3_SETTINGS_H */
