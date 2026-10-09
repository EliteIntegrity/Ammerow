/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/legal.h
 * \brief Release-manifest-backed legal presentation for the SDL3 home screen.
 *
 *
 */

#ifndef INCLUDED_SDL3_LEGAL_H
#define INCLUDED_SDL3_LEGAL_H

#include <SDL3/SDL.h>
#include <stdbool.h>

struct sdl3_theme;
struct sdl3_visual;

bool sdl3_legal_manifest_valid(void);
int sdl3_legal_max_offset(int width, int available_rows);
void sdl3_legal_draw(SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width, int offset);

#endif /* INCLUDED_SDL3_LEGAL_H */
