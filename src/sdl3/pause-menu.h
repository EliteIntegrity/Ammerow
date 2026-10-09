/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/pause-menu.h
 * \brief Renderer-owned in-game Escape menu for the SDL3 frontend.
 */

#ifndef INCLUDED_SDL3_PAUSE_MENU_H
#define INCLUDED_SDL3_PAUSE_MENU_H

#include <SDL3/SDL.h>

#include "sdl3/pause-menu-model.h"

struct sdl3_theme;
struct sdl3_visual;

void sdl3_pause_menu_draw(const struct sdl3_pause_menu *menu,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme);
#endif /* INCLUDED_SDL3_PAUSE_MENU_H */
