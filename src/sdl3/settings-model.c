/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/settings-model.c
 * \brief Platform-independent navigation state for SDL3 settings screens.
 */

#include "sdl3/settings-model.h"

#include <string.h>

static const enum sdl3_settings_row appearance_rows[] = {
	SDL3_SETTINGS_THEME,
	SDL3_SETTINGS_MAP_FONT,
	SDL3_SETTINGS_TILE_MODE,
	SDL3_SETTINGS_HYBRID_DETAIL,
	SDL3_SETTINGS_TERRAIN
};

static const enum sdl3_settings_row interface_rows[] = {
	SDL3_SETTINGS_INTERFACE_FONT,
	SDL3_SETTINGS_INTERFACE_DENSITY,
	SDL3_SETTINGS_BIG_STAT_CARDS,
	SDL3_SETTINGS_DOCK_VISIBLE,
	SDL3_SETTINGS_DOCK_PLACEMENT,
	SDL3_SETTINGS_DOCK_SIZE,
	SDL3_SETTINGS_HUD_STATS
};

static const enum sdl3_settings_row sound_rows[] = {
	SDL3_SETTINGS_AUDIO_ENABLED,
	SDL3_SETTINGS_AUDIO_MUSIC_ENABLED,
	SDL3_SETTINGS_AUDIO_MOVEMENT,
	SDL3_SETTINGS_AUDIO_MASTER,
	SDL3_SETTINGS_AUDIO_MUSIC,
	SDL3_SETTINGS_AUDIO_INTERFACE,
	SDL3_SETTINGS_AUDIO_GAMEPLAY,
	SDL3_SETTINGS_AUDIO_CREATURE,
	SDL3_SETTINGS_AUDIO_AMBIENT
};

static const enum sdl3_settings_row display_rows[] = {
	SDL3_SETTINGS_FULLSCREEN,
	SDL3_SETTINGS_ZOOM,
	SDL3_SETTINGS_ANIMATED_COMBAT,
	SDL3_SETTINGS_WEATHER
};

static const enum sdl3_settings_row *page_rows(
		enum sdl3_settings_page page, int *count)
{
	switch (page) {
	case SDL3_SETTINGS_APPEARANCE:
		*count = (int)(sizeof(appearance_rows) / sizeof(appearance_rows[0]));
		return appearance_rows;
	case SDL3_SETTINGS_INTERFACE:
		*count = (int)(sizeof(interface_rows) / sizeof(interface_rows[0]));
		return interface_rows;
	case SDL3_SETTINGS_SOUND:
		*count = (int)(sizeof(sound_rows) / sizeof(sound_rows[0]));
		return sound_rows;
	case SDL3_SETTINGS_DISPLAY:
		*count = (int)(sizeof(display_rows) / sizeof(display_rows[0]));
		return display_rows;
	default:
		*count = 0;
		return NULL;
	}
}

int sdl3_settings_page_row_count(enum sdl3_settings_page page)
{
	int count;

	(void)page_rows(page, &count);
	return count;
}

bool sdl3_settings_page_row_at(enum sdl3_settings_page page, int index,
		enum sdl3_settings_row *row)
{
	const enum sdl3_settings_row *rows;
	int count;

	if (!row) return false;
	rows = page_rows(page, &count);
	if (!rows || index < 0 || index >= count) return false;
	*row = rows[index];
	return true;
}

static void select_first_page_row(struct sdl3_settings_overlay *overlay)
{
	const enum sdl3_settings_row *rows;
	int count;

	rows = page_rows(overlay->page, &count);
	if (rows && count > 0) overlay->selected_row = rows[0];
}

void sdl3_settings_init(struct sdl3_settings_overlay *overlay)
{
	if (!overlay) return;
	memset(overlay, 0, sizeof(*overlay));
	overlay->page = SDL3_SETTINGS_HUB;
	overlay->selected_row = SDL3_SETTINGS_THEME;
}

void sdl3_settings_open(struct sdl3_settings_overlay *overlay)
{
	if (!overlay) return;
	overlay->visible = true;
	overlay->page = SDL3_SETTINGS_HUB;
}

void sdl3_settings_toggle(struct sdl3_settings_overlay *overlay)
{
	if (!overlay) return;
	if (overlay->visible) {
		overlay->visible = false;
	} else {
		sdl3_settings_open(overlay);
	}
}

void sdl3_settings_move(struct sdl3_settings_overlay *overlay, int delta)
{
	const enum sdl3_settings_row *rows;
	int count;
	int index;

	if (!overlay || !delta) return;
	if (overlay->page == SDL3_SETTINGS_HUB) {
		overlay->selected_hub_row += delta < 0 ? -1 : 1;
		if (overlay->selected_hub_row < 0) {
			overlay->selected_hub_row = SDL3_SETTINGS_HUB_ROW_COUNT - 1;
		}
		if (overlay->selected_hub_row >= SDL3_SETTINGS_HUB_ROW_COUNT) {
			overlay->selected_hub_row = 0;
		}
		return;
	}
	rows = page_rows(overlay->page, &count);
	if (!rows || count <= 0) return;
	for (index = 0; index < count; index++) {
		if (rows[index] == overlay->selected_row) break;
	}
	if (index >= count) index = 0;
	index += delta < 0 ? -1 : 1;
	if (index < 0) index = count - 1;
	if (index >= count) index = 0;
	overlay->selected_row = rows[index];
}

bool sdl3_settings_activate(struct sdl3_settings_overlay *overlay)
{
	if (!overlay || overlay->page != SDL3_SETTINGS_HUB) return false;
	overlay->page = (enum sdl3_settings_page)
		(SDL3_SETTINGS_APPEARANCE + overlay->selected_hub_row);
	select_first_page_row(overlay);
	return true;
}

bool sdl3_settings_back(struct sdl3_settings_overlay *overlay)
{
	if (!overlay || overlay->page == SDL3_SETTINGS_HUB) return false;
	overlay->selected_hub_row = overlay->page - SDL3_SETTINGS_APPEARANCE;
	overlay->page = SDL3_SETTINGS_HUB;
	return true;
}
