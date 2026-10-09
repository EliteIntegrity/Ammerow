/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-entry.c
 * \brief Named entry definitions and runtime resolution for world locations.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "world-entry.h"
#include "world-transition.h"

/** A local bypass for every walkable neighbour proves this cell is not a
 * chokepoint. Conservative: omit a decorative rock if a bypass is farther
 * away. Movement uses the same eight-way adjacency as ordinary walking. */
static bool landmark_preserves_paths(struct chunk *c, struct loc grid)
{
	struct loc neighbours[8];
	bool reached[8] = { false };
	int count = 0, i, j;
	bool changed;

	for (i = 0; i < 8; i++) {
		struct loc next = loc_sum(grid, loc(ddx_ddd[i], ddy_ddd[i]));
		if (square_ispassable(c, next)) neighbours[count++] = next;
	}
	if (count < 2) return true;
	reached[0] = true;
	do {
		changed = false;
		for (i = 0; i < count; i++) {
			if (!reached[i]) continue;
			for (j = 0; j < count; j++) {
				if (!reached[j] && ABS(neighbours[i].x - neighbours[j].x) <= 1 &&
						ABS(neighbours[i].y - neighbours[j].y) <= 1) {
					reached[j] = true;
					changed = true;
				}
			}
		}
	} while (changed);
	for (i = 0; i < count; i++) {
		if (!reached[i]) return false;
	}
	return true;
}

/** Apply authored landmark rocks on generation/arrival/load. The resolved
 * entry (not guessed coordinates) is the authority, including repaired saves.
 * Existing content and movement connectivity take precedence over decoration. */
bool world_entry_materialize_landmarks(struct chunk *actual,
		struct chunk *known)
{
	const struct level *level = world_chunk_level(actual);
	const struct world_entry *entry;

	if (!actual || actual->is_known || !level) return false;
	if (known && (known == actual || known->width != actual->width ||
			known->height != actual->height)) return false;
	for (entry = level->entries; entry; entry = entry->next) {
		const struct world_entry_rock *rock;
		struct loc origin;
		if (!entry->rocks) continue;
		if (world_entry_locate(actual, entry->id, &origin) != WORLD_ENTRY_OK)
			return false;
		for (rock = entry->rocks; rock; rock = rock->next) {
			struct loc grid = loc_sum(origin, rock->offset);
			int feat;
			/* Feature flags become available after the world-data parser. */
			if (!feat_is_wall(rock->feat) ||
					tf_has(f_info[rock->feat].flags, TF_PASSABLE)) return false;
			if (!square_in_bounds_fully(actual, grid)) continue;
			feat = square(actual, grid)->feat;
			if (feat != FEAT_GRANITE && feat != FEAT_FLOOR) continue;
			if (square(actual, grid)->mon || square_object(actual, grid) ||
					square(actual, grid)->trap || square_isroad(actual, grid) ||
					square_iswater(actual, grid) || square_iswood(actual, grid) ||
					world_entry_is_overworld_gate_grid(actual, grid)) continue;
			if (feat == FEAT_FLOOR && !landmark_preserves_paths(actual, grid))
				continue;
			square_set_feat(actual, grid, rock->feat);
			/* Upgrade already remembered cells, never reveal unseen terrain. */
			if (known && square(known, grid)->feat != FEAT_NONE)
				square_set_feat(known, grid, rock->feat);
		}
	}
	return true;
}

/** Whether one orthogonal leg can be carved without crossing permanent rock. */
static bool shaft_leg_is_clear(struct chunk *c, struct loc from,
		struct loc to)
{
	struct loc step = loc(SGN(to.x - from.x), SGN(to.y - from.y));
	struct loc grid = from;

	assert(!step.x || !step.y);
	while (!loc_eq(grid, to)) {
		if (!square_in_bounds_fully(c, grid) || square_isperm(c, grid)) {
			return false;
		}
		grid = loc_sum(grid, step);
	}
	return square_in_bounds_fully(c, to) && !square_isperm(c, to);
}

