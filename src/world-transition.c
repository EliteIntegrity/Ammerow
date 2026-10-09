/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-transition.c
 * \brief Transaction boundary for movement between stable world locations.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "world-entry.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-system.h"

/** Whether a route represents one ordinary, zero-skipped-time movement. */
bool world_route_is_physical(const struct world_route *route)
{
	const struct level *from;
	const struct level *to;

	if (!route || route->travel_turns != 0) return false;
	if (streq(route->kind, "edge")) return true;
	if (!streq(route->kind, "stairs")) return false;
	from = level_by_id(route->from);
	to = level_by_id(route->to);
	return from && to && (!from->has_legacy_depth || !to->has_legacy_depth);
}

/** Whether a route is one zero-time boundary between the two launch modes. */
bool world_route_is_cross_mode(const struct world_route *route)
{
	const struct level *from;
	const struct level *to;

	if (!route || !streq(route->kind, "shaft") || route->travel_turns != 0) {
		return false;
	}
	from = level_by_id(route->from);
	to = level_by_id(route->to);
	return from && to && from->mode != to->mode &&
		(from->mode == WORLD_MODE_SPELUNKING ||
		 to->mode == WORLD_MODE_SPELUNKING);
}

bool world_route_is_recall(const struct world_route *route)
{
	const struct level *from;
	const struct level *to;
	if (!route || !streq(route->kind, "recall") || route->travel_turns != 0)
		return false;
	from = level_by_id(route->from);
	to = level_by_id(route->to);
	return from && to && from->mode == WORLD_MODE_SPELUNKING &&
		to->mode == WORLD_MODE_TOP_DOWN && to->kind == WORLD_LOCATION_HUB;
}

/** Whether a route represents an approved aggregate-time journey. */
bool world_route_is_aggregate(const struct world_route *route)
{
	return route && streq(route->kind, "trail") && route->travel_turns > 0;
}

/** A save-compatibility identity which must never be offered or executed. */
bool world_route_is_retired(const struct world_route *route)
{
	return route && streq(route->kind, "retired");
}

/**
 * Validate a directed route and create an immutable transition ticket.  This
 * deliberately performs no player mutation or time advancement.
 */
enum world_transition_result world_transition_prepare_route(
		const struct player *p, const char *route_id, bool require_unlocked,
		struct world_transition *transition)
{
	const struct level *source;
	const struct level *destination;
	struct world_route *route;

	if (!p || !transition || !world_id_is_valid(route_id)) {
		return WORLD_TRANSITION_INVALID;
	}
	memset(transition, 0, sizeof(*transition));
	route = world_route_by_id(route_id);
	if (!route) return WORLD_TRANSITION_UNKNOWN_ROUTE;
	if (world_route_is_retired(route)) return WORLD_TRANSITION_LOCKED;
	source = world_player_level(p);
	if (!source || !streq(source->id, route->from)) {
		return WORLD_TRANSITION_WRONG_SOURCE;
	}
	if ((require_unlocked || route->requires_unlock) &&
			!world_player_route_is_unlocked(p, route_id)) {
		return WORLD_TRANSITION_LOCKED;
	}
	destination = level_by_id(route->to);
	if (!destination || !world_id_is_valid(route->to_entry)) {
		return WORLD_TRANSITION_UNKNOWN_DESTINATION;
	}

	my_strcpy(transition->route_id, route->id,
		sizeof(transition->route_id));
	my_strcpy(transition->source_id, source->id,
		sizeof(transition->source_id));
	my_strcpy(transition->destination_id, destination->id,
		sizeof(transition->destination_id));
	my_strcpy(transition->destination_entry, route->to_entry,
		sizeof(transition->destination_entry));
	transition->destination_danger = destination->danger;
	transition->travel_turns = route->travel_turns;
	transition->requires_unlocked_route = require_unlocked ||
		route->requires_unlock;
	return WORLD_TRANSITION_OK;
}

/**
 * Prepare the stricter fast-travel form of a route.  Both endpoints must have
 * been activated.  Aggregate trails may be started from any safe position in
 * their source location: the node records the learned stop, not a square the
 * player must repeatedly hunt down.  Other route kinds retain exact-entry
 * occupancy for defensive callers and tests.
 */
