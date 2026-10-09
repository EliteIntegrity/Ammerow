/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-location.c
 * \brief Stable world identity, discovery, routes, and transitions.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "world-entry.h"

/** Find a site by its immutable ID. */
struct world_site *world_site_by_id(const char *id)
{
	struct world_site *site = world_sites;

	if (!id || !id[0]) return NULL;
	while (site) {
		if (streq(site->id, id)) break;
		site = site->next;
	}
	return site;
}

/** Stable site presentation shared by every local floor at that place. */
const char *world_level_area_name(const struct level *lev)
{
	const struct world_site *site = lev ? world_site_by_id(lev->site_id) : NULL;

	if (site && site->name && site->name[0]) return site->name;
	if (lev && lev->name && lev->name[0]) return lev->name;
	return "Unknown place";
}

/** Format spatial progress without presenting surface danger as depth. */
void world_level_format_position(const struct level *lev, char *buf,
		size_t size)
{
	if (!buf || !size) return;
	if (!lev) {
		my_strcpy(buf, "Unknown", size);
	} else if (lev->is_overworld_cell) {
		if (lev->danger > 0) strnfmt(buf, size, "Danger %d", lev->danger);
		else my_strcpy(buf, "Safe", size);
	} else if (!lev->has_legacy_depth && lev->local_floor > 0) {
		strnfmt(buf, size, "L%d Danger %d", lev->local_floor, lev->danger);
	} else if (lev->has_legacy_depth && lev->local_floor > 0) {
		strnfmt(buf, size, "%d' (L%d)", lev->local_floor * 50,
			lev->local_floor);
	} else if (lev->danger > 0) {
		strnfmt(buf, size, "Danger %d", lev->danger);
	} else {
		my_strcpy(buf, "Surface", size);
	}
}

/** Only ordinary dungeon floors contribute to maximum delve progress. */
bool world_level_tracks_dungeon_depth(const struct level *lev)
{
	return lev && lev->has_legacy_depth &&
		lev->kind == WORLD_LOCATION_DUNGEON && lev->local_floor > 0;
}

/** Find a level by its immutable location ID. */
struct level *level_by_id(const char *id)
{
	struct level *lev = world;

	if (!id || !id[0]) return NULL;
	while (lev) {
		if (lev->id && streq(lev->id, id)) break;
		lev = lev->next;
	}
	return lev;
}

/** Find a route by its immutable route ID. */
struct world_route *world_route_by_id(const char *id)
{
	struct world_route *route = world_routes;

	if (!id || !id[0]) return NULL;
	while (route) {
		if (streq(route->id, id)) break;
		route = route->next;
	}
	return route;
}

/** Find a travel node by its immutable ID. */
struct world_travel_node *world_travel_node_by_id(const char *id)
{
	struct world_travel_node *node = world_travel_nodes;

	if (!id || !id[0]) return NULL;
	while (node) {
		if (streq(node->id, id)) break;
		node = node->next;
	}
	return node;
}

/** Find the unique travel node attached to a named location entry. */
struct world_travel_node *world_travel_node_by_endpoint(
		const char *location_id, const char *entry_id)
{
	struct world_travel_node *node = world_travel_nodes;

	if (!world_id_is_valid(location_id) || !world_id_is_valid(entry_id)) {
		return NULL;
	}
	while (node) {
		if (streq(node->location_id, location_id) &&
				streq(node->entry_id, entry_id)) {
			return node;
		}
		node = node->next;
	}
	return NULL;
}

/**
 * Check a stable world identifier.  IDs are lower-case ASCII components
 * separated by single dots.  Hyphen and underscore are permitted within a
 * component so future content need not encode presentation names.
 */
bool world_id_is_valid(const char *id)
{
	const char *cursor;
	size_t length;
	bool previous_dot = false;

	if (!id || !id[0]) return false;
	length = strlen(id);
	if (length >= WORLD_ID_LEN || id[0] == '.' || id[length - 1] == '.') {
		return false;
	}

	for (cursor = id; *cursor; cursor++) {
		char c = *cursor;

		if (c == '.') {
			if (previous_dot) return false;
			previous_dot = true;
			continue;
		}
		previous_dot = false;
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				c == '-' || c == '_')) {
			return false;
		}
	}
	return true;
}

