/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-publication.c
 * \brief Transactional assembly of unpublished procedural cave systems.
 */

#include "angband.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "world-spelunking-generation-endpoint.h"
#include "world-spelunking-generation-population.h"
#include "world-spelunking-generation-system.h"
#include "world-spelunking-generation.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-data-util.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-runtime.h"
#include "z-util.h"

#include <string.h>

#define ROOT_ENDPOINT_MAX \
	(WORLD_SPELUNK_SYSTEM_EDGE_MAX + WORLD_SPELUNK_SYSTEM_PORTAL_MAX)

enum root_endpoint_kind {
	ROOT_ENDPOINT_PASSAGE = 0,
	ROOT_ENDPOINT_PORTAL
};

struct root_endpoint_placement {
	enum root_endpoint_kind kind;
	const char *id;
	uint16_t minimum_depth;
	uint16_t maximum_depth;
	int required_route_station;
	int x;
	int y;
};

static const struct world_spelunk_system_portal_contract *portal_contract(
		const struct world_spelunk_system_profile *profile,
		const char *portal_id)
{
	int i;

	if (!profile || !portal_id) return NULL;
	for (i = 0; i < profile->portal_count; i++) {
		if (streq(profile->portals[i].id, portal_id)) {
			return &profile->portals[i];
		}
	}
	return NULL;
}

static const struct world_spelunk_system_portal_contract *arrival_contract(
		const struct world_spelunk_system_profile *profile,
		const char *entry_id)
{
	int i;

	if (!profile || !entry_id) return NULL;
	for (i = 0; i < profile->portal_count; i++) {
		if (profile->portals[i].target == WORLD_SPELUNK_PORTAL_ROOT &&
				profile->portals[i].initially_enabled &&
				streq(profile->portals[i].entry_id, entry_id)) {
			return &profile->portals[i];
		}
	}
	return NULL;
}

static bool build_root_placements(
		const struct world_spelunk_system_profile *profile,
		const char *root_id, const struct world_spelunk_system_edge *edges,
		size_t edge_count,
		const struct world_spelunk_recipe_definition *recipe,
		struct world_spelunk_generated_layout *generated,
		struct root_endpoint_placement *placements, size_t *placement_count,
		size_t *arrival_index, const char *arrival_entry_id)
{
	struct world_spelunk_endpoint_request requests[ROOT_ENDPOINT_MAX];
	struct world_spelunk_endpoint_placement selected[ROOT_ENDPOINT_MAX];
	size_t count = 0;
	size_t i;

	*arrival_index = SIZE_MAX;
	for (i = 0; i < profile->portal_count; i++) {
		const struct world_spelunk_system_portal_contract *portal =
			&profile->portals[i];
		struct root_endpoint_placement *placement;

		if (portal->target != WORLD_SPELUNK_PORTAL_ROOT) continue;
		if (count >= ROOT_ENDPOINT_MAX) return false;
		placement = &placements[count];
		placement->kind = ROOT_ENDPOINT_PORTAL;
		placement->id = portal->id;
		placement->minimum_depth = portal->min_depth_percent;
		placement->maximum_depth = portal->max_depth_percent;
		placement->required_route_station =
			portal->anchor == WORLD_SPELUNK_PORTAL_UPPER_RAIL ? 0 : -1;
		placement->x = -1;
		placement->y = -1;
		if (arrival_entry_id && streq(portal->entry_id, arrival_entry_id)) {
			*arrival_index = count;
		}
		count++;
	}
	for (i = 0; i < edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &edges[i];
		struct root_endpoint_placement *placement;

		if (!streq(edge->node_a_id, root_id) &&
				!streq(edge->node_b_id, root_id)) {
			continue;
		}
		if (count >= ROOT_ENDPOINT_MAX) return false;
		placement = &placements[count++];
		placement->kind = ROOT_ENDPOINT_PASSAGE;
		placement->id = streq(edge->node_a_id, root_id) ?
			edge->endpoint_a_id : edge->endpoint_b_id;
		placement->minimum_depth = recipe->passages[
			WORLD_SPELUNK_PASSAGE_DOWN].min_depth_percent;
		placement->maximum_depth = recipe->passages[
			WORLD_SPELUNK_PASSAGE_DOWN].max_depth_percent;
		placement->required_route_station = -1;
		placement->x = -1;
		placement->y = -1;
	}
	if (arrival_entry_id && *arrival_index == SIZE_MAX) return false;
	for (i = 0; i < count; i++) {
		requests[i].id = placements[i].id;
		requests[i].minimum_depth_percent = placements[i].minimum_depth;
		requests[i].maximum_depth_percent = placements[i].maximum_depth;
		requests[i].required_route_station =
			placements[i].required_route_station;
	}
	if (!world_spelunk_place_endpoints(generated, requests, count, selected) ||
			!world_spelunk_certify_endpoints(generated, selected, count,
				world_spelunk_traversal_profile_by_id(
					generated->traversal_profile_id), NULL)) {
		return false;
	}
	for (i = 0; i < count; i++) {
		placements[i].x = selected[i].x;
		placements[i].y = selected[i].y;
	}
	*placement_count = count;
	return true;
}

