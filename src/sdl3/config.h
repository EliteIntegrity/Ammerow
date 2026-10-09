/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/config.h
 * \brief Persistent settings for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_CONFIG_H
#define INCLUDED_SDL3_CONFIG_H

#include "angband.h"

#include "sdl3/audio.h"
#include "sdl3/layout.h"
#include "sdl3/terrain.h"
#include "sdl3/weather.h"

#define SDL3_CONFIG_VERSION 22
#define SDL3_CONFIG_FILE "ammerow.ini"
#define SDL3_CONFIG_LEGACY_FILE "sdl3init.txt"
#define SDL3_CONFIG_PATH_CAPACITY 1024
#define SDL3_CONFIG_FONT_CAPACITY 1024
#define SDL3_CONFIG_THEME_CAPACITY 32
#define SDL3_CONFIG_SAVE_CAPACITY 256
#define SDL3_DEFAULT_WINDOW_WIDTH 960
#define SDL3_DEFAULT_WINDOW_HEIGHT 720
#define SDL3_MIN_WINDOW_WIDTH 320
#define SDL3_MIN_WINDOW_HEIGHT 240
#define SDL3_MAX_WINDOW_SIZE 16384

struct sdl3_config {
	char file_path[SDL3_CONFIG_PATH_CAPACITY];
	char font[SDL3_CONFIG_FONT_CAPACITY];
	char map_font[SDL3_CONFIG_FONT_CAPACITY];
	char theme[SDL3_CONFIG_THEME_CAPACITY];
	char last_save[SDL3_CONFIG_SAVE_CAPACITY];
	int window_width;
	int window_height;
	int map_zoom_percent;
	enum sdl3_interface_density interface_density;
	enum sdl3_terrain_style terrain_style;
	enum sdl3_weather_mode weather_mode;
	int dock_rows;
	int dock_cols;
	enum sdl3_dock_placement dock_placement;
	struct sdl3_audio_settings audio;
	bool fullscreen;
	bool dock_visible;
	bool hud_stats_visible;
	bool big_stat_cards;
	bool animated_combat;
	bool tile_mode;
	int hybrid_tile_size;
	bool writable;
};

void sdl3_config_init(struct sdl3_config *config);
bool sdl3_config_load(struct sdl3_config *config);
bool sdl3_config_save(const struct sdl3_config *config);

#endif /* INCLUDED_SDL3_CONFIG_H */
