/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-passage.c
 * \brief Reciprocal travel between materialized cave-system sections.
 */

#include "angband.h"

#include "world-spelunking-passage.h"

#include "world-spelunking-adapter.h"
#include "world-spelunking-visibility.h"
#include "z-util.h"

#include <string.h>

struct passage_side {
	struct world_spelunk_system_node *node;
	const char *endpoint_id;
	int x;
	int y;
	bool materialized;
};

struct materialized_endpoint_before {
	struct world_spelunk_system_edge *edge;
	bool is_a;
	int16_t x;
	int16_t y;
	bool materialized;
};

struct materialization_before {
	struct world_spelunk_system_node *node;
	bool discovered;
	struct materialized_endpoint_before endpoints[
		WORLD_SPELUNK_SYSTEM_EDGE_MAX];
	size_t endpoint_count;
};

static bool resolve_passage(struct world_spelunk_system *system,
		const char *endpoint_id, struct world_spelunk_system_edge **edge_out,
		struct passage_side *source, struct passage_side *destination)
{
	size_t i;

	for (i = 0; i < system->edge_count; i++) {
		struct world_spelunk_system_edge *edge = &system->edges[i];
		bool from_a;

		if (streq(edge->endpoint_a_id, endpoint_id)) {
			from_a = true;
		} else if (streq(edge->endpoint_b_id, endpoint_id)) {
			from_a = false;
		} else {
			continue;
		}
		source->node = world_spelunk_system_node_by_id_mutable(system,
			from_a ? edge->node_a_id : edge->node_b_id);
		destination->node = world_spelunk_system_node_by_id_mutable(system,
			from_a ? edge->node_b_id : edge->node_a_id);
		source->endpoint_id = from_a ? edge->endpoint_a_id :
			edge->endpoint_b_id;
		destination->endpoint_id = from_a ? edge->endpoint_b_id :
			edge->endpoint_a_id;
		source->x = from_a ? edge->endpoint_a_x : edge->endpoint_b_x;
		source->y = from_a ? edge->endpoint_a_y : edge->endpoint_b_y;
		destination->x = from_a ? edge->endpoint_b_x : edge->endpoint_a_x;
		destination->y = from_a ? edge->endpoint_b_y : edge->endpoint_a_y;
		source->materialized = from_a ? edge->endpoint_a_materialized :
			edge->endpoint_b_materialized;
		destination->materialized = from_a ? edge->endpoint_b_materialized :
			edge->endpoint_a_materialized;
		*edge_out = edge;
		return source->node && destination->node;
	}
	return false;
}

static bool destination_is_safe(const struct passage_side *destination)
{
	const struct world_spelunk_runtime *runtime = destination->node->runtime;
	int width;
	int height;

	if (!destination->materialized || !runtime) return false;
	width = runtime->state.map.width;
	height = runtime->state.map.height;
	return destination->x >= 0 && destination->x < width &&
		destination->y >= 0 && destination->y + 1 < height &&
		runtime->cells[(size_t)destination->y * width + destination->x] ==
			WORLD_SPELUNK_AIR &&
		runtime->cells[(size_t)(destination->y + 1) * width +
			destination->x] == WORLD_SPELUNK_ROCK &&
		!world_spelunk_runtime_ground_object_at(runtime, destination->x,
			destination->y) &&
		!world_spelunk_runtime_actor_at(runtime, destination->x,
			destination->y);
}

enum world_spelunk_passage_result world_spelunk_system_traverse_passage(
		struct world_spelunk_system *system, const char *endpoint_id)
{
	struct world_spelunk_system_edge *edge = NULL;
	struct passage_side source = { 0 };
	struct passage_side destination = { 0 };
	struct world_spelunk_state destination_before;
	char active_before[WORLD_ID_LEN];
	bool edge_discovered_before;
	bool source_discovered_before;
	bool destination_discovered_before;
	struct world_spelunk_runtime *source_runtime;
	struct world_spelunk_runtime *destination_runtime;