static bool runtime_matches_generated(
		const struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated)
{
	size_t cells;

	if (!runtime || !generated || !generated->cells ||
			runtime->state.map.width != generated->width ||
			runtime->state.map.height != generated->height ||
			runtime->state.map.stride != generated->width ||
			!streq(runtime->geology_profile_id,
				generated->material_profile_id) ||
			runtime->geology_version != generated->geology_version ||
			!world_spelunk_perception_matches(&runtime->perception,
				&generated->perception) ||
			memcmp(&runtime->state.rules, &generated->rules,
				sizeof(generated->rules)) != 0) {
		return false;
	}
	cells = (size_t)generated->width * (size_t)generated->height;
	return runtime->cells && runtime->material_tags &&
		runtime->decoration_tags &&
		memcmp(runtime->cells, generated->cells,
			cells * sizeof(*generated->cells)) == 0 &&
		memcmp(runtime->material_tags, generated->material_tags, cells) == 0 &&
		memcmp(runtime->decoration_tags, generated->decoration_tags,
			cells) == 0;
}

static bool persistent_endpoint_grid(
		const struct world_spelunk_system *system, const char *node_id,
		const char *endpoint_id, int *x, int *y)
{
	size_t i;

	if (!system || !node_id || !endpoint_id || !x || !y) return false;
	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &system->edges[i];

		if (streq(edge->node_a_id, node_id) &&
				streq(edge->endpoint_a_id, endpoint_id) &&
				edge->endpoint_a_materialized) {
			*x = edge->endpoint_a_x;
			*y = edge->endpoint_a_y;
			return true;
		}
		if (streq(edge->node_b_id, node_id) &&
				streq(edge->endpoint_b_id, endpoint_id) &&
				edge->endpoint_b_materialized) {
			*x = edge->endpoint_b_x;
			*y = edge->endpoint_b_y;
			return true;
		}
	}
	for (i = 0; i < system->portal_count; i++) {
		const struct world_spelunk_system_portal *portal = &system->portals[i];

		if (streq(portal->node_id, node_id) && streq(portal->id, endpoint_id) &&
				portal->materialized) {
			*x = portal->x;
			*y = portal->y;
			return true;
		}
	}
	return false;
}

static bool reconstructed_root_placements_match(
		const struct world_spelunk_system *system,
		const struct root_endpoint_placement *placements, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		int x;
		int y;

		if (placements[i].kind == ROOT_ENDPOINT_PORTAL) {
			const struct world_spelunk_system_portal *portal =
				world_spelunk_system_portal_by_id(system, placements[i].id);

			if (!portal || !portal->materialized) return false;
			x = portal->x;
			y = portal->y;
		} else if (!persistent_endpoint_grid(system, system->nodes[0].id,
				placements[i].id, &x, &y)) {
			return false;
		}
		if (placements[i].x != x || placements[i].y != y) return false;
	}
	return true;
}

static bool reconstructed_node_placements_match(
		const struct world_spelunk_system *system, const char *node_id,
		const struct world_spelunk_system_endpoint_grid *grids, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		int x;
		int y;

		if (!persistent_endpoint_grid(system, node_id, grids[i].endpoint_id,
				&x, &y) || grids[i].x != x || grids[i].y != y) {
			return false;
		}
	}
	return true;
}

