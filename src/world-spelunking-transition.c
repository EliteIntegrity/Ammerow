/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-transition.c
 * \brief Transactional entry and exit for spelunking locations.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "init.h"
#include "mon-make.h"
#include "player-timed.h"
#include "player-util.h"
#include "world-entry.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-layout.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-system.h"
#include "world-spelunking-system-data.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-visibility.h"

#define SPELUNKING_ENVELOPE_WIDTH 3
#define SPELUNKING_ENVELOPE_HEIGHT 3

static const struct loc envelope_arrival = { 1, 1 };

static uint32_t mix32(uint32_t value)
{
	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	value *= UINT32_C(0x846ca68b);
	value ^= value >> 16;
	return value;
}

/** Derive a stable per-save system seed without advancing gameplay RNG. */
static uint32_t generated_system_seed(
		const struct world_spelunk_system_profile *profile)
{
	uint32_t hash = UINT32_C(2166136261);
	const unsigned char *cursor = (const unsigned char *)profile->id;

	while (*cursor) {
		hash ^= *cursor++;
		hash *= UINT32_C(16777619);
	}
	return mix32(seed_flavor ^ hash ^ UINT32_C(0x63617665));
}

/** Destroy a newly allocated envelope which was never adopted. */
static void discard_envelope(struct chunk *actual, struct chunk *known)
{
	if (known) cave_free(known);
	if (actual) {
		wipe_mon_list(actual, player);
		cave_free(actual);
	}
}

/**
 * Allocate the smallest legal active chunk pair used by legacy save blocks.
 * It is a persistence envelope only: the mode router prevents terrain,
 * monsters, and top-down presentation from processing it.
 */
static bool make_envelope(const struct level *level, struct chunk **actual,
		struct chunk **known)
{
	struct chunk *a;
	struct chunk *k;
	int x;
	int y;

	if (!level || !actual || !known || level->mode != WORLD_MODE_SPELUNKING) {
		return false;
	}
	*actual = NULL;
	*known = NULL;
	a = cave_new(SPELUNKING_ENVELOPE_HEIGHT, SPELUNKING_ENVELOPE_WIDTH);
	k = cave_new(SPELUNKING_ENVELOPE_HEIGHT, SPELUNKING_ENVELOPE_WIDTH);
	if (!a || !k) {
		discard_envelope(a, k);
		return false;
	}
	a->depth = level->danger;
	k->depth = level->danger;
	if (!world_chunk_set_location(a, level->id, false) ||
			!world_chunk_set_location(k, level->id, true)) {
		discard_envelope(a, k);
		return false;
	}
	for (y = 0; y < SPELUNKING_ENVELOPE_HEIGHT; y++) {
		for (x = 0; x < SPELUNKING_ENVELOPE_WIDTH; x++) {
			square_set_feat(a, loc(x, y), FEAT_PERM);
			square_set_feat(k, loc(x, y), FEAT_PERM);
		}
	}
	square_set_feat(a, envelope_arrival, FEAT_FLOOR);
	square_set_feat(k, envelope_arrival, FEAT_FLOOR);
	*actual = a;
	*known = k;
	return true;
}

/** Verify the active pair really is an inert envelope for this location. */
static bool envelope_pair_is_active(const struct player *p,
		struct chunk *actual, struct chunk *known)
{
	const struct level *level = world_player_level(p);

	return p && p == player && actual && known && actual == cave &&
		known == p->cave && level && level->mode == WORLD_MODE_SPELUNKING &&
		actual->height == SPELUNKING_ENVELOPE_HEIGHT &&
		actual->width == SPELUNKING_ENVELOPE_WIDTH &&
		known->height == SPELUNKING_ENVELOPE_HEIGHT &&
		known->width == SPELUNKING_ENVELOPE_WIDTH &&
		!actual->is_known && known->is_known &&
		actual->depth == level->danger && known->depth == level->danger &&
		actual->obj_max == known->obj_max &&
		streq(actual->location_id, level->id) &&
		streq(known->location_id, level->id) &&
		loc_eq(p->grid, envelope_arrival) &&
		square_isfloor(actual, envelope_arrival) &&
		square_isfloor(known, envelope_arrival) &&
		square(actual, envelope_arrival)->mon == -1 &&
		square(known, envelope_arrival)->mon == 0;
}

