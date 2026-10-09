/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation.c
 * \brief Deterministic procedural cave candidates, not gameplay publication.
 */

#include "world-spelunking-generation.h"

#include "world-spelunking-generation-geology.h"
#include "world-spelunking-generation-route.h"
#include "world-spelunking-generation-witness.h"
#include "world-spelunking-population-data.h"
#include "world-spelunking-traversal-data.h"

#include "z-util.h"
#include "z-virt.h"

#include <stdlib.h>
#include <string.h>

#define GENERATION_ATTEMPT_MAX 8u
#define CHAMBER_MAX 24u

struct generation_rng {
	uint32_t state;
};

struct chamber_anchor {
	int x;
	int y;
	int radius_x;
	int radius_y;
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

static uint32_t rng_next(struct generation_rng *rng)
{
	uint32_t value;

	rng->state += 0x9e3779b9u;
	value = rng->state;
	return mix32(value);
}

static int rng_range(struct generation_rng *rng, int minimum, int maximum)
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

static size_t cell_index(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	return (size_t)y * generated->width + x;
}

static void carve_rectangle(struct world_spelunk_generated_layout *generated,
		int x1, int y1, int x2, int y2)
{
	int x;
	int y;

	x1 = MAX(1, x1);
	y1 = MAX(1, y1);
	x2 = MIN(generated->width - 2, x2);
	y2 = MIN(generated->height - 2, y2);
	for (y = y1; y <= y2; y++) {
		for (x = x1; x <= x2; x++) {
			generated->cells[cell_index(generated, x, y)] =
				WORLD_SPELUNK_AIR;
		}
	}
}

static void carve_ellipse(struct world_spelunk_generated_layout *generated,
		int center_x, int center_y, int radius_x, int radius_y)
{
	int radius_x_squared = radius_x * radius_x;
	int radius_y_squared = radius_y * radius_y;
	int limit = radius_x_squared * radius_y_squared;
	int x;
	int y;

	for (y = center_y - radius_y; y <= center_y + radius_y; y++) {
		for (x = center_x - radius_x; x <= center_x + radius_x; x++) {
			int dx = x - center_x;
			int dy = y - center_y;

			if (x <= 0 || y <= 0 || x >= generated->width - 1 ||
					y >= generated->height - 1 ||
					dx * dx * radius_y_squared +
						dy * dy * radius_x_squared > limit) {
				continue;
			}
			generated->cells[cell_index(generated, x, y)] =
				WORLD_SPELUNK_AIR;
		}
	}
}

static void carve_horizontal(
		struct world_spelunk_generated_layout *generated,
		int x1, int x2, int y)
{
	carve_rectangle(generated, MIN(x1, x2), y, MAX(x1, x2), y + 1);
}

static bool tile_is_supported_air(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	return x > 0 && x < generated->width - 1 && y > 0 &&
		y < generated->height - 1 &&
		generated->cells[cell_index(generated, x, y)] == WORLD_SPELUNK_AIR &&
		generated->cells[cell_index(generated, x, y + 1)] ==
			WORLD_SPELUNK_ROCK;
}

static int depth_percent(
		const struct world_spelunk_generated_layout *generated, int y)
{
	return y * 100 / (generated->height - 1);
}

static bool apply_lower_basin(
		const struct world_spelunk_water_profile *profile,
		const struct world_spelunk_traversal_profile *traversal_profile,
		struct generation_rng *rng,
		struct world_spelunk_generated_layout *generated)
{
	int minimum_width = (generated->width * profile->min_width_percent +
		99) / 100;
	int maximum_width = generated->width * profile->max_width_percent / 100;
	int depth = rng_range(rng, profile->min_depth_tiles,
		profile->max_depth_tiles);
	int minimum_surface = ((generated->height - 1) *
		profile->min_surface_depth_percent + 99) / 100;
	int maximum_surface = (generated->height - 1) *
		profile->max_surface_depth_percent / 100;
	int maximum_grip_span =
		world_spelunk_generated_route_maximum_grip_span(generated);
	int maximum_access_span;
	int width;
	int surface;
	int bed;
	int left_minimum;
	int left_maximum;
	int left;
	int right;
	int stance;
	int approach;
	bool water_to_right = (generated->route_segment_count % 2) == 0;
	bool uses_rope;
	int existing_bottom = generated->entrance_y;
	int x;
	int y;