static bool build_node_placements(const struct world_spelunk_system *system,
		const struct world_spelunk_system_node *node,
		const struct world_spelunk_recipe_definition *recipe,
		struct world_spelunk_generated_layout *generated,
		const char *arrival_endpoint_id,
		struct world_spelunk_system_endpoint_grid *grids, size_t *grid_count,
		size_t *arrival_index)
{
	const struct world_spelunk_system_profile *profile =
		world_spelunk_system_profile_by_id(system ? system->id : NULL);
	struct world_spelunk_endpoint_request requests[
		WORLD_SPELUNK_SYSTEM_ENDPOINT_MAX];
	struct world_spelunk_endpoint_placement placements[
		WORLD_SPELUNK_SYSTEM_ENDPOINT_MAX];
	size_t count = 0;
	size_t i;

	*arrival_index = SIZE_MAX;
	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &system->edges[i];
		enum world_spelunk_passage_direction direction;
		const char *endpoint_id;

		if (streq(edge->node_a_id, node->id)) {
			direction = WORLD_SPELUNK_PASSAGE_DOWN;
			endpoint_id = edge->endpoint_a_id;
		} else if (streq(edge->node_b_id, node->id)) {
			direction = WORLD_SPELUNK_PASSAGE_UP;
			endpoint_id = edge->endpoint_b_id;
		} else {
			continue;
		}
		if (count >= WORLD_SPELUNK_SYSTEM_ENDPOINT_MAX ||
				!recipe->passages[direction].capacity) {
			return false;
		}
		requests[count].id = endpoint_id;
		requests[count].minimum_depth_percent =
			recipe->passages[direction].min_depth_percent;
		requests[count].maximum_depth_percent =
			recipe->passages[direction].max_depth_percent;
		requests[count].required_route_station = -1;
		grids[count].endpoint_id = endpoint_id;
		grids[count].x = -1;
		grids[count].y = -1;
		if (streq(endpoint_id, arrival_endpoint_id)) *arrival_index = count;
		count++;
	}
	for (i = 0; i < system->portal_count; i++) {
		const struct world_spelunk_system_portal *portal = &system->portals[i];
		const struct world_spelunk_system_portal_contract *contract;

		if (!streq(portal->node_id, node->id)) continue;
		contract = portal_contract(profile, portal->id);
		if (!contract || count >= WORLD_SPELUNK_SYSTEM_ENDPOINT_MAX) {
			return false;
		}
		requests[count].id = portal->id;
		requests[count].minimum_depth_percent = contract->min_depth_percent;
		requests[count].maximum_depth_percent = contract->max_depth_percent;
		requests[count].required_route_station =
			contract->anchor == WORLD_SPELUNK_PORTAL_UPPER_RAIL ? 0 : -1;
		grids[count].endpoint_id = portal->id;
		grids[count].x = -1;
		grids[count].y = -1;
		count++;
	}
	if (!count || *arrival_index == SIZE_MAX ||
			!world_spelunk_place_endpoints(generated, requests, count,
				placements) ||
			!world_spelunk_certify_endpoints(generated, placements, count,
				world_spelunk_traversal_profile_by_id(
					generated->traversal_profile_id), NULL)) {
		return false;
	}
	for (i = 0; i < count; i++) {
		grids[i].x = placements[i].x;
		grids[i].y = placements[i].y;
	}
	*grid_count = count;
	return true;
}

static void delete_detached_object(struct object **obj)
{
	struct object *known;

	if (!obj || !*obj) return;
	known = (*obj)->known;
	(*obj)->known = NULL;
	if (known) object_delete(NULL, NULL, &known);
	object_delete(NULL, NULL, obj);
}

static struct object *make_landmark_object(
		const struct world_spelunk_generated_landmark *landmark)
{
	struct object_kind *kind;
	struct object *obj;
	int tval;
	int sval;

	if (!landmark || landmark->kind != WORLD_SPELUNK_LANDMARK_OBJECT) {
		return NULL;
	}
	tval = tval_find_idx(landmark->object_tval);
	sval = tval > 0 ? lookup_sval(tval, landmark->object_sval) : -1;
	kind = sval >= 0 ? lookup_kind(tval, sval) : NULL;
	if (!kind) return NULL;
	obj = object_new();
	object_prep(obj, kind, 0, AVERAGE);
	obj->origin = ORIGIN_FLOOR;
	obj->known = object_new();
	object_copy(obj->known, obj);
	obj->known->known = NULL;
	return obj;
}

static bool add_landmark_objects(struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated)
{
	int i;

	for (i = 0; i < generated->landmark_count; i++) {
		const struct world_spelunk_generated_landmark *landmark =
			&generated->landmarks[i];
		struct object *obj;

		if (landmark->kind != WORLD_SPELUNK_LANDMARK_OBJECT) continue;
		obj = make_landmark_object(landmark);
		if (!obj || !world_spelunk_runtime_add_ground_object(runtime, obj,
				landmark->x, landmark->y)) {
			if (obj) delete_detached_object(&obj);
			return false;
		}
	}
	return true;
}