/** Find an ID in a sorted per-run ledger, or its insertion point. */
static size_t world_ledger_position(const struct world_id_ledger *ledger,
		const char *id, bool *found)
{
	size_t low = 0;
	size_t high = ledger ? ledger->count : 0;

	while (low < high) {
		size_t middle = low + (high - low) / 2;
		int comparison = strcmp(ledger->ids[middle], id);

		if (comparison < 0) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	*found = ledger && low < ledger->count &&
		streq(ledger->ids[low], id);
	return low;
}

static bool world_ledger_contains(const struct world_id_ledger *ledger,
		const char *id)
{
	bool found;

	if (!ledger || !world_id_is_valid(id)) return false;
	(void)world_ledger_position(ledger, id, &found);
	return found;
}

static bool world_ledger_add(struct world_id_ledger *ledger, const char *id)
{
	bool found;
	size_t position;

	if (!ledger || !world_id_is_valid(id)) return false;
	position = world_ledger_position(ledger, id, &found);
	if (found) return true;
	if (ledger->count >= WORLD_LEDGER_LIMIT) return false;

	if (ledger->count == ledger->capacity) {
		uint16_t capacity = ledger->capacity ? ledger->capacity * 2 : 8;

		if (capacity > WORLD_LEDGER_LIMIT || capacity < ledger->capacity) {
			capacity = WORLD_LEDGER_LIMIT;
		}
		ledger->ids = mem_realloc(ledger->ids,
			capacity * sizeof(*ledger->ids));
		ledger->capacity = capacity;
	}
	if (position < ledger->count) {
		memmove(&ledger->ids[position + 1], &ledger->ids[position],
			(ledger->count - position) * sizeof(*ledger->ids));
	}
	ledger->ids[position] = string_make(id);
	ledger->count++;
	return true;
}

static void world_ledger_clear(struct world_id_ledger *ledger)
{
	uint16_t i;

	if (!ledger) return;
	for (i = 0; i < ledger->count; i++) {
		string_free(ledger->ids[i]);
	}
	mem_free(ledger->ids);
	memset(ledger, 0, sizeof(*ledger));
}

bool world_player_has_discovered_location(const struct player *p,
		const char *id)
{
	return p && world_ledger_contains(&p->discovered_locations, id);
}

bool world_player_discover_location(struct player *p, const char *id)
{
	if (!p || !level_by_id(id)) return false;
	return world_ledger_add(&p->discovered_locations, id);
}

bool world_player_route_is_unlocked(const struct player *p, const char *id)
{
	return p && world_ledger_contains(&p->unlocked_routes, id);
}

bool world_player_unlock_route(struct player *p, const char *id)
{
	if (!p || !world_route_by_id(id)) return false;
	return world_ledger_add(&p->unlocked_routes, id);
}

/** Discard any unfinished observation of an ordinary physical journey. */
void world_player_cancel_physical_route(struct player *p)
{
	if (!p || !p->upkeep) return;
	p->upkeep->physical_route.route_id[0] = '\0';
}

/**
 * Begin observing an ordinary physical journey from a cell in the declared
 * source entry.  This is deliberately separate from fast travel: only a real
 * movement command calls it.
 */
bool world_player_begin_physical_route(struct player *p, struct chunk *c,
		const char *destination_id)
{
	struct world_route *route = world_routes;
	const struct level *source;

	if (!p || !p->upkeep) return false;
	world_player_cancel_physical_route(p);
	if (!c || c->is_known || !level_by_id(destination_id)) return false;
	source = world_player_level(p);
	if (!source || !streq(c->location_id, source->id)) return false;

	/* Using a declared endpoint counts as physically reaching its node. */
	(void)world_player_activate_travel_nodes_here(p, c);
	while (route) {
		if (!world_route_is_retired(route) &&
				route->travel_turns == 0 &&
				(streq(route->kind, "edge") ||
				 streq(route->kind, "stairs")) &&
				streq(route->from, source->id) &&
				streq(route->to, destination_id) &&
				world_entry_contains(c, route->from_entry, p->grid)) {
			my_strcpy(p->upkeep->physical_route.route_id, route->id,
				sizeof(p->upkeep->physical_route.route_id));
			return true;
		}
		route = route->next;
	}
	return false;
}

/**
 * Finish an observed physical journey only at a cell belonging to its
 * declared destination entry.  The pending observation is one-shot and is
 * never save state.
 */
bool world_player_finish_physical_route(struct player *p, struct chunk *c)
{
	struct world_route *route;
	bool matched = false;

	if (!p || !p->upkeep || !p->upkeep->physical_route.route_id[0]) {
		return false;
	}
	route = world_route_by_id(p->upkeep->physical_route.route_id);
	if (route && c && !c->is_known &&
			streq(p->world_location.id, route->to) &&
			streq(c->location_id, route->to) &&
			world_entry_contains(c, route->to_entry, p->grid)) {
		matched = world_player_unlock_route(p, route->id);
	}
	world_player_cancel_physical_route(p);
	return matched;
}

bool world_player_has_activated_travel_node(const struct player *p,
		const char *id)
{
	return p && world_ledger_contains(&p->activated_travel_nodes, id);
}

/** Record an explicitly reached or granted travel endpoint. */
bool world_player_activate_travel_node(struct player *p, const char *id)
{
	if (!p || !world_travel_node_by_id(id)) return false;
	return world_ledger_add(&p->activated_travel_nodes, id);
}

/**
 * Reconstruct endpoint activation for a version-two development save.  Such
 * saves recorded completed ordinary routes but predated the travel-node
 * ledger.  A completed physical route proves that both of its exact endpoints
 * were occupied; aggregate routes and explicit grants deliberately prove
 * nothing here.
 */
unsigned int world_player_backfill_travel_nodes(struct player *p)
{
	struct world_route *route = world_routes;
	unsigned int activated = 0;

	if (!p) return 0;
	while (route) {
		struct world_travel_node *source;
		struct world_travel_node *destination;
		bool had_source;
		bool had_destination;

		if ((!world_route_is_physical(route) &&
				!(world_route_is_retired(route) &&
				  route->travel_turns == 0)) ||
				!world_player_route_is_unlocked(p, route->id)) {
			route = route->next;
			continue;
		}
		source = world_travel_node_by_endpoint(route->from,
			route->from_entry);
		destination = world_travel_node_by_endpoint(route->to,
			route->to_entry);
		had_source = source && world_player_has_activated_travel_node(p,
			source->id);
		had_destination = destination &&
			world_player_has_activated_travel_node(p, destination->id);
		if (source && world_player_activate_travel_node(p, source->id) &&
				!had_source) {
			activated++;
		}
		if (destination && world_player_activate_travel_node(p,
				destination->id) && !had_destination) {
			activated++;
		}
		route = route->next;
	}
	return activated;
}

/** Activate a node only when the player occupies one of its runtime cells. */
bool world_player_activate_travel_node_here(struct player *p,
		struct chunk *c, const char *id)
{
	struct world_travel_node *node = world_travel_node_by_id(id);
	const struct level *level;

	if (!p || !c || !node || c->is_known) return false;
	level = world_player_level(p);
	if (!level || !streq(level->id, node->location_id) ||
			!streq(c->location_id, node->location_id) ||
			!world_entry_contains(c, node->entry_id, p->grid)) {
		return false;
	}
	return world_player_activate_travel_node(p, node->id);
}

/**
 * Unlock authored aggregate routes whose two endpoints have both been
 * physically activated.  This is derived from durable node discovery, so it
 * also upgrades older saves after new convenience routes are added to data.
 */
unsigned int world_player_unlock_activated_travel_routes(struct player *p)
{
	struct world_route *route = world_routes;
	unsigned int unlocked = 0;

	if (!p) return 0;
	while (route) {
		struct world_travel_node *source;
		struct world_travel_node *destination;

		if (!world_route_is_aggregate(route)) {
			route = route->next;
			continue;
		}
		source = world_travel_node_by_endpoint(route->from,
			route->from_entry);
		destination = world_travel_node_by_endpoint(route->to,
			route->to_entry);
		if (source && destination &&
				world_player_has_activated_travel_node(p, source->id) &&
				world_player_has_activated_travel_node(p, destination->id) &&
				world_player_unlock_route(p, route->id)) {
			unlocked++;
		}
		route = route->next;
	}
	return unlocked;
}

/** Activate every declared endpoint containing the player's current cell. */
unsigned int world_player_activate_travel_nodes_here(struct player *p,
		struct chunk *c)
{
	struct world_travel_node *node = world_travel_nodes;
	unsigned int activated = 0;

	while (node) {
		bool had_node = world_player_has_activated_travel_node(p, node->id);

		if (world_player_activate_travel_node_here(p, c, node->id) &&
				!had_node) {
			activated++;
		}
		node = node->next;
	}
	(void)world_player_unlock_activated_travel_routes(p);
	return activated;
}

/** Release the bounded dynamic world ledgers owned by a player. */
void world_player_clear_ledgers(struct player *p)
{
	if (!p) return;
	world_ledger_clear(&p->discovered_locations);
	world_ledger_clear(&p->unlocked_routes);
	world_ledger_clear(&p->activated_travel_nodes);
}

/** Return the stable level that owns a chunk. */
const struct level *world_chunk_level(const struct chunk *c)
{
	if (!c || !world_id_is_valid(c->location_id)) return NULL;
	return level_by_id(c->location_id);
}

/** Assign and validate a stable location and actual/known role for a chunk. */
bool world_chunk_set_location(struct chunk *c, const char *id, bool is_known)
{
	struct level *lev = level_by_id(id);

	if (!c || !lev || c->depth != lev->danger) return false;
	my_strcpy(c->location_id, lev->id, sizeof(c->location_id));
	c->is_known = is_known;
	return true;
}

/** Migrate an old chunk whose only durable identity was its depth. */
bool world_chunk_restore_location(struct chunk *c)
{
	struct level *lev;

	if (!c) return false;
	if (c->location_id[0]) {
		lev = level_by_id(c->location_id);
		if (!lev) return false;
		/* Stable identity is authoritative for non-legacy places.  This makes
		 * authored danger a safely tunable balance value between releases. */
		if (c->depth != lev->danger) {
			if (lev->has_legacy_depth) return false;
			c->depth = lev->danger;
		}
		return world_chunk_set_location(c, c->location_id, c->is_known);
	}
	lev = level_by_depth(c->depth);
	return lev && world_chunk_set_location(c, lev->id, c->is_known);
}

/**
 * Validate or migrate all runtime chunk ownership after loading a save.  The
 * current pair must match the player's committed location.  Stored chunks
 * are unique by location and actual/known role.
 */
bool world_player_restore_chunks(const struct player *p,
		struct chunk *actual, struct chunk *known,
		struct chunk *const *stored, uint16_t stored_count)
{
	uint16_t i, j;
	bool arena;

	if (!p) return false;
	if (p->is_dead) return true;
	if (!actual || !known || !world_id_is_valid(p->world_location.id)) {
		return false;
	}

	arena = actual->name && streq(actual->name, "arena");
	if (arena) {
		/* The transient arena is not a stable world location. */
		if (actual->location_id[0] || known->location_id[0]) return false;
		actual->is_known = false;
		known->is_known = true;
	} else {
		if (!actual->location_id[0]) {
			if (!world_chunk_set_location(actual, p->world_location.id,
					false)) {
				return false;
			}
		} else {
			if (!streq(actual->location_id, p->world_location.id)) return false;
			actual->is_known = false;
			if (!world_chunk_restore_location(actual)) return false;
		}

		if (!known->location_id[0]) {
			if (!world_chunk_set_location(known, p->world_location.id, true)) {
				return false;
			}
		} else {
			if (!streq(known->location_id, p->world_location.id)) return false;
			known->is_known = true;
			if (!world_chunk_restore_location(known)) return false;
		}
	}

	if (actual->height != known->height || actual->width != known->width) {
		return false;
	}
	for (i = 0; i < stored_count; i++) {
		if (!stored || !stored[i]) return false;
		if (!stored[i]->location_id[0] && stored[i]->name &&
				suffix(stored[i]->name, " known")) {
			stored[i]->is_known = true;
		}
		if (!world_chunk_restore_location(stored[i])) {
			return false;
		}
		if (!arena &&
				streq(stored[i]->location_id, actual->location_id)) {
			return false;
		}
		for (j = 0; j < i; j++) {
			if (stored[j]->is_known == stored[i]->is_known &&
					streq(stored[j]->location_id,
						stored[i]->location_id)) {
				return false;
			}
		}
	}
	return true;
}

/**
 * Return the current level record, preferring the stable saved identity and
 * falling back to depth only for old saves during migration.
 */
const struct level *world_player_level(const struct player *p)
{
	struct level *lev;

	if (!p) return NULL;
	if (p->world_location.id[0]) {
		return level_by_id(p->world_location.id);
	}
	lev = level_by_depth(p->depth);
	return lev;
}

/** Set the player's stable location and its named arrival entry. */
bool world_player_set_location(struct player *p, const char *id,
		const char *entry)
{
	struct level *lev = level_by_id(id);
	const char *resolved_entry = (entry && entry[0]) ? entry : "default";

	if (!p || !lev || lev->danger < 0 || lev->danger > INT16_MAX ||
			!world_id_is_valid(resolved_entry)) {
		return false;
	}
	if (!world_player_discover_location(p, lev->id)) return false;
	my_strcpy(p->world_location.id, lev->id,
		sizeof(p->world_location.id));
	my_strcpy(p->world_location.entry, resolved_entry,
		sizeof(p->world_location.entry));
	p->depth = (int16_t)lev->danger;
	return true;
}

/** Set a location through the legacy linear depth map. */
bool world_player_set_location_by_depth(struct player *p, int depth,
		const char *entry)
{
	struct level *lev = level_by_depth(depth);

	return lev ? world_player_set_location(p, lev->id, entry) : false;
}

/** Validate a loaded stable identity, or migrate a save that only has depth. */
bool world_player_restore_location(struct player *p)
{
	struct level *lev;

	if (!p) return false;
	if (!p->world_location.id[0]) {
		return world_player_set_location_by_depth(p, p->depth, "default");
	}
	if (!world_id_is_valid(p->world_location.id) ||
			!world_id_is_valid(p->world_location.entry)) {
		return false;
	}

	lev = level_by_id(p->world_location.id);
	if (!lev) return false;
	if (lev->danger != p->depth) {
		if (lev->has_legacy_depth) return false;
		p->depth = (int16_t)lev->danger;
	}
	return world_player_discover_location(p, lev->id);
}