	/* Keep the reservoir below the already-carved dry cave.  A retaining
	 * basin may replace rock, but must not wall off an existing chamber. */
	for (y = 1; y < generated->height - 1; y++) {
		for (x = 1; x < generated->width - 1; x++) {
			if (generated->cells[cell_index(generated, x, y)] !=
					WORLD_SPELUNK_ROCK) {
				existing_bottom = MAX(existing_bottom, y);
			}
		}
	}

	minimum_width = MAX(6, minimum_width);
	maximum_width = MIN(generated->width - 12, maximum_width);
	maximum_surface = MIN(maximum_surface,
		generated->height - depth - 2);
	maximum_access_span = traversal_profile &&
		traversal_profile->piton_budget > 0 &&
		traversal_profile->rope_segment_budget > 0 ?
		MIN(generated->rules.rope_max_length,
			traversal_profile->rope_segment_budget) : maximum_grip_span;
	maximum_surface = MIN(maximum_surface,
		generated->deepest_y + MAX(3, maximum_access_span));
	minimum_surface = MAX(minimum_surface, existing_bottom + 2);
	if (minimum_width > maximum_width || minimum_surface > maximum_surface) {
		return false;
	}
	width = rng_range(rng, minimum_width, maximum_width);
	surface = rng_range(rng, minimum_surface, maximum_surface);
	uses_rope = surface - generated->deepest_y > maximum_grip_span;
	bed = surface + depth;
	if (water_to_right) {
		left_minimum = profile->min_shore_tiles + 2;
		left_maximum = generated->width - width - 3;
	} else {
		left_minimum = 3;
		left_maximum = generated->width - width -
			profile->min_shore_tiles - 2;
	}
	if (left_minimum > left_maximum) return false;
	left = rng_range(rng, left_minimum, left_maximum);
	right = left + width - 1;
	stance = water_to_right ? left - 1 : right + 1;
	approach = water_to_right ?
		stance - profile->min_shore_tiles + 1 :
		stance + profile->min_shore_tiles - 1;

	/* Build a sealed rectangular bed and banks first.  The one open bank is
	 * the supported fishing stance; the dry approach continues away from it. */
	for (y = surface; y <= bed; y++) {
		generated->cells[cell_index(generated, left - 1, y)] =
			WORLD_SPELUNK_ROCK;
		generated->cells[cell_index(generated, right + 1, y)] =
			WORLD_SPELUNK_ROCK;
	}
	for (x = left; x <= right; x++) {
		for (y = surface; y < bed; y++) {
			generated->cells[cell_index(generated, x, y)] =
				WORLD_SPELUNK_WATER;
		}
		generated->cells[cell_index(generated, x, bed)] =
			WORLD_SPELUNK_ROCK;
		generated->cells[cell_index(generated, x, surface - 1)] =
			WORLD_SPELUNK_AIR;
	}
	world_spelunk_generated_route_carve_supported(generated, approach,
		stance, surface);
	/* Open only the top of the near bank so water is horizontally adjacent to
	 * a stable stance while the submerged bank remains retaining rock. */
	generated->cells[cell_index(generated, stance, surface)] =
		WORLD_SPELUNK_AIR;

