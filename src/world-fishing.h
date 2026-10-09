/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-fishing.h
 * \brief Bounded turn-based fishing activity state and rules.
 *
 *
 */

#ifndef WORLD_FISHING_H
#define WORLD_FISHING_H

#include "fishing-data.h"
#include "h-basic.h"
#include "world-location.h"
#include "z-type.h"

#define WORLD_FISHING_DEPTH_ROWS WORLD_FISHING_DEPTH_SCALE
#define WORLD_FISHING_INITIAL_REACH 1
#define WORLD_FISHING_BASELINE_MAX_REACH (WORLD_FISHING_COLUMNS - 1)
#define WORLD_FISHING_RIG_NAME_LEN 40

enum world_fishing_phase {
	WORLD_FISHING_INACTIVE = 0,
	WORLD_FISHING_WAITING,
	WORLD_FISHING_BITE,
	WORLD_FISHING_WINDING
};

enum world_fishing_action {
	WORLD_FISHING_EXTEND = 0,
	WORLD_FISHING_RETRACT,
	WORLD_FISHING_LOWER,
	WORLD_FISHING_RAISE,
	WORLD_FISHING_WAIT,
	WORLD_FISHING_STRIKE,
	WORLD_FISHING_REEL,
	WORLD_FISHING_CANCEL
};

enum world_fishing_event {
	WORLD_FISHING_EVENT_NONE = 0,
	WORLD_FISHING_EVENT_MOVED,
	WORLD_FISHING_EVENT_APPROACH,
	WORLD_FISHING_EVENT_WAITED,
	WORLD_FISHING_EVENT_BITE,
	WORLD_FISHING_EVENT_STRUCK,
	WORLD_FISHING_EVENT_PULL,
	WORLD_FISHING_EVENT_LANDED,
	WORLD_FISHING_EVENT_ESCAPED,
	WORLD_FISHING_EVENT_CANCELLED,
	WORLD_FISHING_EVENT_BLOCKED
};

struct world_fishing_fish {
	world_fishing_kind kind;
	char glyph;
	uint8_t attr;
	int col;
	int row;
	int depth_min;
	int depth_max;
	int run_chance;
	int wind_needed;
	int bite_timeout;
	bool biting;
};

struct world_fishing_runtime {
	bool active;
	enum world_fishing_phase phase;
	struct world_fishing_fish fish[WORLD_FISHING_MAX_FISH];
	int fish_count;
	int columns;
	int depth_rows;
	int rod_reach;
	int maximum_reach;
	int depth;
	int maximum_depth;
	enum world_fishing_habitat habitat;
	int location_danger;
	char rig_id[WORLD_ID_LEN];
	char rig_name[WORLD_FISHING_RIG_NAME_LEN];
	int fish_move_chance_percent;
	int fish_depth_move_chance_percent;
	int moving_hook_evasion_chance_percent;
	int moving_hook_evasion_radius;
	int patience_turns;
	int attraction_horizontal;
	int attraction_vertical;
	int wait_counter;
	int hooked_index;
	int wind_turns;
	int wind_needed;
	int bite_counter;
	struct loc origin;
	uint32_t rng_state;
};

struct world_fishing_report {
	enum world_fishing_event event;
	bool consumes_turn;
	world_fishing_kind landed_kind;
	int landed_depth;
};

void world_fishing_runtime_start(struct world_fishing_runtime *runtime,
		uint32_t seed, enum world_fishing_habitat habitat,
		int location_danger, int maximum_reach, int maximum_depth,
		const char *rig_id, const char *rig_name, struct loc origin);
void world_fishing_runtime_cancel(struct world_fishing_runtime *runtime);
bool world_fishing_runtime_is_valid(const struct world_fishing_runtime *runtime);
int world_fishing_hook_col(const struct world_fishing_runtime *runtime);
bool world_fishing_apply(struct world_fishing_runtime *runtime,
		enum world_fishing_action action, struct world_fishing_report *report);
const char *world_fishing_phase_name(enum world_fishing_phase phase);
bool world_fishing_kind_is_valid(world_fishing_kind kind);
const char *world_fishing_kind_name(world_fishing_kind kind);
char world_fishing_kind_glyph(world_fishing_kind kind);
uint32_t world_fishing_kind_larder_value(world_fishing_kind kind);
int world_fishing_kind_catch_experience(world_fishing_kind kind);
int world_fishing_kind_donation_experience(world_fishing_kind kind);
int world_fishing_habitat_weight(enum world_fishing_habitat habitat,
		int location_danger, world_fishing_kind kind);

#endif /* !WORLD_FISHING_H */
