/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-local.c
 * \brief Spelunking-local infrastructure and ground-object ownership.
 *
 *
 */

#include "world-spelunking-local.h"

#include "obj-pile.h"
#include "object.h"

static bool runtime_in_bounds(const struct world_spelunk_runtime *runtime,
		int x, int y)
{
	return runtime && x >= 0 && y >= 0 && x < runtime->state.map.width &&
		y < runtime->state.map.height;
}

static size_t runtime_index(const struct world_spelunk_runtime *runtime,
		int x, int y)
{
	return (size_t)y * (size_t)runtime->state.map.width + (size_t)x;
}

static bool infrastructure_is_valid(
		const struct world_spelunk_runtime *runtime)
{
	size_t count = (size_t)runtime->state.map.width *
		(size_t)runtime->state.map.height;
	size_t i;

	if (!runtime->pitons || !runtime->ropes) return false;
	for (i = 0; i < count; i++) {
		if (runtime->pitons[i] > 1 || runtime->ropes[i] > 1 ||
				(runtime->pitons[i] && runtime->ropes[i]) ||
				((runtime->pitons[i] || runtime->ropes[i]) &&
				runtime->cells[i] != WORLD_SPELUNK_AIR)) {
			return false;
		}
	}
	return true;
}

static bool ground_objects_are_valid(
		const struct world_spelunk_runtime *runtime)
{
	const struct object *previous = NULL;
	const struct object *obj;
	size_t count = 0;

	for (obj = runtime->ground_objects; obj; obj = obj->next) {
		const struct object *known = obj->known;

		if (++count > WORLD_SPELUNK_OBJECT_MAX || obj->prev != previous ||
				!obj->kind || !known || !known->kind ||
				known->kind != obj->kind || obj->number == 0 ||
				known->number != obj->number || obj->oidx || known->oidx ||
				obj->held_m_idx || known->held_m_idx || obj->mimicking_m_idx ||
				known->mimicking_m_idx || known->known || known->prev ||
				known->next || !runtime_in_bounds(runtime, obj->grid.x,
					obj->grid.y) || !loc_eq(obj->grid, known->grid) ||
				runtime->cells[runtime_index(runtime, obj->grid.x,
					obj->grid.y)] != WORLD_SPELUNK_AIR) {
			return false;
		}
		previous = obj;
	}
	return true;
}

bool world_spelunk_runtime_local_is_valid(
		const struct world_spelunk_runtime *runtime)
{
	return runtime && infrastructure_is_valid(runtime) &&
		ground_objects_are_valid(runtime);
}

void world_spelunk_runtime_local_free(struct world_spelunk_runtime *runtime)
{
	struct object *obj;

	if (!runtime) return;
	obj = runtime->ground_objects;
	while (obj) {
		struct object *next = obj->next;
		struct object *known = obj->known;

		obj->prev = NULL;
		obj->next = NULL;
		obj->known = NULL;
		if (known) object_delete(NULL, NULL, &known);
		object_delete(NULL, NULL, &obj);
		obj = next;
	}
	runtime->ground_objects = NULL;
}

bool world_spelunk_runtime_has_piton(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return runtime_in_bounds(runtime, x, y) && runtime->pitons &&
		runtime->pitons[runtime_index(runtime, x, y)] != 0;
}

bool world_spelunk_runtime_has_rope(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return runtime_in_bounds(runtime, x, y) && runtime->ropes &&
		runtime->ropes[runtime_index(runtime, x, y)] != 0;
}

static enum world_spelunk_infrastructure_result piton_placement_result(
		const struct world_spelunk_runtime *runtime)
{
	static const int dx[4] = { -1, 1, 0, 0 };
	static const int dy[4] = { 0, 0, -1, 1 };
	const struct world_spelunk_state *state;
	int i;

	if (!world_spelunk_runtime_is_valid(runtime)) {
		return WORLD_SPELUNK_INFRASTRUCTURE_INVALID;
	}
	state = &runtime->state;
	if (state->movement == WORLD_SPELUNK_FALLING ||
			state->movement == WORLD_SPELUNK_JUMP_APEX) {
		return WORLD_SPELUNK_INFRASTRUCTURE_UNSTABLE;
	}
	if (world_spelunk_runtime_has_piton(runtime, state->x, state->y) ||
			world_spelunk_runtime_has_rope(runtime, state->x, state->y)) {
		return WORLD_SPELUNK_INFRASTRUCTURE_ALREADY_PRESENT;
	}
	for (i = 0; i < 4; i++) {
		int x = state->x + dx[i];
		int y = state->y + dy[i];

		if (runtime_in_bounds(runtime, x, y) &&
				runtime->cells[runtime_index(runtime, x, y)] ==
					WORLD_SPELUNK_ROCK) {
			return WORLD_SPELUNK_INFRASTRUCTURE_OK;
		}
	}
	return WORLD_SPELUNK_INFRASTRUCTURE_NO_BRACE;
}

enum world_spelunk_infrastructure_result world_spelunk_runtime_place_piton(
		struct world_spelunk_runtime *runtime)
{
	enum world_spelunk_infrastructure_result result =
		piton_placement_result(runtime);