	generated->water_surface_y = surface;
	generated->water_left_x = left;
	generated->water_right_x = right;
	generated->water_bed_y = bed;
	generated->fishing_stance_x = stance;
	generated->fishing_stance_y = surface;
	{
		int previous = generated->route_station_count - 1;
		int station = world_spelunk_generated_route_record_station(generated,
			stance, surface);

		if (previous < 0 || station < 0 ||
				!world_spelunk_generated_route_connect(generated, previous,
					station, water_to_right ? WORLD_SPELUNK_ROUTE_LEFT :
					WORLD_SPELUNK_ROUTE_RIGHT, uses_rope)) {
			return false;
		}
	}
	return true;
}

static bool apply_hydrology(
		const struct world_spelunk_water_profile *profile,
		const struct world_spelunk_traversal_profile *traversal_profile,
		struct generation_rng *rng,
		struct world_spelunk_generated_layout *generated)
{
	if (!profile || profile->version != 1) return false;
	switch (profile->family) {
	case WORLD_SPELUNK_WATER_LOWER_BASIN:
		return apply_lower_basin(profile, traversal_profile, rng, generated);
	}
	return false;
}

static bool landmark_position_used(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	int i;

	for (i = 0; i < generated->landmark_count; i++) {
		if (generated->landmarks[i].x == x &&
				generated->landmarks[i].y == y) {
			return true;
		}
	}
	return false;
}

static bool select_supported_landmark_position(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_landmark_contract *contract,
		int *selected_x, int *selected_y, int *selected_station)
{
	int target = (contract->min_depth_percent +
		contract->max_depth_percent) / 2;
	int best_score = INT_MAX;
	int i;

	for (i = 0; i < generated->route_station_count; i++) {
		int x = generated->route_stations[i].x;
		int y = generated->route_stations[i].y;
		int depth = depth_percent(generated, y);
		int score;

		if (depth < contract->min_depth_percent ||
				depth > contract->max_depth_percent) {
			continue;
		}
		if (!tile_is_supported_air(generated, x, y) ||
				landmark_position_used(generated, x, y) ||
				(x == generated->entrance_x && y == generated->entrance_y) ||
				(x == generated->fishing_stance_x &&
					y == generated->fishing_stance_y)) {
			continue;
		}
		/* Prefer the authored depth while keeping required content on the
		 * semantic rail whose exact movement witness will be replayed. */
		score = abs(depth - target) * 100 + abs(x - generated->width / 2);
		if (contract->kind == WORLD_SPELUNK_LANDMARK_SECTION_EXIT) {
			score -= depth * 200;
		}
		if (score < best_score) {
			best_score = score;
			*selected_x = x;
			*selected_y = y;
			*selected_station = i;
		}
	}
	return best_score != INT_MAX;
}

static bool place_landmarks(
		const struct world_spelunk_recipe_definition *recipe,
		struct world_spelunk_generated_layout *generated)
{
	int i;

	for (i = 0; i < recipe->landmark_count; i++) {
		const struct world_spelunk_landmark_contract *contract =
			&recipe->landmarks[i];
		struct world_spelunk_generated_landmark *placed =
			&generated->landmarks[generated->landmark_count];
		int x = -1;
		int y = -1;
		int station = -1;
		int j;

		if (generated->landmark_count >= WORLD_SPELUNK_LANDMARK_MAX) {
			return false;
		}
		if (contract->kind == WORLD_SPELUNK_LANDMARK_FISHING_STANCE) {
			x = generated->fishing_stance_x;
			y = generated->fishing_stance_y;
			for (j = 0; j < generated->route_station_count; j++) {
				if (generated->route_stations[j].x == x &&
						generated->route_stations[j].y == y) {
					station = j;
					break;
				}
			}
		} else if (!select_supported_landmark_position(generated, contract,
				&x, &y, &station)) {
			return false;
		}
		if (station < 0 || landmark_position_used(generated, x, y)) {
			return false;
		}
		my_strcpy(placed->id, contract->id, sizeof(placed->id));
		placed->kind = contract->kind;
		placed->x = x;
		placed->y = y;
		my_strcpy(placed->object_tval, contract->object_tval,
			sizeof(placed->object_tval));
		my_strcpy(placed->object_sval, contract->object_sval,
			sizeof(placed->object_sval));
		generated->route_stations[station].checkpoint_mask |= 1u << i;
		generated->landmark_count++;
	}
	return true;
}

static bool generate_chamber_chain(
		const struct world_spelunk_recipe_definition *recipe,
		const struct world_spelunk_water_profile *water_profile,
		const struct world_spelunk_traversal_profile *traversal_profile,
		struct generation_rng *rng,
		struct world_spelunk_generated_layout *generated)
{
	const struct world_spelunk_route_profile *route = &recipe->route;
	struct chamber_anchor anchors[CHAMBER_MAX];
	int minimum_x = route->side_margin + route->max_radius_x + 1;
	int maximum_x = generated->width - route->side_margin -
		route->max_radius_x - 2;
	int first_y = route->entrance_y + route->max_radius_y + 4;
	int maximum_center_y = generated->height - route->bottom_margin -
		route->max_radius_y - 2;
	int maximum_water_surface = (generated->height - 1) *
		water_profile->max_surface_depth_percent / 100;
	int maximum_fit;
	int chamber_count;
	int gap_maximum;
	int gap;
	int branch_count;
	int previous_station;
	int i;

