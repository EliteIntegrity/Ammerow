/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-system.c
 * \brief Persistent ownership for a connected side-view cave system.
 */

#include "world-spelunking-system.h"

#include "z-util.h"
#include "z-virt.h"

#include <string.h>

static bool bounded_id(const char *id, bool allow_empty)
{
	if (!id) return allow_empty;
	if (!id[0]) return allow_empty;
	return strlen(id) < WORLD_ID_LEN;
}

struct world_spelunk_system *world_spelunk_system_create(const char *id,
		uint32_t seed, uint16_t graph_version)
{
	struct world_spelunk_system *system;

	if (!world_id_is_valid(id)) return NULL;
	system = mem_zalloc(sizeof(*system));
	my_strcpy(system->id, id, sizeof(system->id));
	system->seed = seed;
	system->graph_version = graph_version;
	return system;
}

bool world_spelunk_system_set_location(struct world_spelunk_system *system,
		const char *location_id)
{
	size_t i;

	if (!system || !world_id_is_valid(location_id) ||
			(system->location_id[0] &&
			 !streq(system->location_id, location_id))) {
		return false;
	}
	for (i = 0; i < system->node_count; i++) {
		if (system->nodes[i].runtime &&
				!streq(system->nodes[i].runtime->location_id, location_id)) {
			return false;
		}
	}
	my_strcpy(system->location_id, location_id,
		sizeof(system->location_id));
	return true;
}

const struct world_spelunk_system_node *world_spelunk_system_node_by_id(
		const struct world_spelunk_system *system, const char *node_id)
{
	size_t i;

	if (!system || !node_id) return NULL;
	for (i = 0; i < system->node_count; i++) {
		if (streq(system->nodes[i].id, node_id)) return &system->nodes[i];
	}
	return NULL;
}

struct world_spelunk_system_node *world_spelunk_system_node_by_id_mutable(
		struct world_spelunk_system *system, const char *node_id)
{
	return (struct world_spelunk_system_node *)
		world_spelunk_system_node_by_id(system, node_id);
}

bool world_spelunk_system_add_node(struct world_spelunk_system *system,
		const char *node_id, const char *recipe_id, uint32_t seed,
		uint16_t generator_version, uint16_t depth,
		struct world_spelunk_runtime *runtime)
{
	struct world_spelunk_system_node *node;

	if (!system || !world_id_is_valid(node_id) ||
			!bounded_id(recipe_id, true) ||
			system->node_count >= WORLD_SPELUNK_SYSTEM_NODE_MAX ||
			world_spelunk_system_node_by_id(system, node_id) ||
			(runtime && !world_spelunk_runtime_is_valid(runtime)) ||
			(runtime && system->location_id[0] &&
			 !streq(runtime->location_id, system->location_id))) {
		return false;
	}
	node = &system->nodes[system->node_count++];
	my_strcpy(node->id, node_id, sizeof(node->id));
	if (recipe_id) my_strcpy(node->recipe_id, recipe_id,
		sizeof(node->recipe_id));
	node->seed = seed;
	node->generator_version = generator_version;
	node->depth = depth;
	node->discovered = runtime != NULL;
	node->runtime = runtime;
	return true;
}

static struct world_spelunk_system_edge *edge_by_id_mutable(
		struct world_spelunk_system *system, const char *edge_id)
{
	size_t i;

	if (!system || !edge_id) return NULL;
	for (i = 0; i < system->edge_count; i++) {
		if (streq(system->edges[i].id, edge_id)) return &system->edges[i];
	}
	return NULL;
}

const struct world_spelunk_system_edge *world_spelunk_system_edge_by_id(
		const struct world_spelunk_system *system, const char *edge_id)
{
	return edge_by_id_mutable((struct world_spelunk_system *)system, edge_id);
}

static bool edge_endpoint_is_used(const struct world_spelunk_system *system,
		const char *endpoint_id)
{
	size_t i;

	for (i = 0; i < system->edge_count; i++) {
		if (streq(system->edges[i].endpoint_a_id, endpoint_id) ||
				streq(system->edges[i].endpoint_b_id, endpoint_id)) {
			return true;
		}
	}
	return false;
}

