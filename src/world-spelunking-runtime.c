/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-runtime.c
 * \brief Owned mutable state and invariants for a spelunking location.
 *
 *
 */

#include "world-spelunking-runtime.h"
#include "world-spelunking-local.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-data-util.h"
#include "world-spelunking-geology-data.h"
#include "world-spelunking-visibility.h"
#include "z-color.h"
#include "z-util.h"
#include "z-virt.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

static const struct world_spelunk_perception fallback_perception = {
	10, 6, 2, 16, 28, 5, 2, 24,
	L'.', COLOUR_SLATE, L'.', COLOUR_L_DARK
};

static bool persisted_values_fit(const struct world_spelunk_state *state)
{
	return state->rules.safe_fall_tiles <= UINT16_MAX &&
		state->rules.fall_base_damage <= UINT16_MAX &&
		state->rules.jump_stamina_cost <= UINT16_MAX &&
		state->rules.grip_up_stamina_cost <= UINT16_MAX &&
		state->rules.grip_lateral_stamina_cost <= UINT16_MAX &&
		state->rules.grip_down_stamina_cost <= UINT16_MAX &&
		state->rules.rest_stamina_gain <= UINT16_MAX &&
		state->rules.rope_max_length <= UINT16_MAX &&
		state->rules.rope_up_stamina_cost <= UINT16_MAX &&
		state->rules.rope_lateral_stamina_cost <= UINT16_MAX &&
		state->rules.rope_down_stamina_cost <= UINT16_MAX &&
		state->rules.breath_turns <= UINT16_MAX &&
		state->rules.drowning_damage <= UINT16_MAX &&
		state->rules.swim_turn_stamina_cost <= UINT16_MAX &&
		state->max_stamina <= UINT16_MAX && state->stamina <= UINT16_MAX &&
		state->breath <= UINT16_MAX;
}

static bool terrain_is_valid(const struct world_spelunk_map *map)
{
	int x;
	int y;

	for (y = 0; y < map->height; y++) {
		for (x = 0; x < map->width; x++) {
			enum world_spelunk_tile tile = map->cells[
				y * map->stride + x];

			if (tile != WORLD_SPELUNK_AIR &&
					tile != WORLD_SPELUNK_ROCK &&
					tile != WORLD_SPELUNK_WATER) {
				return false;
			}
		}
	}
	return true;
}

static bool actors_are_valid(const struct world_spelunk_runtime *runtime)
{
	size_t i;
	size_t j;

	if (runtime->actor_count > WORLD_SPELUNK_ACTOR_MAX ||
			(!runtime->actor_roster_initialized && runtime->actor_count != 0)) {
		return false;
	}
	for (i = 0; i < runtime->actor_count; i++) {
		const struct world_spelunk_actor *actor = &runtime->actors[i];
		const struct world_spelunk_actor_definition *definition =
			world_spelunk_actor_by_id(actor->id);

		if (!definition ||
				actor->x < 0 || actor->x >= runtime->state.map.width ||
				actor->y < 0 || actor->y >= runtime->state.map.height ||
				actor->hp <= 0 || actor->hp > actor->max_hp ||
				actor->max_hp != definition->hitpoints ||
				actor->energy > runtime->state.rules.action_energy ||
				runtime->cells[(size_t)actor->y *
					runtime->state.map.width + actor->x] ==
					WORLD_SPELUNK_ROCK) {
			return false;
		}
		for (j = i + 1; j < runtime->actor_count; j++) {
			if (runtime->actors[j].x == actor->x &&
					runtime->actors[j].y == actor->y) {
				return false;
			}
		}
	}
	return true;
}

static bool geology_has_cardinal_tile(
		const struct world_spelunk_runtime *runtime, int x, int y,
		enum world_spelunk_tile tile)
{
	static const int dx[4] = { -1, 1, 0, 0 };
	static const int dy[4] = { 0, 0, -1, 1 };
	int direction;

	for (direction = 0; direction < 4; direction++) {
		int other_x = x + dx[direction];
		int other_y = y + dy[direction];

		if (other_x >= 0 && other_y >= 0 &&
				other_x < runtime->state.map.width &&
				other_y < runtime->state.map.height &&
				runtime->cells[(size_t)other_y *
					runtime->state.map.width + other_x] == tile) {
			return true;
		}
	}
	return false;
}

