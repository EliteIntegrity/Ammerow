/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/spelunking-presenter.h
 * \brief SDL3 play-area adapter for side-view spelunking.
 */

#ifndef INCLUDED_SDL3_SPELUNKING_PRESENTER_H
#define INCLUDED_SDL3_SPELUNKING_PRESENTER_H

#include "sdl3/map-view.h"
#include "world-spelunking-view.h"

/** Persistent, allocation-free-after-resize presentation storage. */
struct sdl3_spelunking_presenter {
	struct sdl3_grid map;
	struct sdl3_terrain_overlay terrain;
	uint8_t snapshot[WORLD_SPELUNK_CELL_MAX];
};

void sdl3_spelunking_presenter_free(
		struct sdl3_spelunking_presenter *presenter);

/**
 * Capture a knowledge-safe side-view map sized for the current map zoom.
 * The supplied viewport is in composite terminal cells; UI outside it is not
 * scaled.
 */
bool sdl3_spelunking_presenter_configure(
		struct sdl3_spelunking_presenter *presenter,
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int viewport_col,
		int viewport_row, int viewport_cols, int viewport_rows,
		int zoom_percent, struct sdl3_map_view *view);

/** Configure around a semantic Look focus. Positive source extents override
 * terminal-derived density for square ASCII; zero preserves Classic/tiles. */
bool sdl3_spelunking_presenter_configure_focus(
		struct sdl3_spelunking_presenter *presenter,
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int viewport_col,
		int viewport_row, int viewport_cols, int viewport_rows,
		int zoom_percent, int focus_x, int focus_y, bool cursor_visible,
		int requested_source_cols, int requested_source_rows,
		struct sdl3_map_view *view);

#endif /* INCLUDED_SDL3_SPELUNKING_PRESENTER_H */
