/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-system.h
 * \brief Persistent ownership for a connected side-view cave system.
 */

#ifndef WORLD_SPELUNKING_SYSTEM_H
#define WORLD_SPELUNKING_SYSTEM_H

#include "world-location.h"
#include "world-spelunking-runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORLD_SPELUNK_SYSTEM_NODE_MAX 16u
#define WORLD_SPELUNK_SYSTEM_EDGE_MAX 24u
#define WORLD_SPELUNK_SYSTEM_PORTAL_MAX 8u
#define WORLD_SPELUNK_SYSTEM_ENDPOINT_MAX \
	(WORLD_SPELUNK_SYSTEM_EDGE_MAX + WORLD_SPELUNK_SYSTEM_PORTAL_MAX)
#define WORLD_SPELUNK_SYSTEM_GRAPH_VERSION_LEGACY 0u
#define WORLD_SPELUNK_SYSTEM_GRAPH_VERSION 1u

/** One stable cave section.  A null runtime is an unvisited planned node. */
struct world_spelunk_system_node {
	char id[WORLD_ID_LEN];
	char recipe_id[WORLD_ID_LEN];
	uint32_t seed;
	uint16_t generator_version;
	uint16_t depth;
	bool discovered;
	struct world_spelunk_runtime *runtime;
};

/**
 * One reciprocal connection with a stable endpoint in each section. In a
 * current generated graph, A is the shallower parent/down endpoint and B is
 * its depth-plus-one child/up endpoint.
 */
struct world_spelunk_system_edge {
	char id[WORLD_ID_LEN];
	char node_a_id[WORLD_ID_LEN];
	char node_b_id[WORLD_ID_LEN];
	char endpoint_a_id[WORLD_ID_LEN];
	char endpoint_b_id[WORLD_ID_LEN];
	int16_t endpoint_a_x;
	int16_t endpoint_a_y;
	int16_t endpoint_b_x;
	int16_t endpoint_b_y;
	bool endpoint_a_materialized;
	bool endpoint_b_materialized;
	bool discovered;
};

/** A named top-down entrance materialized in one cave-system section. */
struct world_spelunk_system_portal {
	char id[WORLD_ID_LEN];
	char entry_id[WORLD_ENTRY_LEN];
	char node_id[WORLD_ID_LEN];
	int16_t x;
	int16_t y;
	bool enabled;
	bool materialized;
	bool discovered;
};

/** One prepared coordinate for an endpoint owned by a section. */
struct world_spelunk_system_endpoint_grid {
	const char *endpoint_id;
	int x;
	int y;
};

/**
 * One named cave system.  This owns every non-null node runtime.  The player
 * retains a non-owning alias to the active runtime for existing side-view
 * rules and presentation code.
 */
struct world_spelunk_system {
	char id[WORLD_ID_LEN];
	/** Stable world level shared by this system's generated sections. */
	char location_id[WORLD_ID_LEN];
	uint32_t seed;
	uint16_t graph_version;
	char active_node_id[WORLD_ID_LEN];
	struct world_spelunk_system_node nodes[WORLD_SPELUNK_SYSTEM_NODE_MAX];
	size_t node_count;
	struct world_spelunk_system_edge edges[WORLD_SPELUNK_SYSTEM_EDGE_MAX];
	size_t edge_count;
	struct world_spelunk_system_portal
		portals[WORLD_SPELUNK_SYSTEM_PORTAL_MAX];
	size_t portal_count;
};

struct world_spelunk_system *world_spelunk_system_create(const char *id,
		uint32_t seed, uint16_t graph_version);
struct world_spelunk_system *world_spelunk_system_create_legacy(
		struct world_spelunk_runtime *runtime);
void world_spelunk_system_free(struct world_spelunk_system *system);
bool world_spelunk_system_set_location(struct world_spelunk_system *system,
		const char *location_id);

/** Transfers runtime ownership on success; leaves it with the caller on failure. */
bool world_spelunk_system_add_node(struct world_spelunk_system *system,
		const char *node_id, const char *recipe_id, uint32_t seed,
		uint16_t generator_version, uint16_t depth,
		struct world_spelunk_runtime *runtime);
bool world_spelunk_system_set_active_node(struct world_spelunk_system *system,
		const char *node_id);
bool world_spelunk_system_add_edge(struct world_spelunk_system *system,
		const char *edge_id, const char *node_a_id, const char *endpoint_a_id,
		const char *node_b_id, const char *endpoint_b_id, bool discovered);
bool world_spelunk_system_discover_edge(struct world_spelunk_system *system,
		const char *edge_id);
bool world_spelunk_system_set_endpoint_grid(
		struct world_spelunk_system *system, const char *endpoint_id,
		int x, int y);
bool world_spelunk_system_add_portal(struct world_spelunk_system *system,
		const char *portal_id, const char *entry_id, const char *node_id,
		bool enabled);
bool world_spelunk_system_enable_portal(struct world_spelunk_system *system,
		const char *portal_id);
bool world_spelunk_system_set_portal_grid(struct world_spelunk_system *system,
		const char *portal_id, int x, int y);
bool world_spelunk_system_discover_portal(struct world_spelunk_system *system,
		const char *portal_id);

/**
 * Atomically attach a first-visit runtime and every incident endpoint.
 * Transfers runtime ownership on success; leaves it with the caller on
 * failure.  Existing discovered state is preserved.
 */
bool world_spelunk_system_materialize_node(
		struct world_spelunk_system *system, const char *node_id,
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_system_endpoint_grid *grids,
		size_t grid_count);

const struct world_spelunk_system_node *world_spelunk_system_node_by_id(
		const struct world_spelunk_system *system, const char *node_id);
struct world_spelunk_system_node *world_spelunk_system_node_by_id_mutable(
		struct world_spelunk_system *system, const char *node_id);
const struct world_spelunk_system_edge *world_spelunk_system_edge_by_id(
		const struct world_spelunk_system *system, const char *edge_id);
const struct world_spelunk_system_portal *world_spelunk_system_portal_by_id(
		const struct world_spelunk_system *system, const char *portal_id);
const struct world_spelunk_system_portal *world_spelunk_system_portal_by_entry(
		const struct world_spelunk_system *system, const char *entry_id);
struct world_spelunk_runtime *world_spelunk_system_active_runtime(
		const struct world_spelunk_system *system);
bool world_spelunk_system_owns_runtime(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime);
bool world_spelunk_system_is_valid(const struct world_spelunk_system *system);

#endif /* !WORLD_SPELUNKING_SYSTEM_H */
