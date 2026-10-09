/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file game-world.h
 * \brief Game core management of the game world
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include "cave.h"
#include "world-location.h"
#include "world-transition.h"
#include "world-destination.h"

struct world_entry;
struct world_story_scene;

#define WORLD_SITE_MAP_STAMP_WIDTH 5
#define WORLD_SITE_MAP_STAMP_HEIGHT 3

struct level {
	int depth;
	bool has_legacy_depth;
	char *name;
	char *up;
	char *down;
	char *id;
	char *site_id;
	char *up_id;
	char *down_id;
	enum world_location_kind kind;
	enum world_location_mode mode;
	int local_floor;
	int danger;
	uint16_t height;
	uint16_t width;
	bool is_overworld_cell;
	uint8_t overworld_slot;
	enum world_outdoor_biome biome;
	uint8_t overworld_gate_width;
	uint16_t overworld_population;
	enum world_fishing_habitat fishing_habitat;
	bool has_store_services;
	/* First-descent story authored for this stable location. */
	char *arrival_message;
	const struct world_story_scene *arrival_scene;
	struct world_entry *entries;
	struct level *next;
};

/** Stable presentation metadata shared by every floor at one world site. */
struct world_site {
	char *id;
	char *name;
	/* Authored environment recipe used by every location at this site. */
	char *visual_profile;
	/* Optional presentation and prose after the completed-run victory. */
	char *victory_visual_profile;
	char *victory_message;
	/* Authored singular label for numbered local floors at this site. */
	char *floor_term;
	const struct world_story_scene *opening_scene;
	/* Compact, spoiler-safe identity used by scalable world diagrams. */
	char map_stamp[WORLD_SITE_MAP_STAMP_HEIGHT]
		[WORLD_SITE_MAP_STAMP_WIDTH + 1];
	char *map_description;
	uint8_t map_attr;
	bool has_map_visual;
	/* Presentation-only coordinates for the discovered-world diagram. */
	int16_t map_x;
	int16_t map_y;
	bool has_map_position;
	char *anchor_site_id;
	int16_t anchor_dx;
	int16_t anchor_dy;
	bool has_map_anchor;
	struct world_site *next;
};

struct world_route {
	char *id;
	char *from;
	char *from_entry;
	char *to;
	char *to_entry;
	char *kind;
	char *message;
	unsigned int travel_turns;
	bool requires_unlock;
	bool runtime_layout;
	struct world_route *next;
};

/** A stable fast-travel endpoint attached to one named location entry. */
struct world_travel_node {
	char *id;
	char *location_id;
	char *entry_id;
	struct world_travel_node *next;
};

extern uint16_t daycount;
extern uint32_t seed_randart;
extern uint32_t seed_flavor;
extern int32_t turn;
extern bool character_generated;
extern bool character_dungeon;
extern const uint8_t extract_energy[200];
extern struct level *world;
extern struct world_site *world_sites;
extern struct world_route *world_routes;
extern struct world_travel_node *world_travel_nodes;

struct level *level_by_name(const char *name);
struct level *level_by_depth(int depth);
bool is_daytime(void);
int turn_energy(int speed);
void play_ambient_sound(void);
void process_world(struct chunk *c);
void on_leave_level(void);
void on_new_level(void);
/** Run mode-specific arrival/resume housekeeping for the active location. */
bool on_enter_world_location(void);
/** Reconcile data-authored services attached to the active retained chunk. */
bool world_reconcile_location_services(struct chunk *actual,
		struct chunk *known);
/** Return the authored history line attached to the player's opening site. */
const char *world_player_opening_history(const struct player *p);
/** Complete arrival at an already published top-down destination. */
void world_top_down_enter_destination(
		const struct world_transition *transition, struct chunk *actual,
		struct chunk *known, struct player *p, bool fast_travel);
const struct world_destination_lifecycle *world_top_down_route_lifecycle(void);
const struct world_destination_lifecycle *
	world_top_down_physical_route_lifecycle(void);
void process_player(void);
void run_game_loop(void);
void make_noise(struct player *p, const struct loc *origin,
		const uint16_t *falloff);
void forget_noise(void);
void age_scent(void);


#endif /* !GAME_WORLD_H */
