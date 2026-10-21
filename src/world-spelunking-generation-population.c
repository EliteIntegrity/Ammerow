/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-population.c
 * \brief Private-seed population placement for generated cave sections.
 */

#include "world-spelunking-generation-population.h"
#include "world-spelunking-door.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-visibility.h"
#include "world-spelunking-traversal-proof.h"

#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "z-util.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define GENERATED_POPULATION_OBJECT_MAX \
	(WORLD_SPELUNK_POPULATION_OBJECT_ENTRY_MAX * \
		WORLD_SPELUNK_POPULATION_COUNT_MAX)

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

static int authored_count(uint32_t seed, const char *identity,
		uint16_t minimum, uint16_t maximum)
{
	uint32_t span = (uint32_t)maximum - minimum + 1u;

	return minimum + (int)(mix32(seed ^ hash_text(identity)) % span);
}

static int depth_percent(
		const struct world_spelunk_generated_layout *generated, int y)
{
	return y * 100 / (generated->height - 1);
}

static bool within_radius(int x, int y, int other_x, int other_y,
		uint16_t radius)
{
	return abs(x - other_x) + abs(y - other_y) <= radius;
}

static bool excluded_by_contract(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_population_profile *profile, int x, int y)
{
	int i;

	if (within_radius(x, y, generated->entrance_x, generated->entrance_y,
			profile->entrance_exclusion_radius)) {
		return true;
	}
	for (i = 0; i < generated->landmark_count; i++) {
		if (within_radius(x, y, generated->landmarks[i].x,
				generated->landmarks[i].y,
				profile->landmark_exclusion_radius)) {
			return true;
		}
	}
	for (i = 0; i < generated->route_visit_count; i++) {
		if (within_radius(x, y, generated->route_visits[i].x,
				generated->route_visits[i].y,
				profile->endpoint_exclusion_radius)) {
			return true;
		}
	}
	return false;
}

static bool is_legal_population_cell(
		const struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_population_profile *profile,
		int x, int y, uint16_t minimum_depth, uint16_t maximum_depth)
{
	int depth;

	if (x <= 0 || x >= generated->width - 1 || y <= 0 ||
			y >= generated->height - 1 ||
			runtime->cells[(size_t)y * generated->width + x] !=
				WORLD_SPELUNK_AIR ||
			runtime->cells[(size_t)(y + 1) * generated->width + x] !=
				WORLD_SPELUNK_ROCK ||
			(x == runtime->state.x && y == runtime->state.y) ||
			world_spelunk_runtime_actor_at(runtime, x, y) ||
			world_spelunk_runtime_ground_object_at(runtime, x, y) ||
			excluded_by_contract(generated, profile, x, y)) {
		return false;
	}
	depth = depth_percent(generated, y);
	return depth >= minimum_depth && depth <= maximum_depth;
}

static bool select_population_cell(
		const struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_population_profile *profile,
		uint16_t minimum_depth, uint16_t maximum_depth, uint32_t salt,
		int *selected_x, int *selected_y)
{
	uint32_t best_score = UINT32_MAX;
	bool found = false;
	int x;
	int y;

	for (y = 1; y < generated->height - 1; y++) {
		for (x = 1; x < generated->width - 1; x++) {
			uint32_t index;
			uint32_t score;

			if (!is_legal_population_cell(runtime, generated, profile, x, y,
					minimum_depth, maximum_depth)) {
				continue;
			}
			index = (uint32_t)y * (uint32_t)generated->width + (uint32_t)x;
			score = mix32(salt ^ index * 0x9e3779b9u);
			if (!found || score < best_score) {
				found = true;
				best_score = score;
				*selected_x = x;
				*selected_y = y;
			}
		}
	}
	return found;
}

static void delete_detached_object(struct object **obj)
{
	struct object *known;

	if (!obj || !*obj) return;
	known = (*obj)->known;
	(*obj)->known = NULL;
	if (known) object_delete(NULL, NULL, &known);
	object_delete(NULL, NULL, obj);
}

