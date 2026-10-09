/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/config.c
 * \brief Persistent settings for the SDL3 frontend.
 *
 *
 */

#include "sdl3/config.h"

#include "init.h"
#include "sdl3/layout.h"
#include "sdl3/theme.h"
#include "sdl3/zoom.h"

#include <SDL3/SDL.h>

#include <errno.h>

static void set_defaults(struct sdl3_config *config)
{
	config->font[0] = '\0';
	config->map_font[0] = '\0';
	config->last_save[0] = '\0';
	my_strcpy(config->theme, SDL3_DEFAULT_THEME, sizeof(config->theme));
	config->window_width = SDL3_DEFAULT_WINDOW_WIDTH;
	config->window_height = SDL3_DEFAULT_WINDOW_HEIGHT;
	config->map_zoom_percent = SDL3_ZOOM_DEFAULT;
	config->interface_density = SDL3_INTERFACE_DENSITY_DEFAULT;
	config->fullscreen = true;
	config->dock_visible = true;
	config->hud_stats_visible = true;
	config->big_stat_cards = true;
	config->animated_combat = true;
	config->tile_mode = false;
	config->hybrid_tile_size = 64;
	config->terrain_style = SDL3_TERRAIN_NATURAL;
	config->weather_mode = SDL3_WEATHER_AUTO;
	config->dock_rows = SDL3_LAYOUT_DEFAULT_DOCK_ROWS;
	config->dock_cols = SDL3_LAYOUT_DEFAULT_DOCK_COLS;
	config->dock_placement = SDL3_DOCK_BOTTOM;
	sdl3_audio_settings_defaults(&config->audio);
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

static bool parse_integer(const char *text, int minimum, int maximum,
		int *result)
{
	char *end;
	long parsed;

	errno = 0;
	parsed = strtol(text, &end, 10);
	while (*end && isspace((unsigned char)*end)) end++;
	if (errno || end == text || *end || parsed < minimum || parsed > maximum) {
		return false;
	}
	*result = (int)parsed;
	return true;
}

void sdl3_config_init(struct sdl3_config *config)
{
	if (!config) return;
	memset(config, 0, sizeof(*config));
	set_defaults(config);
	path_build(config->file_path, sizeof(config->file_path), ANGBAND_DIR_USER,
		SDL3_CONFIG_FILE);
	config->writable = dir_create(ANGBAND_DIR_USER);
	if (!config->writable) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not create the SDL3 settings directory: %s",
			ANGBAND_DIR_USER);
	}
}

