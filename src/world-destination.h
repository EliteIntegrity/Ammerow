/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-destination.h
 * \brief Preparation and transactional publication of destination chunks.
 *
 *
 */

#ifndef WORLD_DESTINATION_H
#define WORLD_DESTINATION_H

#include "world-transition.h"
#include "z-type.h"

enum world_destination_result {
	WORLD_DESTINATION_OK = 0,
	WORLD_DESTINATION_INVALID,
	WORLD_DESTINATION_BUSY,
	WORLD_DESTINATION_NOT_PREPARED,
	WORLD_DESTINATION_NOT_FOUND,
	WORLD_DESTINATION_DUPLICATE,
	WORLD_DESTINATION_INCONSISTENT,
	WORLD_DESTINATION_READY_REJECTED,
	WORLD_DESTINATION_ENTRY_MISMATCH,
	WORLD_DESTINATION_ENTRY_NOT_FOUND,
	WORLD_DESTINATION_ENTRY_UNRESOLVED,
	WORLD_DESTINATION_INVALID_ARRIVAL,
	WORLD_DESTINATION_STALE,
	WORLD_DESTINATION_SOURCE_INCONSISTENT,
	WORLD_DESTINATION_STORAGE_FULL,
	WORLD_DESTINATION_UNSUPPORTED,
	WORLD_DESTINATION_TIME_NOT_STAGED,
	WORLD_DESTINATION_CLOCK_STALE,
	WORLD_DESTINATION_RESOURCES_NOT_STAGED,
	WORLD_DESTINATION_RESOURCES_STALE,
	WORLD_DESTINATION_AGE_STALE,
	WORLD_DESTINATION_RETIRE_BUSY,
	WORLD_DESTINATION_COMMIT_REJECTED
};

enum world_source_policy {
	WORLD_SOURCE_STORE = 0,
	WORLD_SOURCE_RETIRE
};

struct chunk;
struct player;

/**
 * A post-commit lifecycle hook.  Publication has become irreversible before
 * either hook runs, so implementations must complete without reporting
 * failure.  The source hook runs during the brief committed-but-not-swapped
 * interval and must not save or expose that transient state.  The immutable
 * transition copy identifies both sides even while the active globals are
 * changing.
 */
typedef void (*world_destination_lifecycle_hook)(
		const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		void *user);

struct world_destination_lifecycle {
	world_destination_lifecycle_hook leave_source;
	void *leave_user;
	world_destination_lifecycle_hook enter_destination;
	void *enter_user;
};

/** How a prepared candidate pair is owned before publication. */
enum world_destination_candidate_storage {
	WORLD_DESTINATION_CANDIDATE_EMPTY = 0,
	WORLD_DESTINATION_CANDIDATE_STORED,
	WORLD_DESTINATION_CANDIDATE_OWNED
};

/**
 * A validated actual/known pair that has not been published.  Stored pairs are
 * borrowed from the chunk list.  Owned pairs remain outside both the list and
 * active globals; successful preparation transfers them to this candidate.
 */
struct world_destination_candidate {
	struct chunk *actual;
	struct chunk *known;
	struct loc arrival;
	bool has_arrival;
	/** Last-active turn captured from both chunks during preparation. */
	int32_t inactive_turn;
	/** Clear artifacts registered by speculative generation if aborted. */
	bool rollback_artifacts;
	/** Stamp this speculative destination with its committed arrival turn. */
	bool generated_for_transition;
	enum world_destination_candidate_storage storage;
};

/**
 * Ownership returned when the source is retired rather than stored.  The
 * caller must run any source leave hooks before disposing of this pair.
 */
struct world_retired_chunks {
	struct chunk *actual;
	struct chunk *known;
};

enum world_destination_result world_destination_prepare_stored(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator);
/**
 * Adopt a newly created or loaded off-list pair.  On failure, ownership stays
 * with the caller; on success, abort will dispose of the pair.
 */
enum world_destination_result world_destination_prepare_owned(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct chunk *actual, struct chunk *known);
/** Generate and adopt a top-down destination without changing the live pair. */
enum world_destination_result world_destination_generate_owned(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p, int height, int width);
enum world_destination_result world_destination_candidate_set_arrival(
		struct world_destination_candidate *candidate,
		const struct world_transition_coordinator *coordinator,
		const char *entry, struct loc arrival);
enum world_destination_result world_destination_candidate_resolve_arrival(
		struct world_destination_candidate *candidate,
		const struct world_transition_coordinator *coordinator);
enum world_destination_result world_destination_publish(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p, enum world_source_policy source_policy,
		struct world_retired_chunks *retired);
enum world_destination_result world_destination_publish_with_lifecycle(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p, enum world_source_policy source_policy,
		struct world_retired_chunks *retired,
		const struct world_destination_lifecycle *lifecycle);
bool world_destination_candidate_abort(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p);
bool world_destination_candidate_is_ready(
		const struct world_destination_candidate *candidate);
bool world_destination_candidate_is_publishable(
		const struct world_destination_candidate *candidate);
/** Dispose of a retired pair after its source leave hooks have completed. */
bool world_retired_chunks_dispose(struct world_retired_chunks *retired,
		struct player *p);

#endif /* !WORLD_DESTINATION_H */
