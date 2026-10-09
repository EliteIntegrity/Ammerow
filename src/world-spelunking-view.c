/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-view.c
 * \brief Immutable presentation snapshots for side-view spelunking.
 *
 *
 */

#include "world-spelunking-view.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-passage.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-visibility.h"
#include "object.h"
#include "obj-tval.h"

#include <string.h>

static uint32_t appearance_hash(int x, int y)
{
	uint32_t value = (uint32_t)x * UINT32_C(0x9e3779b1) ^
		(uint32_t)y * UINT32_C(0x85ebca6b) ^ UINT32_C(0xc2b2ae35);

	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	return value;
}

static enum world_spelunk_view_cell visible_terrain_cell(
		const struct world_spelunk_runtime *runtime,
		enum world_spelunk_tile tile, int x, int y)
{
	const struct world_spelunk_state *state = &runtime->state;
	uint32_t hash = appearance_hash(x, y);
	size_t index = (size_t)y * state->map.stride + x;

	if (runtime->geology_profile_id[0]) {
		if (runtime->decoration_tags[index] !=
				WORLD_SPELUNK_GEOLOGY_TAG_NONE) {
			return WORLD_SPELUNK_VIEW_DECORATION;
		}
		if (tile == WORLD_SPELUNK_ROCK) {
			return WORLD_SPELUNK_VIEW_MATERIAL;
		}
	}

	if (tile == WORLD_SPELUNK_ROCK) {
		switch (hash % 47U) {
		case 0:
		case 1:
			return WORLD_SPELUNK_VIEW_ROCK_CALCITE;
		case 2:
		case 3:
			return WORLD_SPELUNK_VIEW_ROCK_IRON;
		case 4:
			return WORLD_SPELUNK_VIEW_ROCK_COPPER;
		default:
			return WORLD_SPELUNK_VIEW_ROCK;
		}
	}
	if (tile == WORLD_SPELUNK_WATER) return WORLD_SPELUNK_VIEW_WATER;
	if (y > 0 && state->map.cells[(y - 1) * state->map.stride + x] ==
			WORLD_SPELUNK_ROCK && hash % 11U == 0) {
		return WORLD_SPELUNK_VIEW_STALACTITE;
	}
	if (y + 1 < state->map.height &&
			state->map.cells[(y + 1) * state->map.stride + x] ==
			WORLD_SPELUNK_ROCK && (hash >> 8) % 13U == 0) {
		return WORLD_SPELUNK_VIEW_STALAGMITE;
	}
	return WORLD_SPELUNK_VIEW_AIR;
}

enum world_spelunk_view_cell world_spelunk_view_base_cell_at(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	if (!runtime || !world_spelunk_visibility_is_terrain_visible(runtime, x, y)) {
		return WORLD_SPELUNK_VIEW_UNKNOWN;
	}
	return visible_terrain_cell(runtime,
		runtime->state.map.cells[y * runtime->state.map.stride + x], x, y);
}

static int centered_start(int focus, int count, int limit)
{
	int start;

	if (count >= limit) return 0;
	start = focus - count / 2;
	if (start < 0) return 0;
	if (start > limit - count) return limit - count;
	return start;
}