enum world_transition_result world_transition_prepare_travel(
		const struct player *p, struct chunk *actual, const char *route_id,
		struct world_transition *transition)
{
	enum world_transition_result result;
	struct world_transition prepared;
	struct world_route *route;
	struct world_travel_node *source_node;
	struct world_travel_node *destination_node;
	struct loc source_representative;

	if (!transition) return WORLD_TRANSITION_INVALID;
	memset(transition, 0, sizeof(*transition));
	result = world_transition_prepare_route(p, route_id, true, &prepared);
	if (result != WORLD_TRANSITION_OK) return result;
	route = world_route_by_id(route_id);
	source_node = world_travel_node_by_endpoint(route->from,
		route->from_entry);
	destination_node = world_travel_node_by_endpoint(route->to,
		route->to_entry);
	if (!source_node || !destination_node) {
		return WORLD_TRANSITION_UNKNOWN_TRAVEL_NODE;
	}
	if (!world_player_has_activated_travel_node(p, source_node->id)) {
		return WORLD_TRANSITION_SOURCE_NODE_INACTIVE;
	}
	if (!world_player_has_activated_travel_node(p, destination_node->id)) {
		return WORLD_TRANSITION_DESTINATION_NODE_INACTIVE;
	}
	if (!actual || actual->is_known ||
			!streq(actual->location_id, route->from) ||
			world_entry_locate(actual, route->from_entry,
				&source_representative) != WORLD_ENTRY_OK) {
		return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
	}
	if (!world_route_is_aggregate(route) &&
			!world_entry_contains(actual, route->from_entry, p->grid)) {
		return WORLD_TRANSITION_NOT_AT_SOURCE_NODE;
	}

	prepared.requires_activated_nodes = true;
	my_strcpy(prepared.source_node_id, source_node->id,
		sizeof(prepared.source_node_id));
	my_strcpy(prepared.destination_node_id, destination_node->id,
		sizeof(prepared.destination_node_id));
	*transition = prepared;
	return WORLD_TRANSITION_OK;
}

/**
 * Prepare an ordinary boundary crossing.  Unlike fast travel, the route need
 * not be unlocked and its destination node need not have been visited.  The
 * player must still occupy the authored source entry and only zero-time edge
 * or stair routes can use this path.
 */
enum world_transition_result world_transition_prepare_physical(
		const struct player *p, struct chunk *actual, const char *route_id,
		struct world_transition *transition)
{
	enum world_transition_result result;
	struct world_transition prepared;
	struct world_route *route;
	struct loc source_representative;

	if (!transition) return WORLD_TRANSITION_INVALID;
	memset(transition, 0, sizeof(*transition));
	result = world_transition_prepare_route(p, route_id, false, &prepared);
	if (result != WORLD_TRANSITION_OK) return result;
	route = world_route_by_id(route_id);
	if (!world_route_is_physical(route)) {
		return WORLD_TRANSITION_WRONG_ROUTE_KIND;
	}
	if (!actual || actual->is_known ||
			!streq(actual->location_id, route->from) ||
			world_entry_locate(actual, route->from_entry,
				&source_representative) != WORLD_ENTRY_OK) {
		return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
	}
	if (!world_entry_contains(actual, route->from_entry, p->grid)) {
		return WORLD_TRANSITION_NOT_AT_SOURCE_NODE;
	}

	*transition = prepared;
	return WORLD_TRANSITION_OK;
}

/**
 * Prepare a shaft crossing from the exact coordinate in the active mode.
 * Generated side-view coordinates come from the persistent system portal;
 * legacy saves use the authored entry. Neither uses the inert chunk envelope.
 */
enum world_transition_result world_transition_prepare_cross_mode(
		const struct player *p, struct chunk *actual, const char *route_id,
		struct world_transition *transition)
{
	enum world_transition_result result;
	struct world_transition prepared;
	struct world_route *route;
	const struct level *source;
	const struct world_entry *source_entry;
	struct loc source_grid;

