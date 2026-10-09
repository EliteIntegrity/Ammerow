/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-actor-data.h
 * \brief Validated immutable data for side-view actors and authored spawns.
 *
 *
 */

#ifndef WORLD_SPELUNKING_ACTOR_DATA_H
#define WORLD_SPELUNKING_ACTOR_DATA_H

#include "h-basic.h"
#include "world-location.h"

#define WORLD_SPELUNK_ACTOR_DEFINITION_MAX 32
#define WORLD_SPELUNK_SPAWN_MAX 32
#define WORLD_SPELUNK_SPAWN_CANDIDATE_MAX 16

#define WORLD_SPELUNK_CHASM_SKITTER_ID \
	"core.spelunk.actor.chasm-skitter"

struct file_parser;

/** Immutable combat and progression authority for one side-view actor. */
struct world_spelunk_actor_definition {
	const char *id;
	const char *name;
	const char *death_cause;
	wchar_t glyph;
	uint8_t attr;
	int hitpoints;
	int armour;
	int to_hit;
	int damage_dice;
	int damage_sides;
	int speed;
	int experience;
};

/** One explicitly ordered fallback position for an authored spawn. */
struct world_spelunk_spawn_candidate {
	int priority;
	int x;
	int y;
};

/** One persistent actor to place once in an authored side-view location. */
struct world_spelunk_spawn_definition {
	const char *id;
	const char *location_id;
	const struct world_spelunk_actor_definition *actor;
	int candidate_count;
	struct world_spelunk_spawn_candidate
		candidates[WORLD_SPELUNK_SPAWN_CANDIDATE_MAX];
};

/** Parser lifecycle for lib/gamedata/spelunking_actor.txt. */
extern struct file_parser spelunking_actor_parser;

int world_spelunk_actor_definition_count(void);
const struct world_spelunk_actor_definition *world_spelunk_actor_by_index(
		int index);
const struct world_spelunk_actor_definition *world_spelunk_actor_by_id(
		const char *id);

int world_spelunk_spawn_definition_count(void);
const struct world_spelunk_spawn_definition *world_spelunk_spawn_by_index(
		int index);
const struct world_spelunk_spawn_definition *world_spelunk_spawn_by_id(
		const char *id);

/** Validate spawn location references after world.txt has been parsed. */
bool world_spelunk_actor_data_validate_world(void);

#endif /* !WORLD_SPELUNKING_ACTOR_DATA_H */
