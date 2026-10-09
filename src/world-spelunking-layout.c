/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-layout.c
 * \brief Runtime creation and revisioned migration for authored spelunking.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "world-entry.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-layout.h"
#include "world-spelunking-layout-data.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-visibility.h"
#include "z-util.h"
#include "z-virt.h"

#include <stddef.h>
#include <string.h>

static void delete_detached_object(struct object **obj)
{
	struct object *known;

	if (!obj || !*obj) return;
	known = (*obj)->known;
	(*obj)->known = NULL;
	if (known) object_delete(NULL, NULL, &known);
	object_delete(NULL, NULL, obj);
}

static struct object *make_authored_object(
		const struct world_spelunk_layout_object *placement)
{
	struct object *obj;

	if (!placement || !placement->kind) return NULL;
	obj = object_new();
	object_prep(obj, placement->kind, 0, AVERAGE);
	obj->origin = ORIGIN_FLOOR;
	obj->known = object_new();
	object_copy(obj->known, obj);
	obj->known->known = NULL;
	return obj;
}

/** Add each object exactly once, as part of its append-only layout revision. */
static bool initialize_authored_objects(
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_layout_definition *definition,
		uint16_t from_revision, bool include_all)
{
	struct object *added[WORLD_SPELUNK_LAYOUT_OBJECT_MAX] = { NULL };
	int added_count = 0;
	int i;

	for (i = 0; i < definition->object_count; i++) {
		const struct world_spelunk_layout_object *placement =
			&definition->objects[i];
		struct object *obj;

		if (!include_all && placement->revision <= from_revision) continue;
		obj = make_authored_object(placement);
		if (!obj || !world_spelunk_runtime_add_ground_object(runtime, obj,
				placement->x, placement->y)) {
			int j;

			if (obj) delete_detached_object(&obj);
			for (j = added_count - 1; j >= 0; j--) {
				struct object *rollback = added[j];

				(void)world_spelunk_runtime_take_ground_object(runtime,
					rollback);
				delete_detached_object(&rollback);
			}
			return false;
		}
		added[added_count++] = obj;
	}
	return true;
}

/** Place each authored spawn once without respawning defeated creatures. */
static bool initialize_authored_actors(struct world_spelunk_runtime *runtime)
{
	size_t original_count;
	int i;

	if (runtime->actor_roster_initialized) return true;
	original_count = runtime->actor_count;
	runtime->actor_roster_initialized = true;
	for (i = 0; i < world_spelunk_spawn_definition_count(); i++) {
		const struct world_spelunk_spawn_definition *spawn =
			world_spelunk_spawn_by_index(i);
		bool placed = false;
		int j;

		if (!spawn || !spawn->actor ||
				!streq(spawn->location_id, runtime->location_id)) {
			continue;
		}
		for (j = 0; j < spawn->candidate_count; j++) {
			int x = spawn->candidates[j].x;
			int y = spawn->candidates[j].y;

			if (x < 0 || x >= runtime->state.map.width || y < 0 ||
					y + 1 >= runtime->state.map.height ||
					runtime->cells[(size_t)(y + 1) *
						runtime->state.map.width + x] != WORLD_SPELUNK_ROCK) {
				continue;
			}
			if (world_spelunk_runtime_add_actor(runtime, spawn->actor->id,
					x, y)) {
				placed = true;
				break;
			}
		}
		if (placed) continue;
		runtime->actor_count = original_count;
		memset(runtime->actors + original_count, 0,
			(WORLD_SPELUNK_ACTOR_MAX - original_count) *
				sizeof(runtime->actors[0]));
		runtime->actor_roster_initialized = false;
		return false;
	}
	return true;
}

enum world_spelunk_layout_result world_spelunk_layout_create(
		const struct level *level, const char *entry_id,
		unsigned int action_energy,
		struct world_spelunk_runtime **runtime)
{
	const struct world_spelunk_layout_definition *definition;
	const struct world_entry *entry;
	enum world_spelunk_tile *cells;
	struct world_spelunk_map map = { 0 };
	struct world_spelunk_rules rules;
	struct world_spelunk_state state;
	size_t count;

	if (!runtime) return WORLD_SPELUNK_LAYOUT_INVALID_ARGUMENT;
	*runtime = NULL;
	if (!level || !entry_id || !action_energy || !level->id ||
			level->mode != WORLD_MODE_SPELUNKING) {
		return WORLD_SPELUNK_LAYOUT_INVALID_ARGUMENT;
	}
	definition = world_spelunk_layout_definition_by_id(level->id);
	if (!definition) return WORLD_SPELUNK_LAYOUT_UNSUPPORTED_LOCATION;
	if (level->width != definition->width ||
			level->height != definition->height) {
		return WORLD_SPELUNK_LAYOUT_INVALID_METADATA;
	}
	entry = world_entry_by_id(level, entry_id);
	if (!entry || entry->kind != WORLD_ENTRY_GRID || entry->grid.x < 0 ||
			entry->grid.y < 0 || entry->grid.x >= level->width ||
			entry->grid.y >= level->height - 1) {
		return WORLD_SPELUNK_LAYOUT_INVALID_ENTRY;
	}

	count = (size_t)level->width * level->height;
	cells = mem_alloc(count * sizeof(*cells));
	if (!world_spelunk_layout_build_cells(definition, cells, level->width,
			level->height)) {
		mem_free(cells);
		return WORLD_SPELUNK_LAYOUT_INVALID_METADATA;
	}
	if (cells[(size_t)entry->grid.y * level->width + entry->grid.x] !=
			WORLD_SPELUNK_AIR ||
			cells[(size_t)(entry->grid.y + 1) * level->width +
				entry->grid.x] != WORLD_SPELUNK_ROCK) {
		mem_free(cells);
		return WORLD_SPELUNK_LAYOUT_INVALID_ENTRY;
	}