	if (!transition) return WORLD_TRANSITION_INVALID;
	memset(transition, 0, sizeof(*transition));
	result = world_transition_prepare_route(p, route_id, false, &prepared);
	if (result != WORLD_TRANSITION_OK) return result;
	route = world_route_by_id(route_id);
	if (!world_route_is_cross_mode(route)) {
		return WORLD_TRANSITION_WRONG_ROUTE_KIND;
	}
	source = world_player_level(p);
	source_entry = source ? world_entry_by_id(source, route->from_entry) :
		NULL;
	if (!source || !source_entry || source_entry->kind != WORLD_ENTRY_GRID ||
			!actual || actual->is_known || actual != cave ||
			!streq(actual->location_id, route->from)) {
		return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
	}
	if (source->mode == WORLD_MODE_TOP_DOWN) {
		if (world_entry_locate(actual, route->from_entry, &source_grid) !=
				WORLD_ENTRY_OK) {
			return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
		}
		if (!world_entry_contains(actual, route->from_entry, p->grid)) {
			return WORLD_TRANSITION_NOT_AT_SOURCE_NODE;
		}
	} else if (source->mode == WORLD_MODE_SPELUNKING) {
		const struct world_spelunk_system *system = p->spelunking_system;

		if (!world_spelunk_player_is_active(p)) {
			return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
		}
		source_grid = loc(p->spelunking->state.x,
			p->spelunking->state.y);
		if (system && system->graph_version ==
				WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
			const struct world_spelunk_system_portal *portal;

			if (!world_spelunk_system_is_valid(system)) {
				return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
			}
			portal = world_spelunk_system_portal_by_entry(system,
				route->from_entry);
			if (!portal || !portal->enabled || !portal->materialized ||
					!streq(portal->node_id, system->active_node_id)) {
				return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
			}
			if (portal->x != source_grid.x || portal->y != source_grid.y) {
				return WORLD_TRANSITION_NOT_AT_SOURCE_NODE;
			}
		} else if (!loc_eq(source_entry->grid, source_grid)) {
			return WORLD_TRANSITION_NOT_AT_SOURCE_NODE;
		}
	} else {
		return WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
	}

	*transition = prepared;
	return WORLD_TRANSITION_OK;
}

/** Commit a prepared ticket if its source and route are still current. */
static bool world_transition_commit_ticket(struct player *p,
		const struct world_transition *transition)
{
	const struct level *source;
	const struct level *destination;
	struct world_route *route;

	if (!p || !transition) return false;
	source = world_player_level(p);
	destination = level_by_id(transition->destination_id);
	route = world_route_by_id(transition->route_id);
	if (!source || !destination || !route ||
		!streq(source->id, transition->source_id) ||
		!streq(route->from, transition->source_id) ||
		!streq(route->to, transition->destination_id) ||
		!streq(route->to_entry, transition->destination_entry) ||
		destination->danger != transition->destination_danger ||
		route->travel_turns != transition->travel_turns) {
		return false;
	}
	if (transition->requires_unlocked_route &&
			!world_player_route_is_unlocked(p, route->id)) {
		return false;
	}
	if (transition->requires_activated_nodes) {
		struct world_travel_node *source_node =
			world_travel_node_by_endpoint(route->from, route->from_entry);
		struct world_travel_node *destination_node =
			world_travel_node_by_endpoint(route->to, route->to_entry);

		if (!source_node || !destination_node ||
				!streq(source_node->id, transition->source_node_id) ||
				!streq(destination_node->id,
					transition->destination_node_id) ||
				!world_player_has_activated_travel_node(p,
					source_node->id) ||
				!world_player_has_activated_travel_node(p,
					destination_node->id)) {
			return false;
		}
	}
	return world_player_set_location(p, transition->destination_id,
		transition->destination_entry);
}

/** Begin one runtime transaction.  An active coordinator cannot be reused. */
enum world_transition_result world_transition_coordinator_begin_route(
		struct world_transition_coordinator *coordinator,
		const struct player *p, const char *route_id, bool require_unlocked)
{
	enum world_transition_result result;
	struct world_transition transition;

	if (!coordinator) return WORLD_TRANSITION_INVALID;
	if (coordinator->phase != WORLD_TRANSITION_IDLE) {
		return WORLD_TRANSITION_BUSY;
	}
	result = world_transition_prepare_route(p, route_id, require_unlocked,
		&transition);
	if (result != WORLD_TRANSITION_OK) return result;

	coordinator->transition = transition;
	coordinator->phase = WORLD_TRANSITION_PREPARED;
	return WORLD_TRANSITION_OK;
}

