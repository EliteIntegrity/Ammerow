/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-location.h
 * \brief Stable identities for world locations and routes.
 *
 *
 */

#ifndef WORLD_LOCATION_H
#define WORLD_LOCATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORLD_ID_LEN 64
#define WORLD_ENTRY_LEN 64
#define WORLD_LEDGER_LIMIT 1024
#define WORLD_LOCATION_HEIGHT_MAX 66
#define WORLD_LOCATION_WIDTH_MAX 198
#define WORLD_OVERWORLD_CELL_LIMIT 64
#define WORLD_OVERWORLD_VERSION 1
#define WORLD_OVERWORLD_LAUNCH_WIDTH 3
#define WORLD_OVERWORLD_LAUNCH_HEIGHT 3
#define WORLD_OVERWORLD_LAUNCH_CELLS 9
#define WORLD_OVERWORLD_GATE_WIDTH_DEFAULT 5
#define WORLD_OVERWORLD_GATE_WIDTH_MAX 15

enum world_location_kind {
	WORLD_LOCATION_HUB = 0,
	WORLD_LOCATION_OUTDOORS,
	WORLD_LOCATION_DUNGEON,
	WORLD_LOCATION_INTERIOR,
	WORLD_LOCATION_SPECIAL
};

enum world_location_mode {
	WORLD_MODE_TOP_DOWN = 0,
	WORLD_MODE_SPELUNKING
};

enum world_outdoor_biome {
	WORLD_BIOME_NONE = 0,
	WORLD_BIOME_VILLAGE,
	WORLD_BIOME_CAVEWOOD,
	WORLD_BIOME_LAKE,
	WORLD_BIOME_PINE_WOOD,
	WORLD_BIOME_MARSH,
	WORLD_BIOME_HEATH,
	WORLD_BIOME_MEADOW,
	WORLD_BIOME_RUINS,
	WORLD_BIOME_SCRUB
};

/** Authored water identities used by the bounded fishing activity. */
enum world_fishing_habitat {
	WORLD_FISHING_HABITAT_NONE = 0,
	WORLD_FISHING_HABITAT_RAINWATER,
	WORLD_FISHING_HABITAT_OPEN_LAKE,
	WORLD_FISHING_HABITAT_FOREST_POOL,
	WORLD_FISHING_HABITAT_MARSH_POOL,
	WORLD_FISHING_HABITAT_RUIN_CISTERN,
	WORLD_FISHING_HABITAT_CAVE_POOL,
	WORLD_FISHING_HABITAT_MAX
};

/** Compact, reproducible assignment of stable places to surface cells. */
struct world_overworld_layout {
	uint32_t seed;
	uint8_t version;
	uint8_t width;
	uint8_t height;
	uint8_t count;
};

/** The small part of world state that always travels with the player. */
struct world_player_location {
	char id[WORLD_ID_LEN];
	char entry[WORLD_ENTRY_LEN];
};

/** A bounded, sorted set of stable IDs owned by one run. */
struct world_id_ledger {
	char **ids;
	uint16_t count;
	uint16_t capacity;
};

/** One ordinary, in-progress journey which may teach a declared route. */
struct world_physical_route_pending {
	char route_id[WORLD_ID_LEN];
};

struct level;
struct chunk;
struct player;
struct world_site;
struct world_route;
struct world_travel_node;

struct level *level_by_id(const char *id);
struct world_site *world_site_by_id(const char *id);
struct world_route *world_route_by_id(const char *id);
struct world_travel_node *world_travel_node_by_id(const char *id);
struct world_travel_node *world_travel_node_by_endpoint(
		const char *location_id, const char *entry_id);
const char *world_level_area_name(const struct level *lev);
void world_level_format_position(const struct level *lev, char *buf,
		size_t size);
bool world_level_tracks_dungeon_depth(const struct level *lev);
bool world_id_is_valid(const char *id);
bool world_player_set_location(struct player *p, const char *id,
		const char *entry);
bool world_player_set_location_by_depth(struct player *p, int depth,
		const char *entry);
bool world_player_restore_location(struct player *p);
const struct level *world_player_level(const struct player *p);
bool world_player_has_discovered_location(const struct player *p,
		const char *id);
bool world_player_discover_location(struct player *p, const char *id);
bool world_player_route_is_unlocked(const struct player *p, const char *id);
bool world_player_unlock_route(struct player *p, const char *id);
bool world_player_begin_physical_route(struct player *p, struct chunk *c,
		const char *destination_id);
bool world_player_finish_physical_route(struct player *p, struct chunk *c);
void world_player_cancel_physical_route(struct player *p);
bool world_player_has_activated_travel_node(const struct player *p,
		const char *id);
bool world_player_activate_travel_node(struct player *p, const char *id);
bool world_player_activate_travel_node_here(struct player *p,
		struct chunk *c, const char *id);
unsigned int world_player_activate_travel_nodes_here(struct player *p,
		struct chunk *c);
unsigned int world_player_backfill_travel_nodes(struct player *p);
unsigned int world_player_unlock_activated_travel_routes(struct player *p);
void world_player_clear_ledgers(struct player *p);
const struct level *world_chunk_level(const struct chunk *c);
bool world_chunk_set_location(struct chunk *c, const char *id,
		bool is_known);
bool world_chunk_restore_location(struct chunk *c);
bool world_player_restore_chunks(const struct player *p,
		struct chunk *actual, struct chunk *known,
		struct chunk *const *stored, uint16_t stored_count);

#endif /* !WORLD_LOCATION_H */