bool world_spelunk_view_capture_at_system(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int focus_x, int focus_y,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view)
{
	const struct world_spelunk_state *state;
	int cols;
	int rows;
	int source_x;
	int source_y;
	size_t needed;
	int col;
	int row;

	if (view) memset(view, 0, sizeof(*view));
	if (!runtime || !storage || !view || requested_cols <= 0 ||
			requested_rows <= 0 || focus_x < 0 || focus_y < 0 ||
			!world_spelunk_runtime_is_valid(runtime)) {
		return false;
	}
	state = &runtime->state;
	if (focus_x >= state->map.width || focus_y >= state->map.height) {
		return false;
	}
	cols = requested_cols < state->map.width ? requested_cols :
		state->map.width;
	rows = requested_rows < state->map.height ? requested_rows :
		state->map.height;
	needed = (size_t)cols * (size_t)rows;
	if (cols <= 0 || rows <= 0 || needed > storage_capacity) return false;

	source_x = centered_start(focus_x, cols, state->map.width);
	source_y = centered_start(focus_y, rows, state->map.height);
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			int map_x = source_x + col;
			int map_y = source_y + row;
			enum world_spelunk_tile tile =
				state->map.cells[map_y * state->map.stride + map_x];
			bool explored = world_spelunk_visibility_is_explored(runtime,
				map_x, map_y);
			bool terrain_visible =
				world_spelunk_visibility_is_terrain_visible(runtime,
				map_x, map_y);
			enum world_spelunk_view_cell cell;

			if (!explored) {
				cell = WORLD_SPELUNK_VIEW_UNKNOWN;
			} else if (!terrain_visible) {
				cell = tile == WORLD_SPELUNK_ROCK ?
					WORLD_SPELUNK_VIEW_REMEMBERED_ROCK :
					(tile == WORLD_SPELUNK_WATER ?
					WORLD_SPELUNK_VIEW_REMEMBERED_WATER :
					WORLD_SPELUNK_VIEW_REMEMBERED_AIR);
			} else {
				cell = visible_terrain_cell(runtime, tile, map_x, map_y);
			}
			storage[(size_t)row * cols + col] = (uint8_t)cell;
		}
	}
	/* Stable exits come from the authored route graph and remain visible once
	 * explored.  They are overlaid once per snapshot, before occupants. */
	{
		struct loc exit_grid;
		size_t exit_index = 0;

		while (world_spelunking_exit_grid_at(system, runtime, exit_index++,
				&exit_grid)) {
			int local_col = exit_grid.x - source_x;
			int local_row = exit_grid.y - source_y;

			if (local_col >= 0 && local_col < cols && local_row >= 0 &&
					local_row < rows &&
					world_spelunk_visibility_is_explored(runtime,
						exit_grid.x, exit_grid.y)) {
				storage[(size_t)local_row * cols + local_col] =
					(uint8_t)WORLD_SPELUNK_VIEW_EXIT;
			}
		}
	}
	/* Internal endpoints are persistent section geometry. Like an external
	 * portal, their semantic direction remains known after exploration. */
	{
		struct loc passage_grid;
		enum world_spelunk_passage_direction direction;
		size_t passage_index = 0;

		while (world_spelunk_system_passage_grid_at(system, runtime,
				passage_index++, &passage_grid, &direction)) {
			int local_col = passage_grid.x - source_x;
			int local_row = passage_grid.y - source_y;

			if (local_col >= 0 && local_col < cols && local_row >= 0 &&
					local_row < rows &&
					world_spelunk_visibility_is_explored(runtime,
						passage_grid.x, passage_grid.y)) {
				storage[(size_t)local_row * cols + local_col] = (uint8_t)
					(direction == WORLD_SPELUNK_PASSAGE_UP ?
					 WORLD_SPELUNK_VIEW_PASSAGE_UP :
					 WORLD_SPELUNK_VIEW_PASSAGE_DOWN);
			}
		}
	}
	for (row = 0; row < rows; row++) {
		for (col = 0; col < cols; col++) {
			int map_x = source_x + col;
			int map_y = source_y + row;
			bool explored = world_spelunk_visibility_is_explored(runtime,
				map_x, map_y);
			bool visible = world_spelunk_visibility_is_visible(runtime,
				map_x, map_y);
			bool detected = world_spelunk_visibility_is_detected(runtime,
				map_x, map_y);
			enum world_spelunk_view_cell cell =
				(enum world_spelunk_view_cell)storage[(size_t)row * cols + col];
			struct object *object = (visible || detected) ?
				world_spelunk_runtime_ground_object_at(runtime, map_x,
					map_y) : NULL;
			const struct world_spelunk_actor *actor = (visible || detected) ?
				world_spelunk_runtime_actor_at(runtime, map_x, map_y) : NULL;

			/* Player-built infrastructure is stable knowledge once its cell has
			 * been explored, unlike transient objects and actors. */
			if (explored && world_spelunk_runtime_has_rope(runtime, map_x, map_y)) {
				cell = WORLD_SPELUNK_VIEW_ROPE;
			}
			if (explored && world_spelunk_runtime_has_piton(runtime, map_x, map_y)) {
				cell = WORLD_SPELUNK_VIEW_PITON;
			}
			/* One subject owns a tile.  Loose objects cover infrastructure,
			 * creatures cover objects, and the player remains final authority. */
			if (object) {
				cell = WORLD_SPELUNK_VIEW_OBJECT;
			}
			if (actor) {
				cell = WORLD_SPELUNK_VIEW_ACTOR;
			}
			if (visible && world_spelunk_has_active_grip(state) &&
					map_x == state->grip_target_x &&
					map_y == state->grip_target_y) {
				cell = WORLD_SPELUNK_VIEW_GRIP;
			}
			if (visible && map_x == state->x && map_y == state->y) {
				cell = WORLD_SPELUNK_VIEW_PLAYER;
			}
			if (runtime->peeking && visible &&
					(map_x != state->x || map_y != state->y) &&
					map_x == runtime->peek_x && map_y == runtime->peek_y) {
				cell = WORLD_SPELUNK_VIEW_PEEK;
			}
			if (cell == WORLD_SPELUNK_VIEW_ACTOR) {
				const struct world_spelunk_actor_definition *definition =
					world_spelunk_actor_by_id(actor->id);
				struct world_spelunk_view_actor_appearance *appearance;

				if (!definition || view->actor_appearance_count >=
						WORLD_SPELUNK_ACTOR_MAX) {
					memset(view, 0, sizeof(*view));
					return false;
				}
				appearance = &view->actor_appearances[
					view->actor_appearance_count++];
				appearance->col = col;
				appearance->row = row;
				appearance->glyph = definition->glyph;
				appearance->attr = definition->attr;
				appearance->actor_id = visible ? definition->id : NULL;
			} else if (cell == WORLD_SPELUNK_VIEW_OBJECT) {
				struct world_spelunk_view_object_appearance *appearance;

				if (!object || !object->kind ||
						view->object_appearance_count >=
						WORLD_SPELUNK_OBJECT_MAX) {
					memset(view, 0, sizeof(*view));
					return false;
				}
				appearance = &view->object_appearances[
					view->object_appearance_count++];
				appearance->col = col;
				appearance->row = row;
				/* Definition colours keep this snapshot independent of terminal
				 * preference arrays; unaware items use their public flavour. */
				appearance->glyph = object->kind->flavor && !object->kind->aware ?
					object->kind->flavor->d_char : object->kind->d_char;
				appearance->attr = object->kind->flavor && !object->kind->aware ?
					object->kind->flavor->d_attr : object->kind->d_attr;
				if (visible) {
					const struct object_kind *kind = object->kind;
					appearance->art_id = kind->flavor && !kind->aware ?
						(kind->base ? kind->base->unaware_art_id : NULL) : kind->art_id;
					appearance->fallback_id = tval_find_name(kind->tval);
				}
			}
			storage[(size_t)row * cols + col] = (uint8_t)cell;
		}
	}

	view->cells = storage;
	view->cell_count = needed;
	view->cols = cols;
	view->rows = rows;
	view->source_x = source_x;
	view->source_y = source_y;
	view->map_width = state->map.width;
	view->map_height = state->map.height;
	view->player_col = state->x - source_x;
	view->player_row = state->y - source_y;
	view->peek_col = runtime->peek_x - source_x;
	view->peek_row = runtime->peek_y - source_y;
	view->peeking = runtime->peeking;
	view->stamina = state->stamina;
	view->max_stamina = state->max_stamina;
	view->breath = state->breath;
	view->max_breath = state->rules.breath_turns;
	view->movement = state->movement;
	view->visible_air_glyph = runtime->perception.visible_air_glyph;
	view->visible_air_attr = runtime->perception.visible_air_attr;
	view->remembered_air_glyph = runtime->perception.remembered_air_glyph;
	view->remembered_air_attr = runtime->perception.remembered_air_attr;
	view->remembered_brightness_percent =
		runtime->perception.remembered_brightness_percent;
	if (runtime->geology_profile_id[0]) {
		view->material_tags = runtime->material_tags;
		view->decoration_tags = runtime->decoration_tags;
		view->geology_stride = runtime->state.map.width;
		view->geology_version = runtime->geology_version;
		my_strcpy(view->geology_profile_id, runtime->geology_profile_id,
			sizeof(view->geology_profile_id));
	}
	memcpy(view->location_id, runtime->location_id,
		strlen(runtime->location_id) + 1);
	return true;
}