/** Begin a route that has passed all persistent and runtime travel gates. */
enum world_transition_result world_transition_coordinator_begin_travel(
		struct world_transition_coordinator *coordinator,
		const struct player *p, struct chunk *actual, const char *route_id)
{
	enum world_transition_result result;
	struct world_transition transition;

	if (!coordinator) return WORLD_TRANSITION_INVALID;
	if (coordinator->phase != WORLD_TRANSITION_IDLE) {
		return WORLD_TRANSITION_BUSY;
	}
	result = world_transition_prepare_travel(p, actual, route_id,
		&transition);
	if (result != WORLD_TRANSITION_OK) return result;

	coordinator->transition = transition;
	coordinator->source_grid = p->grid;
	coordinator->source_grid_staged = true;
	coordinator->phase = WORLD_TRANSITION_PREPARED;
	return WORLD_TRANSITION_OK;
}

/** Begin an ordinary physical route from its exact authored source entry. */
enum world_transition_result world_transition_coordinator_begin_physical(
		struct world_transition_coordinator *coordinator,
		const struct player *p, struct chunk *actual, const char *route_id)
{
	enum world_transition_result result;
	struct world_transition transition;

	if (!coordinator) return WORLD_TRANSITION_INVALID;
	if (coordinator->phase != WORLD_TRANSITION_IDLE) {
		return WORLD_TRANSITION_BUSY;
	}
	result = world_transition_prepare_physical(p, actual, route_id,
		&transition);
	if (result != WORLD_TRANSITION_OK) return result;

	coordinator->transition = transition;
	coordinator->source_grid = p->grid;
	coordinator->source_grid_staged = true;
	coordinator->phase = WORLD_TRANSITION_PREPARED;
	return WORLD_TRANSITION_OK;
}

/** Begin an exact-position crossing between top-down and side-view modes. */
enum world_transition_result world_transition_coordinator_begin_cross_mode(
		struct world_transition_coordinator *coordinator,
		const struct player *p, struct chunk *actual, const char *route_id)
{
	enum world_transition_result result;
	struct world_transition transition;
	const struct level *source;

	if (!coordinator) return WORLD_TRANSITION_INVALID;
	if (coordinator->phase != WORLD_TRANSITION_IDLE) {
		return WORLD_TRANSITION_BUSY;
	}
	result = world_transition_prepare_cross_mode(p, actual, route_id,
		&transition);
	if (result != WORLD_TRANSITION_OK) return result;

	source = world_player_level(p);
	coordinator->transition = transition;
	coordinator->source_grid = source->mode == WORLD_MODE_SPELUNKING ?
		loc(p->spelunking->state.x, p->spelunking->state.y) : p->grid;
	coordinator->source_grid_staged = true;
	coordinator->phase = WORLD_TRANSITION_PREPARED;
	return WORLD_TRANSITION_OK;
}

/**
 * Begin a position-independent relocation along an authored cross-mode route.
 *
 * This is intentionally narrower than the ordinary route primitive: callers
 * may waive portal occupancy for an approved relocation effect, but may not
 * invent either endpoint or cross between unsupported location modes.
 */
enum world_transition_result world_transition_coordinator_begin_relocation(
		struct world_transition_coordinator *coordinator,
		const struct player *p, const char *route_id)
{
	enum world_transition_result result;
	struct world_transition transition;
	const struct world_route *route;
	const struct level *source;

	if (!coordinator) return WORLD_TRANSITION_INVALID;
	if (coordinator->phase != WORLD_TRANSITION_IDLE) {
		return WORLD_TRANSITION_BUSY;
	}
	result = world_transition_prepare_route(p, route_id, false, &transition);
	if (result != WORLD_TRANSITION_OK) return result;
	route = world_route_by_id(route_id);
	source = world_player_level(p);
	if (!world_route_is_recall(route) || !source ||
			source->mode != WORLD_MODE_SPELUNKING ||
			!world_spelunk_player_is_active(p)) {
		return WORLD_TRANSITION_WRONG_ROUTE_KIND;
	}

	coordinator->transition = transition;
	coordinator->source_grid = loc(p->spelunking->state.x,
		p->spelunking->state.y);
	coordinator->source_grid_staged = true;
	coordinator->phase = WORLD_TRANSITION_PREPARED;
	return WORLD_TRANSITION_OK;
}

