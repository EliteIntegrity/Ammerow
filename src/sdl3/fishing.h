/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/fishing.h
 * \brief Transparent SDL3 presentation for the fishing activity.
 */

#ifndef INCLUDED_SDL3_FISHING_H
#define INCLUDED_SDL3_FISHING_H

#include <SDL3/SDL.h>

#define SDL3_FISHING_ANIMATION_MS 200

struct sdl3_theme;
struct sdl3_visual;
struct world_fishing_runtime;

bool sdl3_fishing_is_active(const struct world_fishing_runtime *runtime);
void sdl3_fishing_draw(const struct world_fishing_runtime *runtime,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int reserved_bottom_rows);

#endif /* INCLUDED_SDL3_FISHING_H */