static bool geology_decoration_fits(
		const struct world_spelunk_runtime *runtime,
		const struct world_spelunk_decoration_definition *decoration,
		int x, int y)
{
	size_t index = (size_t)y * runtime->state.map.width + x;

	if (x <= 0 || y <= 0 || x >= runtime->state.map.width - 1 ||
			y >= runtime->state.map.height - 1) {
		return false;
	}
	switch (decoration->placement) {
	case WORLD_SPELUNK_DECORATION_ROCK_FACE:
		return runtime->cells[index] == WORLD_SPELUNK_ROCK &&
			(geology_has_cardinal_tile(runtime, x, y, WORLD_SPELUNK_AIR) ||
			 geology_has_cardinal_tile(runtime, x, y, WORLD_SPELUNK_WATER));
	case WORLD_SPELUNK_DECORATION_CEILING:
		return runtime->cells[index] == WORLD_SPELUNK_AIR &&
			runtime->cells[index - runtime->state.map.width] ==
				WORLD_SPELUNK_ROCK;
	case WORLD_SPELUNK_DECORATION_FLOOR:
		return runtime->cells[index] == WORLD_SPELUNK_AIR &&
			runtime->cells[index + runtime->state.map.width] ==
				WORLD_SPELUNK_ROCK;
	case WORLD_SPELUNK_DECORATION_WATER_EDGE:
		return runtime->cells[index] == WORLD_SPELUNK_AIR &&
			runtime->cells[index + runtime->state.map.width] ==
				WORLD_SPELUNK_ROCK &&
			geology_has_cardinal_tile(runtime, x, y, WORLD_SPELUNK_WATER);
	}
	return false;
}

static bool geology_is_valid(const struct world_spelunk_runtime *runtime)
{
	const struct world_spelunk_geology_profile *profile;
	size_t count;
	size_t i;

	if (!runtime->geology_profile_id[0]) {
		return runtime->geology_version == 0 && !runtime->material_tags &&
			!runtime->decoration_tags;
	}
	if (!world_id_is_valid(runtime->geology_profile_id) ||
			!runtime->geology_version || !runtime->material_tags ||
			!runtime->decoration_tags) {
		return false;
	}
	profile = world_spelunk_geology_profile_by_id(
		runtime->geology_profile_id);
	if (!profile || profile->version != runtime->geology_version) return false;
	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	for (i = 0; i < count; i++) {
		uint8_t material_tag = runtime->material_tags[i];
		uint8_t decoration_tag = runtime->decoration_tags[i];
		int x = (int)(i % runtime->state.map.width);
		int y = (int)(i / runtime->state.map.width);

		if (runtime->cells[i] == WORLD_SPELUNK_ROCK) {
			if (!world_spelunk_geology_material_by_tag(profile,
					material_tag)) {
				return false;
			}
		} else if (material_tag != WORLD_SPELUNK_GEOLOGY_TAG_NONE) {
			return false;
		}
		if (decoration_tag != WORLD_SPELUNK_GEOLOGY_TAG_NONE) {
			const struct world_spelunk_decoration_definition *decoration =
				world_spelunk_geology_decoration_by_tag(profile,
					decoration_tag);
			size_t j;

			if (!decoration ||
					!geology_decoration_fits(runtime, decoration, x, y)) {
				return false;
			}
			for (j = i + 1; j < count; j++) {
				uint8_t other_tag = runtime->decoration_tags[j];
				const struct world_spelunk_decoration_definition *other;
				int other_x;
				int other_y;
				int required;

				if (other_tag == WORLD_SPELUNK_GEOLOGY_TAG_NONE) continue;
				other = world_spelunk_geology_decoration_by_tag(profile,
					other_tag);
				if (!other) return false;
				other_x = (int)(j % runtime->state.map.width);
				other_y = (int)(j / runtime->state.map.width);
				required = MAX(decoration->minimum_spacing,
					other->minimum_spacing);
				if (abs(other_x - x) + abs(other_y - y) < required) {
					return false;
				}
			}
		}
	}
	return true;
}