/**
 * Record that the caller has created or loaded precisely the destination
 * described by the ticket.  The coordinator does not own that runtime data;
 * the caller must discard it if the transaction is aborted.
 */
bool world_transition_coordinator_destination_ready(
		struct world_transition_coordinator *coordinator,
		const char *destination_id, const char *destination_entry,
		int destination_danger)
{
	const struct world_transition *transition;

	if (!coordinator || (coordinator->phase != WORLD_TRANSITION_PREPARED &&
			coordinator->phase != WORLD_TRANSITION_DESTINATION_READY)) {
		return false;
	}
	transition = &coordinator->transition;
	if (!destination_id || !destination_entry ||
		!streq(destination_id, transition->destination_id) ||
		!streq(destination_entry, transition->destination_entry) ||
		destination_danger != transition->destination_danger) {
		return false;
	}
	coordinator->phase = WORLD_TRANSITION_DESTINATION_READY;
	return true;
}

/** Stage an overflow-safe arrival turn for explicitly approved effects. */
bool world_transition_coordinator_stage_clock(
		struct world_transition_coordinator *coordinator)
{
	unsigned int remaining;

	if (!coordinator || coordinator->phase == WORLD_TRANSITION_IDLE ||
			coordinator->clock_staged || turn < 0) {
		return false;
	}
	remaining = (unsigned int)(INT32_MAX - turn);
	if (coordinator->transition.travel_turns > remaining) return false;

	coordinator->departure_turn = turn;
	coordinator->arrival_turn = turn +
		(int32_t)coordinator->transition.travel_turns;
	coordinator->clock_staged = true;
	return true;
}

/** Stage bounded player-resource effects against the unmodified source. */
enum world_travel_resources_result world_transition_coordinator_stage_resources(
		struct world_transition_coordinator *coordinator, struct player *p)
{
	if (!coordinator || !coordinator->clock_staged) {
		return WORLD_TRAVEL_RESOURCES_INVALID;
	}
	return world_travel_resources_stage(&coordinator->resources, p,
		coordinator->departure_turn, coordinator->arrival_turn);
}

/** Commit the stable player state only after the destination is ready. */
bool world_transition_coordinator_commit(
		struct world_transition_coordinator *coordinator, struct player *p)
{
	const struct level *source;
	struct loc source_grid;

	if (!coordinator ||
			coordinator->phase != WORLD_TRANSITION_DESTINATION_READY) {
		return false;
	}
	if (coordinator->clock_staged && turn != coordinator->departure_turn) {
		return false;
	}
	if (coordinator->resources.staged &&
			!world_travel_resources_match(&coordinator->resources, p)) {
		return false;
	}
	if (coordinator->source_grid_staged) {
		source = world_player_level(p);
		if (!source) return false;
		if (source->mode == WORLD_MODE_SPELUNKING) {
			if (!world_spelunk_player_is_active(p)) return false;
			source_grid = loc(p->spelunking->state.x,
				p->spelunking->state.y);
		} else {
			source_grid = p->grid;
		}
		if (!loc_eq(source_grid, coordinator->source_grid)) return false;
	}
	if (!world_transition_commit_ticket(p, &coordinator->transition)) {
		return false;
	}
	if (coordinator->resources.staged) {
		world_travel_resources_commit(&coordinator->resources, p);
	}
	if (coordinator->clock_staged) turn = coordinator->arrival_turn;
	world_transition_coordinator_abort(coordinator);
	return true;
}

/** Return a runtime transaction to idle without changing player state. */
void world_transition_coordinator_abort(
		struct world_transition_coordinator *coordinator)
{
	if (coordinator) memset(coordinator, 0, sizeof(*coordinator));
}

bool world_transition_coordinator_is_active(
		const struct world_transition_coordinator *coordinator)
{
	return coordinator && coordinator->phase != WORLD_TRANSITION_IDLE;
}

bool world_transition_coordinator_is_ready(
		const struct world_transition_coordinator *coordinator)
{
	return coordinator &&
		coordinator->phase == WORLD_TRANSITION_DESTINATION_READY;
}