bool world_spelunk_view_capture(
		const struct world_spelunk_runtime *runtime,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view)
{
	int focus_x;
	int focus_y;

	if (!runtime) {
		if (view) memset(view, 0, sizeof(*view));
		return false;
	}
	focus_x = runtime->peeking ? runtime->peek_x : runtime->state.x;
	focus_y = runtime->peeking ? runtime->peek_y : runtime->state.y;
	return world_spelunk_view_capture_at_system(NULL, runtime, focus_x, focus_y,
		requested_cols, requested_rows, storage, storage_capacity, view);
}

bool world_spelunk_view_capture_system(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view)
{
	int focus_x;
	int focus_y;

	if (!runtime) {
		if (view) memset(view, 0, sizeof(*view));
		return false;
	}
	focus_x = runtime->peeking ? runtime->peek_x : runtime->state.x;
	focus_y = runtime->peeking ? runtime->peek_y : runtime->state.y;
	return world_spelunk_view_capture_at_system(system, runtime, focus_x,
		focus_y, requested_cols, requested_rows, storage, storage_capacity,
		view);
}

bool world_spelunk_view_capture_at(
		const struct world_spelunk_runtime *runtime, int focus_x, int focus_y,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view)
{
	return world_spelunk_view_capture_at_system(NULL, runtime, focus_x,
		focus_y, requested_cols, requested_rows, storage, storage_capacity,
		view);
}

