/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-lore-screen.h
 * \brief SDL3 renderer for knowledge-safe creature recall.
 */

#ifndef INCLUDED_SDL3_MONSTER_LORE_SCREEN_H
#define INCLUDED_SDL3_MONSTER_LORE_SCREEN_H

#include <SDL3/SDL.h>

struct sdl3_screen;
struct sdl3_theme;
struct sdl3_visual;

bool sdl3_monster_lore_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width);

#endif /* INCLUDED_SDL3_MONSTER_LORE_SCREEN_H */