bool world_spelunking_active_storage_is_valid(const struct player *p)
{
	return world_spelunk_player_is_active(p) &&
		p->spelunking_system &&
		world_spelunk_system_is_valid(p->spelunking_system) &&
		p->spelunking == world_spelunk_system_active_runtime(
			p->spelunking_system) &&
		envelope_pair_is_active(p, cave, p->cave);
}

bool world_spelunking_exit_grid_at(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, size_t index,
		struct loc *grid)
{
	const struct level *level;
	struct world_route *route;
	size_t current = 0;
	size_t i;

	if (!runtime || !grid || !world_spelunk_runtime_is_valid(runtime)) {
		return false;
	}
	if (system &&
			system->graph_version == WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		const struct world_spelunk_system_node *node = NULL;

		if (!world_spelunk_system_is_valid(system)) return false;
		for (i = 0; i < system->node_count; i++) {
			if (system->nodes[i].runtime == runtime) {
				node = &system->nodes[i];
				break;
			}
		}
		if (!node) return false;
		for (i = 0; i < system->portal_count; i++) {
			const struct world_spelunk_system_portal *portal =
				&system->portals[i];

			if (!portal->enabled || !portal->materialized ||
					!streq(portal->node_id, node->id)) {
				continue;
			}
			if (current++ == index) {
				*grid = loc(portal->x, portal->y);
				return true;
			}
		}
		return false;
	}
	level = level_by_id(runtime->location_id);
	if (!level || level->mode != WORLD_MODE_SPELUNKING) return false;
	for (route = world_routes; route; route = route->next) {
		const struct world_entry *entry;
		struct world_route *other;
		int matches = 0;

		if (!streq(route->from, level->id) ||
				!world_route_is_cross_mode(route)) {
			continue;
		}
		entry = world_entry_by_id(level, route->from_entry);
		if (!entry || entry->kind != WORLD_ENTRY_GRID) continue;
		/* An ambiguous coordinate is not a valid player-facing exit. */
		for (other = world_routes; other; other = other->next) {
			const struct world_entry *other_entry;

			if (!streq(other->from, level->id) ||
					!world_route_is_cross_mode(other)) {
				continue;
			}
			other_entry = world_entry_by_id(level, other->from_entry);
			if (other_entry && other_entry->kind == WORLD_ENTRY_GRID &&
					loc_eq(other_entry->grid, entry->grid)) {
				matches++;
			}
		}
		if (matches != 1) continue;
		if (current++ == index) {
			*grid = entry->grid;
			return true;
		}
	}
	return false;
}

static bool stage_system_arrival(struct world_spelunking_entry_stage *stage,
		const char *entry_id)
{
	const struct world_spelunk_system_portal *portal;
	const struct world_spelunk_system_node *node;
	struct world_spelunk_runtime *runtime;

	if (!stage || !stage->system || !entry_id ||
			stage->system->graph_version != WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		return false;
	}
	portal = world_spelunk_system_portal_by_entry(stage->system, entry_id);
	node = portal ? world_spelunk_system_node_by_id(stage->system,
		portal->node_id) : NULL;
	runtime = node ? node->runtime : NULL;
	if (!portal || !portal->enabled || !portal->materialized || !runtime ||
			portal->x < 0 || portal->x >= runtime->state.map.width ||
			portal->y < 0 || portal->y + 1 >= runtime->state.map.height ||
			runtime->cells[(size_t)portal->y * runtime->state.map.width +
				portal->x] != WORLD_SPELUNK_AIR ||
			runtime->cells[(size_t)(portal->y + 1) *
				runtime->state.map.width + portal->x] != WORLD_SPELUNK_ROCK) {
		return false;
	}
	stage->runtime = runtime;
	my_strcpy(stage->arrival_node_id, node->id,
		sizeof(stage->arrival_node_id));
	stage->arrival_x = portal->x;
	stage->arrival_y = portal->y;
	return true;
}

