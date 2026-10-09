/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/theme.h
 * \brief Presentation-only colour themes for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_THEME_H
#define INCLUDED_SDL3_THEME_H

#include "angband.h"

#include <SDL3/SDL.h>

#define SDL3_DEFAULT_THEME "midnight"
#define SDL3_THEME_ID_CAPACITY 32

struct sdl3_theme {
	const char *id;
	const char *name;
	const SDL_Color *palette;
	SDL_Color canvas;
	SDL_Color panel;
	SDL_Color panel_border;
	SDL_Color title;
	SDL_Color text;
	SDL_Color muted;
	SDL_Color accent;
	SDL_Color selection;
	uint8_t remembered_world_brightness;
};

int sdl3_theme_count(void);
const struct sdl3_theme *sdl3_theme_by_index(int index);
const struct sdl3_theme *sdl3_theme_by_id(const char *id);
int sdl3_theme_index(const struct sdl3_theme *theme);
SDL_Color sdl3_theme_color(const struct sdl3_theme *theme, uint8_t index);
/** Canonical world colour; only the explicit accessibility theme remaps it. */
SDL_Color sdl3_world_color(const struct sdl3_theme *theme, uint8_t index);
/** Desaturate and dim a world colour to an explicit brightness. */
SDL_Color sdl3_dim_world_color(SDL_Color visible, uint8_t brightness);
/** Desaturate and dim a known world colour outside the current field of view. */
SDL_Color sdl3_theme_remembered_world_color(const struct sdl3_theme *theme,
		SDL_Color visible);

#endif /* INCLUDED_SDL3_THEME_H */
