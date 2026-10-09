/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-destination.c
 * \brief Preparation and transactional publication of destination chunks.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "generate.h"
#include "mon-make.h"
#include "player-util.h"
#include "world-destination.h"
#include "world-entry.h"

/** Locate both candidate pointers exactly once in the stored chunk list. */
static bool stored_candidate_indices(
		const struct world_destination_candidate *candidate,
		uint16_t *actual_index, uint16_t *known_index)
{
	uint16_t i;
	unsigned int actual_count = 0, known_count = 0;

	if (!candidate || !actual_index || !known_index || !candidate->actual ||
			!candidate->known ||
			candidate->actual == candidate->known) {
		return false;
	}
	for (i = 0; i < chunk_list_max; i++) {
		if (chunk_list[i] == candidate->actual) {
			*actual_index = i;
			actual_count++;
		}
		if (chunk_list[i] == candidate->known) {
			*known_index = i;
			known_count++;
		}
	}
	return actual_count == 1 && known_count == 1;
}

/** Revalidate where a candidate pair is held before using its pointers. */
static bool candidate_storage_is_valid(
		const struct world_destination_candidate *candidate,
		uint16_t *actual_index, uint16_t *known_index)
{
	uint16_t ignored_actual, ignored_known;

	if (!candidate || !candidate->actual || !candidate->known) return false;
	if (!actual_index) actual_index = &ignored_actual;
	if (!known_index) known_index = &ignored_known;

	switch (candidate->storage) {
		case WORLD_DESTINATION_CANDIDATE_STORED:
			return stored_candidate_indices(candidate, actual_index,
				known_index);
		case WORLD_DESTINATION_CANDIDATE_OWNED:
			return !chunk_find(candidate->actual) &&
				!chunk_find(candidate->known) &&
				candidate->actual != cave && candidate->known != cave &&
				(!player || (candidate->actual != player->cave &&
					candidate->known != player->cave));
		case WORLD_DESTINATION_CANDIDATE_EMPTY:
		default:
			return false;
	}
}

/** Remove a prevalidated stored-list index without allocating. */
static void remove_stored_index(uint16_t index)
{
	uint16_t i;

	for (i = index + 1; i < chunk_list_max; i++) {
		chunk_list[i - 1] = chunk_list[i];
	}
	chunk_list_max--;
	chunk_list[chunk_list_max] = NULL;
}

/** Count player markers without interpreting ordinary monster indices. */
static unsigned int chunk_player_markers(struct chunk *c)
{
	unsigned int count = 0;
	int x, y;

	if (!c) return 0;
	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			if (square(c, loc(x, y))->mon == -1) count++;
		}
	}
	return count;
}

/** Roll back global registrations and destroy an unpublished owned pair. */
static void discard_owned_pair(struct chunk *actual, struct chunk *known,
		struct player *p, bool rollback_artifacts)
{
	assert(actual);
	assert(known);
	assert(p);
	if (rollback_artifacts) uncreate_artifacts(actual);
	cave_free(known);
	wipe_mon_list(actual, p);
	cave_free(actual);
}

/** Allocate the legacy display label expected for a stored stable chunk. */
static char *make_stored_chunk_name(const struct chunk *c)
{
	const struct level *lev = world_chunk_level(c);
	char *name;

	if (!lev) return NULL;
	name = string_make(lev->name);
	if (c->is_known) name = string_append(name, " known");
	return name;
}

/** Revalidate the destination pair against an immutable ticket. */
static bool candidate_matches_transition(
		const struct world_destination_candidate *candidate,
		const struct world_transition *transition)
{
	return candidate && transition && candidate->actual && candidate->known &&
		candidate->actual != candidate->known &&
		streq(candidate->actual->location_id, transition->destination_id) &&
		streq(candidate->known->location_id, transition->destination_id) &&
		!candidate->actual->is_known && candidate->known->is_known &&
		candidate->actual->depth == transition->destination_danger &&
		candidate->known->depth == transition->destination_danger &&
		candidate->actual->height == candidate->known->height &&
		candidate->actual->width == candidate->known->width &&
		candidate->actual->obj_max == candidate->known->obj_max &&
		chunk_player_markers(candidate->actual) == 0 &&
		chunk_player_markers(candidate->known) == 0 &&
		world_chunk_level(candidate->actual) &&
		world_chunk_level(candidate->known);
}