static bool entry_stage_is_ready(
		const struct world_spelunking_entry_stage *stage,
		const struct world_transition_coordinator *coordinator,
		const struct player *p)
{
	const struct level *source;
	const struct level *destination;
	const struct world_route *route;

	if (!stage || !coordinator || !p || stage->committed ||
			!world_spelunk_runtime_is_valid(stage->runtime)) {
		return false;
	}
	source = world_player_level(p);
	destination = level_by_id(coordinator->transition.destination_id);
	route = world_route_by_id(coordinator->transition.route_id);
	if (!source || source->mode != WORLD_MODE_TOP_DOWN ||
			!streq(source->id, coordinator->transition.source_id)) {
		return false;
	}
	if (!route || !world_route_is_cross_mode(route) ||
			!coordinator->source_grid_staged || !destination ||
			destination->mode != WORLD_MODE_SPELUNKING ||
			!streq(stage->runtime->location_id, destination->id)) {
		return false;
	}
	if (stage->system) {
		if (!world_spelunk_system_is_valid(stage->system) ||
				!world_spelunk_system_owns_runtime(stage->system,
					stage->runtime) || !stage->arrival_node_id[0]) {
			return false;
		}
		return stage->owns_system ?
			p->spelunking_system == NULL && p->spelunking == NULL :
			p->spelunking_system == stage->system &&
			p->spelunking == world_spelunk_system_active_runtime(
				stage->system);
	}
	return stage->owns_runtime ? p->spelunking == NULL :
		p->spelunking == stage->runtime;
}

/** The top-down source is still active during this post-commit hook. */
static void leave_top_down_source(const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		void *user)
{
	struct world_spelunking_entry_stage *stage = user;
	const struct level *source = level_by_id(transition->source_id);

	(void)stage;
	(void)source;
	(void)actual;
	(void)known;
	(void)p;
	assert(stage && !stage->committed);
	assert(source && source->mode == WORLD_MODE_TOP_DOWN);
	assert(actual == cave && known == p->cave);
	on_leave_level();
}

/** Transfer the already validated runtime after the destination is active. */
static void enter_spelunking_destination(
		const struct world_transition *transition, struct chunk *actual,
		struct chunk *known, struct player *p, void *user)
{
	struct world_spelunking_entry_stage *stage = user;
	const struct level *destination = level_by_id(transition->destination_id);
	const struct world_entry *entry = destination ?
		world_entry_by_id(destination, transition->destination_entry) : NULL;
	bool entered;
	bool unlocked;
	bool selected;

	(void)transition;
	(void)actual;
	(void)known;
	assert(stage && !stage->committed && stage->runtime);
	assert(streq(stage->runtime->location_id, transition->destination_id));
	assert(actual == cave && known == p->cave);
	if (stage->system) {
		selected = world_spelunk_system_set_active_node(stage->system,
			stage->arrival_node_id);
		assert(selected);
		(void)selected;
		if (stage->owns_system) {
			assert(!p->spelunking_system && !p->spelunking);
			p->spelunking_system = stage->system;
			stage->owns_system = false;
		} else {
			assert(p->spelunking_system == stage->system);
		}
		p->spelunking = world_spelunk_system_active_runtime(stage->system);
		assert(p->spelunking == stage->runtime);
	} else if (stage->owns_runtime) {
		assert(!p->spelunking);
		p->spelunking = stage->runtime;
		stage->owns_runtime = false;
	} else {
		assert(p->spelunking == stage->runtime);
	}
	/* The compatibility pointer is non-owning from this point onward. */
	assert(player_spelunking_ensure_system(p));
	/* A persistent location can have several physical entrances.  Re-entering
	 * through another one must place the player at that portal, while terrain,
	 * infrastructure, objects, and explored cells remain untouched. */
	assert(entry && entry->kind == WORLD_ENTRY_GRID);
	p->spelunking->state.x = stage->system ? stage->arrival_x : entry->grid.x;
	p->spelunking->state.y = stage->system ? stage->arrival_y : entry->grid.y;
	p->spelunking->state.fall_start_y = p->spelunking->state.y;
	p->spelunking->state.grip_target_x = -1;
	p->spelunking->state.grip_target_y = -1;
	p->spelunking->state.movement = WORLD_SPELUNK_STANDING;
	p->spelunking->state.jump_holding = false;
	selected = world_spelunk_player_sync_resources(p);
	assert(selected);
	(void)selected;
	world_spelunk_visibility_follow_player(p->spelunking);
	assert(world_spelunk_runtime_is_valid(p->spelunking));
	stage->committed = true;
	p->upkeep->autosave = true;
	unlocked = world_player_unlock_route(p, transition->route_id);
	assert(unlocked);
	(void)unlocked;
	assert(world_spelunk_player_is_active(p));
	entered = on_enter_world_location();
	assert(entered);
	(void)entered;
}

