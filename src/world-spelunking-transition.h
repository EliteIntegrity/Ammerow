/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-transition.h
 * \brief Transactional entry and exit for spelunking locations.
 *
 *
 */

#ifndef WORLD_SPELUNKING_TRANSITION_H
#define WORLD_SPELUNKING_TRANSITION_H

#include "world-destination.h"
#include "world-spelunking-runtime.h"
#include "world-travel-action.h"

struct world_spelunk_system;

/** Generated-system or legacy-runtime ownership staged before entry commits. */
struct world_spelunking_entry_stage {
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_system *system;
	char arrival_node_id[WORLD_ID_LEN];
	int arrival_x;
	int arrival_y;
	bool owns_runtime;
	bool owns_system;
	bool committed;
};

enum world_spelunking_entry_result {
	WORLD_SPELUNKING_ENTRY_OK = 0,
	WORLD_SPELUNKING_ENTRY_INVALID,
	WORLD_SPELUNKING_ENTRY_SAFETY_REJECTED,
	WORLD_SPELUNKING_ENTRY_TRANSITION_REJECTED,
	WORLD_SPELUNKING_ENTRY_TIME_REJECTED,
	WORLD_SPELUNKING_ENTRY_RESOURCES_REJECTED,
	WORLD_SPELUNKING_ENTRY_DESTINATION_REJECTED
};

/** Exact diagnostics for the synchronous player-facing entry action. */
struct world_spelunking_entry_report {
	enum world_spelunking_entry_result result;
	enum world_travel_safety_result safety;
	enum world_transition_result transition;
	enum world_travel_resources_result resources;
	enum world_destination_result destination;
};

enum world_spelunking_exit_result {
	WORLD_SPELUNKING_EXIT_OK = 0,
	WORLD_SPELUNKING_EXIT_INVALID,
	WORLD_SPELUNKING_EXIT_UNSTABLE,
	WORLD_SPELUNKING_EXIT_NOT_HERE,
	WORLD_SPELUNKING_EXIT_TRANSITION_REJECTED,
	WORLD_SPELUNKING_EXIT_TIME_REJECTED,
	WORLD_SPELUNKING_EXIT_RESOURCES_REJECTED,
	WORLD_SPELUNKING_EXIT_DESTINATION_REJECTED
};

/** Exact diagnostics for the synchronous player-facing exit action. */
struct world_spelunking_exit_report {
	enum world_spelunking_exit_result result;
	enum world_transition_result transition;
	enum world_travel_resources_result resources;
	enum world_destination_result destination;
	char route_id[WORLD_ID_LEN];
};

/** Whether a live side-view player has the required inert save envelope. */
bool world_spelunking_active_storage_is_valid(const struct player *p);

/**
 * Return an indexed external portal for a side-view runtime. Generated systems
 * use their persistent materialized portal coordinates; legacy saves resolve
 * unambiguous authored route entries.
 */
bool world_spelunking_exit_grid_at(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, size_t index,
		struct loc *grid);

/**
 * Prepare a spelunking system and its inert compatibility envelope.
 *
 * The coordinator must already contain a top-down to spelunking route.  On
 * success, the candidate owns the envelope and the stage either owns a newly
 * generated system, borrows the player's matching dormant system, or follows
 * the legacy one-runtime migration path. Neither live chunks nor player state
 * change before publication.
 */
enum world_destination_result world_spelunking_entry_prepare(
		struct world_spelunking_entry_stage *stage,
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p);

/** Commit and publish a prepared entry while retaining the top-down source. */
enum world_destination_result world_spelunking_entry_publish(
		struct world_spelunking_entry_stage *stage,
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p);

/** Abort both halves of a prepared entry without changing the live world. */
bool world_spelunking_entry_abort(
		struct world_spelunking_entry_stage *stage,
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p);

/** Forget post-publication staging metadata without freeing player state. */
void world_spelunking_entry_finish(
		struct world_spelunking_entry_stage *stage);

/**
 * Enter one selected top-down shaft as an ordinary player action.  The route,
 * runtime, compatibility envelope, resource snapshot, and retained source are
 * all committed as one synchronous transaction.
 */
enum world_spelunking_entry_result world_spelunking_entry_execute(
		struct player *p, struct chunk *actual, const char *route_id,
		struct world_spelunking_entry_report *report);

/**
 * Publish a prepared top-down destination and retire the active inert
 * spelunking envelope.  The caller disposes the returned retired pair.
 */
enum world_destination_result world_spelunking_exit_publish(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p,
		struct world_retired_chunks *retired);

/**
 * Use the unique external portal beneath the standing player as one ordinary
 * action. Generated portal identity selects the authored world route; legacy
 * saves use the route's authored grid. The transition commits atomically and
 * disposes the inert compatibility envelope after top-down play resumes.
 */
enum world_spelunking_exit_result world_spelunking_exit_execute(
		struct player *p, struct chunk *actual,
		struct world_spelunking_exit_report *report);

/**
 * Resolve delayed Recall from anywhere in a side-view cave through its unique
 * authored route to a hub.  This does not count as physical route discovery.
 */
bool world_spelunking_recall_execute(struct player *p, struct chunk *actual);

#endif /* !WORLD_SPELUNKING_TRANSITION_H */