/** Revalidate the pair's shared last-active turn before ageing it. */
static bool candidate_age_matches(
		const struct world_destination_candidate *candidate,
		int32_t latest_turn)
{
	return candidate && candidate->actual && candidate->known &&
		candidate->inactive_turn >= 0 &&
		candidate->inactive_turn <= latest_turn &&
		candidate->actual->turn == candidate->inactive_turn &&
		candidate->known->turn == candidate->inactive_turn;
}

/**
 * Validate a stored destination pair and report it ready to the coordinator.
 * The pair stays in the stored list, so abort is a zero-mutation operation.
 */
enum world_destination_result world_destination_prepare_stored(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator)
{
	const struct world_transition *transition;
	struct chunk *actual;
	struct chunk *known;
	size_t actual_count, known_count;

	if (!candidate || !coordinator) return WORLD_DESTINATION_INVALID;
	if (candidate->actual || candidate->known || candidate->has_arrival ||
			candidate->storage != WORLD_DESTINATION_CANDIDATE_EMPTY) {
		return WORLD_DESTINATION_BUSY;
	}
	if (coordinator->phase != WORLD_TRANSITION_PREPARED) {
		return WORLD_DESTINATION_NOT_PREPARED;
	}
	transition = &coordinator->transition;
	actual_count = chunk_count_location(transition->destination_id, false);
	known_count = chunk_count_location(transition->destination_id, true);
	if (actual_count > 1 || known_count > 1) {
		return WORLD_DESTINATION_DUPLICATE;
	}
	if (actual_count != 1 || known_count != 1) {
		return WORLD_DESTINATION_NOT_FOUND;
	}

	actual = chunk_find_location(transition->destination_id, false);
	known = chunk_find_location(transition->destination_id, true);
	if (!actual || !known) {
		return WORLD_DESTINATION_INCONSISTENT;
	}
	candidate->actual = actual;
	candidate->known = known;
	candidate->inactive_turn = actual->turn;
	candidate->storage = WORLD_DESTINATION_CANDIDATE_STORED;
	if (!candidate_matches_transition(candidate, transition) ||
			!candidate_age_matches(candidate, turn)) {
		memset(candidate, 0, sizeof(*candidate));
		return WORLD_DESTINATION_INCONSISTENT;
	}

	if (!world_transition_coordinator_destination_ready(coordinator,
			transition->destination_id, transition->destination_entry,
			transition->destination_danger)) {
		memset(candidate, 0, sizeof(*candidate));
		return WORLD_DESTINATION_READY_REJECTED;
	}
	return WORLD_DESTINATION_OK;
}

/**
 * Validate and adopt a destination pair produced away from the active globals.
 * Nothing is transferred until every pair check and the coordinator readiness
 * report succeeds.
 */
enum world_destination_result world_destination_prepare_owned(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct chunk *actual, struct chunk *known)
{
	const struct world_transition *transition;
	struct world_destination_candidate prepared = { 0 };

	if (!candidate || !coordinator || !actual || !known) {
		return WORLD_DESTINATION_INVALID;
	}
	if (candidate->actual || candidate->known || candidate->has_arrival ||
			candidate->storage != WORLD_DESTINATION_CANDIDATE_EMPTY) {
		return WORLD_DESTINATION_BUSY;
	}
	if (coordinator->phase != WORLD_TRANSITION_PREPARED) {
		return WORLD_DESTINATION_NOT_PREPARED;
	}

	prepared.actual = actual;
	prepared.known = known;
	prepared.inactive_turn = actual->turn;
	prepared.storage = WORLD_DESTINATION_CANDIDATE_OWNED;
	transition = &coordinator->transition;
	if (!candidate_matches_transition(&prepared, transition) ||
			!candidate_age_matches(&prepared, turn)) {
		return WORLD_DESTINATION_INCONSISTENT;
	}
	if (!candidate_storage_is_valid(&prepared, NULL, NULL)) {
		return WORLD_DESTINATION_STALE;
	}
	if (!world_transition_coordinator_destination_ready(coordinator,
			transition->destination_id, transition->destination_entry,
			transition->destination_danger)) {
		return WORLD_DESTINATION_READY_REJECTED;
	}