/** Test one of the two deterministic L-shaped connections to existing floor. */
static bool shaft_path_is_clear(struct chunk *c, struct loc from,
		struct loc to, bool horizontal_first)
{
	struct loc corner = horizontal_first ? loc(to.x, from.y) :
		loc(from.x, to.y);

	return shaft_leg_is_clear(c, from, corner) &&
		shaft_leg_is_clear(c, corner, to);
}

/** Carve a previously checked orthogonal leg, retaining its floor endpoint. */
static void carve_shaft_leg(struct chunk *c, struct loc from, struct loc to)
{
	struct loc step = loc(SGN(to.x - from.x), SGN(to.y - from.y));
	struct loc grid = from;

	while (!loc_eq(grid, to)) {
		square_set_feat(c, grid, FEAT_FLOOR);
		grid = loc_sum(grid, step);
	}
}

/** Connect one fixed shaft cell to the nearest deterministic floor. */
static bool connect_shaft_source(struct chunk *c, struct loc source)
{
	struct loc best = loc(0, 0);
	bool best_horizontal = false;
	int best_distance = INT_MAX;
	int x;
	int y;

	if (!square_in_bounds_fully(c, source) || square_isperm(c, source)) {
		return false;
	}
	if (square_isfloor(c, source)) return true;
	for (y = 1; y < c->height - 1; y++) {
		for (x = 1; x < c->width - 1; x++) {
			struct loc candidate = loc(x, y);
			int distance;
			bool horizontal;

			if (!square_isfloor(c, candidate)) continue;
			distance = ABS(source.x - candidate.x) +
				ABS(source.y - candidate.y);
			if (distance >= best_distance) continue;
			horizontal = shaft_path_is_clear(c, source, candidate, true);
			if (!horizontal &&
					!shaft_path_is_clear(c, source, candidate, false)) {
				continue;
			}
			best = candidate;
			best_horizontal = horizontal;
			best_distance = distance;
		}
	}
	if (best_distance == INT_MAX) return false;
	if (best_horizontal) {
		struct loc corner = loc(best.x, source.y);

		carve_shaft_leg(c, source, corner);
		carve_shaft_leg(c, corner, best);
	} else {
		struct loc corner = loc(source.x, best.y);

		carve_shaft_leg(c, source, corner);
		carve_shaft_leg(c, corner, best);
	}
	return true;
}

static const struct world_route *edge_route_from_entry(
		const struct level *level, const struct world_entry *entry)
{
	struct world_route *route;

	if (!level || !entry || entry->kind != WORLD_ENTRY_GRID) return NULL;
	for (route = world_routes; route; route = route->next) {
		if (streq(route->from, level->id) &&
				streq(route->from_entry, entry->id) &&
				streq(route->kind, "edge") && route->travel_turns == 0) {
			return route;
		}
	}
	return NULL;
}

enum world_edge_side {
	WORLD_EDGE_NONE = 0,
	WORLD_EDGE_NORTH,
	WORLD_EDGE_EAST,
	WORLD_EDGE_SOUTH,
	WORLD_EDGE_WEST
};

/** Edge identity is stable data; coordinates are resolved per retained chunk. */
static enum world_edge_side edge_side_from_entry(const struct level *level,
		const struct world_entry *entry)
{
	if (!edge_route_from_entry(level, entry)) return WORLD_EDGE_NONE;
	if (streq(entry->id, "edge.north")) return WORLD_EDGE_NORTH;
	if (streq(entry->id, "edge.east")) return WORLD_EDGE_EAST;
	if (streq(entry->id, "edge.south")) return WORLD_EDGE_SOUTH;
	if (streq(entry->id, "edge.west")) return WORLD_EDGE_WEST;
	return WORLD_EDGE_NONE;
}

static struct loc edge_arrival_grid(const struct chunk *c,
		enum world_edge_side side)
{
	switch (side) {
		case WORLD_EDGE_NORTH: return loc(c->width / 2, 2);
		case WORLD_EDGE_EAST: return loc(c->width - 3, c->height / 2);
		case WORLD_EDGE_SOUTH: return loc(c->width / 2, c->height - 3);
		case WORLD_EDGE_WEST: return loc(2, c->height / 2);
		default: return loc(0, 0);
	}
}