	/* Reserve enough vertical space for the data-authored lower basin rather
	 * than hoping a chamber chain leaves room after carving. */
	maximum_center_y = MIN(maximum_center_y,
		maximum_water_surface - route->max_radius_y - 2);
	if (minimum_x > maximum_x || first_y >= maximum_center_y) return false;
	maximum_fit = 1 + (maximum_center_y - first_y) /
		route->min_vertical_gap;
	chamber_count = rng_range(rng, route->min_chambers,
		MIN(route->max_chambers, maximum_fit));
	if (chamber_count < route->min_chambers || chamber_count > CHAMBER_MAX) {
		return false;
	}
	gap_maximum = chamber_count > 1 ?
		(maximum_center_y - first_y) / (chamber_count - 1) :
		route->max_vertical_gap;
	gap_maximum = MIN(gap_maximum, route->max_vertical_gap);
	/* Any mandatory shaft not covered by the rope contract must remain
	 * climbable on one full stamina pool.  When data requires rope for every
	 * shaft, retain the authored scale instead of collapsing the cave to the
	 * free-climbing limit. */
	if (traversal_profile->rope_shaft_count < chamber_count) {
		gap_maximum = MIN(gap_maximum,
			world_spelunk_generated_route_maximum_grip_span(generated));
	}
	if (gap_maximum < route->min_vertical_gap) return false;
	gap = rng_range(rng, route->min_vertical_gap, gap_maximum);

	for (i = 0; i < chamber_count; i++) {
		anchors[i].x = rng_range(rng, minimum_x, maximum_x);
		anchors[i].y = first_y + i * gap;
		anchors[i].radius_x = rng_range(rng, route->min_radius_x,
			route->max_radius_x);
		anchors[i].radius_y = rng_range(rng, route->min_radius_y,
			route->max_radius_y);
		carve_ellipse(generated, anchors[i].x, anchors[i].y,
			anchors[i].radius_x, anchors[i].radius_y);
	}

	generated->entrance_y = route->entrance_y;
	generated->entrance_x = rng_range(rng, minimum_x, maximum_x);

	branch_count = rng_range(rng, route->min_branches,
		route->max_branches);
	for (i = 0; i < branch_count; i++) {
		const struct chamber_anchor *source =
			&anchors[rng_range(rng, 0, chamber_count - 1)];
		int direction = rng_range(rng, 0, 1) ? 1 : -1;
		int branch_x = direction > 0 ? maximum_x : minimum_x;
		int branch_y = source->y + rng_range(rng,
			-(int)route->min_radius_y, (int)route->min_radius_y);
		int radius_x = rng_range(rng, route->min_radius_x,
			route->max_radius_x);
		int radius_y = rng_range(rng, route->min_radius_y,
			route->max_radius_y);

		branch_y = MAX(2 + radius_y,
			MIN(generated->height - 3 - radius_y, branch_y));
		carve_ellipse(generated, branch_x, branch_y, radius_x, radius_y);
		carve_horizontal(generated, source->x, branch_x, branch_y);
	}

	/* Lay the semantic traversal rail after irregular chambers and branches so
	 * later decorative carving cannot erase its support or grip walls. */
	previous_station = world_spelunk_generated_route_add_station(generated,
		generated->entrance_x, generated->entrance_y);
	if (previous_station != 0) return false;
	for (i = 0; i < chamber_count; i++) {
		int station = world_spelunk_generated_route_add_station(generated,
			anchors[i].x, anchors[i].y);

		if (station < 0 || !world_spelunk_generated_route_connect(generated,
				previous_station, station, WORLD_SPELUNK_ROUTE_ALTERNATE,
				i < traversal_profile->rope_shaft_count)) {
			return false;
		}
		previous_station = station;
	}