static struct object *make_population_object(
		const struct world_spelunk_population_object_entry *entry)
{
	int tval = tval_find_idx(entry->object_tval);
	int sval = tval > 0 ? lookup_sval(tval, entry->object_sval) : -1;
	struct object_kind *kind = sval >= 0 ? lookup_kind(tval, sval) : NULL;
	struct object *obj;

	if (!kind) return NULL;
	obj = object_new();
	object_prep(obj, kind, 0, AVERAGE);
	obj->origin = ORIGIN_FLOOR;
	obj->known = object_new();
	object_copy(obj->known, obj);
	obj->known->known = NULL;
	return obj;
}

static bool place_actors(struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_population_profile *profile, uint32_t seed)
{
	int entry_index;

	for (entry_index = 0; entry_index < profile->actor_entry_count;
			entry_index++) {
		const struct world_spelunk_population_actor_entry *entry =
			&profile->actors[entry_index];
		int count = authored_count(seed, entry->actor_id,
			entry->minimum_count, entry->maximum_count);
		int ordinal;

		for (ordinal = 0; ordinal < count; ordinal++) {
			uint32_t salt = mix32(seed ^ hash_text(entry->actor_id) ^
				(uint32_t)ordinal * 0x85ebca6bu);
			int x = -1;
			int y = -1;

			if (!select_population_cell(runtime, generated, profile,
					entry->minimum_depth_percent,
					entry->maximum_depth_percent, salt, &x, &y) ||
					!world_spelunk_runtime_add_actor(runtime, entry->actor_id,
						x, y)) {
				return false;
			}
		}
	}
	return true;
}

static bool place_objects(struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_population_profile *profile, uint32_t seed,
		struct object **added, int *added_count)
{
	int entry_index;

	for (entry_index = 0; entry_index < profile->object_entry_count;
			entry_index++) {
		const struct world_spelunk_population_object_entry *entry =
			&profile->objects[entry_index];
		uint32_t identity = hash_text(entry->object_tval) ^
			hash_text(entry->object_sval);
		int count = authored_count(seed, entry->object_sval,
			entry->minimum_count, entry->maximum_count);
		int ordinal;

		for (ordinal = 0; ordinal < count; ordinal++) {
			uint32_t salt = mix32(seed ^ identity ^
				(uint32_t)ordinal * 0xc2b2ae35u);
			struct object *obj;
			int x = -1;
			int y = -1;

			if (*added_count >= GENERATED_POPULATION_OBJECT_MAX ||
					!select_population_cell(runtime, generated, profile,
						entry->minimum_depth_percent,
						entry->maximum_depth_percent, salt, &x, &y)) {
				return false;
			}
			obj = make_population_object(entry);
			if (!obj || !world_spelunk_runtime_add_ground_object(runtime,
					obj, x, y)) {
				if (obj) delete_detached_object(&obj);
				return false;
			}
			added[(*added_count)++] = obj;
		}
	}
	return true;
}

static bool near_certified_route(const struct world_spelunk_generated_layout *g,
	int x, int y)
{
	size_t i;
	for (i = 0; i < g->witness_step_count; i++) {
		const struct world_spelunk_witness_step *step = &g->witness_steps[i];
		if (abs(x-step->expected_x) <= 1 && abs(y-step->expected_y) <= 1) return true;
	}
	return false;
}

