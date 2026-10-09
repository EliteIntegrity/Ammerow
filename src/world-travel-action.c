/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-travel-action.c
 * \brief Player-facing policy and orchestration for stable-route travel.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "init.h"
#include "player-timed.h"
#include "player-util.h"
#include "trap.h"
#include "world-location.h"
#include "world-travel-action.h"
#include "world-turn.h"

/** Reject state whose passage of time is not yet modelled for route travel. */
enum world_travel_safety_result world_travel_check_safety(
		const struct player *p, struct chunk *actual)
{
	const struct level *level;
	int i;

	if (!p || !actual || p != player || actual != cave || actual->is_known ||
			p->cave == actual || !p->upkeep) {
		return WORLD_TRAVEL_SAFETY_INVALID;
	}
	level = world_player_level(p);
	if (!level || !world_player_mode_has_capability(p,
			WORLD_MODE_CAP_NATIVE_CAVE) ||
			!streq(actual->location_id, level->id) ||
			p->upkeep->arena_level) {
		return WORLD_TRAVEL_SAFETY_UNSUPPORTED_LOCATION;
	}
	if (square_iswebbed(actual, p->grid)) {
		return WORLD_TRAVEL_SAFETY_RESTRAINED;
	}
	if (p->word_recall || p->deep_descent) {
		return WORLD_TRAVEL_SAFETY_PENDING_RELOCATION;
	}
	if (player_has_monster_in_view(p)) {
		return WORLD_TRAVEL_SAFETY_THREATENED;
	}
	for (i = 0; i < TMD_MAX; i++) {
		/* Hunger already has an explicit aggregate travel rule. */
		if (i != TMD_FOOD && p->timed[i] > 0) {
			return WORLD_TRAVEL_SAFETY_TEMPORARY_EFFECT;
		}
	}
	return WORLD_TRAVEL_SAFETY_OK;
}

static void abort_travel(struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p)
{
	if (!world_destination_candidate_abort(candidate, coordinator, p)) {
		/* A synchronous action has no legitimate concurrent owner.  If a
		 * defensive ownership check still rejects cleanup, at least release
		 * the non-owning transaction ticket rather than leaving it busy. */
		world_transition_coordinator_abort(coordinator);
	}
}

/** Execute the shared transaction, varying only discovery and time policy. */
static enum world_travel_action_result execute_route(struct player *p,
		struct chunk *actual, const char *route_id, bool physical,
		struct world_travel_action_report *report)
{
	struct world_travel_action_report local = {
		.result = WORLD_TRAVEL_ACTION_INVALID,
		.safety = WORLD_TRAVEL_SAFETY_INVALID,
		.transition = WORLD_TRANSITION_INVALID,
		.resources = WORLD_TRAVEL_RESOURCES_INVALID,
		.destination = WORLD_DESTINATION_INVALID
	};
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	enum world_destination_result destination;

	if (report) *report = local;
	if (!p || !actual || p != player || actual != cave ||
			!world_id_is_valid(route_id)) {
		return WORLD_TRAVEL_ACTION_INVALID;
	}

	/* Reaching a node is discovery, even when this particular journey is
	 * refused later by a route or safety rule. */
	world_player_activate_travel_nodes_here(p, actual);
	local.transition = physical ?
		world_transition_coordinator_begin_physical(&coordinator, p, actual,
			route_id) :
		world_transition_coordinator_begin_travel(&coordinator, p, actual,
			route_id);
	if (local.transition != WORLD_TRANSITION_OK) {
		local.result = WORLD_TRAVEL_ACTION_TRANSITION_REJECTED;
		if (report) *report = local;
		return local.result;
	}
	local.safety = world_travel_check_safety(p, actual);
	if (local.safety != WORLD_TRAVEL_SAFETY_OK &&
			(!physical || (local.safety != WORLD_TRAVEL_SAFETY_THREATENED &&
			local.safety != WORLD_TRAVEL_SAFETY_TEMPORARY_EFFECT))) {
		local.result = WORLD_TRAVEL_ACTION_SAFETY_REJECTED;
		abort_travel(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	if (!world_transition_coordinator_stage_clock(&coordinator)) {
		local.result = WORLD_TRAVEL_ACTION_TIME_REJECTED;
		abort_travel(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	local.resources = world_transition_coordinator_stage_resources(&coordinator,
		p);
	if (local.resources != WORLD_TRAVEL_RESOURCES_OK) {
		local.result = WORLD_TRAVEL_ACTION_RESOURCES_REJECTED;
		abort_travel(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}

	destination = world_destination_prepare_stored(&candidate, &coordinator);
	if (destination == WORLD_DESTINATION_NOT_FOUND) {
		destination = world_destination_generate_owned(&candidate,
			&coordinator, p, 0, 0);
	}
	if (destination != WORLD_DESTINATION_OK) {
		local.destination = destination;
		local.result = WORLD_TRAVEL_ACTION_DESTINATION_REJECTED;
		abort_travel(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	destination = world_destination_candidate_resolve_arrival(&candidate,
		&coordinator);
	if (destination != WORLD_DESTINATION_OK) {
		local.destination = destination;
		local.result = WORLD_TRAVEL_ACTION_DESTINATION_REJECTED;
		abort_travel(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	if (physical && (!world_player_begin_physical_route(p, actual,
			coordinator.transition.destination_id) ||
			!streq(p->upkeep->physical_route.route_id, route_id))) {
		world_player_cancel_physical_route(p);
		local.transition = WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE;
		local.result = WORLD_TRAVEL_ACTION_TRANSITION_REJECTED;
		abort_travel(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	destination = world_destination_publish_with_lifecycle(&candidate,
		&coordinator, p, WORLD_SOURCE_STORE, NULL, physical ?
			world_top_down_physical_route_lifecycle() :
			world_top_down_route_lifecycle());
	if (destination != WORLD_DESTINATION_OK) {
		if (physical) world_player_cancel_physical_route(p);
		local.destination = destination;
		local.result = WORLD_TRAVEL_ACTION_DESTINATION_REJECTED;
		abort_travel(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}

	if (physical) p->upkeep->energy_use = z_info->move_energy;
	local.destination = WORLD_DESTINATION_OK;
	local.result = WORLD_TRAVEL_ACTION_OK;
	if (report) *report = local;
	return local.result;
}

/** Execute one complete synchronous aggregate-time route transaction. */
enum world_travel_action_result world_travel_execute(struct player *p,
		struct chunk *actual, const char *route_id,
		struct world_travel_action_report *report)
{
	return execute_route(p, actual, route_id, false, report);
}

/**
 * Use an authored edge or passage as one ordinary player action.  Nearby
 * monsters and temporary effects continue normally because this does not skip
 * time; restraints and pending relocations still make changing chunks unsafe.
 */
enum world_travel_action_result world_route_execute_physical(struct player *p,
		struct chunk *actual, const char *route_id,
		struct world_travel_action_report *report)
{
	return execute_route(p, actual, route_id, true, report);
}
