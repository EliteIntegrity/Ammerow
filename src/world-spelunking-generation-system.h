/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-system.h
 * \brief Deterministic unpublished plans for connected cave systems.
 */

#ifndef WORLD_SPELUNKING_GENERATION_SYSTEM_H
#define WORLD_SPELUNKING_GENERATION_SYSTEM_H

#include "world-spelunking-system-data.h"
#include "world-spelunking-system.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct world_spelunk_system_plan_node {
	char id[WORLD_ID_LEN];
	char recipe_id[WORLD_ID_LEN];
	uint32_t seed;
	uint16_t generator_version;
	uint16_t depth;
};

struct world_spelunk_system_plan {
	char system_id[WORLD_ID_LEN];
	uint32_t seed;
	uint16_t graph_version;
	struct world_spelunk_system_plan_node
		nodes[WORLD_SPELUNK_SYSTEM_NODE_MAX];
	size_t node_count;
	struct world_spelunk_system_edge edges[WORLD_SPELUNK_SYSTEM_EDGE_MAX];
	size_t edge_count;
};

enum world_spelunk_system_plan_result {
	WORLD_SPELUNK_SYSTEM_PLAN_OK = 0,
	WORLD_SPELUNK_SYSTEM_PLAN_INVALID_ARGUMENT,
	WORLD_SPELUNK_SYSTEM_PLAN_UNRESOLVED_RECIPE,
	WORLD_SPELUNK_SYSTEM_PLAN_UNSUPPORTED_VERSION,
	WORLD_SPELUNK_SYSTEM_PLAN_EXHAUSTED
};

enum world_spelunk_system_plan_result world_spelunk_plan_system(
		const struct world_spelunk_system_profile *profile, uint32_t seed,
		struct world_spelunk_system_plan *plan);
bool world_spelunk_system_plan_is_valid(
		const struct world_spelunk_system_plan *plan,
		const struct world_spelunk_system_profile *profile);

#endif /* !WORLD_SPELUNKING_GENERATION_SYSTEM_H */
