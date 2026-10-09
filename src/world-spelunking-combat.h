/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-combat.h
 * \brief Small persistent actor and melee adapter for side-view locations.
 *
 *
 */

#ifndef WORLD_SPELUNKING_COMBAT_H
#define WORLD_SPELUNKING_COMBAT_H

#include "z-type.h"
#include "world-spelunking-runtime.h"

#include <stddef.h>

struct player;
struct object;

#define WORLD_SPELUNK_PROJECTILE_PATH_MAX 256u

enum world_spelunk_combat_result {
	WORLD_SPELUNK_COMBAT_INVALID = 0,
	WORLD_SPELUNK_COMBAT_NO_TARGET,
	WORLD_SPELUNK_COMBAT_UNSTABLE,
	WORLD_SPELUNK_COMBAT_TURN,
	WORLD_SPELUNK_COMBAT_KILLED
};

struct world_spelunk_combat_report {
	const char *actor_id;
	int x;
	int y;
	int damage;
	bool hit;
	bool killed;
};

enum world_spelunk_actor_event {
	WORLD_SPELUNK_ACTOR_EVENT_NONE = 0,
	WORLD_SPELUNK_ACTOR_EVENT_MOVED,
	WORLD_SPELUNK_ACTOR_EVENT_MISSED,
	WORLD_SPELUNK_ACTOR_EVENT_HIT
};

struct world_spelunk_actor_report {
	enum world_spelunk_actor_event event;
	const char *actor_id;
	int from_x;
	int from_y;
	int to_x;
	int to_y;
	int damage;
};

enum world_spelunk_projectile_result {
	WORLD_SPELUNK_PROJECTILE_INVALID = 0,
	WORLD_SPELUNK_PROJECTILE_UNSTABLE,
	WORLD_SPELUNK_PROJECTILE_NO_STORAGE,
	WORLD_SPELUNK_PROJECTILE_TURN
};

struct world_spelunk_projectile_hit {
	const char *actor_id;
	int x;
	int y;
	int damage;
	uint32_t message_type;
	bool visible;
	bool hit;
	bool killed;
};

struct world_spelunk_projectile_report {
	struct loc path[WORLD_SPELUNK_PROJECTILE_PATH_MAX];
	size_t path_length;
	struct world_spelunk_projectile_hit hits[WORLD_SPELUNK_ACTOR_MAX];
	size_t hit_count;
	int landing_x;
	int landing_y;
	bool hit_target;
	bool recoverable;
};

const char *world_spelunk_actor_name(const char *actor_id);

/** Attack an actor occupying the adjacent requested movement cell. */
enum world_spelunk_combat_result world_spelunk_player_bump_attack(
		struct player *p, int dx, int dy,
		struct world_spelunk_combat_report *report);

/** Resolve the rare case where a jump or fall lands directly on an actor. */
enum world_spelunk_combat_result world_spelunk_player_resolve_collision(
		struct player *p, struct world_spelunk_combat_report *report);

/** Accumulate and, when ready, spend one action for side-view actors. */
bool world_spelunk_process_actors(struct player *p,
		struct world_spelunk_actor_report *reports, size_t capacity,
		size_t *written);

/**
 * Project a side-view ray through open cells.  A bolt stops at the first
 * actor; a beam continues through every actor.  The native projection type is
 * retained for messaging and frontend animation, while actor resistances stay
 * neutral until their data schema authors elemental traits.
 */
bool world_spelunk_player_project_ray(struct player *p, int dx, int dy,
		int range, int damage, int projection_type, bool stop_at_first_actor,
		size_t *hits);

/** Travel along one side-view ray, then damage actors in a rock-occluded ball. */
bool world_spelunk_player_project_ball(struct player *p, int dx, int dy,
		int range, int radius, int damage, int projection_type, size_t *hits);

/** Find the closest visible actor reachable by one unobstructed shot. */
bool world_spelunk_closest_visible_actor(const struct player *p, int range,
		struct loc *target);

/** Resolve one launched or thrown object against the side-view owner. */
enum world_spelunk_projectile_result world_spelunk_player_projectile(
		struct player *p, struct object *missile, struct object *launcher,
		struct loc target, bool through_target, int range, int shots,
		struct world_spelunk_projectile_report *report);

#endif /* !WORLD_SPELUNKING_COMBAT_H */
