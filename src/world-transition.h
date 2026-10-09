/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-transition.h
 * \brief Transaction boundary for movement between stable world locations.
 *
 *
 */

#ifndef WORLD_TRANSITION_H
#define WORLD_TRANSITION_H

#include "z-type.h"
#include "world-location.h"
#include "world-travel-resources.h"

/**
 * A validated transition ticket.  Preparing a ticket does not mutate the
 * player.  The destination loader uses the immutable fields to decide what to
 * create and where to place the player.
 */
struct world_transition {
	char route_id[WORLD_ID_LEN];
	char source_id[WORLD_ID_LEN];
	char destination_id[WORLD_ID_LEN];
	char destination_entry[WORLD_ENTRY_LEN];
	int destination_danger;
	unsigned int travel_turns;
	bool requires_unlocked_route;
	bool requires_activated_nodes;
	char source_node_id[WORLD_ID_LEN];
	char destination_node_id[WORLD_ID_LEN];
};

enum world_transition_result {
	WORLD_TRANSITION_OK = 0,
	WORLD_TRANSITION_INVALID,
	WORLD_TRANSITION_BUSY,
	WORLD_TRANSITION_UNKNOWN_ROUTE,
	WORLD_TRANSITION_WRONG_SOURCE,
	WORLD_TRANSITION_LOCKED,
	WORLD_TRANSITION_UNKNOWN_DESTINATION,
	WORLD_TRANSITION_UNKNOWN_TRAVEL_NODE,
	WORLD_TRANSITION_SOURCE_NODE_INACTIVE,
	WORLD_TRANSITION_DESTINATION_NODE_INACTIVE,
	WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE,
	WORLD_TRANSITION_NOT_AT_SOURCE_NODE,
	WORLD_TRANSITION_WRONG_ROUTE_KIND
};

enum world_transition_phase {
	WORLD_TRANSITION_IDLE = 0,
	WORLD_TRANSITION_PREPARED,
	WORLD_TRANSITION_DESTINATION_READY
};

/**
 * Runtime-only transaction state.  It deliberately is not part of the player
 * or savefile: before commit, the player's source location remains the only
 * authoritative saved state.
 */
struct world_transition_coordinator {
	enum world_transition_phase phase;
	struct world_transition transition;
	bool clock_staged;
	int32_t departure_turn;
	int32_t arrival_turn;
	bool source_grid_staged;
	struct loc source_grid;
	struct world_travel_resources resources;
};

struct player;
struct chunk;

bool world_route_is_physical(const struct world_route *route);
bool world_route_is_cross_mode(const struct world_route *route);
/** A magical side-view return to a hub, never a physical portal. */
bool world_route_is_recall(const struct world_route *route);
bool world_route_is_aggregate(const struct world_route *route);
bool world_route_is_retired(const struct world_route *route);
enum world_transition_result world_transition_prepare_route(
		const struct player *p, const char *route_id, bool require_unlocked,
		struct world_transition *transition);
enum world_transition_result world_transition_prepare_travel(
		const struct player *p, struct chunk *actual, const char *route_id,
		struct world_transition *transition);
enum world_transition_result world_transition_prepare_physical(
		const struct player *p, struct chunk *actual, const char *route_id,
		struct world_transition *transition);
enum world_transition_result world_transition_prepare_cross_mode(
		const struct player *p, struct chunk *actual, const char *route_id,
		struct world_transition *transition);

enum world_transition_result world_transition_coordinator_begin_route(
		struct world_transition_coordinator *coordinator,
		const struct player *p, const char *route_id, bool require_unlocked);
enum world_transition_result world_transition_coordinator_begin_travel(
		struct world_transition_coordinator *coordinator,
		const struct player *p, struct chunk *actual, const char *route_id);
enum world_transition_result world_transition_coordinator_begin_physical(
		struct world_transition_coordinator *coordinator,
		const struct player *p, struct chunk *actual, const char *route_id);
enum world_transition_result world_transition_coordinator_begin_cross_mode(
		struct world_transition_coordinator *coordinator,
		const struct player *p, struct chunk *actual, const char *route_id);
/** Begin authored magical Recall without requiring physical portal occupancy. */
enum world_transition_result world_transition_coordinator_begin_relocation(
		struct world_transition_coordinator *coordinator,
		const struct player *p, const char *route_id);
bool world_transition_coordinator_destination_ready(
		struct world_transition_coordinator *coordinator,
		const char *destination_id, const char *destination_entry,
		int destination_danger);
/** Stage the route's clock change without advancing the live world. */
bool world_transition_coordinator_stage_clock(
		struct world_transition_coordinator *coordinator);
enum world_travel_resources_result world_transition_coordinator_stage_resources(
		struct world_transition_coordinator *coordinator, struct player *p);
bool world_transition_coordinator_commit(
		struct world_transition_coordinator *coordinator, struct player *p);
void world_transition_coordinator_abort(
		struct world_transition_coordinator *coordinator);
bool world_transition_coordinator_is_active(
		const struct world_transition_coordinator *coordinator);
bool world_transition_coordinator_is_ready(
		const struct world_transition_coordinator *coordinator);

#endif /* !WORLD_TRANSITION_H */
