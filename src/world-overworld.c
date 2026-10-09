/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-overworld.c
 * \brief Save-stable layout and derived routes for the explorable surface.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "world-entry.h"
#include "world-overworld.h"

#define WORLD_OVERWORLD_MAP_SCALE 4
#define WORLD_OVERWORLD_CELL_TRAVEL_TURNS 375

static const char *const biome_names[] = {
	"none", "village", "cavewood", "lake", "pine-wood", "marsh",
	"heath", "meadow", "ruins", "scrub"
};

const char *world_overworld_biome_name(enum world_outdoor_biome biome)
{
	if (biome < WORLD_BIOME_NONE || biome > WORLD_BIOME_SCRUB) return "none";
	return biome_names[biome];
}

bool world_overworld_biome_from_name(const char *name,
		enum world_outdoor_biome *biome)
{
	int i;

	if (!name || !biome) return false;
	for (i = WORLD_BIOME_VILLAGE; i <= WORLD_BIOME_SCRUB; i++) {
		if (streq(name, biome_names[i])) {
			*biome = (enum world_outdoor_biome)i;
			return true;
		}
	}
	return false;
}

/** Collect the stable role records in slot order. */
static bool collect_cells(const struct level **cells, uint8_t count)
{
	const struct level *lev;
	uint8_t found = 0;

	if (!cells || !count || count > WORLD_OVERWORLD_CELL_LIMIT) return false;
	memset(cells, 0, WORLD_OVERWORLD_CELL_LIMIT * sizeof(*cells));
	for (lev = world; lev; lev = lev->next) {
		if (!lev->is_overworld_cell) continue;
		if (lev->overworld_slot >= count || cells[lev->overworld_slot]) {
			return false;
		}
		cells[lev->overworld_slot] = lev;
		found++;
	}
	if (found != count) return false;
	for (found = 0; found < count; found++) {
		if (!cells[found]) return false;
	}
	return true;
}

/** Fixed local generator: layout version one must never change this sequence. */
static uint32_t layout_random(uint32_t *state)
{
	*state = *state * 1664525U + 1013904223U;
	return *state;
}

/** Resolve the layout to one row-major array without retaining a cache. */
static bool resolve_cells(const struct player *p, const struct level **placed)
{
	const struct level *roles[WORLD_OVERWORLD_CELL_LIMIT];
	const struct level *hub = NULL;
	const struct level *cavewood = NULL;
	uint32_t state;
	int i;

	if (!p || !placed || !world_overworld_layout_valid(p) ||
			!collect_cells(roles, p->overworld_layout.count)) {
		return false;
	}
	memset(placed, 0, WORLD_OVERWORLD_CELL_LIMIT * sizeof(*placed));
	if (p->overworld_layout.seed) {
		for (i = 0; i < p->overworld_layout.count; i++) placed[i] = roles[i];
		state = p->overworld_layout.seed;
		for (i = p->overworld_layout.count - 1; i > 0; i--) {
			int j = (int)(layout_random(&state) % (uint32_t)(i + 1));
			const struct level *swap = placed[i];

			placed[i] = placed[j];
			placed[j] = swap;
		}
		return true;
	}

	/* Version-three and earlier development saves retain the old eastward
	 * relationship while gaining the remaining seven cells deterministically. */
	for (i = 0; i < p->overworld_layout.count; i++) {
		if (roles[i]->kind == WORLD_LOCATION_HUB) hub = roles[i];
		if (roles[i]->biome == WORLD_BIOME_CAVEWOOD) cavewood = roles[i];
	}
	if (!hub || !cavewood || p->overworld_layout.width != 3 ||
			p->overworld_layout.height != 3) {
		return false;
	}
	placed[4] = hub;
	placed[5] = cavewood;
	for (i = 0; i < p->overworld_layout.count; i++) {
		int position;

		if (roles[i] == hub || roles[i] == cavewood) continue;
		for (position = 0; position < p->overworld_layout.count; position++) {
			if (!placed[position]) {
				placed[position] = roles[i];
				break;
			}
		}
	}
	return true;
}

bool world_overworld_layout_valid(const struct player *p)
{
	const struct world_overworld_layout *layout;
	const struct level *cells[WORLD_OVERWORLD_CELL_LIMIT];

	if (!p) return false;
	layout = &p->overworld_layout;
	if (layout->version != WORLD_OVERWORLD_VERSION ||
			layout->width != WORLD_OVERWORLD_LAUNCH_WIDTH ||
			layout->height != WORLD_OVERWORLD_LAUNCH_HEIGHT ||
			layout->count != WORLD_OVERWORLD_LAUNCH_CELLS ||
			layout->count != layout->width * layout->height) {
		return false;
	}
	return collect_cells(cells, layout->count);
}