/** Test one cell in a route's arrival or automatic-travel band. */
static bool edge_band_contains(const struct chunk *c,
		enum world_edge_side side, struct loc grid, bool arrival_band)
{
	const struct level *level = world_chunk_level(c);
	int half;

	if (!c || !level || !level->overworld_gate_width) return false;
	half = level->overworld_gate_width / 2;
	switch (side) {
		case WORLD_EDGE_NORTH:
			return grid.y == (arrival_band ? 2 : 1) &&
				ABS(grid.x - c->width / 2) <= half;
		case WORLD_EDGE_EAST:
			return grid.x == (arrival_band ? c->width - 3 : c->width - 2) &&
				ABS(grid.y - c->height / 2) <= half;
		case WORLD_EDGE_SOUTH:
			return grid.y == (arrival_band ? c->height - 3 : c->height - 2) &&
				ABS(grid.x - c->width / 2) <= half;
		case WORLD_EDGE_WEST:
			return grid.x == (arrival_band ? 2 : 1) &&
				ABS(grid.y - c->height / 2) <= half;
		default:
			return false;
	}
}

/** A boundary may be entered from any adjacent cell, including diagonals. */
static bool edge_can_activate_from(const struct chunk *c,
		enum world_edge_side side, struct loc grid)
{
	int dx, dy;
	for (dy = -1; dy <= 1; dy++) {
		for (dx = -1; dx <= 1; dx++) {
			if (edge_band_contains(c, side, loc(grid.x + dx, grid.y + dy), false))
				return true;
		}
	}
	return false;
}

const struct world_entry *world_entry_by_id(const struct level *level,
		const char *id)
{
	const struct world_entry *entry;

	if (!level || !id) return NULL;
	for (entry = level->entries; entry; entry = entry->next) {
		if (streq(entry->id, id)) return entry;
	}
	return NULL;
}

/** Select the first matching terrain cell in stable row-major order. */
static bool resolve_terrain_entry(struct chunk *c,
		enum world_entry_kind kind, struct loc *grid)
{
	int x, y;

	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			struct loc candidate = loc(x, y);
			bool matches = (kind == WORLD_ENTRY_UP_STAIR) ?
				square_isupstairs(c, candidate) :
				square_isdownstairs(c, candidate);

			if (matches) {
				*grid = candidate;
				return true;
			}
		}
	}
	return false;
}

/** Resolve one named entry to its exact cell without testing occupancy. */
enum world_entry_result world_entry_locate(struct chunk *c, const char *id,
		struct loc *grid)
{
	const struct level *level;
	const struct world_entry *entry;
	struct loc resolved;

	if (!c || !id || !grid || !world_id_is_valid(id)) {
		return WORLD_ENTRY_INVALID;
	}
	level = world_chunk_level(c);
	if (!level || c->is_known || !streq(c->location_id, level->id)) {
		return WORLD_ENTRY_WRONG_LOCATION;
	}
	entry = world_entry_by_id(level, id);
	if (!entry) return WORLD_ENTRY_NOT_FOUND;

	if (entry->kind == WORLD_ENTRY_GRID) {
		enum world_edge_side side = edge_side_from_entry(level, entry);

		resolved = side != WORLD_EDGE_NONE ? edge_arrival_grid(c, side) :
			entry->grid;
	} else if (entry->kind == WORLD_ENTRY_UP_STAIR ||
			entry->kind == WORLD_ENTRY_DOWN_STAIR) {
		if (!resolve_terrain_entry(c, entry->kind, &resolved)) {
			return WORLD_ENTRY_NO_MATCH;
		}
	} else {
		return WORLD_ENTRY_INVALID;
	}

	if (!square_in_bounds_fully(c, resolved)) return WORLD_ENTRY_INVALID_CELL;
	*grid = resolved;
	return WORLD_ENTRY_OK;
}

/**
 * Test whether a cell belongs to a named entry.  Fixed entries contain one
 * cell, except surface gates which accept every cell adjacent to their exit
 * band. Terrain entries contain every matching stair; locating one of those
 * entries still selects a deterministic member for arrival.
 */