	generated->deepest_x = anchors[chamber_count - 1].x;
	generated->deepest_y = anchors[chamber_count - 1].y;
	world_spelunk_generated_route_seal_disconnected(generated);
	return true;
}

void world_spelunk_generated_layout_dispose(
		struct world_spelunk_generated_layout *generated)
{
	if (!generated) return;
	mem_free(generated->witness_steps);
	mem_free(generated->decoration_tags);
	mem_free(generated->material_tags);
	mem_free(generated->cells);
	memset(generated, 0, sizeof(*generated));
}

bool world_spelunk_generated_layout_is_structurally_valid(
		const struct world_spelunk_generated_layout *generated)
{
	uint8_t *visited;
	uint32_t *queue;
	size_t cell_count;
	size_t passable_count = 0;
	size_t visited_count = 0;
	size_t head = 0;
	size_t tail = 0;
	int x;
	int y;

	if (!generated || !world_id_is_valid(generated->recipe_id) ||
			!world_id_is_valid(generated->system_id) || !generated->cells ||
			generated->width < 3 ||
			generated->width > WORLD_SPELUNK_WIDTH_MAX ||
			generated->height < 3 ||
			generated->height > WORLD_SPELUNK_HEIGHT_MAX ||
			generated->entrance_x <= 0 ||
			generated->entrance_x >= generated->width - 1 ||
			generated->entrance_y <= 0 ||
			generated->entrance_y >= generated->height - 2 ||
			generated->deepest_x <= 0 ||
			generated->deepest_x >= generated->width - 1 ||
			generated->deepest_y <= generated->entrance_y ||
			generated->deepest_y >= generated->height - 1) {
		return false;
	}
	for (x = 0; x < generated->width; x++) {
		if (generated->cells[cell_index(generated, x, 0)] !=
				WORLD_SPELUNK_ROCK ||
				generated->cells[cell_index(generated, x,
					generated->height - 1)] != WORLD_SPELUNK_ROCK) {
			return false;
		}
	}
	for (y = 0; y < generated->height; y++) {
		if (generated->cells[cell_index(generated, 0, y)] !=
				WORLD_SPELUNK_ROCK ||
				generated->cells[cell_index(generated,
					generated->width - 1, y)] != WORLD_SPELUNK_ROCK) {
			return false;
		}
	}
	if (generated->cells[cell_index(generated, generated->entrance_x,
			generated->entrance_y)] != WORLD_SPELUNK_AIR ||
			generated->cells[cell_index(generated, generated->entrance_x,
				generated->entrance_y + 1)] != WORLD_SPELUNK_ROCK ||
			generated->cells[cell_index(generated, generated->deepest_x,
				generated->deepest_y)] != WORLD_SPELUNK_AIR) {
		return false;
	}

	cell_count = (size_t)generated->width * generated->height;
	visited = mem_zalloc(cell_count);
	queue = mem_alloc(cell_count * sizeof(*queue));
	for (y = 1; y < generated->height - 1; y++) {
		for (x = 1; x < generated->width - 1; x++) {
			if (generated->cells[cell_index(generated, x, y)] !=
					WORLD_SPELUNK_ROCK) {
				passable_count++;
			}
		}
	}
	queue[tail++] = (uint32_t)cell_index(generated,
		generated->entrance_x, generated->entrance_y);
	visited[queue[0]] = 1;
	while (head < tail) {
		uint32_t index = queue[head++];
		int current_x = (int)(index % generated->width);
		int current_y = (int)(index / generated->width);
		static const int dx[4] = { -1, 1, 0, 0 };
		static const int dy[4] = { 0, 0, -1, 1 };
		int direction;

		visited_count++;
		for (direction = 0; direction < 4; direction++) {
			int next_x = current_x + dx[direction];
			int next_y = current_y + dy[direction];
			size_t next = cell_index(generated, next_x, next_y);

			if (!visited[next] && generated->cells[next] !=
					WORLD_SPELUNK_ROCK) {
				visited[next] = 1;
				queue[tail++] = (uint32_t)next;
			}
		}
	}
	mem_free(queue);
	mem_free(visited);
	return passable_count >= 32 && visited_count == passable_count;
}

enum world_spelunk_generation_result world_spelunk_generate_layout(
		const struct world_spelunk_recipe_definition *recipe, uint32_t seed,
		unsigned int action_energy,
		struct world_spelunk_generated_layout *generated)
{
	const struct world_spelunk_water_profile *water_profile;
	const struct world_spelunk_geology_profile *geology_profile;
	const struct world_spelunk_traversal_profile *traversal_profile;
	const struct world_spelunk_population_profile *population_profile;
	unsigned int attempt;