bool world_spelunk_view_inspect_system(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int x, int y,
		struct world_spelunk_inspect *inspect)
{
	const struct world_spelunk_state *state;
	bool explored;
	bool visible;
	bool terrain_visible;
	bool detected;
	struct loc exit_grid;
	size_t exit_index = 0;
	struct loc passage_grid;
	enum world_spelunk_passage_direction passage_direction;
	size_t passage_index = 0;

	if (inspect) memset(inspect, 0, sizeof(*inspect));
	if (!runtime || !inspect || !world_spelunk_runtime_is_valid(runtime)) {
		return false;
	}
	state = &runtime->state;
	inspect->x = x;
	inspect->y = y;
	if (x < 0 || y < 0 || x >= state->map.width || y >= state->map.height) {
		inspect->kind = WORLD_SPELUNK_INSPECT_VOID;
		return true;
	}
	explored = world_spelunk_visibility_is_explored(runtime, x, y);
	visible = world_spelunk_visibility_is_visible(runtime, x, y);
	terrain_visible = world_spelunk_visibility_is_terrain_visible(runtime,
		x, y);
	detected = world_spelunk_visibility_is_detected(runtime, x, y);
	inspect->knowledge = terrain_visible ? WORLD_SPELUNK_INSPECT_VISIBLE :
		(explored ? WORLD_SPELUNK_INSPECT_REMEMBERED :
		WORLD_SPELUNK_INSPECT_UNSEEN);
	if (!explored) {
		inspect->kind = WORLD_SPELUNK_INSPECT_UNKNOWN;
		return true;
	}
	switch (state->map.cells[y * state->map.stride + x]) {
	case WORLD_SPELUNK_ROCK:
		inspect->kind = WORLD_SPELUNK_INSPECT_ROCK;
		break;
	case WORLD_SPELUNK_WATER:
		inspect->kind = WORLD_SPELUNK_INSPECT_WATER;
		break;
	case WORLD_SPELUNK_AIR:
	default:
		inspect->kind = WORLD_SPELUNK_INSPECT_AIR;
		break;
	}
	if (runtime->geology_profile_id[0]) {
		const struct world_spelunk_geology_profile *profile =
			world_spelunk_geology_profile_by_id(
				runtime->geology_profile_id);
		size_t index = (size_t)y * state->map.stride + x;

		if (!profile || profile->version != runtime->geology_version) {
			return false;
		}
		if (runtime->material_tags[index] !=
				WORLD_SPELUNK_GEOLOGY_TAG_NONE) {
			inspect->material = world_spelunk_geology_material_by_tag(
				profile, runtime->material_tags[index]);
			if (!inspect->material) return false;
		}
		if (runtime->decoration_tags[index] !=
				WORLD_SPELUNK_GEOLOGY_TAG_NONE) {
			inspect->decoration = world_spelunk_geology_decoration_by_tag(
				profile, runtime->decoration_tags[index]);
			if (!inspect->decoration) return false;
		}
	}
	/* The legacy deterministic formations are presentation-derived rather
	 * than geology tags.  Preserve that lightweight appearance for old layouts,
	 * but expose the same semantic identity to Look instead of reporting the
	 * visible formation as empty air. */
	if (visible && inspect->kind == WORLD_SPELUNK_INSPECT_AIR &&
			!inspect->decoration) {
		enum world_spelunk_view_cell terrain = visible_terrain_cell(runtime,
			WORLD_SPELUNK_AIR, x, y);

		if (terrain == WORLD_SPELUNK_VIEW_STALACTITE) {
			inspect->kind = WORLD_SPELUNK_INSPECT_STALACTITE;
		} else if (terrain == WORLD_SPELUNK_VIEW_STALAGMITE) {
			inspect->kind = WORLD_SPELUNK_INSPECT_STALAGMITE;
		}
	}
	while (world_spelunking_exit_grid_at(system, runtime, exit_index++,
			&exit_grid)) {
		if (exit_grid.x == x && exit_grid.y == y) {
			inspect->kind = WORLD_SPELUNK_INSPECT_EXIT;
			break;
		}
	}
	while (world_spelunk_system_passage_grid_at(system, runtime,
			passage_index++, &passage_grid, &passage_direction)) {
		if (passage_grid.x == x && passage_grid.y == y) {
			inspect->kind = passage_direction == WORLD_SPELUNK_PASSAGE_UP ?
				WORLD_SPELUNK_INSPECT_PASSAGE_UP :
				WORLD_SPELUNK_INSPECT_PASSAGE_DOWN;
			break;
		}
	}
	if (world_spelunk_runtime_has_rope(runtime, x, y)) {
		inspect->kind = WORLD_SPELUNK_INSPECT_ROPE;
	}
	if (world_spelunk_runtime_has_piton(runtime, x, y)) {
		inspect->kind = WORLD_SPELUNK_INSPECT_PITON;
	}
	if (visible || detected) {
		inspect->object = world_spelunk_runtime_ground_object_at(runtime, x, y);
		if (inspect->object) {
			inspect->kind = WORLD_SPELUNK_INSPECT_OBJECT;
			if (!visible) {
				inspect->knowledge = WORLD_SPELUNK_INSPECT_DETECTED;
			}
		}
		inspect->actor = world_spelunk_runtime_actor_at(runtime, x, y);
		if (inspect->actor) {
			inspect->actor_definition =
				world_spelunk_actor_by_id(inspect->actor->id);
			if (!inspect->actor_definition) return false;
			inspect->object = NULL;
			inspect->kind = WORLD_SPELUNK_INSPECT_ACTOR;
			if (!visible) {
				inspect->knowledge = WORLD_SPELUNK_INSPECT_DETECTED;
			}
		}
	}
	if (visible && world_spelunk_has_active_grip(state) &&
			state->grip_target_x == x && state->grip_target_y == y) {
		inspect->object = NULL;
		inspect->actor = NULL;
		inspect->actor_definition = NULL;
		inspect->kind = WORLD_SPELUNK_INSPECT_GRIP;
	}
	if (visible && state->x == x && state->y == y) {
		inspect->object = NULL;
		inspect->actor = NULL;
		inspect->actor_definition = NULL;
		inspect->kind = WORLD_SPELUNK_INSPECT_PLAYER;
	}
	return true;
}