/** Side-view departure performs no cave-specific cleanup. */
static void leave_spelunking_source(
		const struct world_transition *transition, struct chunk *actual,
		struct chunk *known, struct player *p, void *user)
{
	const struct level *source = level_by_id(transition->source_id);

	(void)user;
	(void)source;
	(void)actual;
	(void)known;
	assert(source && source->mode == WORLD_MODE_SPELUNKING);
	assert(actual == cave && known == p->cave);
	world_spelunk_visibility_end_peek(p->spelunking);
	player_clear_timed(p, TMD_COMMAND, false, false);
	world_player_cancel_physical_route(p);
	event_signal(EVENT_MESSAGE_FLUSH);
}

/** Resume ordinary top-down housekeeping after the envelope is retired. */
static void enter_top_down_destination(
		const struct world_transition *transition, struct chunk *actual,
		struct chunk *known, struct player *p, void *user)
{
	const struct level *destination = level_by_id(transition->destination_id);
	bool unlocked;

	(void)user;
	(void)destination;
	assert(destination && destination->mode == WORLD_MODE_TOP_DOWN);
	assert(actual == cave && known == p->cave);
	unlocked = world_player_unlock_route(p, transition->route_id);
	assert(unlocked);
	(void)unlocked;
	world_top_down_enter_destination(transition, actual, known, p, true);
}

enum world_destination_result world_spelunking_entry_prepare(
		struct world_spelunking_entry_stage *stage,
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p)
{
	const struct level *source;
	const struct level *destination;
	const struct world_route *route;
	const struct world_spelunk_system_profile *profile;
	struct world_spelunk_runtime *runtime = NULL;
	struct world_spelunk_system *system = NULL;
	struct chunk *actual = NULL;
	struct chunk *known = NULL;
	enum world_destination_result result;
	bool owns_runtime = false;

	if (!stage || !candidate || !coordinator || !p || p != player ||
			stage->runtime || stage->system || stage->owns_runtime ||
			stage->owns_system || stage->committed ||
			coordinator->phase != WORLD_TRANSITION_PREPARED) {
		return WORLD_DESTINATION_INVALID;
	}
	source = world_player_level(p);
	destination = level_by_id(coordinator->transition.destination_id);
	route = world_route_by_id(coordinator->transition.route_id);
	if (!source || source->mode != WORLD_MODE_TOP_DOWN || !destination ||
			destination->mode != WORLD_MODE_SPELUNKING ||
			!route || !world_route_is_cross_mode(route) ||
			!coordinator->source_grid_staged ||
			!world_entry_by_id(destination,
				coordinator->transition.destination_entry)) {
		return WORLD_DESTINATION_UNSUPPORTED;
	}

	if (p->spelunking_system &&
			p->spelunking_system->graph_version ==
				WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		if (!world_spelunk_system_is_valid(p->spelunking_system) ||
				!streq(p->spelunking_system->location_id,
					destination->id) ||
				p->spelunking != world_spelunk_system_active_runtime(
					p->spelunking_system)) {
			return WORLD_DESTINATION_INCONSISTENT;
		}
		stage->system = p->spelunking_system;
		if (!stage_system_arrival(stage,
				coordinator->transition.destination_entry)) {
			stage->system = NULL;
			return WORLD_DESTINATION_INCONSISTENT;
		}
		runtime = stage->runtime;
	} else if (p->spelunking) {
		if (!world_spelunk_runtime_is_valid(p->spelunking) ||
				!streq(p->spelunking->location_id, destination->id)) {
			return WORLD_DESTINATION_INCONSISTENT;
		}
		if (!world_spelunk_layout_upgrade_runtime(p->spelunking)) {
			return WORLD_DESTINATION_INCONSISTENT;
		}
		runtime = p->spelunking;
	} else {
		profile = world_spelunk_system_profile_by_location(destination->id);
		if (!profile) return WORLD_DESTINATION_UNSUPPORTED;
		if (world_spelunk_publish_system_candidate(profile,
				generated_system_seed(profile), z_info->move_energy,
				coordinator->transition.destination_entry, &system) !=
				WORLD_SPELUNK_PUBLICATION_OK || !system) {
			return WORLD_DESTINATION_INVALID;
		}
		stage->system = system;
		stage->owns_system = true;
		if (!stage_system_arrival(stage,
				coordinator->transition.destination_entry)) {
			world_spelunk_system_free(system);
			memset(stage, 0, sizeof(*stage));
			return WORLD_DESTINATION_INVALID;
		}
		runtime = stage->runtime;
	}

	if (!make_envelope(destination, &actual, &known)) {
		if (stage->owns_system) world_spelunk_system_free(stage->system);
		if (owns_runtime) world_spelunk_runtime_free(runtime);
		memset(stage, 0, sizeof(*stage));
		return WORLD_DESTINATION_INVALID;
	}
	result = world_destination_prepare_owned(candidate, coordinator, actual,
		known);
	if (result != WORLD_DESTINATION_OK) {
		discard_envelope(actual, known);
		if (stage->owns_system) world_spelunk_system_free(stage->system);
		if (owns_runtime) world_spelunk_runtime_free(runtime);
		memset(stage, 0, sizeof(*stage));
		return result;
	}
	result = world_destination_candidate_set_arrival(candidate, coordinator,
		coordinator->transition.destination_entry, envelope_arrival);
	if (result != WORLD_DESTINATION_OK) {
		(void)world_destination_candidate_abort(candidate, coordinator, p);
		if (stage->owns_system) world_spelunk_system_free(stage->system);
		if (owns_runtime) world_spelunk_runtime_free(runtime);
		memset(stage, 0, sizeof(*stage));
		return result;
	}
	if (!stage->system) {
		stage->runtime = runtime;
		stage->owns_runtime = owns_runtime;
	}
	return WORLD_DESTINATION_OK;
}

