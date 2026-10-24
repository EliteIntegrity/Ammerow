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
	int x, int y, int radius)
{
	size_t i;
	for (i = 0; i < g->witness_step_count; i++) {
		const struct world_spelunk_witness_step *step = &g->witness_steps[i];
		if (abs(x-step->expected_x) <= radius && abs(y-step->expected_y) <= radius) return true;
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
				near_certified_route(g, x, y, 1) ||
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
			if (near_certified_route(g, x, y, 1) ||
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

/* Pockets use existing dry footing, never new carving. */
static bool discovery_walk_cell(const struct world_spelunk_runtime *r, int x, int y)
{
	int w = r->state.map.width;
	return x > 0 && x < w - 1 && y > 0 && y < r->state.map.height - 1 &&
		r->cells[y*w+x] == WORLD_SPELUNK_AIR &&
		r->cells[(y+1)*w+x] == WORLD_SPELUNK_ROCK &&
		!world_spelunk_door_at(r, x, y) && !world_spelunk_fixture_at(r, x, y) &&
		!world_spelunk_runtime_actor_at(r, x, y);
}

/* Prove each shelf step with the same grip/move rules used in play. Requiring
 * this in BOTH directions avoids a tempting one-way drop masquerading as a
 * safe detour. Each endpoint has footing, so the player can rest there. */
static bool discovery_step(const struct world_spelunk_runtime *r,
	int x, int y, int dx, int dy)
{
	struct world_spelunk_state state;
	struct world_spelunk_command move = { WORLD_SPELUNK_COMMAND_MOVE, dx, dy };
	struct world_spelunk_command grip = { WORLD_SPELUNK_COMMAND_GRIP, 0, 0 };
	struct world_spelunk_action_report report;
	int attempt;
	if (!world_spelunk_state_init(&state, &r->state.map, &r->state.rules, x, y, 100)) return false;
	for (attempt = 0; attempt <= 8; attempt++) {
		struct world_spelunk_state trial = state;
		if (world_spelunk_apply_command(&trial, &move, &report) != WORLD_SPELUNK_ACTION_REJECTED &&
			!report.damage && trial.x == x + dx && trial.y == y + dy &&
			trial.movement == WORLD_SPELUNK_STANDING) return true;
		if (world_spelunk_apply_command(&state, &grip, &report) == WORLD_SPELUNK_ACTION_REJECTED) break;
	}
	return false;
}

/* Multi-source flood from the certified route, bounded to twelve reversible
 * supported steps. This admits uneven chamber shelves without free-climbing
 * an unproven shaft. Zero means unreachable; one is a route cell. */
static uint8_t *discovery_access(const struct world_spelunk_runtime *r,
	const struct world_spelunk_generated_layout *g)
{
	size_t count = (size_t)g->width*g->height, i, head = 0, tail = 0;
	uint8_t *distance = mem_zalloc(count);
	size_t *queue = mem_alloc(count*sizeof(*queue));
	for (i = 0; i < g->witness_step_count; i++) {
		int x = g->witness_steps[i].expected_x, y = g->witness_steps[i].expected_y;
		if (discovery_walk_cell(r, x, y) && !distance[y*g->width+x]) {
			size_t index = (size_t)y*g->width+x;
			distance[index] = 1; queue[tail++] = index;
		}
	}
	while (head < tail) {
		size_t index = queue[head++];
		int x = (int)(index % g->width), y = (int)(index / g->width), dx, dy;
		if (distance[index] >= 13) continue;
		for (dy = -1; dy <= 1; dy++) for (dx = -1; dx <= 1; dx++) {
			int nx = x + dx, ny = y + dy;
			size_t next;
			if ((!dx && !dy) || !discovery_walk_cell(r, nx, ny)) continue;
			next = (size_t)ny*g->width+nx;
			if (distance[next] || !discovery_step(r, x, y, dx, dy) ||
				!discovery_step(r, nx, ny, -dx, -dy)) continue;
			distance[next] = distance[index] + 1; queue[tail++] = next;
		}
	}
	mem_free(queue);
	return distance;
}

/* Optional life beyond a find, never between it and the proven approach.
 * The pre-fixture distance map proves a shorter way home on the opposite side.
 * No carving, new rolls on opening, or changes to the ordinary population. */
static bool place_discovery_guardian(struct world_spelunk_runtime *r,
	const struct world_spelunk_generated_layout *g,
	const struct world_spelunk_population_profile *profile, uint32_t seed,
	const uint8_t *access, int x, int y)
{
	const struct world_spelunk_population_actor_entry *entry = NULL;
	uint32_t salt = mix32(seed ^ 0x67756172u), best = UINT32_MAX;
	int i, direction, best_x = -1, w = g->width;
	if (!profile->discovery_guardian_id ||
		salt % 100 >= profile->discovery_guardian_chance ||
		r->actor_count >= WORLD_SPELUNK_ACTOR_MAX) return true;
	for (i = 0; i < profile->actor_entry_count; i++) {
		if (streq(profile->actors[i].actor_id, profile->discovery_guardian_id))
			entry = &profile->actors[i];
	}
	if (!entry) return false;
	/* An existing neighbour already supplies the encounter. Don't pile on. */
	for (i = 0; i < (int)r->actor_count; i++) {
		if (within_radius(x, y, r->actors[i].x, r->actors[i].y, 6)) return true;
	}
	for (direction = -1; direction <= 1; direction += 2) {
		int approach = x - direction, distance;
		if (!discovery_walk_cell(r, approach, y) || !access[y*w+approach] ||
			access[y*w+approach] >= access[y*w+x]) continue;
		for (distance = 1; distance <= 4; distance++) {
			int gx = x + direction*distance;
			size_t index;
			uint32_t score;
			if (!discovery_walk_cell(r, gx, y)) break;
			index = (size_t)y*w+gx;
			if (world_spelunk_runtime_ground_object_at(r, gx, y) ||
				r->pitons[index] || r->ropes[index] ||
				(r->decoration_tags && r->decoration_tags[index])) break;
			if (distance < 2 || access[index] <= access[y*w+x] ||
				near_certified_route(g, gx, y, 2) ||
				!is_legal_population_cell(r, g, profile, gx, y,
					entry->minimum_depth_percent, entry->maximum_depth_percent) ||
				!world_spelunk_visibility_line_is_clear(r, approach, y, gx, y)) continue;
			score = mix32(salt ^ (uint32_t)index);
			if (best_x < 0 || score < best) { best = score; best_x = gx; }
		}
	}
	return best_x < 0 || world_spelunk_runtime_add_actor(r,
		profile->discovery_guardian_id, best_x, y);
}

static bool place_discovery(struct world_spelunk_runtime *r,
	const struct world_spelunk_generated_layout *g,
	const struct world_spelunk_population_profile *profile, uint32_t seed)
{
	const struct world_spelunk_population_discovery_entry *entry = NULL;
	uint32_t salt = mix32(seed ^ 0x66696e64u), best = UINT32_MAX;
	unsigned int total = 0, roll;
	int i, x, y, best_x = -1, best_y = -1;
	uint8_t *access;
	bool placed = true;
	if (!profile->discovery_entry_count || salt % 100 >= profile->discovery_chance)
		return true;
	for (i = 0; i < profile->discovery_entry_count; i++) total += profile->discoveries[i].weight;
	if (!total) return false;
	roll = mix32(salt ^ 0x76617279u) % total;
	for (i = 0; i < profile->discovery_entry_count; i++) {
		entry = &profile->discoveries[i];
		if (roll < entry->weight) break;
		roll -= entry->weight;
	}
	access = discovery_access(r, g);
	for (y = 1; y < g->height - 1; y++) {
		for (x = 1; x < g->width - 1; x++) {
			uint32_t score;
			if (near_certified_route(g, x, y, 1) ||
				!is_legal_population_cell(r, g, profile, x, y,
					entry->minimum_depth_percent, entry->maximum_depth_percent) ||
				!discovery_walk_cell(r, x, y) ||
				r->pitons[y*g->width+x] || r->ropes[y*g->width+x] ||
				(r->decoration_tags && r->decoration_tags[y*g->width+x]) || !access[y*g->width+x]) continue;
			score = mix32(salt ^ (uint32_t)(y*g->width+x));
			if (score < best) { best = score; best_x = x; best_y = y; }
		}
	}
	/* No safe side shelf: omit the find, with no replacement or generation retry. */
	if (best_x >= 0) {
		placed = world_spelunk_fixture_add(r, entry->fixture_id, best_x, best_y) &&
			place_discovery_guardian(r, g, profile, seed, access, best_x, best_y);
	}
	mem_free(access);
	return placed;
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
		place_discovery(runtime, generated, profile, seed) &&
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