static struct world_spelunk_runtime *create_generated_runtime(
		const char *location_id,
		const struct world_spelunk_generated_layout *generated,
		int arrival_x, int arrival_y)
{
	struct world_spelunk_map map;
	struct world_spelunk_state state;
	struct world_spelunk_runtime *runtime;

	memset(&map, 0, sizeof(map));
	map.cells = generated->cells;
	map.width = generated->width;
	map.height = generated->height;
	map.stride = generated->width;
	if (!world_spelunk_state_init(&state, &map, &generated->rules,
			arrival_x, arrival_y, generated->initial_stamina)) {
		return NULL;
	}
	runtime = world_spelunk_runtime_create_with_perception(location_id, &state,
		&generated->perception);
	if (!runtime) return NULL;
	if (!world_spelunk_runtime_set_geology(runtime,
			generated->material_profile_id, generated->geology_version,
			generated->material_tags, generated->decoration_tags) ||
			!add_landmark_objects(runtime, generated) ||
			!world_spelunk_populate_generated_runtime(runtime, generated,
				world_spelunk_population_profile_by_id(
					generated->population_profile_id)) ||
			!world_spelunk_runtime_is_valid(runtime)) {
		world_spelunk_runtime_free(runtime);
		return NULL;
	}
	return runtime;
}

static bool add_plan_to_system(struct world_spelunk_system *system,
		const struct world_spelunk_system_profile *profile,
		const struct world_spelunk_system_plan *plan,
		struct world_spelunk_runtime **root_runtime)
{
	size_t i;

	if (!world_spelunk_system_set_location(system, profile->location_id)) {
		return false;
	}
	for (i = 0; i < plan->node_count; i++) {
		if (!world_spelunk_system_add_node(system, plan->nodes[i].id,
				plan->nodes[i].recipe_id, plan->nodes[i].seed,
				plan->nodes[i].generator_version, plan->nodes[i].depth,
				i == 0 ? *root_runtime : NULL)) {
			return false;
		}
		if (i == 0) *root_runtime = NULL;
	}
	for (i = 0; i < plan->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &plan->edges[i];

		if (!world_spelunk_system_add_edge(system, edge->id,
				edge->node_a_id, edge->endpoint_a_id, edge->node_b_id,
				edge->endpoint_b_id, false)) {
			return false;
		}
	}
	for (i = 0; i < profile->portal_count; i++) {
		const struct world_spelunk_system_portal_contract *portal =
			&profile->portals[i];
		const struct world_spelunk_system_plan_node *target = &plan->nodes[0];
		size_t j;

		if (portal->target == WORLD_SPELUNK_PORTAL_DEEPEST) {
			for (j = 1; j < plan->node_count; j++) {
				if (plan->nodes[j].depth > target->depth) target = &plan->nodes[j];
			}
		}
		if (!world_spelunk_system_add_portal(system, portal->id,
				portal->entry_id, target->id, portal->initially_enabled)) {
			return false;
		}
	}
	return true;
}

static bool attach_root_placements(struct world_spelunk_system *system,
		const struct root_endpoint_placement *placements, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++) {
		bool attached = placements[i].kind == ROOT_ENDPOINT_PORTAL ?
			world_spelunk_system_set_portal_grid(system, placements[i].id,
				placements[i].x, placements[i].y) :
			world_spelunk_system_set_endpoint_grid(system, placements[i].id,
				placements[i].x, placements[i].y);

		if (!attached) return false;
	}
	return true;
}

enum world_spelunk_publication_result
	world_spelunk_publish_system_candidate(
		const struct world_spelunk_system_profile *profile, uint32_t seed,
		unsigned int action_energy, const char *arrival_entry_id,
		struct world_spelunk_system **published)
{
	struct world_spelunk_system_plan plan;
	const struct world_spelunk_recipe_definition *root_recipe;
	const struct world_spelunk_system_portal_contract *arrival;
	struct world_spelunk_generated_layout generated;
	struct root_endpoint_placement placements[ROOT_ENDPOINT_MAX];
	struct world_spelunk_runtime *root_runtime = NULL;
	struct world_spelunk_system *system = NULL;
	size_t placement_count = 0;
	size_t arrival_index = SIZE_MAX;
	enum world_spelunk_publication_result result;

