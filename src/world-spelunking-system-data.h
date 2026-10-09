/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-system-data.h
 * \brief Parsed authority for bounded procedural cave-system topology.
 */

#ifndef WORLD_SPELUNKING_SYSTEM_DATA_H
#define WORLD_SPELUNKING_SYSTEM_DATA_H

#include "init.h"
#include "world-spelunking-system.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_SPELUNK_SYSTEM_PROFILE_MAX 8

/** Which generated section owns an external cave-system portal. */
enum world_spelunk_portal_target {
	WORLD_SPELUNK_PORTAL_ROOT = 0,
	WORLD_SPELUNK_PORTAL_DEEPEST
};

/** Semantic route binding for an external cave-system portal. */
enum world_spelunk_portal_anchor {
	WORLD_SPELUNK_PORTAL_ANY_ROUTE = 0,
	WORLD_SPELUNK_PORTAL_UPPER_RAIL
};

/** An external world entry and its authored procedural placement contract. */
struct world_spelunk_system_portal_contract {
	const char *id;
	const char *entry_id;
	uint16_t min_depth_percent;
	uint16_t max_depth_percent;
	enum world_spelunk_portal_target target;
	enum world_spelunk_portal_anchor anchor;
	bool initially_enabled;
};

struct world_spelunk_system_profile {
	const char *id;
	const char *name;
	const char *description;
	const char *root_recipe_id;
	const char *location_id;
	uint16_t version;
	uint16_t min_sections;
	uint16_t max_sections;
	uint16_t min_root_children;
	uint16_t max_root_children;
	uint16_t max_children;
	uint16_t min_depth;
	uint16_t max_depth;
	uint16_t portal_count;
	struct world_spelunk_system_portal_contract
		portals[WORLD_SPELUNK_SYSTEM_PORTAL_MAX];
};

extern struct file_parser spelunking_system_parser;

int world_spelunk_system_profile_count(void);
const struct world_spelunk_system_profile *world_spelunk_system_profile_by_index(
		int index);
const struct world_spelunk_system_profile *world_spelunk_system_profile_by_id(
		const char *id);
/** Resolve the sole procedural system authored for a side-view location. */
const struct world_spelunk_system_profile *
	world_spelunk_system_profile_by_location(const char *location_id);
bool world_spelunk_system_data_validate_recipes(void);
bool world_spelunk_system_data_validate_world(void);
/** Add newly-authored portals to an older persistent cave graph. */
bool world_spelunk_system_reconcile_portals(
		struct world_spelunk_system *system);

#endif /* !WORLD_SPELUNKING_SYSTEM_DATA_H */