bool world_entry_contains(struct chunk *c, const char *id, struct loc grid)
{
	const struct level *level;
	const struct world_entry *entry;

	if (!c || !id || !world_id_is_valid(id) ||
			!square_in_bounds_fully(c, grid)) {
		return false;
	}
	level = world_chunk_level(c);
	if (!level || c->is_known || !streq(c->location_id, level->id)) {
		return false;
	}
	entry = world_entry_by_id(level, id);
	if (!entry) return false;

	switch (entry->kind) {
		case WORLD_ENTRY_GRID: {
			enum world_edge_side side = edge_side_from_entry(level, entry);

			return side != WORLD_EDGE_NONE ?
				edge_can_activate_from(c, side, grid) :
				loc_eq(grid, entry->grid);
		}
		case WORLD_ENTRY_UP_STAIR:
			return square_isupstairs(c, grid);
		case WORLD_ENTRY_DOWN_STAIR:
			return square_isdownstairs(c, grid);
		default:
			return false;
	}
}

bool world_entry_is_edge_route_entry(const struct level *level,
		const struct world_entry *entry)
{
	return edge_side_from_entry(level, entry) != WORLD_EDGE_NONE;
}

/** Resolve one named entry to an exact, currently safe arrival cell. */
enum world_entry_result world_entry_resolve(struct chunk *c, const char *id,
		struct loc *arrival)
{
	struct loc resolved;
	enum world_entry_result result;

	if (!arrival) return WORLD_ENTRY_INVALID;
	result = world_entry_locate(c, id, &resolved);
	if (result != WORLD_ENTRY_OK) return result;
	if (!square_isarrivable(c, resolved)) return WORLD_ENTRY_INVALID_CELL;
	*arrival = resolved;
	return WORLD_ENTRY_OK;
}

/** Whether a fixed cell is the source of an ordinary adjacent-region route. */
bool world_entry_is_edge_exit_grid(const struct chunk *c, struct loc grid)
{
	const struct level *level;
	const struct world_entry *entry;

	if (!c || grid.x <= 0 || grid.y <= 0 || grid.x >= c->width - 1 ||
			grid.y >= c->height - 1) {
		return false;
	}
	level = world_chunk_level(c);
	if (!level || !streq(c->location_id, level->id)) return false;
	for (entry = level->entries; entry; entry = entry->next) {
		enum world_edge_side side = edge_side_from_entry(level, entry);

		if (side != WORLD_EDGE_NONE &&
				edge_band_contains(c, side, grid, false)) return true;
	}
	return false;
}

/** Reserve both the automatic band and its safe arrival band for travel. */
bool world_entry_is_overworld_gate_grid(const struct chunk *c, struct loc grid)
{
	const struct level *level = c ? world_chunk_level(c) : NULL;
	const struct world_entry *entry;
	if (!level || !square_in_bounds_fully(c, grid)) return false;
	for (entry = level->entries; entry; entry = entry->next) {
		enum world_edge_side side = edge_side_from_entry(level, entry);
		if (side != WORLD_EDGE_NONE &&
				(edge_band_contains(c, side, grid, false) ||
				 edge_band_contains(c, side, grid, true))) return true;
	}
	return false;
}

/** Find the route activated by entering one authored automatic-travel band. */
const struct world_route *world_entry_edge_route_for_step(
		const struct chunk *c, struct loc from, struct loc to)
{
	const struct level *level;
	const struct world_entry *entry;

	if (!c || from.x <= 0 || from.y <= 0 || from.x >= c->width - 1 ||
			from.y >= c->height - 1 || to.x <= 0 || to.y <= 0 ||
			to.x >= c->width - 1 || to.y >= c->height - 1 ||
			distance(from, to) != 1) {
		return NULL;
	}
	level = world_chunk_level(c);
	if (!level || !streq(c->location_id, level->id)) return NULL;
	for (entry = level->entries; entry; entry = entry->next) {
		enum world_edge_side side = edge_side_from_entry(level, entry);

		if (side != WORLD_EDGE_NONE &&
				edge_band_contains(c, side, to, false)) {
			return edge_route_from_entry(level, entry);
		}
	}
	return NULL;
}