static bool endpoint_is_used(const struct world_spelunk_system *system,
		const char *endpoint_id)
{
	size_t i;

	if (edge_endpoint_is_used(system, endpoint_id)) return true;
	for (i = 0; i < system->portal_count; i++) {
		if (streq(system->portals[i].id, endpoint_id)) return true;
	}
	return false;
}

static bool runtime_grid_is_open(const struct world_spelunk_runtime *runtime,
		int x, int y)
{
	return runtime && x >= 0 && y >= 0 &&
		x < runtime->state.map.width && y < runtime->state.map.height &&
		runtime->cells[(size_t)y * runtime->state.map.width + x] !=
			WORLD_SPELUNK_ROCK;
}

static struct world_spelunk_system_edge *edge_by_endpoint_mutable(
		struct world_spelunk_system *system, const char *endpoint_id,
		bool *is_a)
{
	size_t i;

	if (!system || !endpoint_id) return NULL;
	for (i = 0; i < system->edge_count; i++) {
		if (streq(system->edges[i].endpoint_a_id, endpoint_id)) {
			if (is_a) *is_a = true;
			return &system->edges[i];
		}
		if (streq(system->edges[i].endpoint_b_id, endpoint_id)) {
			if (is_a) *is_a = false;
			return &system->edges[i];
		}
	}
	return NULL;
}

static bool node_grid_is_used(const struct world_spelunk_system *system,
		const char *node_id, int x, int y, const char *skip_endpoint_id)
{
	size_t i;

	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &system->edges[i];

		if (edge->endpoint_a_materialized &&
				streq(edge->node_a_id, node_id) &&
				(!skip_endpoint_id ||
				 !streq(edge->endpoint_a_id, skip_endpoint_id)) &&
				edge->endpoint_a_x == x && edge->endpoint_a_y == y) {
			return true;
		}
		if (edge->endpoint_b_materialized &&
				streq(edge->node_b_id, node_id) &&
				(!skip_endpoint_id ||
				 !streq(edge->endpoint_b_id, skip_endpoint_id)) &&
				edge->endpoint_b_x == x && edge->endpoint_b_y == y) {
			return true;
		}
	}
	for (i = 0; i < system->portal_count; i++) {
		const struct world_spelunk_system_portal *portal = &system->portals[i];

		if (portal->materialized && streq(portal->node_id, node_id) &&
				(!skip_endpoint_id || !streq(portal->id, skip_endpoint_id)) &&
				portal->x == x && portal->y == y) {
			return true;
		}
	}
	return false;
}

bool world_spelunk_system_add_edge(struct world_spelunk_system *system,
		const char *edge_id, const char *node_a_id, const char *endpoint_a_id,
		const char *node_b_id, const char *endpoint_b_id, bool discovered)
{
	struct world_spelunk_system_edge *edge;
	struct world_spelunk_system_node *node_a;
	struct world_spelunk_system_node *node_b;
	size_t i;

	if (!system || !world_id_is_valid(edge_id) ||
			!world_id_is_valid(node_a_id) ||
			!world_id_is_valid(node_b_id) ||
			!world_id_is_valid(endpoint_a_id) ||
			!world_id_is_valid(endpoint_b_id) ||
			streq(node_a_id, node_b_id) || streq(endpoint_a_id, endpoint_b_id) ||
			system->edge_count >= WORLD_SPELUNK_SYSTEM_EDGE_MAX ||
			world_spelunk_system_edge_by_id(system, edge_id) ||
			endpoint_is_used(system, endpoint_a_id) ||
			endpoint_is_used(system, endpoint_b_id)) {
		return false;
	}
	node_a = world_spelunk_system_node_by_id_mutable(system, node_a_id);
	node_b = world_spelunk_system_node_by_id_mutable(system, node_b_id);
	if (!node_a || !node_b || (discovered &&
			(!node_a->discovered || !node_b->discovered)) ||
			(system->graph_version == WORLD_SPELUNK_SYSTEM_GRAPH_VERSION &&
			 (uint32_t)node_b->depth != (uint32_t)node_a->depth + 1U)) {
		return false;
	}
	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *other = &system->edges[i];

		if ((streq(other->node_a_id, node_a_id) &&
				 streq(other->node_b_id, node_b_id)) ||
				(streq(other->node_a_id, node_b_id) &&
				 streq(other->node_b_id, node_a_id))) {
			return false;
		}
	}
	edge = &system->edges[system->edge_count++];
	my_strcpy(edge->id, edge_id, sizeof(edge->id));
	my_strcpy(edge->node_a_id, node_a_id, sizeof(edge->node_a_id));
	my_strcpy(edge->node_b_id, node_b_id, sizeof(edge->node_b_id));
	my_strcpy(edge->endpoint_a_id, endpoint_a_id,
		sizeof(edge->endpoint_a_id));
	my_strcpy(edge->endpoint_b_id, endpoint_b_id,
		sizeof(edge->endpoint_b_id));
	edge->discovered = discovered;
	return true;
}