bool world_spelunk_runtime_is_valid(
		const struct world_spelunk_runtime *runtime)
{
	const struct world_spelunk_map *map;

	if (!runtime || !runtime->cells ||
			!world_id_is_valid(runtime->location_id)) {
		return false;
	}
	map = &runtime->state.map;
	if (map->cells != runtime->cells || map->pitons != runtime->pitons ||
			map->ropes != runtime->ropes || map->width <= 0 ||
			map->width > WORLD_SPELUNK_WIDTH_MAX || map->height <= 0 ||
			map->height > WORLD_SPELUNK_HEIGHT_MAX ||
			map->stride != map->width ||
			map->infrastructure_stride != map->width ||
			(size_t)map->width * (size_t)map->height >
				WORLD_SPELUNK_CELL_MAX) {
		return false;
	}
	return terrain_is_valid(map) &&
		world_spelunk_perception_is_valid(&runtime->perception) &&
		actors_are_valid(runtime) &&
		geology_is_valid(runtime) &&
		world_spelunk_runtime_local_is_valid(runtime) &&
		world_spelunk_visibility_is_valid(runtime) &&
		world_spelunk_state_is_valid(&runtime->state) &&
		persisted_values_fit(&runtime->state);
}

bool world_spelunk_runtime_set_geology(
		struct world_spelunk_runtime *runtime, const char *profile_id,
		uint16_t version, const uint8_t *material_tags,
		const uint8_t *decoration_tags)
{
	uint8_t *materials;
	uint8_t *decorations;
	size_t count;

	if (!runtime || runtime->geology_profile_id[0] ||
			!world_id_is_valid(profile_id) || !version || !material_tags ||
			!decoration_tags || !world_spelunk_runtime_is_valid(runtime)) {
		return false;
	}
	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	materials = mem_alloc(count);
	decorations = mem_alloc(count);
	memcpy(materials, material_tags, count);
	memcpy(decorations, decoration_tags, count);
	my_strcpy(runtime->geology_profile_id, profile_id,
		sizeof(runtime->geology_profile_id));
	runtime->geology_version = version;
	runtime->material_tags = materials;
	runtime->decoration_tags = decorations;
	if (!geology_is_valid(runtime)) {
		mem_free(runtime->decoration_tags);
		mem_free(runtime->material_tags);
		runtime->decoration_tags = NULL;
		runtime->material_tags = NULL;
		runtime->geology_version = 0;
		runtime->geology_profile_id[0] = '\0';
		return false;
	}
	return true;
}

struct world_spelunk_runtime *world_spelunk_runtime_create_with_perception(
		const char *location_id, const struct world_spelunk_state *source,
		const struct world_spelunk_perception *perception)
{
	struct world_spelunk_runtime *runtime;
	size_t count;
	int y;

	if (!location_id || !source || !perception ||
			!world_id_is_valid(location_id) ||
			!world_spelunk_perception_is_valid(perception) ||
			source->map.width <= 0 ||
			source->map.width > WORLD_SPELUNK_WIDTH_MAX ||
			source->map.height <= 0 ||
			source->map.height > WORLD_SPELUNK_HEIGHT_MAX ||
			source->map.stride < source->map.width ||
			!world_spelunk_state_is_valid(source) ||
			!persisted_values_fit(source) || !terrain_is_valid(&source->map)) {
		return NULL;
	}
	count = (size_t)source->map.width * (size_t)source->map.height;
	if (count > WORLD_SPELUNK_CELL_MAX) return NULL;

	runtime = mem_zalloc(sizeof(*runtime));
	runtime->cells = mem_alloc(count * sizeof(*runtime->cells));
	runtime->pitons = mem_zalloc(count * sizeof(*runtime->pitons));
	runtime->ropes = mem_zalloc(count * sizeof(*runtime->ropes));
	runtime->explored = mem_zalloc(count * sizeof(*runtime->explored));
	runtime->visible = mem_zalloc(count * sizeof(*runtime->visible));
	runtime->detected = mem_zalloc(count * sizeof(*runtime->detected));
	for (y = 0; y < source->map.height; y++) {
		memcpy(runtime->cells + y * source->map.width,
			source->map.cells + y * source->map.stride,
			(size_t)source->map.width * sizeof(*runtime->cells));
	}
	runtime->state = *source;
	runtime->state.map.cells = runtime->cells;
	runtime->state.map.stride = runtime->state.map.width;
	runtime->state.map.pitons = runtime->pitons;
	runtime->state.map.ropes = runtime->ropes;
	runtime->state.map.infrastructure_stride = runtime->state.map.width;
	runtime->perception = *perception;
	my_strcpy(runtime->location_id, location_id,
		sizeof(runtime->location_id));
	world_spelunk_visibility_follow_player(runtime);
	return runtime;
}

