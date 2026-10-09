/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef SDL3_WORLD_INTRO_H
#define SDL3_WORLD_INTRO_H
#include <SDL3/SDL.h>
struct ui_world_intro;
struct sdl3_visual;
struct sdl3_theme;
void sdl3_world_intro_draw(const struct ui_world_intro *state,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme);
#endif