bool world_spelunk_system_discover_edge(struct world_spelunk_system *system,
		const char *edge_id)
{
	struct world_spelunk_system_edge *edge;
	struct world_spelunk_system_node *node_a;
	struct world_spelunk_system_node *node_b;

	if (!system) return false;
	edge = edge_by_id_mutable(system, edge_id);
	if (!edge) return false;
	node_a = world_spelunk_system_node_by_id_mutable(system, edge->node_a_id);
	node_b = world_spelunk_system_node_by_id_mutable(system, edge->node_b_id);
	if (!node_a || !node_b) return false;
	node_a->discovered = true;
	node_b->discovered = true;
	edge->discovered = true;
	return true;
}

bool world_spelunk_system_set_endpoint_grid(
		struct world_spelunk_system *system, const char *endpoint_id,
		int x, int y)
{
	struct world_spelunk_system_edge *edge;
	const struct world_spelunk_system_node *node;
	bool is_a = false;

	edge = edge_by_endpoint_mutable(system, endpoint_id, &is_a);
	if (!edge) return false;
	node = world_spelunk_system_node_by_id(system,
		is_a ? edge->node_a_id : edge->node_b_id);
	if (!node || !runtime_grid_is_open(node->runtime, x, y) ||
			x > INT16_MAX || y > INT16_MAX ||
			node_grid_is_used(system, node->id, x, y, endpoint_id)) {
		return false;
	}
	if (is_a) {
		edge->endpoint_a_x = (int16_t)x;
		edge->endpoint_a_y = (int16_t)y;
		edge->endpoint_a_materialized = true;
	} else {
		edge->endpoint_b_x = (int16_t)x;
		edge->endpoint_b_y = (int16_t)y;
		edge->endpoint_b_materialized = true;
	}
	return true;
}

static struct world_spelunk_system_portal *portal_by_id_mutable(
		struct world_spelunk_system *system, const char *portal_id)
{
	size_t i;

	if (!system || !portal_id) return NULL;
	for (i = 0; i < system->portal_count; i++) {
		if (streq(system->portals[i].id, portal_id)) {
			return &system->portals[i];
		}
	}
	return NULL;
}

const struct world_spelunk_system_portal *world_spelunk_system_portal_by_id(
		const struct world_spelunk_system *system, const char *portal_id)
{
	return portal_by_id_mutable((struct world_spelunk_system *)system,
		portal_id);
}

const struct world_spelunk_system_portal *world_spelunk_system_portal_by_entry(
		const struct world_spelunk_system *system, const char *entry_id)
{
	size_t i;

	if (!system || !entry_id) return NULL;
	for (i = 0; i < system->portal_count; i++) {
		if (streq(system->portals[i].entry_id, entry_id)) {
			return &system->portals[i];
		}
	}
	return NULL;
}