	*candidate = prepared;
	return WORLD_DESTINATION_OK;
}

/**
 * Generate a destination against a shallow shadow of the live player.  The
 * inherited builders still consult the global player and readiness flag, so
 * those are redirected only for the synchronous call and restored before any
 * candidate is exposed.  Mutable upkeep and position fields are private to the
 * shadow; inventory, options, quests, and other read-mostly run data remain
 * shared.
 */
enum world_destination_result world_destination_generate_owned(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p, int height, int width)
{
	const struct world_transition *transition;
	const struct level *destination;
	const struct world_entry *entry;
	struct player generation_player;
	struct player_upkeep generation_upkeep;
	struct generated_level_pair pair = { 0 };
	struct player *saved_player;
	struct dun_data *saved_dun;
	bool saved_character_dungeon;
	enum world_destination_result result;

	if (!candidate || !coordinator || !p || p != player || !p->upkeep ||
			height < 0 || width < 0) {
		return WORLD_DESTINATION_INVALID;
	}
	if (candidate->actual || candidate->known || candidate->has_arrival ||
			candidate->storage != WORLD_DESTINATION_CANDIDATE_EMPTY) {
		return WORLD_DESTINATION_BUSY;
	}
	if (coordinator->phase != WORLD_TRANSITION_PREPARED) {
		return WORLD_DESTINATION_NOT_PREPARED;
	}
	transition = &coordinator->transition;
	destination = level_by_id(transition->destination_id);
	if (!destination || destination->danger !=
			transition->destination_danger) {
		return WORLD_DESTINATION_INCONSISTENT;
	}
	if (destination->mode != WORLD_MODE_TOP_DOWN) {
		return WORLD_DESTINATION_UNSUPPORTED;
	}
	entry = world_entry_by_id(destination,
		transition->destination_entry);
	if (!entry) return WORLD_DESTINATION_ENTRY_NOT_FOUND;

	generation_player = *p;
	generation_upkeep = *p->upkeep;
	generation_player.upkeep = &generation_upkeep;
	generation_player.cave = NULL;
	generation_player.grid = loc(0, 0);
	generation_player.old_grid = loc(0, 0);
	generation_player.depth = (int16_t)destination->danger;
	my_strcpy(generation_player.world_location.id, destination->id,
		sizeof(generation_player.world_location.id));
	my_strcpy(generation_player.world_location.entry,
		transition->destination_entry,
		sizeof(generation_player.world_location.entry));
	generation_upkeep.health_who = NULL;
	generation_upkeep.arena_level = false;
	generation_upkeep.create_up_stair = entry->kind == WORLD_ENTRY_UP_STAIR;
	generation_upkeep.create_down_stair =
		entry->kind == WORLD_ENTRY_DOWN_STAIR;

	saved_player = player;
	saved_character_dungeon = character_dungeon;
	saved_dun = dun;
	player = &generation_player;
	character_dungeon = false;
	dun = NULL;
	if (!generate_level_pair(&generation_player, height, width, &pair)) {
		player = saved_player;
		character_dungeon = saved_character_dungeon;
		dun = saved_dun;
		return WORLD_DESTINATION_INCONSISTENT;
	}
	player = saved_player;
	character_dungeon = saved_character_dungeon;
	dun = saved_dun;

	/* A generated destination must contain its data-authored cross-mode
	 * sources before the transition resolves the named arrival.  Ordinary
	 * on_new_level() materialization is too late: it runs only after the
	 * candidate has committed.  This is a no-op for locations without such
	 * routes and makes a first-ever cave-to-dungeon crossing reciprocal. */
	if (!world_entry_materialize_cross_mode_sources(pair.actual,
			destination)) {
		discard_owned_pair(pair.actual, pair.known, p, true);
		event_signal_flag(EVENT_GEN_LEVEL_END, false);
		return WORLD_DESTINATION_INCONSISTENT;
	}

	/* Builders place their shadow player; candidates must have no marker. */
	if (chunk_player_markers(pair.actual) != 1 ||
			!square_in_bounds(pair.actual, generation_player.grid) ||
			square(pair.actual, generation_player.grid)->mon != -1 ||
			chunk_player_markers(pair.known) != 0) {
		discard_owned_pair(pair.actual, pair.known, p, true);
		event_signal_flag(EVENT_GEN_LEVEL_END, false);
		return WORLD_DESTINATION_INCONSISTENT;
	}
	square_set_mon(pair.actual, generation_player.grid, 0);

