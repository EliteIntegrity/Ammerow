/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/map-view.h
 * \brief Read-only presentation model for Angband's play area.
 *
 *
 */

#ifndef INCLUDED_SDL3_MAP_VIEW_H
#define INCLUDED_SDL3_MAP_VIEW_H

#include "sdl3/ascii-art.h"
#include "sdl3/terrain.h"
#include "sdl3/term.h"
#include "ui-spelunking.h"

#define SDL3_MAP_ART_CAPACITY 2048

enum sdl3_map_art_priority {
	SDL3_MAP_ART_TERRAIN = 0,
	SDL3_MAP_ART_ITEM,
	SDL3_MAP_ART_MONSTER
};

struct sdl3_map_art_subject {
	int col;
	int row;
	wchar_t glyph;
	uint8_t attr;
	enum sdl3_map_art_priority priority;
	char asset_key[SDL3_ASCII_ART_KEY_CAPACITY];
	char tile_key[128];
	char tile_fallback_key[128];
};

struct sdl3_map_view {
	bool active;
	/* A knowledge-safe map source independent from Angband's terminal panel.
	 * Used for zoom-out and for smooth camera following at normal zoom. */
	bool expanded_source;
	/* Composite-grid rectangle occupied by the play area. */
	int col;
	int row;
	int cols;
	int rows;
	/* Cells rendered into that rectangle.  At normal/enlarged zoom this is
	 * the composite grid; zoom-out supplies a larger knowledge-safe map. */
	struct sdl3_grid *source;
	/* Presentation-only overrides for exposed terrain.  Normal zoom indexes
	 * these with composite-grid coordinates; zoom-out indexes its dedicated
	 * metadata cache with source coordinates. */
	const struct sdl3_terrain_overlay *terrain;
	bool terrain_source_coordinates;
	int source_col;
	int source_row;
	int source_cols;
	int source_rows;
	int focus_col;
	int focus_row;
	int cursor_col;
	int cursor_row;
	/* The physical camera may cover the full output.  These retain Angband's
	 * authoritative terminal map rectangle for cursor and mouse translation. */
	int term_col;
	int term_row;
	int term_cols;
	int term_rows;
	int sidebar_mode;
	bool hud_stats_visible;
	/* Mapping back to Angband's dungeon panel for safe mouse input. */
	int dungeon_col;
	int dungeon_row;
	int term_offset_col;
	int term_offset_row;
	int tile_width;
	int tile_height;
	int zoom_percent;
	bool tile_mode;
	/* Reserved strip: never inferred from the legacy terminal text. */
	struct ui_spelunking_status cave_status;
	int status_rows;
	/* Semantic player vitality for a presentation-only low-health tint. */
	bool player_vitality_visible;
	int player_dungeon_col;
	int player_dungeon_row;
	int player_hp;
	int player_max_hp;
	int player_warning_hp;
	int art_subject_count;
	struct sdl3_map_art_subject art_subjects[SDL3_MAP_ART_CAPACITY];
};

#endif /* INCLUDED_SDL3_MAP_VIEW_H */
