/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home.h
 * \brief SDL3 home, load-run, and help screen state.
 *
 *
 */

#ifndef INCLUDED_SDL3_HOME_H
#define INCLUDED_SDL3_HOME_H

#include <SDL3/SDL.h>

#include "sdl3/home-model.h"

struct sdl3_theme;
struct sdl3_visual;

void sdl3_home_cleanup(struct sdl3_home_screen *home);
bool sdl3_home_refresh_saves(struct sdl3_home_screen *home);
void sdl3_home_mark_save_damaged(struct sdl3_home_screen *home,
		const char *filename);
bool sdl3_home_selected_save_is_live(const struct sdl3_home_screen *home);
bool sdl3_home_delete_selected_save(const struct sdl3_home_screen *home);
bool sdl3_home_select_at(struct sdl3_home_screen *home, int cols, int rows,
		int col, int row);
void sdl3_home_draw(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme);

#endif /* INCLUDED_SDL3_HOME_H */