static bool place_door(struct world_spelunk_runtime *r,
	const struct world_spelunk_generated_layout *g,
	const struct world_spelunk_population_profile *profile, uint32_t seed)
{
	int x, y, best_x = -1, best_y = -1;
	uint32_t best = UINT32_MAX;
	if (!profile->door_id) return true;
	for (y = 1; y < g->height - 1; y++) {
		for (x = 1; x < g->width - 1; x++) {
			uint32_t score;
			if (!world_spelunk_door_site(r, x, y) ||
				near_certified_route(g, x, y) ||
				!is_legal_population_cell(r, g, profile, x, y, 18, 85) ||
				r->decoration_tags[y*g->width+x]) continue;
			score = mix32(seed ^ 0x646f6f72u ^ (uint32_t)(y*g->width+x));
			if (score < best) {
				best = score;
				best_x = x;
				best_y = y;
			}
		}
	}
	/* Missing a suitable throat is normal; never carve terrain to force it. */
	if (best_x < 0) return true;
	if (!world_spelunk_door_add(r, profile->door_id, best_x, best_y)) return false;
	/* Initial sight must be calculated with fixtures in place. The earlier
	 * constructor's empty-geometry view must not reveal behind a closed door. */
	memset(r->explored, 0, (size_t)g->width * g->height);
	world_spelunk_visibility_follow_player(r);
	return true;
}

static bool place_fixture(struct world_spelunk_runtime *r,
	const struct world_spelunk_generated_layout *g,
	const struct world_spelunk_population_profile *profile, const char *id, uint32_t salt)
{
	int x, y, best_x = -1, best_y = -1;
	uint32_t best = UINT32_MAX;
	if (!id) return true;
	for (y = 1; y < g->height - 1; y++) {
		for (x = 1; x < g->width - 1; x++) {
			uint32_t score;
			if (near_certified_route(g, x, y) ||
				!is_legal_population_cell(r, g, profile, x, y, 24, 85) ||
				world_spelunk_door_at(r, x, y) || world_spelunk_fixture_at(r, x, y) ||
				r->pitons[y*g->width+x] || r->ropes[y*g->width+x] ||
				r->decoration_tags[y*g->width+x]) continue;
			score = mix32(salt ^ (uint32_t)(y*g->width+x));
			if (score < best) { best = score; best_x = x; best_y = y; }
		}
	}
	return best_x < 0 || world_spelunk_fixture_add(r, id, best_x, best_y);
}

bool world_spelunk_populate_generated_runtime(
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_population_profile *profile)
{
	struct object *added[GENERATED_POPULATION_OBJECT_MAX] = { NULL };
	size_t original_actor_count;
	int added_count = 0;
	uint32_t seed;
	bool populated;

	if (!runtime || !generated || !profile || profile->version != 1 ||
			runtime->actor_roster_initialized || runtime->actor_count ||
			generated->width != runtime->state.map.width ||
			generated->height != runtime->state.map.height ||
			!streq(generated->population_profile_id, profile->id)) {
		return false;
	}
	original_actor_count = runtime->actor_count;
	runtime->actor_roster_initialized = true;
	seed = mix32(generated->seed ^ hash_text(profile->id) ^ 0x706f7075u);
	populated = place_actors(runtime, generated, profile, seed) &&
		place_objects(runtime, generated, profile, seed, added, &added_count) &&
		place_door(runtime, generated, profile, seed) &&
		place_fixture(runtime, generated, profile, profile->trap_id, seed ^ 0x74726170u) &&
		place_fixture(runtime, generated, profile, profile->chest_id, seed ^ 0x63686573u) &&
		world_spelunk_runtime_is_valid(runtime);
	if (!populated) {
		runtime->fixture_count = 0;
		runtime->door_count = 0;
		world_spelunk_doors_rebuild(runtime);
		while (added_count > 0) {
			struct object *obj = added[--added_count];

			(void)world_spelunk_runtime_take_ground_object(runtime, obj);
			delete_detached_object(&obj);
		}
		runtime->actor_count = original_actor_count;
		memset(runtime->actors + original_actor_count, 0,
			(WORLD_SPELUNK_ACTOR_MAX - original_actor_count) *
				sizeof(runtime->actors[0]));
		runtime->actor_roster_initialized = false;
		return false;
	}
	return true;
}
