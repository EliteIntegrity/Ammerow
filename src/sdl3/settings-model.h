/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/settings-model.h
 * \brief Platform-independent navigation state for SDL3 settings screens.
 */

#ifndef INCLUDED_SDL3_SETTINGS_MODEL_H
#define INCLUDED_SDL3_SETTINGS_MODEL_H

#include <stdbool.h>

enum sdl3_settings_row {
	SDL3_SETTINGS_THEME = 0,
	SDL3_SETTINGS_INTERFACE_FONT,
	SDL3_SETTINGS_INTERFACE_DENSITY,
	SDL3_SETTINGS_BIG_STAT_CARDS,
	SDL3_SETTINGS_MAP_FONT,
	SDL3_SETTINGS_TILE_MODE,
	SDL3_SETTINGS_HYBRID_DETAIL,
	SDL3_SETTINGS_ZOOM,
	SDL3_SETTINGS_ANIMATED_COMBAT,
	SDL3_SETTINGS_TERRAIN,
	SDL3_SETTINGS_WEATHER,
	SDL3_SETTINGS_FULLSCREEN,
	SDL3_SETTINGS_DOCK_VISIBLE,
	SDL3_SETTINGS_DOCK_PLACEMENT,
	SDL3_SETTINGS_DOCK_SIZE,
	SDL3_SETTINGS_HUD_STATS,
	SDL3_SETTINGS_AUDIO_ENABLED,
	SDL3_SETTINGS_AUDIO_MUSIC_ENABLED,
	SDL3_SETTINGS_AUDIO_MOVEMENT,
	SDL3_SETTINGS_AUDIO_MASTER,
	SDL3_SETTINGS_AUDIO_MUSIC,
	SDL3_SETTINGS_AUDIO_INTERFACE,
	SDL3_SETTINGS_AUDIO_GAMEPLAY,
	SDL3_SETTINGS_AUDIO_CREATURE,
	SDL3_SETTINGS_AUDIO_AMBIENT,
	SDL3_SETTINGS_ROW_COUNT
};

enum sdl3_settings_page {
	SDL3_SETTINGS_HUB = 0,
	SDL3_SETTINGS_APPEARANCE,
	SDL3_SETTINGS_INTERFACE,
	SDL3_SETTINGS_SOUND,
	SDL3_SETTINGS_DISPLAY,
	SDL3_SETTINGS_PAGE_COUNT
};

enum sdl3_settings_hub_row {
	SDL3_SETTINGS_HUB_APPEARANCE = 0,
	SDL3_SETTINGS_HUB_INTERFACE,
	SDL3_SETTINGS_HUB_SOUND,
	SDL3_SETTINGS_HUB_DISPLAY,
	SDL3_SETTINGS_HUB_ROW_COUNT
};

struct sdl3_settings_overlay {
	bool visible;
	enum sdl3_settings_page page;
	int selected_hub_row;
	int selected_row;
};

void sdl3_settings_init(struct sdl3_settings_overlay *overlay);
void sdl3_settings_open(struct sdl3_settings_overlay *overlay);
void sdl3_settings_toggle(struct sdl3_settings_overlay *overlay);
void sdl3_settings_move(struct sdl3_settings_overlay *overlay, int delta);
bool sdl3_settings_activate(struct sdl3_settings_overlay *overlay);
bool sdl3_settings_back(struct sdl3_settings_overlay *overlay);
int sdl3_settings_page_row_count(enum sdl3_settings_page page);
bool sdl3_settings_page_row_at(enum sdl3_settings_page page, int index,
		enum sdl3_settings_row *row);

#endif /* INCLUDED_SDL3_SETTINGS_MODEL_H */
