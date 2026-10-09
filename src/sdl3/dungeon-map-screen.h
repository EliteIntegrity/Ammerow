/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/dungeon-map-screen.h
 * \brief SDL3 renderer for the semantic level overview.
 */

#ifndef INCLUDED_SDL3_DUNGEON_MAP_SCREEN_H
#define INCLUDED_SDL3_DUNGEON_MAP_SCREEN_H

#include <SDL3/SDL.h>

struct sdl3_screen;
struct sdl3_theme;
struct sdl3_visual;

bool sdl3_dungeon_map_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width);

#endif /* INCLUDED_SDL3_DUNGEON_MAP_SCREEN_H */
