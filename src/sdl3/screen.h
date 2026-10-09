/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/screen.h
 * \brief Reusable renderer for semantic full-screen SDL3 interfaces.
 */

#ifndef INCLUDED_SDL3_SCREEN_H
#define INCLUDED_SDL3_SCREEN_H

#include <SDL3/SDL.h>

#include "sdl3/screen-model.h"

struct sdl3_theme;
struct sdl3_visual;

void sdl3_screen_draw_document(const struct sdl3_screen *screen,
		struct sdl3_visual *visual, const struct sdl3_theme *theme,
		int left, int width);

void sdl3_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme);

#endif /* INCLUDED_SDL3_SCREEN_H */