bool sdl3_config_load(struct sdl3_config *config)
{
	struct sdl3_config candidate;
	ang_file *file;
	char legacy_path[SDL3_CONFIG_PATH_CAPACITY];
	char line[2048];
	int line_number = 0;
	int version = 0;
	bool saw_version = false;

	if (!config || !config->file_path[0]) return false;
	file = file_open(config->file_path, MODE_READ, FTYPE_TEXT);
	if (!file) {
		/* One-way, non-destructive migration: read the old frontend file when
		 * the Ammerow-owned file does not exist.  The next normal save writes
		 * ammerow.ini and leaves the old file available for older builds. */
		if (!streq(config->file_path + path_filename_index(config->file_path),
				SDL3_CONFIG_FILE)) {
			return false;
		}
		path_build(legacy_path, sizeof(legacy_path), ANGBAND_DIR_USER,
			SDL3_CONFIG_LEGACY_FILE);
		file = file_open(legacy_path, MODE_READ, FTYPE_TEXT);
		if (!file) return false;
	}

	candidate = *config;
	set_defaults(&candidate);
	while (file_getl(file, line, sizeof(line))) {
		char *key;
		char *value;
		char *separator;
		int parsed;

		line_number++;
		key = trim(line);
		if (!key[0] || key[0] == '#') continue;
		separator = strchr(key, '=');
		if (!separator) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Ignoring malformed SDL3 setting on line %d", line_number);
			continue;
		}
		*separator = '\0';
		value = trim(separator + 1);
		key = trim(key);

		if (streq(key, "version")) {
			if (parse_integer(value, 1, SDL3_CONFIG_VERSION, &parsed)) {
				version = parsed;
				saw_version = true;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 settings version on line %d",
					line_number);
			}
		} else if (streq(key, "window_width")) {
			if (parse_integer(value, SDL3_MIN_WINDOW_WIDTH,
					SDL3_MAX_WINDOW_SIZE, &parsed)) {
				candidate.window_width = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 window width on line %d",
					line_number);
			}
		} else if (streq(key, "window_height")) {
			if (parse_integer(value, SDL3_MIN_WINDOW_HEIGHT,
					SDL3_MAX_WINDOW_SIZE, &parsed)) {
				candidate.window_height = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 window height on line %d",
					line_number);
			}
		} else if (streq(key, "fullscreen")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.fullscreen = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 fullscreen value on line %d",
					line_number);
			}
		} else if (streq(key, "map_zoom_percent")) {
			if (parse_integer(value, SDL3_ZOOM_MIN, SDL3_ZOOM_MAX,
					&parsed)) {
				candidate.map_zoom_percent = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 map zoom on line %d", line_number);
			}
		} else if (streq(key, "interface_density")) {
			if (parse_integer(value, SDL3_INTERFACE_LARGE,
					SDL3_INTERFACE_DENSITY_COUNT - 1, &parsed)) {
				candidate.interface_density =
					(enum sdl3_interface_density)parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 interface density on line %d",
					line_number);
			}
		} else if (streq(key, "zoom_percent")) {
			/* Version 4 scaled the complete terminal from 50 to 100 percent.
			 * Version 5 keeps interface text fixed and starts map zoom at 100. */
			if (!parse_integer(value, 50, 100, &parsed)) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid legacy SDL3 zoom on line %d",
					line_number);
			}
		} else if (streq(key, "dock_visible")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.dock_visible = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 dock visibility on line %d",
					line_number);
			}
		} else if (streq(key, "hud_stats_visible")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.hud_stats_visible = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 stats visibility on line %d",
					line_number);
			}
		} else if (streq(key, "big_stat_cards")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.big_stat_cards = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 stat-card size on line %d",
					line_number);
			}
		} else if (streq(key, "animated_combat")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.animated_combat = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 combat animation setting on line %d",
					line_number);
			}
		} else if (streq(key, "tile_mode")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.tile_mode = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 tile-mode setting on line %d",
					line_number);
			}
		} else if (streq(key, "hybrid_tile_size")) {
			if (parse_integer(value, 32, 64, &parsed) &&
					(parsed == 32 || parsed == 64)) {
				candidate.hybrid_tile_size = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid hybrid detail on line %d", line_number);
			}
		} else if (streq(key, "terrain_decoration")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				/* Versions 9 through 11 stored a boolean. */
				candidate.terrain_style = parsed ? SDL3_TERRAIN_NATURAL :
					SDL3_TERRAIN_CLASSIC;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 terrain decoration on line %d",
					line_number);
			}
		} else if (streq(key, "terrain_style")) {
			if (parse_integer(value, SDL3_TERRAIN_CLASSIC, 2, &parsed)) {
				/* Version 12 briefly exposed stored value 2 as Textured.  It was
				 * withdrawn; preserve those users' semantic colours as Natural. */
				candidate.terrain_style = parsed == SDL3_TERRAIN_CLASSIC ?
					SDL3_TERRAIN_CLASSIC : SDL3_TERRAIN_NATURAL;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 terrain style on line %d",
					line_number);
			}
		} else if (streq(key, "weather_mode")) {
			if (parse_integer(value, SDL3_WEATHER_OFF,
					SDL3_WEATHER_MODE_COUNT - 1, &parsed)) {
				/* Retain Off, but retire the old forced rain/snow preferences. */
				candidate.weather_mode = parsed == SDL3_WEATHER_OFF ?
					SDL3_WEATHER_OFF : SDL3_WEATHER_AUTO;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 weather mode on line %d",
					line_number);
			}
		} else if (streq(key, "dock_rows")) {
			if (parse_integer(value, SDL3_LAYOUT_MIN_DOCK_ROWS,
					SDL3_LAYOUT_MAX_DOCK_ROWS, &parsed)) {
				candidate.dock_rows = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 dock height on line %d",
					line_number);
			}
		} else if (streq(key, "dock_cols")) {
			if (parse_integer(value, SDL3_LAYOUT_MIN_DOCK_COLS,
					SDL3_LAYOUT_MAX_DOCK_COLS, &parsed)) {
				candidate.dock_cols = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 dock width on line %d",
					line_number);
			}
		} else if (streq(key, "dock_placement")) {
			if (parse_integer(value, SDL3_DOCK_BOTTOM,
					SDL3_DOCK_PLACEMENT_COUNT - 1, &parsed)) {
				candidate.dock_placement =
					(enum sdl3_dock_placement)parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 dock placement on line %d",
					line_number);
			}
		} else if (streq(key, "audio_enabled")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.audio.enabled = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 audio setting on line %d",
					line_number);
			}
		} else if (streq(key, "audio_movement")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.audio.movement_enabled = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 movement audio setting on line %d",
					line_number);
			}
		} else if (streq(key, "audio_music_enabled")) {
			if (parse_integer(value, 0, 1, &parsed)) {
				candidate.audio.music_enabled = parsed != 0;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 music setting on line %d",
					line_number);
			}
		} else if (streq(key, "audio_master")) {
			if (parse_integer(value, SDL3_AUDIO_VOLUME_MIN,
					SDL3_AUDIO_VOLUME_MAX, &parsed)) {
				candidate.audio.master_volume = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 master volume on line %d",
					line_number);
			}
		} else if (streq(key, "audio_interface")) {
			if (parse_integer(value, SDL3_AUDIO_VOLUME_MIN,
					SDL3_AUDIO_VOLUME_MAX, &parsed)) {
				candidate.audio.interface_volume = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 interface volume on line %d",
					line_number);
			}
		} else if (streq(key, "audio_gameplay")) {
			if (parse_integer(value, SDL3_AUDIO_VOLUME_MIN,
					SDL3_AUDIO_VOLUME_MAX, &parsed)) {
				candidate.audio.gameplay_volume = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 gameplay volume on line %d",
					line_number);
			}
		} else if (streq(key, "audio_creature")) {
			if (parse_integer(value, SDL3_AUDIO_VOLUME_MIN,
					SDL3_AUDIO_VOLUME_MAX, &parsed)) {
				candidate.audio.creature_volume = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 creature volume on line %d",
					line_number);
			}
		} else if (streq(key, "audio_ambient")) {
			if (parse_integer(value, SDL3_AUDIO_VOLUME_MIN,
					SDL3_AUDIO_VOLUME_MAX, &parsed)) {
				candidate.audio.ambient_volume = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 ambient volume on line %d",
					line_number);
			}
		} else if (streq(key, "audio_shop_ambient_percent")) {
			if (parse_integer(value, SDL3_AUDIO_VOLUME_MIN,
					SDL3_AUDIO_VOLUME_MAX, &parsed)) {
				candidate.audio.shop_ambient_percent = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 shop ambience on line %d",
					line_number);
			}
		} else if (streq(key, "audio_music")) {
			if (parse_integer(value, SDL3_AUDIO_VOLUME_MIN,
					SDL3_AUDIO_VOLUME_MAX, &parsed)) {
				candidate.audio.music_volume = parsed;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 music volume on line %d",
					line_number);
			}
		} else if (streq(key, "font")) {
			my_strcpy(candidate.font, value, sizeof(candidate.font));
		} else if (streq(key, "map_font")) {
			my_strcpy(candidate.map_font, value,
				sizeof(candidate.map_font));
		} else if (streq(key, "theme")) {
			my_strcpy(candidate.theme, value, sizeof(candidate.theme));
		} else if (streq(key, "last_save")) {
			my_strcpy(candidate.last_save, value,
				sizeof(candidate.last_save));
		}
	}
	file_close(file);

	if (!saw_version || version < 1 || version > SDL3_CONFIG_VERSION) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Ignoring unsupported SDL3 settings file: %s",
			config->file_path);
		set_defaults(config);
		return false;
	}
	/* Version 9 split the interface and map faces.  Preserve the exact
	 * presentation selected by earlier versions until the player chooses a
	 * different face for either role. */
	if (version < 9) {
		my_strcpy(candidate.map_font, candidate.font,
			sizeof(candidate.map_font));
	}
	/* Version 6 introduced the bundled ambience at an audible default.  The
	 * initial files are tonally wrong for Angband, so migrate that private
	 * development setting to silence once while retaining the channel. */
	if (version < 7) candidate.audio.ambient_volume = 0;
	/* Version 14 briefly used placement one for an opaque left subwindow.
	 * Version 15 restores the intended independent transparent player HUD and
	 * keeps the single message history at the bottom. */
	if (candidate.dock_placement == SDL3_DOCK_LEFT) {
		candidate.hud_stats_visible = candidate.dock_visible;
		candidate.dock_placement = SDL3_DOCK_BOTTOM;
	}
	*config = candidate;
	return true;
}