bool world_spelunk_system_add_portal(struct world_spelunk_system *system,
		const char *portal_id, const char *entry_id, const char *node_id,
		bool enabled)
{
	struct world_spelunk_system_portal *portal;

	if (!system || !world_id_is_valid(system->location_id) ||
			!world_id_is_valid(portal_id) || !world_id_is_valid(entry_id) ||
			!world_id_is_valid(node_id) ||
			system->portal_count >= WORLD_SPELUNK_SYSTEM_PORTAL_MAX ||
			world_spelunk_system_portal_by_id(system, portal_id) ||
			world_spelunk_system_portal_by_entry(system, entry_id) ||
			!world_spelunk_system_node_by_id(system, node_id) ||
			endpoint_is_used(system, portal_id)) {
		return false;
	}
	portal = &system->portals[system->portal_count++];
	my_strcpy(portal->id, portal_id, sizeof(portal->id));
	my_strcpy(portal->entry_id, entry_id, sizeof(portal->entry_id));
	my_strcpy(portal->node_id, node_id, sizeof(portal->node_id));
	portal->enabled = enabled;
	return true;
}

bool world_spelunk_system_enable_portal(struct world_spelunk_system *system,
		const char *portal_id)
{
	struct world_spelunk_system_portal *portal =
		portal_by_id_mutable(system, portal_id);

	if (!portal) return false;
	portal->enabled = true;
	return true;
}

bool world_spelunk_system_set_portal_grid(struct world_spelunk_system *system,
		const char *portal_id, int x, int y)
{
	struct world_spelunk_system_portal *portal =
		portal_by_id_mutable(system, portal_id);
	const struct world_spelunk_system_node *node = portal ?
		world_spelunk_system_node_by_id(system, portal->node_id) : NULL;

	if (!portal || !node || !runtime_grid_is_open(node->runtime, x, y) ||
			x > INT16_MAX || y > INT16_MAX ||
			node_grid_is_used(system, node->id, x, y, portal_id)) {
		return false;
	}
	portal->x = (int16_t)x;
	portal->y = (int16_t)y;
	portal->materialized = true;
	return true;
}

bool world_spelunk_system_discover_portal(struct world_spelunk_system *system,
		const char *portal_id)
{
	struct world_spelunk_system_portal *portal =
		portal_by_id_mutable(system, portal_id);
	struct world_spelunk_system_node *node = portal ?
		world_spelunk_system_node_by_id_mutable(system, portal->node_id) : NULL;

	if (!portal || !portal->enabled || !portal->materialized || !node ||
			!node->runtime) {
		return false;
	}
	portal->discovered = true;
	node->discovered = true;
	return true;
}

static bool endpoint_for_node(struct world_spelunk_system *system,
		const char *node_id, const char *endpoint_id,
		struct world_spelunk_system_edge **edge, bool *is_a)
{
	struct world_spelunk_system_edge *found =
		edge_by_endpoint_mutable(system, endpoint_id, is_a);

	if (!found || !streq(*is_a ? found->node_a_id : found->node_b_id,
			node_id)) {
		return false;
	}
	*edge = found;
	return true;
}

