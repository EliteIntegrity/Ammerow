/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking.h
 * \brief Renderer-free rules core for turn-based side-view spelunking.
 *
 *
 */

#ifndef WORLD_SPELUNKING_H
#define WORLD_SPELUNKING_H

#include <stdbool.h>
#include <stdint.h>

enum world_spelunk_tile {
	WORLD_SPELUNK_AIR = 0,
	WORLD_SPELUNK_ROCK,
	WORLD_SPELUNK_WATER
};

enum world_spelunk_movement {
	WORLD_SPELUNK_STANDING = 0,
	WORLD_SPELUNK_JUMP_APEX,
	WORLD_SPELUNK_FALLING,
	WORLD_SPELUNK_CLIMBING,
	WORLD_SPELUNK_HANGING,
	WORLD_SPELUNK_SWIMMING
};

enum world_spelunk_command_kind {
	WORLD_SPELUNK_COMMAND_MOVE = 0,
	WORLD_SPELUNK_COMMAND_WAIT,
	WORLD_SPELUNK_COMMAND_JUMP,
	WORLD_SPELUNK_COMMAND_GRIP
};

enum world_spelunk_action_outcome {
	WORLD_SPELUNK_ACTION_REJECTED = 0,
	WORLD_SPELUNK_ACTION_FREE,
	WORLD_SPELUNK_ACTION_TURN
};

enum world_spelunk_action_event {
	WORLD_SPELUNK_EVENT_NONE = 0,
	WORLD_SPELUNK_EVENT_MOVED,
	WORLD_SPELUNK_EVENT_RESTED,
	WORLD_SPELUNK_EVENT_LANDED,
	WORLD_SPELUNK_EVENT_FALL_HURT,
	WORLD_SPELUNK_EVENT_JUMPED,
	WORLD_SPELUNK_EVENT_JUMP_HELD,
	WORLD_SPELUNK_EVENT_GRIP_SELECTED,
	WORLD_SPELUNK_EVENT_GRIP_CLEARED,
	WORLD_SPELUNK_EVENT_GRIPPED,
	WORLD_SPELUNK_EVENT_GRIP_DRAINED,
	WORLD_SPELUNK_EVENT_GRIP_EXHAUSTED,
	WORLD_SPELUNK_EVENT_ROPE_ATTACHED,
	WORLD_SPELUNK_EVENT_ROPE_DRAINED,
	WORLD_SPELUNK_EVENT_ROPE_EXHAUSTED,
	WORLD_SPELUNK_EVENT_WATER_LANDED,
	WORLD_SPELUNK_EVENT_SWAM
};

enum world_spelunk_breath_event {
	WORLD_SPELUNK_BREATH_NONE = 0,
	WORLD_SPELUNK_BREATH_SCUBA_USED,
	WORLD_SPELUNK_BREATH_HELD,
	WORLD_SPELUNK_BREATH_RESTORED,
	WORLD_SPELUNK_BREATH_DROWNING
};

/** A caller-owned immutable terrain view. */
struct world_spelunk_map {
	const enum world_spelunk_tile *cells;
	int width;
	int height;
	int stride;
	/** Optional runtime-owned infrastructure views. */
	const uint8_t *pitons;
	const uint8_t *ropes;
	int infrastructure_stride;
};

/** Integer rules keep simulation deterministic and cheap to serialize. */
struct world_spelunk_rules {
	unsigned int action_energy;
	int safe_fall_tiles;
	int fall_base_damage;
	int jump_stamina_cost;
	int grip_up_stamina_cost;
	int grip_lateral_stamina_cost;
	int grip_down_stamina_cost;
	int rest_stamina_gain;
	int rope_max_length;
	int rope_up_stamina_cost;
	int rope_lateral_stamina_cost;
	int rope_down_stamina_cost;
	int breath_turns;
	int drowning_damage;
	int swim_turn_stamina_cost;
};

struct world_spelunk_command {
	enum world_spelunk_command_kind kind;
	int dx;
	int dy;
};

/** Complete state for the first rules-only vertical slice. */
struct world_spelunk_state {
	struct world_spelunk_map map;
	struct world_spelunk_rules rules;
	int x;
	int y;
	int stamina;
	int max_stamina;
	int breath;
	int fall_start_y;
	int grip_target_x;
	int grip_target_y;
	enum world_spelunk_movement movement;
	bool jump_holding;
};

/** Exact result submitted to the shared game after one semantic command. */
struct world_spelunk_action_report {
	enum world_spelunk_action_outcome outcome;
	enum world_spelunk_action_event event;
	unsigned int energy_use;
	int damage;
	int fall_tiles;
	int stamina_delta;
};

/** Result of one already-committed environmental turn. */
struct world_spelunk_breath_report {
	enum world_spelunk_breath_event event;
	int breath_delta;
	int air_supply_used;
	int damage;
};

bool world_spelunk_state_init(struct world_spelunk_state *state,
		const struct world_spelunk_map *map,
		const struct world_spelunk_rules *rules, int x, int y, int stamina);
bool world_spelunk_state_is_valid(const struct world_spelunk_state *state);
/** True when rock or a fixed piton provides weight-bearing support. */
bool world_spelunk_is_grounded(const struct world_spelunk_state *state);
/** True only for a stable rock floor; a lone piton is not a rest site. */
bool world_spelunk_has_stable_floor(const struct world_spelunk_state *state);
/** True when water covers the player without clear air immediately above. */
bool world_spelunk_is_submerged(const struct world_spelunk_state *state);
/** True only while the selected hold remains reachable and usable. */
bool world_spelunk_has_active_grip(const struct world_spelunk_state *state);
enum world_spelunk_action_outcome world_spelunk_apply_command(
		struct world_spelunk_state *state,
		const struct world_spelunk_command *command,
		struct world_spelunk_action_report *report);
bool world_spelunk_apply_breath_turn(struct world_spelunk_state *state,
		bool air_supply_available,
		struct world_spelunk_breath_report *report);

#endif /* !WORLD_SPELUNKING_H */
