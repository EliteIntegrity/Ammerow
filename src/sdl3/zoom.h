/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/zoom.h
 * \brief Bounded play-area zoom for the SDL3 character display.
 *
 *
 */

#ifndef INCLUDED_SDL3_ZOOM_H
#define INCLUDED_SDL3_ZOOM_H

#include <stdbool.h>

#define SDL3_ZOOM_MIN 50
#define SDL3_ZOOM_MAX 800
#define SDL3_ZOOM_DEFAULT 100

int sdl3_zoom_clamp(int percent);
int sdl3_zoom_change(int percent, int direction);
int sdl3_zoom_panel_margin(int cell_count, int percent);
int sdl3_zoom_visible_cells(int viewport_cells, int percent);
int sdl3_zoom_pixel_art_size(int base_cell_height, int percent,
		int native_art_size);
int sdl3_zoom_pixel_art_size(int base_cell_height, int percent,
		int native_art_size);
int sdl3_zoom_centered_start(int focus_cell, int cell_count, int limit);
int sdl3_zoom_source_cell_for_grid(int grid_cell, int dungeon_first,
		int source_first, int tile_span, bool expanded_source);
int sdl3_zoom_grid_cell_for_source(int source_cell, int source_first,
		int dungeon_first, int tile_span, bool expanded_source);
float sdl3_zoom_camera_center(int first_cell, int cell_count, int focus_cell,
		float viewport_size, float cell_size);
/* Keep a complete focus cell clear of HUD edges, allowing blank world margins. */
float sdl3_zoom_safe_center(float center, int focus_cell, float viewport_size,
		float cell_size, float leading_inset, float trailing_inset);
/* Protect the focus and nearby cells; reduce the margin at extreme zoom. */
float sdl3_zoom_safe_focus_center(float center, int focus_cell,
		float viewport_size, float cell_size, float leading_inset,
		float trailing_inset);
int sdl3_zoom_cell_from_pixel(float pixel, float viewport_start,
		float viewport_size, float cell_size, float camera_center);

#endif /* INCLUDED_SDL3_ZOOM_H */