bool world_spelunk_system_materialize_node(
		struct world_spelunk_system *system, const char *node_id,
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_system_endpoint_grid *grids,
		size_t grid_count)
{
	struct world_spelunk_system_node *node =
		world_spelunk_system_node_by_id_mutable(system, node_id);
	bool was_discovered;
	size_t incident_count = 0;
	size_t i;

	if (!system || !node || node->runtime || !runtime || !grids ||
			!grid_count || grid_count > WORLD_SPELUNK_SYSTEM_ENDPOINT_MAX ||
			!world_spelunk_system_is_valid(system) ||
			!world_spelunk_runtime_is_valid(runtime) ||
			!streq(runtime->location_id, system->location_id)) {
		return false;
	}
	for (i = 0; i < system->edge_count; i++) {
		if (streq(system->edges[i].node_a_id, node_id) ||
				streq(system->edges[i].node_b_id, node_id)) {
			incident_count++;
		}
	}
	for (i = 0; i < system->portal_count; i++) {
		if (streq(system->portals[i].node_id, node_id)) incident_count++;
	}
	if (incident_count != grid_count) return false;
	for (i = 0; i < grid_count; i++) {
		struct world_spelunk_system_edge *edge = NULL;
		struct world_spelunk_system_portal *portal =
			portal_by_id_mutable(system, grids[i].endpoint_id);
		bool is_a = false;
		size_t j;

		if (!world_id_is_valid(grids[i].endpoint_id) ||
				!runtime_grid_is_open(runtime, grids[i].x, grids[i].y) ||
				grids[i].x > INT16_MAX || grids[i].y > INT16_MAX ||
				(!portal && !endpoint_for_node(system, node_id,
					grids[i].endpoint_id, &edge, &is_a)) ||
				(portal && (!streq(portal->node_id, node_id) ||
					portal->materialized)) ||
				(edge && (is_a ? edge->endpoint_a_materialized :
					edge->endpoint_b_materialized))) {
			return false;
		}
		for (j = i + 1; j < grid_count; j++) {
			if (streq(grids[i].endpoint_id, grids[j].endpoint_id) ||
					(grids[i].x == grids[j].x && grids[i].y == grids[j].y)) {
				return false;
			}
		}
	}
	was_discovered = node->discovered;
	node->runtime = runtime;
	node->discovered = true;
	for (i = 0; i < grid_count; i++) {
		struct world_spelunk_system_edge *edge = NULL;
		struct world_spelunk_system_portal *portal =
			portal_by_id_mutable(system, grids[i].endpoint_id);
		bool is_a = false;

		if (!portal && !endpoint_for_node(system, node_id,
				grids[i].endpoint_id, &edge, &is_a)) {
			break;
		}
		if (portal) {
			portal->x = (int16_t)grids[i].x;
			portal->y = (int16_t)grids[i].y;
			portal->materialized = true;
		} else if (is_a) {
			edge->endpoint_a_x = (int16_t)grids[i].x;
			edge->endpoint_a_y = (int16_t)grids[i].y;
			edge->endpoint_a_materialized = true;
		} else {
			edge->endpoint_b_x = (int16_t)grids[i].x;
			edge->endpoint_b_y = (int16_t)grids[i].y;
			edge->endpoint_b_materialized = true;
		}
	}
	if (i == grid_count && world_spelunk_system_is_valid(system)) return true;
	while (i > 0) {
		struct world_spelunk_system_edge *edge = NULL;
		struct world_spelunk_system_portal *portal;
		bool is_a = false;

		i--;
		portal = portal_by_id_mutable(system, grids[i].endpoint_id);
		if (portal) {
			portal->x = 0;
			portal->y = 0;
			portal->materialized = false;
			continue;
		}
		(void)endpoint_for_node(system, node_id, grids[i].endpoint_id,
			&edge, &is_a);
		if (is_a) {
			edge->endpoint_a_x = 0;
			edge->endpoint_a_y = 0;
			edge->endpoint_a_materialized = false;
		} else {
			edge->endpoint_b_x = 0;
			edge->endpoint_b_y = 0;
			edge->endpoint_b_materialized = false;
		}
	}
	node->runtime = NULL;
	node->discovered = was_discovered;
	return false;
}

bool world_spelunk_system_set_active_node(struct world_spelunk_system *system,
		const char *node_id)
{
	struct world_spelunk_system_node *node =
		world_spelunk_system_node_by_id_mutable(system, node_id);

	if (!node || !node->runtime) return false;
	node->discovered = true;
	my_strcpy(system->active_node_id, node_id,
		sizeof(system->active_node_id));
	return true;
}

struct world_spelunk_runtime *world_spelunk_system_active_runtime(
		const struct world_spelunk_system *system)
{
	const struct world_spelunk_system_node *node;

	if (!system || !system->active_node_id[0]) return NULL;
	node = world_spelunk_system_node_by_id(system, system->active_node_id);
	return node ? node->runtime : NULL;
}

