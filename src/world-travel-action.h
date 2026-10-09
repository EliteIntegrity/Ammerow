/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-travel-action.h
 * \brief Player-facing policy and orchestration for stable-route travel.
 *
 *
 */

#ifndef WORLD_TRAVEL_ACTION_H
#define WORLD_TRAVEL_ACTION_H

#include "world-destination.h"
#include "world-travel-resources.h"
#include "world-transition.h"

enum world_travel_safety_result {
	WORLD_TRAVEL_SAFETY_OK = 0,
	WORLD_TRAVEL_SAFETY_INVALID,
	WORLD_TRAVEL_SAFETY_THREATENED,
	WORLD_TRAVEL_SAFETY_RESTRAINED,
	WORLD_TRAVEL_SAFETY_TEMPORARY_EFFECT,
	WORLD_TRAVEL_SAFETY_PENDING_RELOCATION,
	WORLD_TRAVEL_SAFETY_UNSUPPORTED_LOCATION
};

enum world_travel_action_result {
	WORLD_TRAVEL_ACTION_OK = 0,
	WORLD_TRAVEL_ACTION_INVALID,
	WORLD_TRAVEL_ACTION_SAFETY_REJECTED,
	WORLD_TRAVEL_ACTION_TRANSITION_REJECTED,
	WORLD_TRAVEL_ACTION_TIME_REJECTED,
	WORLD_TRAVEL_ACTION_RESOURCES_REJECTED,
	WORLD_TRAVEL_ACTION_DESTINATION_REJECTED
};

/** Precise diagnostics without exposing orchestration state to the UI. */
struct world_travel_action_report {
	enum world_travel_action_result result;
	enum world_travel_safety_result safety;
	enum world_transition_result transition;
	enum world_travel_resources_result resources;
	enum world_destination_result destination;
};

struct chunk;
struct player;

enum world_travel_safety_result world_travel_check_safety(
		const struct player *p, struct chunk *actual);
enum world_travel_action_result world_travel_execute(struct player *p,
		struct chunk *actual, const char *route_id,
		struct world_travel_action_report *report);
enum world_travel_action_result world_route_execute_physical(struct player *p,
		struct chunk *actual, const char *route_id,
		struct world_travel_action_report *report);

#endif /* !WORLD_TRAVEL_ACTION_H */
