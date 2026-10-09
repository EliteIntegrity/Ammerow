/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-recipe-data.h
 * \brief Parsed authority for deterministic procedural cave recipes.
 */

#ifndef WORLD_SPELUNKING_RECIPE_DATA_H
#define WORLD_SPELUNKING_RECIPE_DATA_H

#include "init.h"
#include "world-spelunking-perception.h"
#include "world-spelunking-runtime.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_SPELUNK_RECIPE_MAX 32
#define WORLD_SPELUNK_LANDMARK_MAX 8
#define WORLD_SPELUNK_LANDMARK_TVAL_LEN 32
#define WORLD_SPELUNK_LANDMARK_SVAL_LEN 80

enum world_spelunk_recipe_role {
	WORLD_SPELUNK_RECIPE_ROOT = 0,
	WORLD_SPELUNK_RECIPE_OPTIONAL,
	WORLD_SPELUNK_RECIPE_DEEP,
	WORLD_SPELUNK_RECIPE_LINK
};

enum world_spelunk_macro_family {
	WORLD_SPELUNK_MACRO_CHAMBER_CHAIN = 0
};

enum world_spelunk_landmark_kind {
	WORLD_SPELUNK_LANDMARK_OBJECT = 0,
	WORLD_SPELUNK_LANDMARK_FISHING_STANCE,
	WORLD_SPELUNK_LANDMARK_SECTION_EXIT
};

enum world_spelunk_passage_direction {
	WORLD_SPELUNK_PASSAGE_UP = 0,
	WORLD_SPELUNK_PASSAGE_DOWN,
	WORLD_SPELUNK_PASSAGE_DIRECTION_COUNT
};

/** A variable-capacity placement band for planned graph endpoints. */
struct world_spelunk_passage_contract {
	uint16_t min_depth_percent;
	uint16_t max_depth_percent;
	uint16_t capacity;
};

/** A semantic placement request; coordinates are chosen after carving. */
struct world_spelunk_landmark_contract {
	const char *id;
	enum world_spelunk_landmark_kind kind;
	uint16_t min_depth_percent;
	uint16_t max_depth_percent;
	const char *object_tval;
	const char *object_sval;
};

struct world_spelunk_size_profile {
	uint16_t min_width;
	uint16_t max_width;
	uint16_t min_height;
	uint16_t max_height;
};

struct world_spelunk_route_profile {
	uint16_t min_chambers;
	uint16_t max_chambers;
	uint16_t min_radius_x;
	uint16_t max_radius_x;
	uint16_t min_radius_y;
	uint16_t max_radius_y;
	uint16_t min_branches;
	uint16_t max_branches;
	uint16_t side_margin;
	uint16_t entrance_y;
	uint16_t bottom_margin;
	uint16_t min_vertical_gap;
	uint16_t max_vertical_gap;
};

/** Immutable recipe.  Algorithms recognize families, never recipe IDs. */
struct world_spelunk_recipe_definition {
	const char *id;
	const char *system_id;
	const char *name;
	const char *description;
	const char *water_profile_id;
	const char *material_profile_id;
	const char *traversal_profile_id;
	const char *population_profile_id;
	enum world_spelunk_recipe_role role;
	enum world_spelunk_macro_family macro_family;
	uint16_t weight;
	uint16_t generator_version;
	uint16_t initial_stamina;
	struct world_spelunk_size_profile size;
	struct world_spelunk_route_profile route;
	struct world_spelunk_rules rules;
	struct world_spelunk_perception perception;
	uint16_t landmark_count;
	struct world_spelunk_landmark_contract
		landmarks[WORLD_SPELUNK_LANDMARK_MAX];
	struct world_spelunk_passage_contract
		passages[WORLD_SPELUNK_PASSAGE_DIRECTION_COUNT];
};

extern struct file_parser spelunking_recipe_parser;

int world_spelunk_recipe_definition_count(void);
const struct world_spelunk_recipe_definition *
	world_spelunk_recipe_definition_by_index(int index);
const struct world_spelunk_recipe_definition *
	world_spelunk_recipe_definition_by_id(const char *id);

/** Copy authored balance rules and inject the engine action-energy unit. */
bool world_spelunk_recipe_rules(
		const struct world_spelunk_recipe_definition *definition,
		unsigned int action_energy, struct world_spelunk_rules *rules);

/** Resolve cross-file hydrology, geology, traversal and object references. */
bool world_spelunk_recipe_data_validate_world(void);

#endif /* !WORLD_SPELUNKING_RECIPE_DATA_H */