	if (!system || !world_id_is_valid(endpoint_id) ||
			!world_spelunk_system_is_valid(system) ||
			!resolve_passage(system, endpoint_id, &edge, &source,
				&destination)) {
		return WORLD_SPELUNK_PASSAGE_INVALID;
	}
	if (!streq(source.node->id, system->active_node_id) ||
			!source.materialized || !source.node->runtime) {
		return WORLD_SPELUNK_PASSAGE_NOT_HERE;
	}
	source_runtime = source.node->runtime;
	if (source_runtime->state.x != source.x ||
			source_runtime->state.y != source.y) {
		return WORLD_SPELUNK_PASSAGE_NOT_HERE;
	}
	if (source_runtime->state.movement != WORLD_SPELUNK_STANDING ||
			!world_spelunk_is_grounded(&source_runtime->state)) {
		return WORLD_SPELUNK_PASSAGE_UNSTABLE;
	}
	if (!destination.materialized || !destination.node->runtime) {
		return WORLD_SPELUNK_PASSAGE_DESTINATION_UNAVAILABLE;
	}
	if (!destination_is_safe(&destination)) {
		return WORLD_SPELUNK_PASSAGE_DESTINATION_BLOCKED;
	}
	destination_runtime = destination.node->runtime;
	destination_before = destination_runtime->state;
	my_strcpy(active_before, system->active_node_id, sizeof(active_before));
	edge_discovered_before = edge->discovered;
	source_discovered_before = source.node->discovered;
	destination_discovered_before = destination.node->discovered;
	destination_runtime->state.x = destination.x;
	destination_runtime->state.y = destination.y;
	destination_runtime->state.fall_start_y = destination.y;
	destination_runtime->state.grip_target_x = -1;
	destination_runtime->state.grip_target_y = -1;
	destination_runtime->state.movement = WORLD_SPELUNK_STANDING;
	destination_runtime->state.jump_holding = false;
	edge->discovered = true;
	source.node->discovered = true;
	destination.node->discovered = true;
	my_strcpy(system->active_node_id, destination.node->id,
		sizeof(system->active_node_id));
	if (!world_spelunk_system_is_valid(system)) {
		destination_runtime->state = destination_before;
		edge->discovered = edge_discovered_before;
		source.node->discovered = source_discovered_before;
		destination.node->discovered = destination_discovered_before;
		my_strcpy(system->active_node_id, active_before,
			sizeof(system->active_node_id));
		return WORLD_SPELUNK_PASSAGE_INVALID;
	}
	world_spelunk_visibility_end_peek(source_runtime);
	world_spelunk_visibility_follow_player(destination_runtime);
	return WORLD_SPELUNK_PASSAGE_OK;
}

bool world_spelunk_system_passage_grid_at(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, size_t index,
		struct loc *grid, enum world_spelunk_passage_direction *direction)
{
	const struct world_spelunk_system_node *node = NULL;
	size_t current = 0;
	size_t i;

	if (!system || !runtime || !grid || !direction ||
			!world_spelunk_system_is_valid(system)) {
		return false;
	}
	for (i = 0; i < system->node_count; i++) {
		if (system->nodes[i].runtime == runtime) {
			node = &system->nodes[i];
			break;
		}
	}
	if (!node) return false;
	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &system->edges[i];
		bool is_a;
		bool materialized;

		if (streq(edge->node_a_id, node->id)) {
			is_a = true;
			materialized = edge->endpoint_a_materialized;
		} else if (streq(edge->node_b_id, node->id)) {
			is_a = false;
			materialized = edge->endpoint_b_materialized;
		} else {
			continue;
		}
		if (!materialized) continue;
		if (current++ != index) continue;
		*grid = is_a ? loc(edge->endpoint_a_x, edge->endpoint_a_y) :
			loc(edge->endpoint_b_x, edge->endpoint_b_y);
		*direction = is_a ? WORLD_SPELUNK_PASSAGE_DOWN :
			WORLD_SPELUNK_PASSAGE_UP;
		return true;
	}
	return false;
}