	if (result == WORLD_SPELUNK_INFRASTRUCTURE_OK) {
		size_t index = runtime_index(runtime, runtime->state.x,
			runtime->state.y);
		struct world_spelunk_state before = runtime->state;

		runtime->pitons[index] = 1;
		runtime->state.movement = WORLD_SPELUNK_STANDING;
		runtime->state.fall_start_y = runtime->state.y;
		runtime->state.jump_holding = false;
		if (runtime->state.grip_target_x == runtime->state.x &&
				runtime->state.grip_target_y == runtime->state.y) {
			runtime->state.grip_target_x = -1;
			runtime->state.grip_target_y = -1;
		}
		if (!world_spelunk_runtime_is_valid(runtime)) {
			runtime->pitons[index] = 0;
			runtime->state = before;
			return WORLD_SPELUNK_INFRASTRUCTURE_INVALID;
		}
	}
	return result;
}

static enum world_spelunk_infrastructure_result rope_deployment_result(
		const struct world_spelunk_runtime *runtime, int available_segments,
		int *segments)
{
	const struct world_spelunk_state *state;
	int count = 0;
	int y;

	if (segments) *segments = 0;
	if (!world_spelunk_runtime_is_valid(runtime) || available_segments <= 0) {
		return WORLD_SPELUNK_INFRASTRUCTURE_INVALID;
	}
	state = &runtime->state;
	if (state->movement == WORLD_SPELUNK_FALLING ||
			state->movement == WORLD_SPELUNK_JUMP_APEX) {
		return WORLD_SPELUNK_INFRASTRUCTURE_UNSTABLE;
	}
	if (!world_spelunk_runtime_has_piton(runtime, state->x, state->y)) {
		return WORLD_SPELUNK_INFRASTRUCTURE_NO_ANCHOR;
	}
	for (y = state->y + 1; y < state->map.height &&
			count < state->rules.rope_max_length &&
			count < available_segments; y++) {
		size_t index = runtime_index(runtime, state->x, y);

		if (runtime->cells[index] == WORLD_SPELUNK_ROCK ||
				runtime->ropes[index] || runtime->pitons[index]) {
			break;
		}
		count++;
	}
	if (!count) return WORLD_SPELUNK_INFRASTRUCTURE_NO_SHAFT;
	if (segments) *segments = count;
	return WORLD_SPELUNK_INFRASTRUCTURE_OK;
}

enum world_spelunk_infrastructure_result world_spelunk_runtime_deploy_rope(
		struct world_spelunk_runtime *runtime, int available_segments,
		int *segments)
{
	int count = 0;
	int i;
	enum world_spelunk_infrastructure_result result =
		rope_deployment_result(runtime, available_segments, &count);

	if (result == WORLD_SPELUNK_INFRASTRUCTURE_OK) {
		for (i = 1; i <= count; i++) {
			runtime->ropes[runtime_index(runtime, runtime->state.x,
				runtime->state.y + i)] = 1;
		}
		if (!world_spelunk_runtime_is_valid(runtime)) {
			for (i = 1; i <= count; i++) {
				runtime->ropes[runtime_index(runtime, runtime->state.x,
					runtime->state.y + i)] = 0;
			}
			result = WORLD_SPELUNK_INFRASTRUCTURE_INVALID;
		}
	}
	if (segments) *segments = result == WORLD_SPELUNK_INFRASTRUCTURE_OK ?
		count : 0;
	return result;
}

size_t world_spelunk_runtime_ground_object_count(
		const struct world_spelunk_runtime *runtime)
{
	const struct object *obj;
	size_t count = 0;

	if (!runtime) return 0;
	for (obj = runtime->ground_objects; obj; obj = obj->next) count++;
	return count;
}

struct object *world_spelunk_runtime_ground_object_at(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	struct object *obj;

	if (!runtime_in_bounds(runtime, x, y)) return NULL;
	for (obj = runtime->ground_objects; obj; obj = obj->next) {
		if (obj->grid.x == x && obj->grid.y == y) return obj;
	}
	return NULL;
}

bool world_spelunk_runtime_can_add_ground_object_at(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return world_spelunk_runtime_is_valid(runtime) &&
		world_spelunk_runtime_ground_object_count(runtime) <
			WORLD_SPELUNK_OBJECT_MAX &&
		runtime_in_bounds(runtime, x, y) &&
		runtime->cells[runtime_index(runtime, x, y)] == WORLD_SPELUNK_AIR;
}

bool world_spelunk_runtime_add_ground_object(
		struct world_spelunk_runtime *runtime, struct object *obj, int x, int y)
{
	if (!world_spelunk_runtime_can_add_ground_object_at(runtime, x, y) ||
			!obj || !obj->known ||
			obj->prev || obj->next || obj->oidx || obj->known->oidx ||
			obj->held_m_idx || obj->mimicking_m_idx || obj->known->prev ||
			obj->known->next || obj->known->known) {
		return false;
	}
	obj->grid = loc(x, y);
	obj->known->grid = obj->grid;
	pile_insert_end(&runtime->ground_objects, obj);
	if (!ground_objects_are_valid(runtime)) {
		pile_excise(&runtime->ground_objects, obj);
		obj->grid = loc(0, 0);
		obj->known->grid = loc(0, 0);
		return false;
	}
	return true;
}

bool world_spelunk_runtime_take_ground_object(
		struct world_spelunk_runtime *runtime, struct object *obj)
{
	if (!world_spelunk_runtime_is_valid(runtime) || !obj ||
			!pile_contains(runtime->ground_objects, obj)) {
		return false;
	}
	pile_excise(&runtime->ground_objects, obj);
	obj->grid = loc(0, 0);
	obj->known->grid = loc(0, 0);
	return true;
}