bool world_overworld_layout_set(struct player *p, uint8_t version,
		uint8_t width, uint8_t height, uint8_t count, uint32_t seed)
{
	struct world_overworld_layout previous;

	if (!p) return false;
	previous = p->overworld_layout;
	p->overworld_layout.version = version;
	p->overworld_layout.width = width;
	p->overworld_layout.height = height;
	p->overworld_layout.count = count;
	p->overworld_layout.seed = seed;
	if (!world_overworld_layout_valid(p)) {
		p->overworld_layout = previous;
		return false;
	}
	return true;
}

bool world_overworld_layout_create(struct player *p, bool legacy)
{
	uint32_t seed = legacy ? 0U :
		(uint32_t)(1 + randint0(0x0fffffff));

	return world_overworld_layout_set(p, WORLD_OVERWORLD_VERSION,
		WORLD_OVERWORLD_LAUNCH_WIDTH, WORLD_OVERWORLD_LAUNCH_HEIGHT,
		WORLD_OVERWORLD_LAUNCH_CELLS, seed);
}

const struct level *world_overworld_location_at(const struct player *p,
		int x, int y)
{
	const struct level *placed[WORLD_OVERWORLD_CELL_LIMIT];

	if (!p || x < 0 || y < 0 || x >= p->overworld_layout.width ||
			y >= p->overworld_layout.height || !resolve_cells(p, placed)) {
		return NULL;
	}
	return placed[y * p->overworld_layout.width + x];
}

bool world_overworld_position(const struct player *p,
		const char *location_id, int *x, int *y)
{
	const struct level *placed[WORLD_OVERWORLD_CELL_LIMIT];
	int i;

	if (!p || !location_id || !x || !y || !resolve_cells(p, placed)) {
		return false;
	}
	for (i = 0; i < p->overworld_layout.count; i++) {
		if (streq(placed[i]->id, location_id)) {
			*x = i % p->overworld_layout.width;
			*y = i / p->overworld_layout.width;
			return true;
		}
	}
	return false;
}

const struct level *world_overworld_neighbor(const struct player *p,
		const char *location_id, int dx, int dy)
{
	int x, y;

	if ((ABS(dx) + ABS(dy) != 1) ||
			!world_overworld_position(p, location_id, &x, &y)) {
		return NULL;
	}
	return world_overworld_location_at(p, x + dx, y + dy);
}

/** Resolve a site either directly to a cell or relative to an anchored cell. */
bool world_overworld_site_position(const struct player *p,
		const struct world_site *site, int *x, int *y)
{
	const struct level *lev;

	if (!p || !site || !x || !y) return false;
	for (lev = world; lev; lev = lev->next) {
		if (lev->is_overworld_cell && streq(lev->site_id, site->id) &&
				world_overworld_position(p, lev->id, x, y)) {
			*x *= WORLD_OVERWORLD_MAP_SCALE;
			*y *= WORLD_OVERWORLD_MAP_SCALE;
			return true;
		}
	}
	if (site->has_map_anchor) {
		for (lev = world; lev; lev = lev->next) {
			if (lev->is_overworld_cell &&
					streq(lev->site_id, site->anchor_site_id) &&
					world_overworld_position(p, lev->id, x, y)) {
				*x = *x * WORLD_OVERWORLD_MAP_SCALE + site->anchor_dx;
				*y = *y * WORLD_OVERWORLD_MAP_SCALE + site->anchor_dy;
				return true;
			}
		}
	}
	if (site->has_map_position) {
		*x = site->map_x;
		*y = site->map_y;
		return true;
	}
	return false;
}

static void free_runtime_route(struct world_route *route)
{
	if (!route) return;
	string_free(route->id);
	string_free(route->from);
	string_free(route->from_entry);
	string_free(route->to);
	string_free(route->to_entry);
	string_free(route->kind);
	string_free(route->message);
	mem_free(route);
}

static void remove_runtime_routes(void)
{
	struct world_route **cursor = &world_routes;

	while (*cursor) {
		if ((*cursor)->runtime_layout) {
			struct world_route *old = *cursor;

			*cursor = old->next;
			free_runtime_route(old);
		} else {
			cursor = &(*cursor)->next;
		}
	}
}