/** Find the adjacent-region route selected by walking out through an edge. */
const struct world_route *world_entry_edge_route_for_direction(
		const struct chunk *c, struct loc grid, int dir)
{
	if (!c || dir < 1 || dir > 9 || dir == 5) return NULL;
	return world_entry_edge_route_for_step(c, grid,
		loc_sum(grid, ddgrid[dir]));
}

/**
 * Treat the familiar stair keys as activation keys for directional boundary
 * glyphs.  `>` activates east and south exits; `<` activates west and north.
 */
const struct world_route *world_entry_edge_route_for_stair_key(
		const struct chunk *c, struct loc grid, bool down)
{
	int dir;

	if (!c) return NULL;
	if (down) {
		dir = world_entry_edge_route_for_direction(c, grid, 6) ? 6 :
			(world_entry_edge_route_for_direction(c, grid, 2) ? 2 : 0);
	} else {
		dir = world_entry_edge_route_for_direction(c, grid, 4) ? 4 :
			(world_entry_edge_route_for_direction(c, grid, 8) ? 8 : 0);
	}
	return dir ? world_entry_edge_route_for_direction(c, grid, dir) : NULL;
}

/** Resolve a terrain label from authored routes using only this chunk's
 * knowledge. Unlike travel resolution, remembered chunks are accepted. */
const struct level *world_entry_known_destination(struct chunk *c,
		struct loc grid)
{
	const struct level *level = world_chunk_level(c), *found = NULL;
	const struct world_route *route;
	int feat;

	if (!level || !square_in_bounds_fully(c, grid)) return NULL;
	feat = square(c, grid)->feat;
	if (feat != FEAT_LESS && feat != FEAT_MORE && feat != FEAT_HOLE)
		return NULL;
	for (route = world_routes; route; route = route->next) {
		const struct world_entry *entry;
		const struct level *destination;
		bool matches;

		if (!streq(route->from, level->id) || world_route_is_retired(route) ||
				(!streq(route->kind, "stairs") && !world_route_is_cross_mode(route)))
			continue;
		entry = world_entry_by_id(level, route->from_entry);
		if (!entry) continue;
		matches = entry->kind == WORLD_ENTRY_GRID ?
			(feat == FEAT_HOLE && loc_eq(entry->grid, grid)) :
			(entry->kind == WORLD_ENTRY_UP_STAIR ? feat == FEAT_LESS :
				feat == FEAT_MORE);
		if (!matches) continue;
		destination = level_by_id(route->to);
		if (found && found != destination) return NULL;
		found = destination;
	}
	return found;
}

/** Find a declared zero-time stair route beneath the player. */
const struct world_route *world_entry_stair_route_here(
		struct chunk *c, struct loc grid)
{
	const struct level *level;
	struct world_route *route;

	if (!c || c->is_known || !square_in_bounds_fully(c, grid)) return NULL;
	level = world_chunk_level(c);
	if (!level || !streq(c->location_id, level->id)) return NULL;
	for (route = world_routes; route; route = route->next) {
		if (streq(route->from, level->id) && streq(route->kind, "stairs") &&
				route->travel_turns == 0 &&
				world_entry_contains(c, route->from_entry, grid)) {
			return route;
		}
	}
	return NULL;
}

/** Find one unambiguous authored mode boundary beneath the top-down player. */
const struct world_route *world_entry_cross_mode_route_here(
		struct chunk *c, struct loc grid)
{
	const struct level *level;
	const struct world_route *found = NULL;
	struct world_route *route;

	if (!c || c->is_known || !square_in_bounds_fully(c, grid)) return NULL;
	level = world_chunk_level(c);
	if (!level || level->mode != WORLD_MODE_TOP_DOWN ||
			!streq(c->location_id, level->id)) {
		return NULL;
	}
	for (route = world_routes; route; route = route->next) {
		if (!streq(route->from, level->id) ||
				!world_route_is_cross_mode(route) ||
				!world_entry_contains(c, route->from_entry, grid)) {
			continue;
		}
		if (found) return NULL;
		found = route;
	}
	return found;
}