static bool replace_config_file(const char *path, const char *temporary,
		const char *backup)
{
	bool had_original = file_exists(path);

	if (had_original) {
		if (file_exists(backup)) file_delete(backup);
		if (!file_move(path, backup)) return false;
	}
	if (!file_move(temporary, path)) {
		if (had_original) file_move(backup, path);
		return false;
	}
	if (had_original && file_exists(backup)) file_delete(backup);
	return true;
}

bool sdl3_config_save(const struct sdl3_config *config)
{
	char temporary[SDL3_CONFIG_PATH_CAPACITY + 8];
	char backup[SDL3_CONFIG_PATH_CAPACITY + 8];
	ang_file *file;
	bool wrote;

	if (!config || !config->writable || !config->file_path[0]) return false;
	strnfmt(temporary, sizeof(temporary), "%s.new", config->file_path);
	strnfmt(backup, sizeof(backup), "%s.old", config->file_path);
	if (file_exists(temporary)) file_delete(temporary);
	file = file_open(temporary, MODE_WRITE, FTYPE_TEXT);
	if (!file) return false;

	wrote = file_putf(file, "# Ammerow: Lands Beyond SDL3 settings\n") &&
		file_putf(file, "version=%d\n", SDL3_CONFIG_VERSION) &&
		file_putf(file, "window_width=%d\n", config->window_width) &&
		file_putf(file, "window_height=%d\n", config->window_height) &&
		file_putf(file, "fullscreen=%d\n", config->fullscreen ? 1 : 0) &&
		file_putf(file, "map_zoom_percent=%d\n",
			config->map_zoom_percent) &&
		file_putf(file, "interface_density=%d\n",
			(int)config->interface_density) &&
		file_putf(file, "dock_visible=%d\n",
			config->dock_visible ? 1 : 0) &&
		file_putf(file, "hud_stats_visible=%d\n",
			config->hud_stats_visible ? 1 : 0) &&
		file_putf(file, "big_stat_cards=%d\n",
			config->big_stat_cards ? 1 : 0) &&
		file_putf(file, "animated_combat=%d\n",
			config->animated_combat ? 1 : 0) &&
		file_putf(file, "tile_mode=%d\n", config->tile_mode ? 1 : 0) &&
		file_putf(file, "hybrid_tile_size=%d\n", config->hybrid_tile_size) &&
		file_putf(file, "terrain_style=%d\n",
			(int)config->terrain_style) &&
		file_putf(file, "weather_mode=%d\n",
			(int)config->weather_mode) &&
		file_putf(file, "dock_rows=%d\n", config->dock_rows) &&
		file_putf(file, "dock_cols=%d\n", config->dock_cols) &&
		file_putf(file, "dock_placement=%d\n",
			(int)config->dock_placement) &&
		file_putf(file, "audio_enabled=%d\n",
			config->audio.enabled ? 1 : 0) &&
		file_putf(file, "audio_movement=%d\n",
			config->audio.movement_enabled ? 1 : 0) &&
		file_putf(file, "audio_music_enabled=%d\n",
			config->audio.music_enabled ? 1 : 0) &&
		file_putf(file, "audio_master=%d\n",
			config->audio.master_volume) &&
		file_putf(file, "audio_interface=%d\n",
			config->audio.interface_volume) &&
		file_putf(file, "audio_gameplay=%d\n",
			config->audio.gameplay_volume) &&
		file_putf(file, "audio_creature=%d\n",
			config->audio.creature_volume) &&
		file_putf(file, "audio_ambient=%d\n",
			config->audio.ambient_volume) &&
		file_putf(file, "audio_shop_ambient_percent=%d\n",
			config->audio.shop_ambient_percent) &&
		file_putf(file, "audio_music=%d\n",
			config->audio.music_volume) &&
		file_putf(file, "font=%s\n", config->font) &&
		file_putf(file, "map_font=%s\n", config->map_font) &&
		file_putf(file, "theme=%s\n", config->theme) &&
		file_putf(file, "last_save=%s\n", config->last_save);
	wrote = file_close(file) && wrote;
	if (!wrote || !replace_config_file(config->file_path, temporary, backup)) {
		if (file_exists(temporary)) file_delete(temporary);
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not save SDL3 settings: %s", config->file_path);
		return false;
	}
	return true;
}