/** Resolve the one directionally correct endpoint beneath the active player. */
static struct world_spelunk_system_edge *passage_here(
		struct world_spelunk_system *system,
		enum world_spelunk_passage_direction direction,
		const char **source_endpoint, const char **destination_node,
		const char **destination_endpoint)
{
	struct world_spelunk_runtime *runtime =
		world_spelunk_system_active_runtime(system);
	struct world_spelunk_system_edge *found = NULL;
	size_t i;

	if (!runtime) return NULL;
	for (i = 0; i < system->edge_count; i++) {
		struct world_spelunk_system_edge *edge = &system->edges[i];
		bool matches = direction == WORLD_SPELUNK_PASSAGE_DOWN ?
			streq(edge->node_a_id, system->active_node_id) :
			streq(edge->node_b_id, system->active_node_id);
		bool materialized = direction == WORLD_SPELUNK_PASSAGE_DOWN ?
			edge->endpoint_a_materialized : edge->endpoint_b_materialized;
		int x = direction == WORLD_SPELUNK_PASSAGE_DOWN ?
			edge->endpoint_a_x : edge->endpoint_b_x;
		int y = direction == WORLD_SPELUNK_PASSAGE_DOWN ?
			edge->endpoint_a_y : edge->endpoint_b_y;

		if (!matches || !materialized || runtime->state.x != x ||
				runtime->state.y != y) {
			continue;
		}
		/* System validation forbids reused coordinates. Retain a fail-closed
		 * ambiguity check at the player boundary. */
		if (found) return NULL;
		found = edge;
	}
	if (!found) return NULL;
	if (direction == WORLD_SPELUNK_PASSAGE_DOWN) {
		*source_endpoint = found->endpoint_a_id;
		*destination_node = found->node_b_id;
		*destination_endpoint = found->endpoint_b_id;
	} else {
		*source_endpoint = found->endpoint_b_id;
		*destination_node = found->node_a_id;
		*destination_endpoint = found->endpoint_a_id;
	}
	return found;
}

static bool snapshot_unmaterialized_node(struct world_spelunk_system *system,
		const char *node_id, struct materialization_before *before)
{
	size_t i;

	memset(before, 0, sizeof(*before));
	before->node = world_spelunk_system_node_by_id_mutable(system, node_id);
	if (!before->node || before->node->runtime) return false;
	before->discovered = before->node->discovered;
	for (i = 0; i < system->edge_count; i++) {
		struct world_spelunk_system_edge *edge = &system->edges[i];
		struct materialized_endpoint_before *endpoint;

		if (!streq(edge->node_a_id, node_id) &&
				!streq(edge->node_b_id, node_id)) {
			continue;
		}
		if (before->endpoint_count >= WORLD_SPELUNK_SYSTEM_EDGE_MAX) {
			return false;
		}
		endpoint = &before->endpoints[before->endpoint_count++];
		endpoint->edge = edge;
		endpoint->is_a = streq(edge->node_a_id, node_id);
		endpoint->x = endpoint->is_a ? edge->endpoint_a_x :
			edge->endpoint_b_x;
		endpoint->y = endpoint->is_a ? edge->endpoint_a_y :
			edge->endpoint_b_y;
		endpoint->materialized = endpoint->is_a ?
			edge->endpoint_a_materialized : edge->endpoint_b_materialized;
	}
	return before->endpoint_count > 0;
}

/** Detach a just-created node after a later traversal check rejects it. */
static bool rollback_materialization(struct world_spelunk_system *system,
		const struct materialization_before *before)
{
	struct world_spelunk_runtime *runtime;
	size_t i;

	if (!system || !before || !before->node || !before->node->runtime ||
			streq(system->active_node_id, before->node->id)) {
		return false;
	}
	runtime = before->node->runtime;
	before->node->runtime = NULL;
	before->node->discovered = before->discovered;
	for (i = 0; i < before->endpoint_count; i++) {
		const struct materialized_endpoint_before *saved =
			&before->endpoints[i];

		if (saved->is_a) {
			saved->edge->endpoint_a_x = saved->x;
			saved->edge->endpoint_a_y = saved->y;
			saved->edge->endpoint_a_materialized = saved->materialized;
		} else {
			saved->edge->endpoint_b_x = saved->x;
			saved->edge->endpoint_b_y = saved->y;
			saved->edge->endpoint_b_materialized = saved->materialized;
		}
	}
	world_spelunk_runtime_free(runtime);
	return world_spelunk_system_is_valid(system);
}

enum world_spelunk_player_passage_result world_spelunk_player_use_passage(
		struct player *p, enum world_spelunk_passage_direction direction,
		struct world_spelunk_player_passage_report *report)
{
	struct world_spelunk_player_passage_report local = {
		.result = WORLD_SPELUNK_PLAYER_PASSAGE_INVALID,
		.passage = WORLD_SPELUNK_PASSAGE_INVALID,
		.publication = WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT
	};
	struct world_spelunk_system_edge *edge;
	struct world_spelunk_system_node *destination;
	struct materialization_before before;
	const char *source_endpoint = NULL;
	const char *destination_node = NULL;
	const char *destination_endpoint = NULL;
	bool resources_synced;