bool world_spelunk_system_owns_runtime(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime)
{
	size_t i;

	if (!system || !runtime) return false;
	for (i = 0; i < system->node_count; i++) {
		if (system->nodes[i].runtime == runtime) return true;
	}
	return false;
}

bool world_spelunk_system_is_valid(const struct world_spelunk_system *system)
{
	size_t i;
	struct world_spelunk_runtime *active;

	if (!system || !world_id_is_valid(system->id) || !system->node_count ||
			system->node_count > WORLD_SPELUNK_SYSTEM_NODE_MAX ||
			system->edge_count > WORLD_SPELUNK_SYSTEM_EDGE_MAX ||
			system->portal_count > WORLD_SPELUNK_SYSTEM_PORTAL_MAX ||
			system->graph_version > WORLD_SPELUNK_SYSTEM_GRAPH_VERSION ||
			(system->graph_version == WORLD_SPELUNK_SYSTEM_GRAPH_VERSION_LEGACY &&
			 system->edge_count != 0)) {
		return false;
	}
	for (i = 0; i < system->node_count; i++) {
		const struct world_spelunk_system_node *node = &system->nodes[i];
		size_t j;

		if (!world_id_is_valid(node->id) ||
				!bounded_id(node->recipe_id, true) ||
				(node->runtime && !node->discovered) ||
				(node->runtime && system->location_id[0] &&
				 !streq(node->runtime->location_id,
					system->location_id)) ||
				(node->runtime &&
				 !world_spelunk_runtime_is_valid(node->runtime))) {
			return false;
		}
		for (j = i + 1; j < system->node_count; j++) {
			if (streq(node->id, system->nodes[j].id) ||
					(node->runtime &&
					 node->runtime == system->nodes[j].runtime)) {
				return false;
			}
		}
	}
	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &system->edges[i];
		const struct world_spelunk_system_node *node_a =
			world_spelunk_system_node_by_id(system, edge->node_a_id);
		const struct world_spelunk_system_node *node_b =
			world_spelunk_system_node_by_id(system, edge->node_b_id);
		size_t j;

		if (!world_id_is_valid(edge->id) ||
				!world_id_is_valid(edge->endpoint_a_id) ||
				!world_id_is_valid(edge->endpoint_b_id) || !node_a || !node_b ||
				streq(node_a->id, node_b->id) ||
				streq(edge->endpoint_a_id, edge->endpoint_b_id) ||
				(system->graph_version == WORLD_SPELUNK_SYSTEM_GRAPH_VERSION &&
				 (uint32_t)node_b->depth != (uint32_t)node_a->depth + 1U) ||
				(edge->endpoint_a_materialized &&
				 !runtime_grid_is_open(node_a->runtime,
					edge->endpoint_a_x, edge->endpoint_a_y)) ||
				(edge->endpoint_a_materialized &&
				 node_grid_is_used(system, node_a->id,
					edge->endpoint_a_x, edge->endpoint_a_y,
					edge->endpoint_a_id)) ||
				(edge->endpoint_b_materialized &&
				 !runtime_grid_is_open(node_b->runtime,
					edge->endpoint_b_x, edge->endpoint_b_y)) ||
				(edge->endpoint_b_materialized &&
				 node_grid_is_used(system, node_b->id,
					edge->endpoint_b_x, edge->endpoint_b_y,
					edge->endpoint_b_id)) ||
				(edge->discovered &&
				 (!node_a->discovered || !node_b->discovered))) {
			return false;
		}
		for (j = i + 1; j < system->edge_count; j++) {
			const struct world_spelunk_system_edge *other = &system->edges[j];

			if (streq(edge->id, other->id) ||
					streq(edge->endpoint_a_id, other->endpoint_a_id) ||
					streq(edge->endpoint_a_id, other->endpoint_b_id) ||
					streq(edge->endpoint_b_id, other->endpoint_a_id) ||
					streq(edge->endpoint_b_id, other->endpoint_b_id) ||
					((streq(edge->node_a_id, other->node_a_id) &&
					  streq(edge->node_b_id, other->node_b_id)) ||
					 (streq(edge->node_a_id, other->node_b_id) &&
					  streq(edge->node_b_id, other->node_a_id)))) {
				return false;
			}
		}
	}
	for (i = 0; i < system->portal_count; i++) {
		const struct world_spelunk_system_portal *portal =
			&system->portals[i];
		const struct world_spelunk_system_node *node =
			world_spelunk_system_node_by_id(system, portal->node_id);
		size_t j;

		if (!world_id_is_valid(system->location_id) ||
				!world_id_is_valid(portal->id) ||
				!world_id_is_valid(portal->entry_id) || !node ||
				edge_endpoint_is_used(system, portal->id) ||
				(portal->materialized &&
				 !runtime_grid_is_open(node->runtime, portal->x, portal->y)) ||
				(portal->materialized &&
				 node_grid_is_used(system, node->id, portal->x, portal->y,
					portal->id)) ||
				(portal->discovered &&
				 (!portal->enabled || !portal->materialized))) {
			return false;
		}
		for (j = i + 1; j < system->portal_count; j++) {
			if (streq(portal->id, system->portals[j].id) ||
					streq(portal->entry_id,
						system->portals[j].entry_id)) {
				return false;
			}
		}
	}
	active = world_spelunk_system_active_runtime(system);
	if (!active) return false;
	{
		const struct world_spelunk_system_node *active_node =
			world_spelunk_system_node_by_id(system, system->active_node_id);

		if (!active_node || !active_node->discovered) return false;
	}
	if (system->graph_version == WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		bool visited[WORLD_SPELUNK_SYSTEM_NODE_MAX] = { false };
		size_t visited_count = 1;
		bool changed = true;

		visited[0] = true;
		while (changed) {
			changed = false;
			for (i = 0; i < system->edge_count; i++) {
				const struct world_spelunk_system_edge *edge =
					&system->edges[i];
				size_t a;
				size_t b;

				for (a = 0; a < system->node_count; a++) {
					if (streq(system->nodes[a].id, edge->node_a_id)) break;
				}
				for (b = 0; b < system->node_count; b++) {
					if (streq(system->nodes[b].id, edge->node_b_id)) break;
				}
				if (a >= system->node_count || b >= system->node_count) {
					return false;
				}
				if (visited[a] && !visited[b]) {
					visited[b] = true;
					visited_count++;
					changed = true;
				} else if (visited[b] && !visited[a]) {
					visited[a] = true;
					visited_count++;
					changed = true;
				}
			}
		}
		if (visited_count != system->node_count) return false;
	}
	return true;
}

