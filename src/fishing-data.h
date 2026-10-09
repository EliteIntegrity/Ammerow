/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file fishing-data.h
 * \brief Validated immutable data for the bounded fishing activity.
 *
 *
 */

#ifndef FISHING_DATA_H
#define FISHING_DATA_H

#include "h-basic.h"
#include "world-location.h"

#define WORLD_FISHING_COLUMNS 72
#define WORLD_FISHING_MAX_FISH 15
#define WORLD_FISHING_MAX_SPECIES 32
#define WORLD_FISHING_MAX_RIGS 16
#define WORLD_FISHING_MAX_DISCOVERIES 8
#define WORLD_FISHING_DEPTH_SCALE 16

typedef int16_t world_fishing_kind;

#define WORLD_FISHING_KIND_NONE ((world_fishing_kind)-1)

struct file_parser;

struct world_fishing_rules {
	int minimum_fish;
	int maximum_fish;
	int move_chance;
	int depth_move_chance;
	int evasion_chance;
	int evasion_radius;
	int patience_turns;
	int attraction_horizontal;
	int attraction_vertical;
};

struct world_fishing_species {
	const char *id;
	const char *name;
	char glyph;
	uint8_t attr;
	int depth_min;
	int depth_max;
	int run_chance;
	int wind_needed;
	int bite_timeout;
	int danger_weight;
	uint32_t larder_value;
	uint16_t catch_experience;
	uint16_t donation_experience;
	int habitat_weights[WORLD_FISHING_HABITAT_MAX];
};

/** One carried object kind and the fishing limits it provides. */
struct world_fishing_rig {
	const char *id;
	const char *name;
	const char *tval_name;
	const char *item_name;
	int priority;
	int maximum_reach;
	int maximum_depth;
	int tval;
	int sval;
};

/** One authored catch condition which reveals persistent world topology. */
struct world_fishing_discovery {
	const char *id;
	const char *species_id;
	const char *rig_id;
	const char *location_id;
	enum world_fishing_habitat habitat;
	int minimum_depth;
	const char *system_id;
	const char *portal_id;
	const char *out_route_id;
	const char *in_route_id;
	const char *message;
};

struct object_kind;

/** Parser lifecycle for lib/gamedata/fishing.txt. */
extern struct file_parser fishing_parser;

/** The validated global activity rules, or NULL before data initialization. */
const struct world_fishing_rules *world_fishing_rules(void);

/** Number of validated species in file order. */
int world_fishing_species_count(void);

/** Look up immutable species data by runtime kind or stable data ID. */
const struct world_fishing_species *world_fishing_species_by_kind(
		world_fishing_kind kind);
world_fishing_kind world_fishing_kind_by_id(const char *id);

/** Ordered, immutable fishing-rig data and post-object-parser linkage. */
int world_fishing_rig_count(void);
const struct world_fishing_rig *world_fishing_rig_by_index(int index);
const struct world_fishing_rig *world_fishing_rig_by_id(const char *id);
bool world_fishing_link_rig_items(void);
const struct world_fishing_rig *world_fishing_rig_for_kind(
		const struct object_kind *kind);

int world_fishing_discovery_count(void);
const struct world_fishing_discovery *world_fishing_discovery_by_index(
		int index);
bool world_fishing_validate_discoveries(void);

/** Convert between authored habitat IDs and player-facing names. */
const char *world_fishing_habitat_name(enum world_fishing_habitat habitat);
bool world_fishing_habitat_from_name(const char *name,
		enum world_fishing_habitat *habitat);

#endif /* !FISHING_DATA_H */
