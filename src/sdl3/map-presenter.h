/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/map-presenter.h
 * \brief Game-state adapter for the SDL3 play-area presentation.
 */

#ifndef INCLUDED_SDL3_MAP_PRESENTER_H
#define INCLUDED_SDL3_MAP_PRESENTER_H

#include "sdl3/layout.h"
#include "sdl3/map-view.h"
#include "sdl3/inspect-card.h"
#include "sdl3/spelunking-presenter.h"

struct sdl3_map_presenter_options {
	int cursor_col;
	int cursor_row;
	bool cursor_visible;
	/* Only modal Look/aiming owns camera focus and examination cards.
	 * A persistent combat-target marker is not an examination request. */
	bool inspection_visible;
	bool settings_visible;
	bool hud_stats_visible;
	bool messages_visible;
	enum sdl3_dock_placement message_placement;
	int message_rows;
	bool big_stat_cards;
	bool animated_combat;
	bool edge_warnings;
	bool tile_mode;
	bool ascii_square;
	bool ascii_enhanced;
	int cell_width;
	int cell_height;
	int zoom_percent;
	enum sdl3_terrain_style terrain_style;
	bool verbose;
};

struct sdl3_map_presenter {
	struct sdl3_grid expanded_map;
	struct sdl3_spelunking_presenter spelunking;
	struct sdl3_terrain_overlay terrain_overlay;
	struct sdl3_terrain_overlay expanded_terrain_overlay;
	struct sdl3_map_view view;
	struct sdl3_inspect_card inspect_card;
	int logged_zoom_percent;
	int expanded_dungeon_col;
	int expanded_dungeon_row;
	int32_t expanded_turn;
	const struct chunk *expanded_cave;
	bool expanded_positioned;
	const struct chunk *terrain_cave;
	uint32_t terrain_level_key;
	int terrain_dungeon_col;
	int terrain_dungeon_row;
	bool terrain_positioned;
};

/* Read-only warning policy, shared by top-down and side-view presentation. */
enum sdl3_edge_warning sdl3_map_player_warning(const struct player *p,
		bool side_view);

void sdl3_map_presenter_free(struct sdl3_map_presenter *presenter);
void sdl3_map_presenter_reset(struct sdl3_map_presenter *presenter);
void sdl3_map_presenter_invalidate_layout(
		struct sdl3_map_presenter *presenter);
void sdl3_map_presenter_invalidate_terrain(
		struct sdl3_map_presenter *presenter);
void sdl3_map_presenter_configure(struct sdl3_map_presenter *presenter,
		struct sdl3_grid *grid, const struct sdl3_pane_layout *pane,
		term *main_term, const struct sdl3_map_presenter_options *options);

#endif /* INCLUDED_SDL3_MAP_PRESENTER_H */