	if (!recipe || !generated || !action_energy ||
			!world_id_is_valid(recipe->id) ||
			!world_id_is_valid(recipe->system_id)) {
		return WORLD_SPELUNK_GENERATION_INVALID_ARGUMENT;
	}
	if (recipe->generator_version != 1) {
		return WORLD_SPELUNK_GENERATION_UNSUPPORTED_VERSION;
	}
	water_profile = world_spelunk_water_profile_by_id(
		recipe->water_profile_id);
	if (!water_profile) {
		return WORLD_SPELUNK_GENERATION_UNRESOLVED_REFERENCE;
	}
	if (water_profile->version != 1) {
		return WORLD_SPELUNK_GENERATION_UNSUPPORTED_VERSION;
	}
	geology_profile = world_spelunk_geology_profile_by_id(
		recipe->material_profile_id);
	if (!geology_profile) {
		return WORLD_SPELUNK_GENERATION_UNRESOLVED_REFERENCE;
	}
	if (geology_profile->version != 1) {
		return WORLD_SPELUNK_GENERATION_UNSUPPORTED_VERSION;
	}
	traversal_profile = world_spelunk_traversal_profile_by_id(
		recipe->traversal_profile_id);
	if (!traversal_profile) {
		return WORLD_SPELUNK_GENERATION_UNRESOLVED_REFERENCE;
	}
	if (traversal_profile->version != 1) {
		return WORLD_SPELUNK_GENERATION_UNSUPPORTED_VERSION;
	}
	population_profile = world_spelunk_population_profile_by_id(
		recipe->population_profile_id);
	if (!population_profile) {
		return WORLD_SPELUNK_GENERATION_UNRESOLVED_REFERENCE;
	}
	if (population_profile->version != 1) {
		return WORLD_SPELUNK_GENERATION_UNSUPPORTED_VERSION;
	}
	memset(generated, 0, sizeof(*generated));
	for (attempt = 0; attempt < GENERATION_ATTEMPT_MAX; attempt++) {
		struct generation_rng rng;
		size_t cell_count;
		size_t i;
		bool carved = false;

		rng.state = mix32(seed ^ hash_text(recipe->id) ^
			((uint32_t)recipe->generator_version << 16) ^
			(attempt * 0x85ebca6bu));
		generated->width = rng_range(&rng, recipe->size.min_width,
			recipe->size.max_width);
		generated->height = rng_range(&rng, recipe->size.min_height,
			recipe->size.max_height);
		cell_count = (size_t)generated->width * generated->height;
		generated->cells = mem_alloc(cell_count * sizeof(*generated->cells));
		for (i = 0; i < cell_count; i++) {
			generated->cells[i] = WORLD_SPELUNK_ROCK;
		}
		my_strcpy(generated->recipe_id, recipe->id,
			sizeof(generated->recipe_id));
		my_strcpy(generated->system_id, recipe->system_id,
			sizeof(generated->system_id));
		my_strcpy(generated->water_profile_id, water_profile->id,
			sizeof(generated->water_profile_id));
		my_strcpy(generated->traversal_profile_id, traversal_profile->id,
			sizeof(generated->traversal_profile_id));
		my_strcpy(generated->population_profile_id, population_profile->id,
			sizeof(generated->population_profile_id));
		generated->seed = seed;
		generated->generator_version = recipe->generator_version;
		generated->initial_stamina = recipe->initial_stamina;
		generated->perception = recipe->perception;
		if (!world_spelunk_recipe_rules(recipe, action_energy,
				&generated->rules)) {
			world_spelunk_generated_layout_dispose(generated);
			return WORLD_SPELUNK_GENERATION_INVALID_ARGUMENT;
		}
		switch (recipe->macro_family) {
		case WORLD_SPELUNK_MACRO_CHAMBER_CHAIN:
				carved = generate_chamber_chain(recipe, water_profile,
					traversal_profile, &rng, generated);
				break;
		}
		if (carved &&
				world_spelunk_generated_layout_is_structurally_valid(generated) &&
				apply_hydrology(water_profile, traversal_profile, &rng,
					generated)) {
			/* The final supported access shaft may close decorative slivers in
			 * the last chamber just as the dry rail can.  Keep only space still
			 * connected to the entrance; disconnected water is subsequently
			 * rejected by the hydrology gate. */
			world_spelunk_generated_route_seal_disconnected(generated);
			if (place_landmarks(recipe, generated) &&
					world_spelunk_generated_layout_apply_geology(
						geology_profile, generated) &&
					world_spelunk_generated_layout_is_structurally_valid(
						generated) &&
					world_spelunk_generated_layout_is_hydrologically_valid(
						generated, water_profile) &&
					world_spelunk_generated_layout_has_valid_landmarks(
						generated, recipe) &&
					world_spelunk_generated_layout_has_valid_geology(generated,
						geology_profile) &&
					world_spelunk_generated_layout_build_witness(generated,
						traversal_profile, NULL)) {
				return WORLD_SPELUNK_GENERATION_OK;
			}
		}
		world_spelunk_generated_layout_dispose(generated);
	}
	return WORLD_SPELUNK_GENERATION_EXHAUSTED;
}