enum world_destination_result world_spelunking_entry_publish(
		struct world_spelunking_entry_stage *stage,
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p)
{
	struct world_destination_lifecycle lifecycle;

	if (!entry_stage_is_ready(stage, coordinator, p)) {
		return WORLD_DESTINATION_STALE;
	}
	lifecycle.leave_source = leave_top_down_source;
	lifecycle.leave_user = stage;
	lifecycle.enter_destination = enter_spelunking_destination;
	lifecycle.enter_user = stage;
	return world_destination_publish_with_lifecycle(candidate, coordinator, p,
		WORLD_SOURCE_STORE, NULL, &lifecycle);
}

bool world_spelunking_entry_abort(
		struct world_spelunking_entry_stage *stage,
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p)
{
	if (!stage || stage->committed ||
			!world_destination_candidate_abort(candidate, coordinator, p)) {
		return false;
	}
	if (stage->owns_system) {
		world_spelunk_system_free(stage->system);
	} else if (stage->owns_runtime) {
		world_spelunk_runtime_free(stage->runtime);
	}
	memset(stage, 0, sizeof(*stage));
	return true;
}

void world_spelunking_entry_finish(
		struct world_spelunking_entry_stage *stage)
{
	if (!stage || !stage->committed || stage->owns_runtime ||
			stage->owns_system) {
		return;
	}
	memset(stage, 0, sizeof(*stage));
}

static void abort_entry(struct world_spelunking_entry_stage *stage,
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p)
{
	if (!world_spelunking_entry_abort(stage, candidate, coordinator, p)) {
		world_transition_coordinator_abort(coordinator);
	}
}

enum world_spelunking_entry_result world_spelunking_entry_execute(
		struct player *p, struct chunk *actual, const char *route_id,
		struct world_spelunking_entry_report *report)
{
	struct world_spelunking_entry_report local = {
		.result = WORLD_SPELUNKING_ENTRY_INVALID,
		.safety = WORLD_TRAVEL_SAFETY_INVALID,
		.transition = WORLD_TRANSITION_INVALID,
		.resources = WORLD_TRAVEL_RESOURCES_INVALID,
		.destination = WORLD_DESTINATION_INVALID
	};
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct world_spelunking_entry_stage stage = { 0 };