	if (report) *report = local;
	if (!p || p != player || !p->upkeep || p->upkeep->energy_use != 0 ||
			(direction != WORLD_SPELUNK_PASSAGE_UP &&
			 direction != WORLD_SPELUNK_PASSAGE_DOWN) ||
			!world_spelunk_player_is_active(p) || !p->spelunking_system ||
			p->spelunking_system->graph_version !=
				WORLD_SPELUNK_SYSTEM_GRAPH_VERSION ||
			!world_spelunk_system_is_valid(p->spelunking_system) ||
			p->spelunking != world_spelunk_system_active_runtime(
				p->spelunking_system)) {
		return local.result;
	}
	edge = passage_here(p->spelunking_system, direction, &source_endpoint,
		&destination_node, &destination_endpoint);
	if (!edge) {
		local.result = WORLD_SPELUNK_PLAYER_PASSAGE_NOT_HERE;
		local.passage = WORLD_SPELUNK_PASSAGE_NOT_HERE;
		if (report) *report = local;
		return local.result;
	}
	local.passage = world_spelunk_system_traverse_passage(
		p->spelunking_system, source_endpoint);
	if (local.passage == WORLD_SPELUNK_PASSAGE_DESTINATION_UNAVAILABLE) {
		destination = world_spelunk_system_node_by_id_mutable(
			p->spelunking_system, destination_node);
		if (!destination || destination->runtime ||
				!snapshot_unmaterialized_node(p->spelunking_system,
					destination_node, &before)) {
			local.result = WORLD_SPELUNK_PLAYER_PASSAGE_INVALID;
			if (report) *report = local;
			return local.result;
		}
		local.publication = world_spelunk_materialize_node_candidate(
			p->spelunking_system, destination_node, z_info->move_energy,
			destination_endpoint);
		if (local.publication != WORLD_SPELUNK_PUBLICATION_OK) {
			local.result = WORLD_SPELUNK_PLAYER_PASSAGE_GENERATION_FAILED;
			if (report) *report = local;
			return local.result;
		}
		local.first_visit = true;
		local.passage = world_spelunk_system_traverse_passage(
			p->spelunking_system, source_endpoint);
		if (local.passage != WORLD_SPELUNK_PASSAGE_OK) {
			if (!rollback_materialization(p->spelunking_system, &before)) {
				local.result = WORLD_SPELUNK_PLAYER_PASSAGE_INVALID;
			} else if (local.passage == WORLD_SPELUNK_PASSAGE_UNSTABLE) {
				local.result = WORLD_SPELUNK_PLAYER_PASSAGE_UNSTABLE;
			} else if (local.passage ==
					WORLD_SPELUNK_PASSAGE_DESTINATION_BLOCKED) {
				local.result = WORLD_SPELUNK_PLAYER_PASSAGE_BLOCKED;
			} else {
				local.result = WORLD_SPELUNK_PLAYER_PASSAGE_INVALID;
			}
			local.first_visit = false;
			if (report) *report = local;
			return local.result;
		}
	}
	if (local.passage != WORLD_SPELUNK_PASSAGE_OK) {
		local.result = local.passage == WORLD_SPELUNK_PASSAGE_NOT_HERE ?
			WORLD_SPELUNK_PLAYER_PASSAGE_NOT_HERE :
			(local.passage == WORLD_SPELUNK_PASSAGE_UNSTABLE ?
			 WORLD_SPELUNK_PLAYER_PASSAGE_UNSTABLE :
			 (local.passage == WORLD_SPELUNK_PASSAGE_DESTINATION_BLOCKED ?
			  WORLD_SPELUNK_PLAYER_PASSAGE_BLOCKED :
			  WORLD_SPELUNK_PLAYER_PASSAGE_INVALID));
		if (report) *report = local;
		return local.result;
	}
	p->spelunking = world_spelunk_system_active_runtime(p->spelunking_system);
	assert(p->spelunking && world_spelunk_system_is_valid(
		p->spelunking_system));
	resources_synced = world_spelunk_player_sync_resources(p);
	assert(resources_synced);
	(void)resources_synced;
	p->upkeep->energy_use = z_info->move_energy;
	p->upkeep->autosave = true;
	local.result = WORLD_SPELUNK_PLAYER_PASSAGE_OK;
	if (report) *report = local;
	return local.result;
}