	if (!published) return WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT;
	*published = NULL;
	if (!profile || !action_energy || !arrival_entry_id ||
			!world_id_is_valid(profile->location_id) ||
			!profile->portal_count ||
			profile->portal_count > WORLD_SPELUNK_SYSTEM_PORTAL_MAX) {
		return WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT;
	}
	arrival = arrival_contract(profile, arrival_entry_id);
	if (!arrival) return WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT;
	if (world_spelunk_plan_system(profile, seed, &plan) !=
			WORLD_SPELUNK_SYSTEM_PLAN_OK) {
		return WORLD_SPELUNK_PUBLICATION_PLAN_FAILED;
	}
	root_recipe = world_spelunk_recipe_definition_by_id(
		plan.nodes[0].recipe_id);
	if (!root_recipe || world_spelunk_generate_layout(root_recipe,
			plan.nodes[0].seed, action_energy, &generated) !=
			WORLD_SPELUNK_GENERATION_OK) {
		return WORLD_SPELUNK_PUBLICATION_GENERATION_FAILED;
	}
	if (!build_root_placements(profile, plan.nodes[0].id, plan.edges,
			plan.edge_count, root_recipe, &generated,
			placements, &placement_count, &arrival_index, arrival_entry_id)) {
		result = WORLD_SPELUNK_PUBLICATION_ENDPOINT_FAILED;
		goto cleanup;
	}
	root_runtime = create_generated_runtime(profile->location_id, &generated,
		placements[arrival_index].x, placements[arrival_index].y);
	if (!root_runtime) {
		result = WORLD_SPELUNK_PUBLICATION_RUNTIME_FAILED;
		goto cleanup;
	}
	system = world_spelunk_system_create(profile->id, seed,
		profile->version);
	if (!system || !add_plan_to_system(system, profile, &plan,
			&root_runtime) || !attach_root_placements(system, placements,
			placement_count) ||
			!world_spelunk_system_discover_portal(system, arrival->id) ||
			!world_spelunk_system_set_active_node(system, plan.nodes[0].id) ||
			!world_spelunk_system_is_valid(system)) {
		result = WORLD_SPELUNK_PUBLICATION_SYSTEM_FAILED;
		goto cleanup;
	}
	*published = system;
	system = NULL;
	result = WORLD_SPELUNK_PUBLICATION_OK;

cleanup:
	world_spelunk_system_free(system);
	world_spelunk_runtime_free(root_runtime);
	world_spelunk_generated_layout_dispose(&generated);
	return result;
}

enum world_spelunk_reconstruction_result
	world_spelunk_reconstruct_node_candidate(
		const struct world_spelunk_system *system, const char *node_id,
		unsigned int action_energy,
		struct world_spelunk_generated_layout *reconstructed)
{
	const struct world_spelunk_system_profile *profile;
	const struct world_spelunk_system_node *node;
	const struct world_spelunk_recipe_definition *recipe;
	enum world_spelunk_reconstruction_result result;