struct world_spelunk_runtime *world_spelunk_runtime_create(
		const char *location_id, const struct world_spelunk_state *source)
{
	return world_spelunk_runtime_create_with_perception(location_id, source,
		&fallback_perception);
}

bool world_spelunk_runtime_set_perception(
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_perception *perception)
{
	if (!runtime || !perception ||
			!world_spelunk_perception_is_valid(perception) ||
			!world_spelunk_runtime_is_valid(runtime)) {
		return false;
	}
	runtime->perception = *perception;
	world_spelunk_visibility_follow_player(runtime);
	return true;
}

const struct world_spelunk_actor *world_spelunk_runtime_actor_at(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	size_t i;

	if (!runtime) return NULL;
	for (i = 0; i < runtime->actor_count; i++) {
		if (runtime->actors[i].x == x && runtime->actors[i].y == y) {
			return &runtime->actors[i];
		}
	}
	return NULL;
}

struct world_spelunk_actor *world_spelunk_runtime_actor_at_mutable(
		struct world_spelunk_runtime *runtime, int x, int y)
{
	size_t i;

	if (!runtime) return NULL;
	for (i = 0; i < runtime->actor_count; i++) {
		if (runtime->actors[i].x == x && runtime->actors[i].y == y) {
			return &runtime->actors[i];
		}
	}
	return NULL;
}

bool world_spelunk_runtime_add_actor(struct world_spelunk_runtime *runtime,
		const char *actor_id, int x, int y)
{
	struct world_spelunk_actor *actor;
	const struct world_spelunk_actor_definition *definition =
		world_spelunk_actor_by_id(actor_id);

	if (!runtime || !runtime->actor_roster_initialized ||
			runtime->actor_count >= WORLD_SPELUNK_ACTOR_MAX ||
			!definition || x < 0 || y < 0 ||
			x >= runtime->state.map.width || y >= runtime->state.map.height ||
			runtime->cells[(size_t)y * runtime->state.map.width + x] ==
				WORLD_SPELUNK_ROCK ||
			(x == runtime->state.x && y == runtime->state.y) ||
			world_spelunk_runtime_actor_at(runtime, x, y)) {
		return false;
	}
	actor = &runtime->actors[runtime->actor_count++];
	my_strcpy(actor->id, definition->id, sizeof(actor->id));
	actor->x = x;
	actor->y = y;
	actor->hp = definition->hitpoints;
	actor->max_hp = definition->hitpoints;
	actor->energy = 0;
	return true;
}

bool world_spelunk_runtime_remove_actor(struct world_spelunk_runtime *runtime,
		struct world_spelunk_actor *actor)
{
	size_t index;

	if (!runtime || !actor || actor < runtime->actors ||
			actor >= runtime->actors + runtime->actor_count) {
		return false;
	}
	index = (size_t)(actor - runtime->actors);
	if (index + 1 < runtime->actor_count) {
		memmove(&runtime->actors[index], &runtime->actors[index + 1],
			(runtime->actor_count - index - 1) * sizeof(runtime->actors[0]));
	}
	runtime->actor_count--;
	memset(&runtime->actors[runtime->actor_count], 0,
		sizeof(runtime->actors[0]));
	return true;
}

void world_spelunk_runtime_free(struct world_spelunk_runtime *runtime)
{
	if (!runtime) return;
	world_spelunk_runtime_local_free(runtime);
	mem_free(runtime->decoration_tags);
	mem_free(runtime->material_tags);
	mem_free(runtime->detected);
	mem_free(runtime->visible);
	mem_free(runtime->explored);
	mem_free(runtime->ropes);
	mem_free(runtime->pitons);
	mem_free(runtime->cells);
	mem_free(runtime);
}
