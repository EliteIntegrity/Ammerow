/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation.h
 * \brief Deterministic unpublished candidates for procedural cave sections.
 */

#ifndef WORLD_SPELUNKING_GENERATION_H
#define WORLD_SPELUNKING_GENERATION_H

#include "world-spelunking-geology-data.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-water-data.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WORLD_SPELUNK_GENERATED_ROUTE_STATION_MAX 32
#define WORLD_SPELUNK_GENERATED_ROUTE_SEGMENT_MAX 31
#define WORLD_SPELUNK_GENERATED_ROUTE_VISIT_MAX 32

struct world_spelunk_witness_step;

struct world_spelunk_generated_route_station {
	int x;
	int y;
	uint32_t checkpoint_mask;
};

/** A descending rail between two supported stations via one narrow shaft. */
struct world_spelunk_generated_route_segment {
	uint16_t from_station;
	uint16_t to_station;
	int shaft_x;
	bool uses_rope;
};

/** An exact side visit proved while the witness is at one rail station. */
struct world_spelunk_generated_route_visit {
	uint16_t station;
	int x;
	int y;
	uint32_t checkpoint_mask;
};

enum world_spelunk_generation_result {
	WORLD_SPELUNK_GENERATION_OK = 0,
	WORLD_SPELUNK_GENERATION_INVALID_ARGUMENT,
	WORLD_SPELUNK_GENERATION_UNRESOLVED_REFERENCE,
	WORLD_SPELUNK_GENERATION_UNSUPPORTED_VERSION,
	WORLD_SPELUNK_GENERATION_EXHAUSTED
};

struct world_spelunk_generated_landmark {
	char id[WORLD_ID_LEN];
	enum world_spelunk_landmark_kind kind;
	int x;
	int y;
	char object_tval[WORLD_SPELUNK_LANDMARK_TVAL_LEN];
	char object_sval[WORLD_SPELUNK_LANDMARK_SVAL_LEN];
};

/**
 * A candidate owns cells but no gameplay state.  Hydrology and landmark gates
 * are necessary but not sufficient for later publication as a runtime.
 */
struct world_spelunk_generated_layout {
	char recipe_id[WORLD_ID_LEN];
	char system_id[WORLD_ID_LEN];
	char water_profile_id[WORLD_ID_LEN];
	char material_profile_id[WORLD_ID_LEN];
	char traversal_profile_id[WORLD_ID_LEN];
	char population_profile_id[WORLD_ID_LEN];
	uint32_t seed;
	uint16_t generator_version;
	uint16_t geology_version;
	int width;
	int height;
	int entrance_x;
	int entrance_y;
	int deepest_x;
	int deepest_y;
	int water_surface_y;
	int water_left_x;
	int water_right_x;
	int water_bed_y;
	int fishing_stance_x;
	int fishing_stance_y;
	int initial_stamina;
	enum world_spelunk_tile *cells;
	uint8_t *material_tags;
	uint8_t *decoration_tags;
	struct world_spelunk_rules rules;
	struct world_spelunk_perception perception;
	uint16_t landmark_count;
	struct world_spelunk_generated_landmark
		landmarks[WORLD_SPELUNK_LANDMARK_MAX];
	/** Transient generation intent; neither route form is persisted. */
	uint16_t route_station_count;
	struct world_spelunk_generated_route_station
		route_stations[WORLD_SPELUNK_GENERATED_ROUTE_STATION_MAX];
	uint16_t route_segment_count;
	struct world_spelunk_generated_route_segment
		route_segments[WORLD_SPELUNK_GENERATED_ROUTE_SEGMENT_MAX];
	uint16_t route_visit_count;
	struct world_spelunk_generated_route_visit
		route_visits[WORLD_SPELUNK_GENERATED_ROUTE_VISIT_MAX];
	struct world_spelunk_witness_step *witness_steps;
	size_t witness_step_count;
	uint32_t witness_required_checkpoint_mask;
};

enum world_spelunk_generation_result world_spelunk_generate_layout(
		const struct world_spelunk_recipe_definition *recipe, uint32_t seed,
		unsigned int action_energy,
		struct world_spelunk_generated_layout *generated);
void world_spelunk_generated_layout_dispose(
		struct world_spelunk_generated_layout *generated);
bool world_spelunk_generated_layout_is_structurally_valid(
		const struct world_spelunk_generated_layout *generated);
bool world_spelunk_generated_layout_is_hydrologically_valid(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_water_profile *profile);
bool world_spelunk_generated_layout_has_valid_landmarks(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_recipe_definition *recipe);

#endif /* !WORLD_SPELUNKING_GENERATION_H */