	if (!reconstructed) return WORLD_SPELUNK_RECONSTRUCTION_INVALID_ARGUMENT;
	memset(reconstructed, 0, sizeof(*reconstructed));
	if (!system || !node_id || !action_energy ||
			!world_spelunk_system_is_valid(system)) {
		return WORLD_SPELUNK_RECONSTRUCTION_INVALID_ARGUMENT;
	}
	profile = world_spelunk_system_profile_by_id(system->id);
	node = world_spelunk_system_node_by_id(system, node_id);
	recipe = node ? world_spelunk_recipe_definition_by_id(node->recipe_id) :
		NULL;
	if (!profile || !node || !node->runtime || !recipe ||
			profile->version != system->graph_version ||
			!streq(profile->location_id, system->location_id) ||
			!streq(recipe->system_id, system->id) ||
			recipe->generator_version != node->generator_version) {
		return WORLD_SPELUNK_RECONSTRUCTION_DATA_MISMATCH;
	}
	if (world_spelunk_generate_layout(recipe, node->seed, action_energy,
			reconstructed) != WORLD_SPELUNK_GENERATION_OK) {
		return WORLD_SPELUNK_RECONSTRUCTION_GENERATION_FAILED;
	}
	if (node == &system->nodes[0]) {
		struct root_endpoint_placement placements[ROOT_ENDPOINT_MAX];
		size_t placement_count = 0;
		size_t unused_arrival = SIZE_MAX;

		if (!streq(recipe->id, profile->root_recipe_id) ||
				!build_root_placements(profile, node->id, system->edges,
					system->edge_count, recipe, reconstructed, placements,
					&placement_count, &unused_arrival, NULL) ||
				!reconstructed_root_placements_match(system, placements,
					placement_count)) {
			result = WORLD_SPELUNK_RECONSTRUCTION_ENDPOINT_MISMATCH;
			goto cleanup;
		}
	} else {
		struct world_spelunk_system_endpoint_grid grids[
			WORLD_SPELUNK_SYSTEM_EDGE_MAX];
		size_t grid_count = 0;
		size_t arrival_index = SIZE_MAX;
		const char *arrival_endpoint_id = NULL;
		size_t i;

		for (i = 0; i < system->edge_count; i++) {
			if (streq(system->edges[i].node_a_id, node->id)) {
				arrival_endpoint_id = system->edges[i].endpoint_a_id;
				break;
			}
			if (streq(system->edges[i].node_b_id, node->id)) {
				arrival_endpoint_id = system->edges[i].endpoint_b_id;
				break;
			}
		}
		if (!arrival_endpoint_id || !build_node_placements(system, node, recipe,
				reconstructed, arrival_endpoint_id, grids, &grid_count,
				&arrival_index) || !reconstructed_node_placements_match(system,
					node->id, grids, grid_count)) {
			result = WORLD_SPELUNK_RECONSTRUCTION_ENDPOINT_MISMATCH;
			goto cleanup;
		}
	}
	if (!runtime_matches_generated(node->runtime, reconstructed)) {
		result = WORLD_SPELUNK_RECONSTRUCTION_RUNTIME_MISMATCH;
		goto cleanup;
	}
	return WORLD_SPELUNK_RECONSTRUCTION_OK;

cleanup:
	world_spelunk_generated_layout_dispose(reconstructed);
	return result;
}

enum world_spelunk_publication_result
	world_spelunk_materialize_node_candidate(
		struct world_spelunk_system *system, const char *node_id,
		unsigned int action_energy, const char *arrival_endpoint_id)
{
	struct world_spelunk_system_node *node;
	const struct world_spelunk_recipe_definition *recipe;
	struct world_spelunk_generated_layout generated;
	struct world_spelunk_system_endpoint_grid grids[
		WORLD_SPELUNK_SYSTEM_ENDPOINT_MAX];
	struct world_spelunk_runtime *runtime = NULL;
	size_t grid_count = 0;
	size_t arrival_index = SIZE_MAX;
	enum world_spelunk_publication_result result;

	if (!system || !node_id || !action_energy || !arrival_endpoint_id ||
			!world_spelunk_system_is_valid(system)) {
		return WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT;
	}
	node = world_spelunk_system_node_by_id_mutable(system, node_id);
	if (!node || node->runtime) {
		return WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT;
	}
	recipe = world_spelunk_recipe_definition_by_id(node->recipe_id);
	if (!recipe || !streq(recipe->system_id, system->id) ||
			recipe->generator_version != node->generator_version) {
		return WORLD_SPELUNK_PUBLICATION_PLAN_FAILED;
	}
	if (world_spelunk_generate_layout(recipe, node->seed, action_energy,
			&generated) != WORLD_SPELUNK_GENERATION_OK) {
		return WORLD_SPELUNK_PUBLICATION_GENERATION_FAILED;
	}
	if (!build_node_placements(system, node, recipe, &generated,
			arrival_endpoint_id, grids, &grid_count, &arrival_index)) {
		result = WORLD_SPELUNK_PUBLICATION_ENDPOINT_FAILED;
		goto cleanup;
	}
	runtime = create_generated_runtime(system->location_id, &generated,
		grids[arrival_index].x, grids[arrival_index].y);
	if (!runtime) {
		result = WORLD_SPELUNK_PUBLICATION_RUNTIME_FAILED;
		goto cleanup;
	}
	if (!world_spelunk_system_materialize_node(system, node_id, runtime,
			grids, grid_count)) {
		result = WORLD_SPELUNK_PUBLICATION_SYSTEM_FAILED;
		goto cleanup;
	}
	runtime = NULL;
	result = WORLD_SPELUNK_PUBLICATION_OK;

cleanup:
	world_spelunk_runtime_free(runtime);
	world_spelunk_generated_layout_dispose(&generated);
	return result;
}