static bool append_route(const char *id, const struct level *from,
		const char *from_entry, const struct level *to, const char *to_entry,
		const char *kind, unsigned int turns)
{
	struct world_route *route;
	struct world_route **tail = &world_routes;

	if (!world_id_is_valid(id) || !from || !to ||
			!world_entry_by_id(from, from_entry) ||
			!world_entry_by_id(to, to_entry) || world_route_by_id(id)) {
		return false;
	}
	route = mem_zalloc(sizeof(*route));
	route->id = string_make(id);
	route->from = string_make(from->id);
	route->from_entry = string_make(from_entry);
	route->to = string_make(to->id);
	route->to_entry = string_make(to_entry);
	route->kind = string_make(kind);
	route->travel_turns = turns;
	route->runtime_layout = true;
	while (*tail) tail = &(*tail)->next;
	*tail = route;
	return true;
}

bool world_overworld_rebuild_routes(struct player *p)
{
	static const int dx[] = { 0, 1, 0, -1 };
	static const int dy[] = { -1, 0, 1, 0 };
	static const char direction[] = { 'n', 'e', 's', 'w' };
	static const char *const entries[] = {
		"edge.north", "edge.east", "edge.south", "edge.west"
	};
	static const char *const opposite[] = {
		"edge.south", "edge.west", "edge.north", "edge.east"
	};
	const struct level *placed[WORLD_OVERWORLD_CELL_LIMIT];
	int i, j;

	remove_runtime_routes();
	if (!p || !resolve_cells(p, placed)) return false;
	for (i = 0; i < p->overworld_layout.count; i++) {
		int x = i % p->overworld_layout.width;
		int y = i / p->overworld_layout.width;

		for (j = 0; j < 4; j++) {
			const struct level *to = world_overworld_location_at(p,
				x + dx[j], y + dy[j]);
			char id[WORLD_ID_LEN];

			if (!to) continue;
			strnfmt(id, sizeof(id), "core.route.grid.%02u.%c",
				placed[i]->overworld_slot, direction[j]);
			if (!append_route(id, placed[i], entries[j], to, opposite[j],
					"edge", 0)) {
				remove_runtime_routes();
				return false;
			}
		}
	}
	for (i = 0; i < p->overworld_layout.count; i++) {
		int from_x = i % p->overworld_layout.width;
		int from_y = i / p->overworld_layout.width;

		for (j = 0; j < p->overworld_layout.count; j++) {
			int to_x, to_y, distance;
			char id[WORLD_ID_LEN];

			if (i == j) continue;
			to_x = j % p->overworld_layout.width;
			to_y = j / p->overworld_layout.width;
			distance = ABS(to_x - from_x) + ABS(to_y - from_y);
			strnfmt(id, sizeof(id), "core.route.travel.%02u.%02u",
				placed[i]->overworld_slot, placed[j]->overworld_slot);
			if (!append_route(id, placed[i], "travel.stop", placed[j],
					"travel.stop", "trail",
					(unsigned int)distance *
					WORLD_OVERWORLD_CELL_TRAVEL_TURNS)) {
				remove_runtime_routes();
				return false;
			}
		}
	}
	return true;
}

/** Activate the logical stop after physically reaching a surface location. */
unsigned int world_overworld_activate_current_stop(struct player *p)
{
	const struct level *lev = world_player_level(p);
	struct world_travel_node *node;
	bool had;

	if (!p || !lev || !lev->is_overworld_cell) return 0;
	node = world_travel_node_by_endpoint(lev->id, "travel.stop");
	if (!node) return 0;
	had = world_player_has_activated_travel_node(p, node->id);
	if (!world_player_activate_travel_node(p, node->id)) return 0;
	(void)world_player_unlock_activated_travel_routes(p);
	return had ? 0 : 1;
}

/** Give migrated runs the logical stop for every surface already discovered. */
unsigned int world_overworld_backfill_discovered_stops(struct player *p)
{
	const struct level *lev;
	unsigned int activated = 0;

	if (!p) return 0;
	for (lev = world; lev; lev = lev->next) {
		struct world_travel_node *node;
		bool had;

		if (!lev->is_overworld_cell ||
				!world_player_has_discovered_location(p, lev->id)) {
			continue;
		}
		node = world_travel_node_by_endpoint(lev->id, "travel.stop");
		if (!node) continue;
		had = world_player_has_activated_travel_node(p, node->id);
		if (world_player_activate_travel_node(p, node->id) && !had) {
			activated++;
		}
	}
	(void)world_player_unlock_activated_travel_routes(p);
	return activated;
}