	result = world_destination_prepare_owned(candidate, coordinator,
		pair.actual, pair.known);
	if (result != WORLD_DESTINATION_OK) {
		discard_owned_pair(pair.actual, pair.known, p, true);
	} else {
		candidate->rollback_artifacts = true;
		candidate->generated_for_transition = true;
	}
	event_signal_flag(EVENT_GEN_LEVEL_END, result == WORLD_DESTINATION_OK);
	return result;
}

/**
 * Attach the cell resolved for the ticket's named entry.  Entry lookup belongs
 * to the destination loader; this boundary verifies its report and the cell.
 */
enum world_destination_result world_destination_candidate_set_arrival(
		struct world_destination_candidate *candidate,
		const struct world_transition_coordinator *coordinator,
		const char *entry, struct loc arrival)
{
	if (!candidate || !coordinator || !entry) {
		return WORLD_DESTINATION_INVALID;
	}
	if (!world_destination_candidate_is_ready(candidate) ||
			coordinator->phase != WORLD_TRANSITION_DESTINATION_READY) {
		return WORLD_DESTINATION_NOT_PREPARED;
	}
	if (!streq(entry, coordinator->transition.destination_entry)) {
		return WORLD_DESTINATION_ENTRY_MISMATCH;
	}
	if (!candidate_storage_is_valid(candidate, NULL, NULL)) {
		return WORLD_DESTINATION_STALE;
	}
	if (!square_in_bounds_fully(candidate->actual, arrival) ||
			!square_isarrivable(candidate->actual, arrival)) {
		return WORLD_DESTINATION_INVALID_ARRIVAL;
	}
	candidate->arrival = arrival;
	candidate->has_arrival = true;
	return WORLD_DESTINATION_OK;
}

/** Resolve and attach the named destination entry from world data. */
enum world_destination_result world_destination_candidate_resolve_arrival(
		struct world_destination_candidate *candidate,
		const struct world_transition_coordinator *coordinator)
{
	enum world_entry_result result;
	struct loc arrival;

	if (!candidate || !coordinator) return WORLD_DESTINATION_INVALID;
	if (!world_destination_candidate_is_ready(candidate) ||
			coordinator->phase != WORLD_TRANSITION_DESTINATION_READY) {
		return WORLD_DESTINATION_NOT_PREPARED;
	}
	result = world_entry_resolve(candidate->actual,
		coordinator->transition.destination_entry, &arrival);
	switch (result) {
		case WORLD_ENTRY_OK:
			return world_destination_candidate_set_arrival(candidate,
				coordinator, coordinator->transition.destination_entry,
				arrival);
		case WORLD_ENTRY_NOT_FOUND:
			return WORLD_DESTINATION_ENTRY_NOT_FOUND;
		case WORLD_ENTRY_NO_MATCH:
			return WORLD_DESTINATION_ENTRY_UNRESOLVED;
		case WORLD_ENTRY_INVALID_CELL:
			return WORLD_DESTINATION_INVALID_ARRIVAL;
		case WORLD_ENTRY_WRONG_LOCATION:
			return WORLD_DESTINATION_INCONSISTENT;
		case WORLD_ENTRY_INVALID:
		default:
			return WORLD_DESTINATION_INVALID;
	}
}

/**
 * Commit and publish an already prepared destination.  All checks, source
 * labels, and any list capacity needed by an owned destination are completed
 * before the stable player commit.  After a successful commit, the remaining
 * pointer, marker, and list operations do not allocate and cannot report
 * failure.  Optional lifecycle hooks run only after that commit and are also
 * required to be non-failing: source cleanup runs before the pointer swap and
 * arrival housekeeping runs after the destination marker and globals exist.
 */