void world_entry_retire_cross_mode_sources(struct chunk *c,
		const struct level *level)
{
	const struct world_route *route;
	if (!c || !level || level->mode != WORLD_MODE_TOP_DOWN ||
			!streq(c->location_id, level->id)) return;
	for (route = world_routes; route; route = route->next) {
		const struct level *destination = level_by_id(route->to);
		const struct world_entry *entry;
		const struct world_route *active;
		bool reused = false;
		if (!world_route_is_retired(route) || !streq(route->from, level->id) ||
				!destination || destination->mode != WORLD_MODE_SPELUNKING) continue;
		entry = world_entry_by_id(level, route->from_entry);
		if (!entry || entry->kind != WORLD_ENTRY_GRID ||
				!square_in_bounds(c, entry->grid)) continue;
		for (active = world_routes; active; active = active->next) {
			if (streq(active->from, level->id) &&
					streq(active->from_entry, entry->id) &&
					world_route_is_cross_mode(active)) reused = true;
		}
		if (!reused && square(c, entry->grid)->feat == FEAT_HOLE) {
			square_set_feat(c, entry->grid, FEAT_FLOOR);
			sqinfo_off(square(c, entry->grid)->info, SQUARE_NO_STAIRS);
		}
	}
}

/** Ensure every fixed top-down shaft source is visible and physically usable. */
bool world_entry_materialize_cross_mode_sources(struct chunk *c,
		const struct level *level)
{
	struct world_route *route;

	if (!c || !level || c->is_known || level->mode != WORLD_MODE_TOP_DOWN) {
		return false;
	}
	world_entry_retire_cross_mode_sources(c, level);
	for (route = world_routes; route; route = route->next) {
		const struct world_entry *entry;
		const struct level *destination;

		if (!streq(route->from, level->id) ||
				!world_route_is_cross_mode(route)) {
			continue;
		}
		destination = level_by_id(route->to);
		entry = world_entry_by_id(level, route->from_entry);
		if (!destination || destination->mode != WORLD_MODE_SPELUNKING ||
				!entry || entry->kind != WORLD_ENTRY_GRID ||
				!connect_shaft_source(c, entry->grid)) {
			return false;
		}
		square_set_feat(c, entry->grid, FEAT_HOLE);
		/* A freshly generated satellite places its authored return stair after
		 * this pass.  Keep the random player-start search from selecting the
		 * shaft and replacing the only return stair with it. */
		sqinfo_on(square(c, entry->grid)->info, SQUARE_NO_STAIRS);
	}
	return true;
}

/**
 * Carve every active surface boundary as a visible, passable travel band.
 * The adjacent inner band is the physical source and safe arrival area, so a
 * completed crossing never leaves the player standing on an automatic tile.
 */
bool world_entry_materialize_overworld_gates(struct chunk *c,
		const struct level *level)
{
	const struct world_entry *entry;
	struct loc grid;

	if (!c || !level || c->is_known || !level->is_overworld_cell ||
			!streq(c->location_id, level->id)) {
		return false;
	}
	for (entry = level->entries; entry; entry = entry->next) {
		enum world_edge_side side = edge_side_from_entry(level, entry);

		if (side == WORLD_EDGE_NONE) continue;
		for (grid.y = 1; grid.y < c->height - 1; grid.y++) {
			for (grid.x = 1; grid.x < c->width - 1; grid.x++) {
				if (!edge_band_contains(c, side, grid, false) &&
						!edge_band_contains(c, side, grid, true)) {
					continue;
				}
				/* Gate refresh must never destroy a dungeon connection. Town
				 * reconciliation relocates legacy conflicts before this pass. */
				if (square_isstairs(c, grid)) continue;
				square_set_feat(c, grid, FEAT_FLOOR);
				sqinfo_off(square(c, grid)->info, SQUARE_WATER);
				sqinfo_off(square(c, grid)->info, SQUARE_WOOD);
				sqinfo_on(square(c, grid)->info, SQUARE_ROAD);
			}
		}
	}
	return true;
}
