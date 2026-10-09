/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-validation.c
 * \brief Independent hydrology and landmark gates for cave candidates.
 */

#include "world-spelunking-generation.h"

#include "z-util.h"
#include "z-virt.h"

#include <string.h>

static size_t cell_index(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	return (size_t)y * generated->width + x;
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

static bool has_supported_shore(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_water_profile *profile)
{
	int direction;

	for (direction = -1; direction <= 1; direction += 2) {
		int x = direction < 0 ? generated->water_left_x - 1 :
			generated->water_right_x + 1;
		int i;

		for (i = 0; i < profile->min_shore_tiles; i++, x += direction) {
			if (!tile_is_supported_air(generated, x,
					generated->water_surface_y)) {
				break;
			}
		}
		if (i == profile->min_shore_tiles) return true;
	}
	return false;
}

bool world_spelunk_generated_layout_is_hydrologically_valid(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_water_profile *profile)
{
	uint8_t *visited;
	uint32_t *queue;
	size_t cell_count;
	size_t water_count = 0;
	size_t visited_count = 0;
	size_t head = 0;
	size_t tail = 0;
	int water_width;
	int water_depth;
	int surface_percent;
	int x;
	int y;

	if (!generated || !profile || !profile->id || !generated->cells ||
			generated->width < 3 || generated->height < 3 ||
			!streq(generated->water_profile_id, profile->id) ||
			profile->family != WORLD_SPELUNK_WATER_LOWER_BASIN ||
			profile->version != 1 || generated->water_left_x <= 0 ||
			generated->water_right_x < generated->water_left_x ||
			generated->water_right_x >= generated->width - 1 ||
			generated->water_surface_y <= 0 ||
			generated->water_bed_y <= generated->water_surface_y ||
			generated->water_bed_y >= generated->height - 1) {
		return false;
	}
	water_width = generated->water_right_x -
		generated->water_left_x + 1;
	water_depth = generated->water_bed_y - generated->water_surface_y;
	surface_percent = depth_percent(generated,
		generated->water_surface_y);
	if (water_width * 100 < generated->width * profile->min_width_percent ||
			water_width * 100 >
				generated->width * profile->max_width_percent ||
			water_depth < profile->min_depth_tiles ||
			water_depth > profile->max_depth_tiles ||
			surface_percent < profile->min_surface_depth_percent ||
			surface_percent > profile->max_surface_depth_percent ||
			!has_supported_shore(generated, profile)) {
		return false;
	}
	for (x = generated->water_left_x;
			x <= generated->water_right_x; x++) {
		for (y = generated->water_surface_y;
				y < generated->water_bed_y; y++) {
			if (generated->cells[cell_index(generated, x, y)] !=
					WORLD_SPELUNK_WATER) {
				return false;
			}
		}
		if (generated->cells[cell_index(generated, x,
				generated->water_bed_y)] != WORLD_SPELUNK_ROCK) {
			return false;
		}
	}

	cell_count = (size_t)generated->width * generated->height;
	visited = mem_zalloc(cell_count);
	queue = mem_alloc(cell_count * sizeof(*queue));
	for (y = 0; y < generated->height; y++) {
		for (x = 0; x < generated->width; x++) {
			size_t index = cell_index(generated, x, y);

			if (generated->cells[index] != WORLD_SPELUNK_WATER) continue;
			if (x == 0 || x == generated->width - 1 || y == 0 ||
					y == generated->height - 1) {
				mem_free(queue);
				mem_free(visited);
				return false;
			}
			water_count++;
			/* Static water may rest on water or rock, never air. */
			if (generated->cells[cell_index(generated, x, y + 1)] ==
					WORLD_SPELUNK_AIR) {
				mem_free(queue);
				mem_free(visited);
				return false;
			}
		}
	}
	if (!water_count) {
		mem_free(queue);
		mem_free(visited);
		return false;
	}
	if (water_count != (size_t)water_width * water_depth) {
		mem_free(queue);
		mem_free(visited);
		return false;
	}
	queue[tail++] = (uint32_t)cell_index(generated,
		generated->water_left_x, generated->water_surface_y);
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
			size_t next;

			if (next_x < 0 || next_x >= generated->width || next_y < 0 ||
					next_y >= generated->height) {
				continue;
			}
			next = cell_index(generated, next_x, next_y);

			if (!visited[next] && generated->cells[next] ==
					WORLD_SPELUNK_WATER) {
				visited[next] = 1;
				queue[tail++] = (uint32_t)next;
			}
		}
	}
	mem_free(queue);
	mem_free(visited);
	return visited_count == water_count;
}

static const struct world_spelunk_generated_landmark *placed_landmark_by_id(
		const struct world_spelunk_generated_layout *generated, const char *id)
{
	int i;

	for (i = 0; i < generated->landmark_count; i++) {
		if (streq(generated->landmarks[i].id, id)) {
			return &generated->landmarks[i];
		}
	}
	return NULL;
}

bool world_spelunk_generated_layout_has_valid_landmarks(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_recipe_definition *recipe)
{
	struct world_spelunk_map map;
	int i;

	if (!generated || !recipe || !generated->cells ||
			generated->landmark_count > WORLD_SPELUNK_LANDMARK_MAX ||
			generated->landmark_count != recipe->landmark_count ||
			!streq(generated->recipe_id, recipe->id)) {
		return false;
	}
	memset(&map, 0, sizeof(map));
	map.width = generated->width;
	map.height = generated->height;
	map.stride = generated->width;
	map.cells = generated->cells;
	for (i = 0; i < recipe->landmark_count; i++) {
		const struct world_spelunk_landmark_contract *contract =
			&recipe->landmarks[i];
		const struct world_spelunk_generated_landmark *placed =
			placed_landmark_by_id(generated, contract->id);
		struct world_spelunk_state state;
		int j;

		if (!placed || placed->kind != contract->kind ||
				depth_percent(generated, placed->y) <
					contract->min_depth_percent ||
				depth_percent(generated, placed->y) >
					contract->max_depth_percent ||
				!tile_is_supported_air(generated, placed->x, placed->y) ||
				!world_spelunk_state_init(&state, &map, &generated->rules,
					placed->x, placed->y, generated->initial_stamina) ||
				state.movement != WORLD_SPELUNK_STANDING) {
			return false;
		}
		if (!streq(placed->object_tval, contract->object_tval) ||
				!streq(placed->object_sval, contract->object_sval)) {
			return false;
		}
		if (contract->kind == WORLD_SPELUNK_LANDMARK_FISHING_STANCE) {
			bool adjacent_water = false;
			static const int dx[4] = { -1, 1, 0, 0 };
			static const int dy[4] = { 0, 0, -1, 1 };

			for (j = 0; j < 4; j++) {
				if (generated->cells[cell_index(generated,
						placed->x + dx[j], placed->y + dy[j])] ==
						WORLD_SPELUNK_WATER) {
					adjacent_water = true;
				}
			}
			if (!adjacent_water) return false;
		}
		for (j = i + 1; j < recipe->landmark_count; j++) {
			const struct world_spelunk_generated_landmark *other =
				placed_landmark_by_id(generated,
					recipe->landmarks[j].id);

			if (other && other->x == placed->x && other->y == placed->y) {
				return false;
			}
		}
	}
	return true;
}