bool world_spelunk_view_inspect(
		const struct world_spelunk_runtime *runtime, int x, int y,
		struct world_spelunk_inspect *inspect)
{
	return world_spelunk_view_inspect_system(NULL, runtime, x, y, inspect);
}

enum world_spelunk_view_cell world_spelunk_view_cell_at(
		const struct world_spelunk_view *view, int col, int row)
{
	size_t index;

	if (!view || !view->cells || col < 0 || row < 0 || col >= view->cols ||
			row >= view->rows) {
		return WORLD_SPELUNK_VIEW_VOID;
	}
	index = (size_t)row * view->cols + col;
	if (index >= view->cell_count ||
			view->cells[index] > WORLD_SPELUNK_VIEW_PEEK) {
		return WORLD_SPELUNK_VIEW_VOID;
	}
	return (enum world_spelunk_view_cell)view->cells[index];
}

bool world_spelunk_view_actor_appearance_at(
		const struct world_spelunk_view *view, int col, int row,
		int *attr, wchar_t *glyph)
{
	size_t i;

	if (!view || !attr || !glyph ||
			world_spelunk_view_cell_at(view, col, row) !=
			WORLD_SPELUNK_VIEW_ACTOR) {
		return false;
	}
	for (i = 0; i < view->actor_appearance_count &&
			i < WORLD_SPELUNK_ACTOR_MAX; i++) {
		const struct world_spelunk_view_actor_appearance *appearance =
			&view->actor_appearances[i];

		if (appearance->col == col && appearance->row == row) {
			*attr = appearance->attr;
			*glyph = appearance->glyph;
			return true;
		}
	}
	return false;
}

bool world_spelunk_view_object_appearance_at(
		const struct world_spelunk_view *view, int col, int row,
		int *attr, wchar_t *glyph)
{
	size_t i;

	if (!view || !attr || !glyph ||
			world_spelunk_view_cell_at(view, col, row) !=
			WORLD_SPELUNK_VIEW_OBJECT) {
		return false;
	}
	for (i = 0; i < view->object_appearance_count &&
			i < WORLD_SPELUNK_OBJECT_MAX; i++) {
		const struct world_spelunk_view_object_appearance *appearance =
			&view->object_appearances[i];

		if (appearance->col == col && appearance->row == row) {
			*attr = appearance->attr;
			*glyph = appearance->glyph;
			return true;
		}
	}
	return false;
}
