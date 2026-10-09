/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/run-summary-screen.h
 * \brief Native renderer for the single authoritative end-of-run summary.
 */

#ifndef INCLUDED_SDL3_RUN_SUMMARY_SCREEN_H
#define INCLUDED_SDL3_RUN_SUMMARY_SCREEN_H

#include <SDL3/SDL.h>

struct sdl3_screen;
struct sdl3_theme;
struct sdl3_visual;

bool sdl3_run_summary_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width);

#endif /* INCLUDED_SDL3_RUN_SUMMARY_SCREEN_H */