	map.cells = cells;
	map.width = level->width;
	map.height = level->height;
	map.stride = level->width;
	if (!world_spelunk_layout_rules(definition, action_energy, &rules) ||
			!world_spelunk_state_init(&state, &map, &rules, entry->grid.x,
				entry->grid.y, definition->initial_stamina)) {
		mem_free(cells);
		return WORLD_SPELUNK_LAYOUT_INVALID_STATE;
	}
	*runtime = world_spelunk_runtime_create_with_perception(level->id, &state,
		&definition->perception);
	mem_free(cells);
	if (*runtime) (*runtime)->layout_revision = definition->current_revision;
	if (!*runtime || !initialize_authored_objects(*runtime, definition, 0,
			true) || !world_spelunk_layout_upgrade_runtime(*runtime)) {
		world_spelunk_runtime_free(*runtime);
		*runtime = NULL;
		return WORLD_SPELUNK_LAYOUT_INVALID_STATE;
	}
	return WORLD_SPELUNK_LAYOUT_OK;
}

bool world_spelunk_layout_upgrade_runtime(
		struct world_spelunk_runtime *runtime)
{
	const struct world_spelunk_layout_definition *definition;
	struct world_spelunk_state original_state;
	struct world_spelunk_actor original_actors[WORLD_SPELUNK_ACTOR_MAX];
	enum world_spelunk_tile *original_cells = NULL;
	size_t original_actor_count;
	bool original_roster_initialized;
	bool preserve_grip = false;
	bool preserve_support = false;
	uint16_t original_revision;
	size_t count;
	int grip_x = -1;
	int grip_y = -1;

	if (!runtime || !world_spelunk_runtime_is_valid(runtime)) return false;
	definition = world_spelunk_layout_definition_by_id(runtime->location_id);
	if (!definition) return true;
	if (runtime->state.map.width != definition->width ||
			runtime->state.map.height != definition->height ||
			runtime->layout_revision > definition->current_revision) {
		return false;
	}
	original_revision = runtime->layout_revision;
	original_state = runtime->state;
	original_actor_count = runtime->actor_count;
	original_roster_initialized = runtime->actor_roster_initialized;
	memcpy(original_actors, runtime->actors, sizeof(original_actors));
	if (!world_spelunk_layout_rules(definition,
			runtime->state.rules.action_energy, &runtime->state.rules)) {
		return false;
	}

	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	if (runtime->layout_revision < definition->current_revision) {
		original_cells = mem_alloc(count * sizeof(*original_cells));
		memcpy(original_cells, runtime->cells,
			count * sizeof(*original_cells));
		grip_x = runtime->state.grip_target_x;
		grip_y = runtime->state.grip_target_y;
		if (grip_x >= 0 && grip_y >= 0) {
			preserve_grip = runtime->cells[(size_t)grip_y *
				runtime->state.map.width + grip_x] == WORLD_SPELUNK_ROCK;
		}
		if (runtime->state.y + 1 < runtime->state.map.height) {
			preserve_support = runtime->cells[(size_t)(runtime->state.y + 1) *
				runtime->state.map.width + runtime->state.x] ==
				WORLD_SPELUNK_ROCK;
		}
		if (!world_spelunk_layout_migrate_cells(definition, runtime->cells,
				runtime->state.map.width, runtime->state.map.height,
				runtime->layout_revision)) {
			memcpy(runtime->cells, original_cells,
				count * sizeof(*original_cells));
			mem_free(original_cells);
			runtime->state = original_state;
			return false;
		}
		/* A dormant save may retain a selected hold or supported posture at a
		 * newly carved boundary.  Those dynamic anchors outrank authored carving. */
		if (preserve_grip) {
			runtime->cells[(size_t)grip_y * runtime->state.map.width + grip_x] =
				WORLD_SPELUNK_ROCK;
		}
		if (preserve_support) {
			runtime->cells[(size_t)(runtime->state.y + 1) *
				runtime->state.map.width + runtime->state.x] =
				WORLD_SPELUNK_ROCK;
		}
		runtime->layout_revision = definition->current_revision;
	}

	if (runtime->cells[(size_t)runtime->state.y *
			runtime->state.map.width + runtime->state.x] ==
			WORLD_SPELUNK_WATER) {
		runtime->state.movement = WORLD_SPELUNK_SWIMMING;
		runtime->state.fall_start_y = runtime->state.y;
		runtime->state.grip_target_x = -1;
		runtime->state.grip_target_y = -1;
		runtime->state.jump_holding = false;
	}
	if (!initialize_authored_actors(runtime) ||
			!world_spelunk_runtime_is_valid(runtime)) {
		if (original_cells) {
			memcpy(runtime->cells, original_cells,
				count * sizeof(*original_cells));
		}
		runtime->layout_revision = original_revision;
		runtime->state = original_state;
		runtime->actor_count = original_actor_count;
		runtime->actor_roster_initialized = original_roster_initialized;
		memcpy(runtime->actors, original_actors, sizeof(original_actors));
		mem_free(original_cells);
		return false;
	}
	if (!initialize_authored_objects(runtime, definition, original_revision,
			false)) {
		if (original_cells) {
			memcpy(runtime->cells, original_cells,
				count * sizeof(*original_cells));
		}
		runtime->layout_revision = original_revision;
		runtime->state = original_state;
		runtime->actor_count = original_actor_count;
		runtime->actor_roster_initialized = original_roster_initialized;
		memcpy(runtime->actors, original_actors, sizeof(original_actors));
		mem_free(original_cells);
		return false;
	}
	mem_free(original_cells);
	world_spelunk_visibility_follow_player(runtime);
	return true;
}