	if (report) *report = local;
	if (!p || !actual || p != player || actual != cave || !p->upkeep ||
			p->upkeep->energy_use != 0 || !world_id_is_valid(route_id) ||
			world_spelunk_player_is_active(p)) {
		return local.result;
	}
	local.transition = world_transition_coordinator_begin_cross_mode(
		&coordinator, p, actual, route_id);
	if (local.transition != WORLD_TRANSITION_OK) {
		local.result = WORLD_SPELUNKING_ENTRY_TRANSITION_REJECTED;
		if (report) *report = local;
		return local.result;
	}
	local.safety = world_travel_check_safety(p, actual);
	if (local.safety != WORLD_TRAVEL_SAFETY_OK &&
			local.safety != WORLD_TRAVEL_SAFETY_THREATENED &&
			local.safety != WORLD_TRAVEL_SAFETY_TEMPORARY_EFFECT) {
		local.result = WORLD_SPELUNKING_ENTRY_SAFETY_REJECTED;
		abort_entry(&stage, &candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	if (!world_transition_coordinator_stage_clock(&coordinator)) {
		local.result = WORLD_SPELUNKING_ENTRY_TIME_REJECTED;
		abort_entry(&stage, &candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	local.resources = world_transition_coordinator_stage_resources(
		&coordinator, p);
	if (local.resources != WORLD_TRAVEL_RESOURCES_OK) {
		local.result = WORLD_SPELUNKING_ENTRY_RESOURCES_REJECTED;
		abort_entry(&stage, &candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	local.destination = world_spelunking_entry_prepare(&stage, &candidate,
		&coordinator, p);
	if (local.destination != WORLD_DESTINATION_OK) {
		local.result = WORLD_SPELUNKING_ENTRY_DESTINATION_REJECTED;
		abort_entry(&stage, &candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	local.destination = world_spelunking_entry_publish(&stage, &candidate,
		&coordinator, p);
	if (local.destination != WORLD_DESTINATION_OK) {
		local.result = WORLD_SPELUNKING_ENTRY_DESTINATION_REJECTED;
		abort_entry(&stage, &candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	world_spelunking_entry_finish(&stage);
	p->upkeep->energy_use = z_info->move_energy;
	local.result = WORLD_SPELUNKING_ENTRY_OK;
	if (report) *report = local;
	return local.result;
}

enum world_destination_result world_spelunking_exit_publish(
		struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p,
		struct world_retired_chunks *retired)
{
	const struct level *destination;
	const struct world_route *route;
	struct world_destination_lifecycle lifecycle = {
		.leave_source = leave_spelunking_source,
		.enter_destination = enter_top_down_destination
	};

	if (!candidate || !coordinator || !p || !retired ||
			!world_spelunking_active_storage_is_valid(p)) {
		return WORLD_DESTINATION_INVALID;
	}
	destination = level_by_id(coordinator->transition.destination_id);
	route = world_route_by_id(coordinator->transition.route_id);
	if (!destination || destination->mode != WORLD_MODE_TOP_DOWN || !route ||
			!world_route_is_cross_mode(route) ||
			!coordinator->source_grid_staged) {
		return WORLD_DESTINATION_UNSUPPORTED;
	}
	return world_destination_publish_with_lifecycle(candidate, coordinator, p,
		WORLD_SOURCE_RETIRE, retired, &lifecycle);
}

/** Find one unambiguous authored shaft at the active side-view coordinate. */
static const struct world_route *exit_route_here(const struct player *p)
{
	const struct level *level = world_player_level(p);
	const struct world_route *found = NULL;
	struct world_route *route;
	struct loc here;
	size_t i;

	if (!level || level->mode != WORLD_MODE_SPELUNKING ||
			!world_spelunk_player_is_active(p)) {
		return NULL;
	}
	here = loc(p->spelunking->state.x, p->spelunking->state.y);
	if (p->spelunking_system->graph_version ==
			WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		const char *active_node_id = p->spelunking_system->active_node_id;

		for (i = 0; i < p->spelunking_system->portal_count; i++) {
			const struct world_spelunk_system_portal *portal =
				&p->spelunking_system->portals[i];

			if (!portal->enabled || !portal->materialized ||
					!streq(portal->node_id, active_node_id) ||
					portal->x != here.x || portal->y != here.y) {
				continue;
			}
			for (route = world_routes; route; route = route->next) {
				if (!streq(route->from, level->id) ||
						!streq(route->from_entry, portal->entry_id) ||
						!world_route_is_cross_mode(route)) {
					continue;
				}
				if (found) return NULL;
				found = route;
			}
		}
		return found;
	}
	for (route = world_routes; route; route = route->next) {
		const struct world_entry *entry;

		if (!streq(route->from, level->id) ||
				!world_route_is_cross_mode(route)) {
			continue;
		}
		entry = world_entry_by_id(level, route->from_entry);
		if (!entry || entry->kind != WORLD_ENTRY_GRID ||
				!loc_eq(entry->grid, here)) {
			continue;
		}
		if (found) return NULL;
		found = route;
	}
	return found;
}

static void abort_exit(struct world_destination_candidate *candidate,
		struct world_transition_coordinator *coordinator, struct player *p)
{
	if (!world_destination_candidate_abort(candidate, coordinator, p)) {
		world_transition_coordinator_abort(coordinator);
	}
}

enum world_spelunking_exit_result world_spelunking_exit_execute(
		struct player *p, struct chunk *actual,
		struct world_spelunking_exit_report *report)
{
	struct world_spelunking_exit_report local = {
		.result = WORLD_SPELUNKING_EXIT_INVALID,
		.transition = WORLD_TRANSITION_INVALID,
		.resources = WORLD_TRAVEL_RESOURCES_INVALID,
		.destination = WORLD_DESTINATION_INVALID
	};
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct world_retired_chunks retired = { 0 };
	const struct world_route *route;
	bool disposed;
	bool discovered;

	if (report) *report = local;
	if (!p || !actual || p != player || actual != cave || !p->upkeep ||
			p->upkeep->energy_use != 0 ||
			!world_spelunking_active_storage_is_valid(p)) {
		return local.result;
	}
	if (p->spelunking->state.movement != WORLD_SPELUNK_STANDING ||
			!world_spelunk_is_grounded(&p->spelunking->state)) {
		local.result = WORLD_SPELUNKING_EXIT_UNSTABLE;
		if (report) *report = local;
		return local.result;
	}
	route = exit_route_here(p);
	if (!route) {
		local.result = WORLD_SPELUNKING_EXIT_NOT_HERE;
		if (report) *report = local;
		return local.result;
	}
	my_strcpy(local.route_id, route->id, sizeof(local.route_id));
	local.transition = world_transition_coordinator_begin_cross_mode(
		&coordinator, p, actual, route->id);
	if (local.transition != WORLD_TRANSITION_OK) {
		local.result = WORLD_SPELUNKING_EXIT_TRANSITION_REJECTED;
		if (report) *report = local;
		return local.result;
	}
	if (!world_transition_coordinator_stage_clock(&coordinator)) {
		local.result = WORLD_SPELUNKING_EXIT_TIME_REJECTED;
		abort_exit(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	local.resources = world_transition_coordinator_stage_resources(
		&coordinator, p);
	if (local.resources != WORLD_TRAVEL_RESOURCES_OK) {
		local.result = WORLD_SPELUNKING_EXIT_RESOURCES_REJECTED;
		abort_exit(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	local.destination = world_destination_prepare_stored(&candidate,
		&coordinator);
	if (local.destination == WORLD_DESTINATION_NOT_FOUND) {
		local.destination = world_destination_generate_owned(&candidate,
			&coordinator, p, 0, 0);
	}
	if (local.destination == WORLD_DESTINATION_OK) {
		local.destination = world_destination_candidate_resolve_arrival(
			&candidate, &coordinator);
	}
	if (local.destination != WORLD_DESTINATION_OK) {
		local.result = WORLD_SPELUNKING_EXIT_DESTINATION_REJECTED;
		abort_exit(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	local.destination = world_spelunking_exit_publish(&candidate,
		&coordinator, p, &retired);
	if (local.destination != WORLD_DESTINATION_OK) {
		local.result = WORLD_SPELUNKING_EXIT_DESTINATION_REJECTED;
		abort_exit(&candidate, &coordinator, p);
		if (report) *report = local;
		return local.result;
	}
	if (p->spelunking_system->graph_version ==
			WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		const struct world_spelunk_system_portal *portal =
			world_spelunk_system_portal_by_entry(p->spelunking_system,
				route->from_entry);

		assert(portal);
		discovered = portal && world_spelunk_system_discover_portal(
			p->spelunking_system, portal->id);
		assert(discovered);
		(void)discovered;
	}
	disposed = world_retired_chunks_dispose(&retired, p);
	assert(disposed);
	(void)disposed;
	p->upkeep->energy_use = z_info->move_energy;
	local.result = WORLD_SPELUNKING_EXIT_OK;
	if (report) *report = local;
	return local.result;
}

/** Find the authored magical return, independently of physical cave exits. */
static const struct world_route *recall_route_for_player(const struct player *p)
{
	const struct level *source = world_player_level(p);
	const struct world_route *found = NULL;
	struct world_route *route;

	if (!source || source->mode != WORLD_MODE_SPELUNKING) return NULL;
	for (route = world_routes; route; route = route->next) {
		const struct level *destination;

		if (!streq(route->from, source->id) || !world_route_is_recall(route)) {
			continue;
		}
		destination = level_by_id(route->to);
		if (!destination || destination->kind != WORLD_LOCATION_HUB ||
				(route->requires_unlock &&
				 !world_player_route_is_unlocked(p, route->id))) {
			continue;
		}
		/* Ambiguous data must fail closed rather than choosing by list order. */
		if (found) return NULL;
		found = route;
	}
	return found;
}

/** Resume top-down play without teaching a route the player did not traverse. */
static void enter_recalled_destination(
		const struct world_transition *transition, struct chunk *actual,
		struct chunk *known, struct player *p, void *user)
{
	(void)user;
	world_top_down_enter_destination(transition, actual, known, p, true);
}

bool world_spelunking_recall_execute(struct player *p, struct chunk *actual)
{
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct world_retired_chunks retired = { 0 };
	struct world_destination_lifecycle lifecycle = {
		.leave_source = leave_spelunking_source,
		.enter_destination = enter_recalled_destination
	};
	const struct world_route *route;
	enum world_destination_result destination;
	bool disposed;

	if (!p || !actual || p != player || actual != cave || !p->upkeep ||
			!world_spelunking_active_storage_is_valid(p)) {
		return false;
	}
	route = recall_route_for_player(p);
	if (!route || world_transition_coordinator_begin_relocation(&coordinator,
			p, route->id) != WORLD_TRANSITION_OK) {
		return false;
	}
	if (!world_transition_coordinator_stage_clock(&coordinator) ||
			world_transition_coordinator_stage_resources(&coordinator, p) !=
				WORLD_TRAVEL_RESOURCES_OK) {
		abort_exit(&candidate, &coordinator, p);
		return false;
	}
	destination = world_destination_prepare_stored(&candidate, &coordinator);
	if (destination == WORLD_DESTINATION_NOT_FOUND) {
		destination = world_destination_generate_owned(&candidate, &coordinator,
			p, 0, 0);
	}
	if (destination == WORLD_DESTINATION_OK) {
		destination = world_destination_candidate_resolve_arrival(&candidate,
			&coordinator);
	}
	if (destination != WORLD_DESTINATION_OK) {
		abort_exit(&candidate, &coordinator, p);
		return false;
	}
	destination = world_destination_publish_with_lifecycle(&candidate,
		&coordinator, p, WORLD_SOURCE_RETIRE, &retired, &lifecycle);
	if (destination != WORLD_DESTINATION_OK) {
		abort_exit(&candidate, &coordinator, p);
		return false;
	}
	disposed = world_retired_chunks_dispose(&retired, p);
	assert(disposed);
	return disposed;
}
