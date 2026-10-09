/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-population-data.h
 * \brief Immutable population profiles for generated cave sections.
 */

#ifndef WORLD_SPELUNKING_POPULATION_DATA_H
#define WORLD_SPELUNKING_POPULATION_DATA_H

#include "init.h"
#include "world-spelunking-runtime.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_SPELUNK_POPULATION_PROFILE_MAX 32
#define WORLD_SPELUNK_POPULATION_ACTOR_ENTRY_MAX 16
#define WORLD_SPELUNK_POPULATION_OBJECT_ENTRY_MAX 16
#define WORLD_SPELUNK_POPULATION_COUNT_MAX 16
#define WORLD_SPELUNK_POPULATION_TVAL_LEN 32
#define WORLD_SPELUNK_POPULATION_SVAL_LEN 80

struct world_spelunk_population_actor_entry {
	const char *actor_id;
	uint16_t minimum_count;
	uint16_t maximum_count;
	uint16_t minimum_depth_percent;
	uint16_t maximum_depth_percent;
};

struct world_spelunk_population_object_entry {
	const char *object_tval;
	const char *object_sval;
	uint16_t minimum_count;
	uint16_t maximum_count;
	uint16_t minimum_depth_percent;
	uint16_t maximum_depth_percent;
};

/** Placement policy and content authority selected by a recipe. */
struct world_spelunk_population_profile {
	const char *id;
	const char *name;
	const char *description;
	uint16_t version;
	uint16_t entrance_exclusion_radius;
	uint16_t landmark_exclusion_radius;
	uint16_t endpoint_exclusion_radius;
	uint16_t actor_entry_count;
	struct world_spelunk_population_actor_entry
		actors[WORLD_SPELUNK_POPULATION_ACTOR_ENTRY_MAX];
	uint16_t object_entry_count;
	struct world_spelunk_population_object_entry
		objects[WORLD_SPELUNK_POPULATION_OBJECT_ENTRY_MAX];
};

extern struct file_parser spelunking_population_parser;

int world_spelunk_population_profile_count(void);
const struct world_spelunk_population_profile *
	world_spelunk_population_profile_by_index(int index);
const struct world_spelunk_population_profile *
	world_spelunk_population_profile_by_id(const char *id);

/** Resolve actor and object identities after all core data is loaded. */
bool world_spelunk_population_data_validate_world(void);

#endif /* !WORLD_SPELUNKING_POPULATION_DATA_H */