enum world_destination_result world_destination_publish_with_lifecycle(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p, enum world_source_policy source_policy,
		struct world_retired_chunks *retired,
		const struct world_destination_lifecycle *lifecycle)
{
	struct world_transition transition_copy;
	const struct world_transition *transition = &transition_copy;
	const struct level *source_level;
	struct chunk *source_actual, *source_known;
	char *source_actual_name = NULL, *source_known_name = NULL;
	struct loc source_grid;
	int32_t source_turn;
	uint16_t actual_index = 0, known_index = 0;
	bool destination_is_stored;
	bool destination_is_generated;
	int i;

	if (!candidate || !coordinator || !p || p != player ||
			(source_policy != WORLD_SOURCE_STORE &&
			 source_policy != WORLD_SOURCE_RETIRE)) {
		return WORLD_DESTINATION_INVALID;
	}
	if (source_policy == WORLD_SOURCE_RETIRE &&
			(!retired || retired->actual || retired->known)) {
		return WORLD_DESTINATION_RETIRE_BUSY;
	}
	if (coordinator->phase != WORLD_TRANSITION_DESTINATION_READY ||
			!world_destination_candidate_is_publishable(candidate)) {
		return WORLD_DESTINATION_NOT_PREPARED;
	}
	if (!coordinator->clock_staged) {
		return WORLD_DESTINATION_TIME_NOT_STAGED;
	}
	if (turn != coordinator->departure_turn) {
		return WORLD_DESTINATION_CLOCK_STALE;
	}
	if (!coordinator->resources.staged) {
		return WORLD_DESTINATION_RESOURCES_NOT_STAGED;
	}
	if (!world_travel_resources_match(&coordinator->resources, p)) {
		return WORLD_DESTINATION_RESOURCES_STALE;
	}
	if (!candidate_age_matches(candidate, coordinator->departure_turn)) {
		return WORLD_DESTINATION_AGE_STALE;
	}
	transition_copy = coordinator->transition;
	source_turn = coordinator->departure_turn;
	if (!candidate_matches_transition(candidate, transition)) {
		return WORLD_DESTINATION_INCONSISTENT;
	}
	if (!candidate_storage_is_valid(candidate, &actual_index, &known_index)) {
		return WORLD_DESTINATION_STALE;
	}
	destination_is_stored = candidate->storage ==
		WORLD_DESTINATION_CANDIDATE_STORED;
	destination_is_generated = candidate->generated_for_transition;
	if (!square_in_bounds_fully(candidate->actual, candidate->arrival) ||
			!square_isarrivable(candidate->actual, candidate->arrival)) {
		return WORLD_DESTINATION_INVALID_ARRIVAL;
	}

	source_actual = cave;
	source_known = p->cave;
	source_grid = p->grid;
	source_level = world_chunk_level(source_actual);
	if (!source_level || !streq(source_level->id, transition->source_id) ||
			!source_known || source_actual == candidate->actual ||
			source_known == candidate->known || chunk_find(source_actual) ||
			chunk_find(source_known) ||
			!world_player_restore_chunks(p, source_actual, source_known,
				chunk_list, chunk_list_max) ||
			!square_in_bounds(source_actual, source_grid) ||
			square(source_actual, source_grid)->mon != -1 ||
			chunk_player_markers(source_actual) != 1 ||
			chunk_player_markers(source_known) != 0) {
		return WORLD_DESTINATION_SOURCE_INCONSISTENT;
	}

	/* Stage legacy labels for inherited persistence/UI consumers. */
	if (source_policy == WORLD_SOURCE_STORE) {
		source_actual_name = make_stored_chunk_name(source_actual);
		source_known_name = make_stored_chunk_name(source_known);
		if (!source_actual_name || !source_known_name) {
			if (source_actual_name) string_free(source_actual_name);
			if (source_known_name) string_free(source_known_name);
			return WORLD_DESTINATION_SOURCE_INCONSISTENT;
		}
		if (!destination_is_stored && !chunk_list_reserve(2)) {
			string_free(source_actual_name);
			string_free(source_known_name);
			return WORLD_DESTINATION_STORAGE_FULL;
		}
	}

	/* This is the last operation that can reject the transition. */
	if (!world_transition_coordinator_commit(coordinator, p)) {
		if (source_actual_name) string_free(source_actual_name);
		if (source_known_name) string_free(source_known_name);
		return WORLD_DESTINATION_COMMIT_REJECTED;
	}
	if (lifecycle && lifecycle->leave_source) {
		lifecycle->leave_source(transition, source_actual, source_known, p,
			lifecycle->leave_user);
	}

	if (source_policy == WORLD_SOURCE_STORE) {
		/* Match Angband's legacy level-change invariant: stored monster lists
		 * are dense.  Without this, a monster killed before a horizontal or
		 * satellite transition leaves a hole that the save format cannot
		 * represent. */
		compact_monsters(source_actual, 0);
		if (source_actual->name) string_free(source_actual->name);
		if (source_known->name) string_free(source_known->name);
		source_actual->name = source_actual_name;
		source_known->name = source_known_name;
		if (destination_is_stored) {
			chunk_list[actual_index] = source_actual;
			chunk_list[known_index] = source_known;
		} else {
			chunk_list_add_reserved(source_actual);
			chunk_list_add_reserved(source_known);
		}
	} else {
		if (destination_is_stored) {
			/* Removing the larger index first keeps the smaller one stable. */
			if (actual_index > known_index) {
				remove_stored_index(actual_index);
				remove_stored_index(known_index);
			} else {
				remove_stored_index(known_index);
				remove_stored_index(actual_index);
			}
		}
		retired->actual = source_actual;
		retired->known = source_known;
	}

	square_set_mon(source_actual, source_grid, 0);
	source_actual->turn = source_turn;
	source_known->turn = source_turn;
	cave = candidate->actual;
	p->cave = candidate->known;
	if (destination_is_generated) {
		cave->turn = turn;
		p->cave->turn = turn;
	}
	for (i = 0; i < p->cave->obj_max; i++) {
		if (cave->objects[i] && p->cave->objects[i]) {
			cave->objects[i]->known = p->cave->objects[i];
		}
	}
	player_place(cave, p, candidate->arrival);
	memset(candidate, 0, sizeof(*candidate));
	if (lifecycle && lifecycle->enter_destination) {
		lifecycle->enter_destination(transition, cave, p->cave, p,
			lifecycle->enter_user);
	}
	return WORLD_DESTINATION_OK;
}

