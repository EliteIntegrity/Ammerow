/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-system.c
 * \brief Deterministic unpublished plans for connected cave systems.
 */

#include "world-spelunking-generation-system.h"

#include "world-spelunking-recipe-data.h"
#include "z-util.h"

#include <string.h>

struct plan_rng {
	uint32_t state;
};

static uint32_t hash_text(const char *text)
{
	uint32_t hash = 2166136261u;

	while (text && *text) {
		hash ^= (uint8_t)*text++;
		hash *= 16777619u;
	}
	return hash;
}

static uint32_t mix32(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

static uint32_t rng_next(struct plan_rng *rng)
{
	uint32_t value;

	rng->state += 0x9e3779b9u;
	value = rng->state;
	return mix32(value);
}

static int rng_range(struct plan_rng *rng, int minimum, int maximum)
{
	uint32_t span;
	uint32_t limit;
	uint32_t value;

	if (minimum >= maximum) return minimum;
	span = (uint32_t)(maximum - minimum + 1);
	limit = UINT32_MAX - UINT32_MAX % span;
	do {
		value = rng_next(rng);
	} while (value >= limit);
	return minimum + (int)(value % span);
}

static const struct world_spelunk_recipe_definition *choose_recipe(
		const struct world_spelunk_system_profile *profile,
		enum world_spelunk_recipe_role role, struct plan_rng *rng)
{
	uint32_t total = 0;
	uint32_t selected;
	int i;

	for (i = 0; i < world_spelunk_recipe_definition_count(); i++) {
		const struct world_spelunk_recipe_definition *recipe =
			world_spelunk_recipe_definition_by_index(i);

		if (recipe && streq(recipe->system_id, profile->id) &&
				recipe->role == role) {
			total += recipe->weight;
		}
	}
	if (!total) return NULL;
	selected = (uint32_t)rng_range(rng, 1, (int)total);
	for (i = 0; i < world_spelunk_recipe_definition_count(); i++) {
		const struct world_spelunk_recipe_definition *recipe =
			world_spelunk_recipe_definition_by_index(i);

		if (!recipe || !streq(recipe->system_id, profile->id) ||
				recipe->role != role) continue;
		if (selected <= recipe->weight) return recipe;
		selected -= recipe->weight;
	}
	return NULL;
}

static bool add_plan_node(const struct world_spelunk_system_profile *profile,
		struct world_spelunk_system_plan *plan, struct plan_rng *rng,
		uint16_t depth, bool root, int *added_index)
{
	const struct world_spelunk_recipe_definition *recipe;
	struct world_spelunk_system_plan_node *node;
	enum world_spelunk_recipe_role role = depth == 0 ?
		WORLD_SPELUNK_RECIPE_ROOT : WORLD_SPELUNK_RECIPE_OPTIONAL;

	if (plan->node_count >= WORLD_SPELUNK_SYSTEM_NODE_MAX) return false;
	if (root) {
		recipe = world_spelunk_recipe_definition_by_id(profile->root_recipe_id);
	} else {
		recipe = choose_recipe(profile, role, rng);
	}
	if (!recipe) return false;
	node = &plan->nodes[plan->node_count];
	strnfmt(node->id, sizeof(node->id), "%s.section.%03u", profile->id,
		(unsigned int)plan->node_count);
	my_strcpy(node->recipe_id, recipe->id, sizeof(node->recipe_id));
	node->seed = rng_next(rng);
	node->generator_version = recipe->generator_version;
	node->depth = depth;
	*added_index = (int)plan->node_count++;
	return true;
}

static bool replace_with_deep_recipe(
		const struct world_spelunk_system_profile *profile,
		struct world_spelunk_system_plan_node *node, struct plan_rng *rng)
{
	const struct world_spelunk_recipe_definition *recipe =
		choose_recipe(profile, WORLD_SPELUNK_RECIPE_DEEP, rng);

	if (!recipe) return false;
	my_strcpy(node->recipe_id, recipe->id, sizeof(node->recipe_id));
	node->generator_version = recipe->generator_version;
	return true;
}

static bool add_plan_edge(const struct world_spelunk_system_profile *profile,
		struct world_spelunk_system_plan *plan, int parent, int child)
{
	struct world_spelunk_system_edge *edge;
	unsigned int index;

	if (parent < 0 || child < 0 || parent >= (int)plan->node_count ||
			child >= (int)plan->node_count || parent == child ||
			plan->edge_count >= WORLD_SPELUNK_SYSTEM_EDGE_MAX) {
		return false;
	}
	index = (unsigned int)plan->edge_count;
	edge = &plan->edges[plan->edge_count++];
	strnfmt(edge->id, sizeof(edge->id), "%s.passage.%03u", profile->id,
		index);
	my_strcpy(edge->node_a_id, plan->nodes[parent].id,
		sizeof(edge->node_a_id));
	my_strcpy(edge->node_b_id, plan->nodes[child].id,
		sizeof(edge->node_b_id));
	strnfmt(edge->endpoint_a_id, sizeof(edge->endpoint_a_id),
		"%s.end.%03ua", profile->id, index);
	strnfmt(edge->endpoint_b_id, sizeof(edge->endpoint_b_id),
		"%s.end.%03ub", profile->id, index);
	edge->discovered = false;
	return true;
}

enum world_spelunk_system_plan_result world_spelunk_plan_system(
		const struct world_spelunk_system_profile *profile, uint32_t seed,
		struct world_spelunk_system_plan *plan)
{
	struct plan_rng rng;
	uint8_t child_count[WORLD_SPELUNK_SYSTEM_NODE_MAX] = { 0 };
	int target_sections;
	int target_depth;
	int target_root_children;
	int root_index;
	int previous;
	int depth;

	if (!profile || !plan || !world_id_is_valid(profile->id) ||
			!world_id_is_valid(profile->root_recipe_id)) {
		return WORLD_SPELUNK_SYSTEM_PLAN_INVALID_ARGUMENT;
	}
	if (profile->version != WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		return WORLD_SPELUNK_SYSTEM_PLAN_UNSUPPORTED_VERSION;
	}
	if (!world_spelunk_recipe_definition_by_id(profile->root_recipe_id)) {
		return WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE;
	}
	memset(plan, 0, sizeof(*plan));
	rng.state = mix32(seed ^ hash_text(profile->id) ^
		((uint32_t)profile->version << 16) ^ 0x67726170u);
	target_sections = rng_range(&rng, profile->min_sections,
		profile->max_sections);
	target_root_children = rng_range(&rng, profile->min_root_children,
		MIN(profile->max_root_children, target_sections - profile->min_depth));
	target_depth = rng_range(&rng, profile->min_depth,
		MIN(profile->max_depth, target_sections - target_root_children));
	my_strcpy(plan->system_id, profile->id, sizeof(plan->system_id));
	plan->seed = seed;
	plan->graph_version = profile->version;
	if (!add_plan_node(profile, plan, &rng, 0, true, &root_index)) {
		return WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE;
	}
	previous = root_index;
	for (depth = 1; depth <= target_depth; depth++) {
		int child;

		if (!add_plan_node(profile, plan, &rng, (uint16_t)depth, false,
				&child) || !add_plan_edge(profile, plan, previous, child)) {
			return WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE;
		}
		child_count[previous]++;
		previous = child;
	}
	if (!replace_with_deep_recipe(profile, &plan->nodes[previous], &rng)) {
		return WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE;
	}
	while (child_count[root_index] < target_root_children) {
		int child;

		if (!add_plan_node(profile, plan, &rng, 1, false, &child) ||
				!add_plan_edge(profile, plan, root_index, child)) {
			return WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE;
		}
		child_count[root_index]++;
	}
	while ((int)plan->node_count < target_sections) {
		int eligible[WORLD_SPELUNK_SYSTEM_NODE_MAX];
		int eligible_count = 0;
		int parent;
		int child;
		size_t i;

		for (i = 1; i < plan->node_count; i++) {
			if (child_count[i] < profile->max_children &&
					plan->nodes[i].depth < target_depth) {
				eligible[eligible_count++] = (int)i;
			}
		}
		if (!eligible_count) return WORLD_SPELUNK_SYSTEM_PLAN_EXHAUSTED;
		parent = eligible[rng_range(&rng, 0, eligible_count - 1)];
		if (!add_plan_node(profile, plan, &rng,
				(uint16_t)(plan->nodes[parent].depth + 1), false, &child) ||
				!add_plan_edge(profile, plan, parent, child)) {
			return WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE;
		}
		child_count[parent]++;
		if (plan->nodes[child].depth == target_depth &&
				!replace_with_deep_recipe(profile, &plan->nodes[child], &rng)) {
			return WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE;
		}
	}
	return world_spelunk_system_plan_is_valid(plan, profile) ?
		WORLD_SPELUNK_SYSTEM_PLAN_OK : WORLD_SPELUNK_SYSTEM_PLAN_EXHAUSTED;
}

static int plan_node_index(const struct world_spelunk_system_plan *plan,
		const char *id)
{
	size_t i;

	for (i = 0; i < plan->node_count; i++) {
		if (streq(plan->nodes[i].id, id)) return (int)i;
	}
	return -1;
}

bool world_spelunk_system_plan_is_valid(
		const struct world_spelunk_system_plan *plan,
		const struct world_spelunk_system_profile *profile)
{
	uint8_t child_count[WORLD_SPELUNK_SYSTEM_NODE_MAX] = { 0 };
	bool visited[WORLD_SPELUNK_SYSTEM_NODE_MAX] = { false };
	int maximum_depth = 0;
	int root_children = 0;
	size_t i;

	if (!plan || !profile || !streq(plan->system_id, profile->id) ||
			plan->graph_version != profile->version ||
			plan->node_count < profile->min_sections ||
			plan->node_count > profile->max_sections ||
			plan->edge_count != plan->node_count - 1 ||
			plan->edge_count > WORLD_SPELUNK_SYSTEM_EDGE_MAX) {
		return false;
	}
	for (i = 0; i < plan->node_count; i++) {
		const struct world_spelunk_system_plan_node *node = &plan->nodes[i];
		const struct world_spelunk_recipe_definition *recipe =
			world_spelunk_recipe_definition_by_id(node->recipe_id);
		size_t j;

		if (!world_id_is_valid(node->id) || !recipe ||
				!streq(recipe->system_id, profile->id) ||
				node->generator_version != recipe->generator_version ||
				node->depth > profile->max_depth ||
				(i == 0 && (!streq(recipe->id, profile->root_recipe_id) ||
				 node->depth != 0)) || (i > 0 && node->depth == 0)) {
			return false;
		}
		maximum_depth = MAX(maximum_depth, node->depth);
		for (j = i + 1; j < plan->node_count; j++) {
			if (streq(node->id, plan->nodes[j].id)) return false;
		}
	}
	for (i = 0; i < plan->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &plan->edges[i];
		int parent = plan_node_index(plan, edge->node_a_id);
		int child = plan_node_index(plan, edge->node_b_id);
		size_t j;

		if (!world_id_is_valid(edge->id) ||
				!world_id_is_valid(edge->endpoint_a_id) ||
				!world_id_is_valid(edge->endpoint_b_id) || parent < 0 || child < 0 ||
				plan->nodes[child].depth != plan->nodes[parent].depth + 1 ||
				edge->discovered) {
			return false;
		}
		child_count[parent]++;
		if (parent == 0) root_children++;
		for (j = i + 1; j < plan->edge_count; j++) {
			const struct world_spelunk_system_edge *other = &plan->edges[j];

			if (streq(edge->id, other->id) ||
					streq(edge->endpoint_a_id, other->endpoint_a_id) ||
					streq(edge->endpoint_a_id, other->endpoint_b_id) ||
					streq(edge->endpoint_b_id, other->endpoint_a_id) ||
					streq(edge->endpoint_b_id, other->endpoint_b_id)) {
				return false;
			}
		}
	}
	for (i = 0; i < plan->node_count; i++) {
		const struct world_spelunk_recipe_definition *recipe =
			world_spelunk_recipe_definition_by_id(plan->nodes[i].recipe_id);

		if (!recipe || child_count[i] > profile->max_children ||
				child_count[i] >
					recipe->passages[WORLD_SPELUNK_PASSAGE_DOWN].capacity ||
				(i > 0 && !recipe->passages[
					WORLD_SPELUNK_PASSAGE_UP].capacity)) {
			return false;
		}
		if (i > 0) {
			if ((plan->nodes[i].depth == maximum_depth &&
					recipe->role != WORLD_SPELUNK_RECIPE_DEEP) ||
					(plan->nodes[i].depth < maximum_depth &&
					 recipe->role != WORLD_SPELUNK_RECIPE_OPTIONAL)) {
				return false;
			}
		}
	}
	if (root_children < profile->min_root_children ||
			root_children > profile->max_root_children ||
			maximum_depth < profile->min_depth ||
			maximum_depth > profile->max_depth) {
		return false;
	}
	visited[0] = true;
	for (i = 0; i < plan->edge_count; i++) {
		int parent = plan_node_index(plan, plan->edges[i].node_a_id);
		int child = plan_node_index(plan, plan->edges[i].node_b_id);

		if (parent < 0 || child < 0 || !visited[parent]) return false;
		visited[child] = true;
	}
	for (i = 0; i < plan->node_count; i++) {
		if (!visited[i]) return false;
	}
	return true;
}
