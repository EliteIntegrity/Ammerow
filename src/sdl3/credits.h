/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/credits.h
 * \brief Chronological credits presentation for the SDL3 home screen.
 *
 *
 */

#ifndef INCLUDED_SDL3_CREDITS_H
#define INCLUDED_SDL3_CREDITS_H

#include <SDL3/SDL.h>

struct sdl3_theme;
struct sdl3_visual;

int sdl3_credits_max_offset(int width, int available_rows);
void sdl3_credits_draw(SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width, int offset);

#endif /* INCLUDED_SDL3_CREDITS_H */