/** Publish without mode-specific lifecycle work. */
enum world_destination_result world_destination_publish(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p, enum world_source_policy source_policy,
		struct world_retired_chunks *retired)
{
	return world_destination_publish_with_lifecycle(candidate, coordinator, p,
		source_policy, retired, NULL);
}

/**
 * Forget a candidate and return its coordinator to idle.  An off-list pair is
 * owned by the candidate and is destroyed here; a stored pair remains owned
 * by the chunk list.
 */
bool world_destination_candidate_abort(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator,
		struct player *p)
{
	if (!candidate || !coordinator) return false;
	if (candidate->storage == WORLD_DESTINATION_CANDIDATE_OWNED) {
		if (!p || p != player ||
				!candidate_storage_is_valid(candidate, NULL, NULL)) {
			return false;
		}
		discard_owned_pair(candidate->actual, candidate->known, p,
			candidate->rollback_artifacts);
	} else if (candidate->storage != WORLD_DESTINATION_CANDIDATE_STORED &&
			candidate->storage != WORLD_DESTINATION_CANDIDATE_EMPTY) {
		return false;
	}
	memset(candidate, 0, sizeof(*candidate));
	world_transition_coordinator_abort(coordinator);
	return true;
}

bool world_destination_candidate_is_ready(
		const struct world_destination_candidate *candidate)
{
	return candidate && candidate->actual && candidate->known &&
		(candidate->storage == WORLD_DESTINATION_CANDIDATE_STORED ||
		 candidate->storage == WORLD_DESTINATION_CANDIDATE_OWNED);
}

bool world_destination_candidate_is_publishable(
		const struct world_destination_candidate *candidate)
{
	return world_destination_candidate_is_ready(candidate) &&
		candidate->has_arrival;
}

/** Destroy a retired pair after the caller has completed source leave hooks. */
bool world_retired_chunks_dispose(struct world_retired_chunks *retired,
		struct player *p)
{
	if (!retired || !p || p != player || !retired->actual ||
			!retired->known || retired->actual == retired->known ||
			retired->actual == cave || retired->actual == p->cave ||
			retired->known == cave || retired->known == p->cave ||
			chunk_find(retired->actual) ||
			chunk_find(retired->known)) {
		return false;
	}
	cave_free(retired->known);
	wipe_mon_list(retired->actual, p);
	cave_free(retired->actual);
	memset(retired, 0, sizeof(*retired));
	return true;
}