struct world_spelunk_system *world_spelunk_system_create_legacy(
		struct world_spelunk_runtime *runtime)
{
	struct world_spelunk_system *system;

	if (!runtime || !world_spelunk_runtime_is_valid(runtime)) return NULL;
	system = world_spelunk_system_create(runtime->location_id, 0,
		WORLD_SPELUNK_SYSTEM_GRAPH_VERSION_LEGACY);
	if (!system) return NULL;
	if (!world_spelunk_system_set_location(system, runtime->location_id)) {
		world_spelunk_system_free(system);
		return NULL;
	}
	if (!world_spelunk_system_add_node(system, runtime->location_id, "", 0,
			0, 0, runtime) ||
			!world_spelunk_system_set_active_node(system,
				runtime->location_id)) {
		/* add_node transfers ownership only when it succeeds. */
		if (world_spelunk_system_owns_runtime(system, runtime)) {
			system->nodes[0].runtime = NULL;
		}
		world_spelunk_system_free(system);
		return NULL;
	}
	return system;
}

void world_spelunk_system_free(struct world_spelunk_system *system)
{
	size_t i;

	if (!system) return;
	for (i = 0; i < system->node_count; i++) {
		world_spelunk_runtime_free(system->nodes[i].runtime);
		system->nodes[i].runtime = NULL;
	}
	mem_free(system);
}
