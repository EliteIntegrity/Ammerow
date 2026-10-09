/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking.c
 * \brief Renderer-free rules core for turn-based side-view spelunking.
 *
 *
 */

#include "world-spelunking.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

static bool map_is_valid(const struct world_spelunk_map *map)
{
	if (!map || !map->cells || map->width <= 0 || map->height <= 0 ||
			map->stride < map->width) {
		return false;
	}
	if (!map->pitons && !map->ropes) {
		return map->infrastructure_stride == 0;
	}
	return map->pitons && map->ropes &&
		map->infrastructure_stride >= map->width;
}

static bool map_in_bounds(const struct world_spelunk_map *map, int x, int y)
{
	return map && x >= 0 && x < map->width && y >= 0 && y < map->height;
}

static bool map_is_solid(const struct world_spelunk_map *map, int x, int y)
{
	if (!map_in_bounds(map, x, y)) return true;
	return map->cells[y * map->stride + x] == WORLD_SPELUNK_ROCK;
}

static bool map_is_water(const struct world_spelunk_map *map, int x, int y)
{
	return map_in_bounds(map, x, y) &&
		map->cells[y * map->stride + x] == WORLD_SPELUNK_WATER;
}

static bool map_is_air(const struct world_spelunk_map *map, int x, int y)
{
	return map_in_bounds(map, x, y) &&
		map->cells[y * map->stride + x] == WORLD_SPELUNK_AIR;
}

static bool map_has_piton(const struct world_spelunk_map *map, int x, int y)
{
	return map_in_bounds(map, x, y) && map->pitons &&
		map->pitons[y * map->infrastructure_stride + x] != 0;
}

static bool map_has_rope(const struct world_spelunk_map *map, int x, int y)
{
	return map_in_bounds(map, x, y) && map->ropes &&
		map->ropes[y * map->infrastructure_stride + x] != 0;
}

static bool map_has_footing(const struct world_spelunk_map *map, int x, int y)
{
	return !map_is_water(map, x, y) &&
		(map_is_solid(map, x, y + 1) || map_has_piton(map, x, y));
}

static bool map_is_grippable(const struct world_spelunk_map *map, int x,
		int y)
{
	return map_in_bounds(map, x, y) &&
		(map_is_solid(map, x, y) || map_has_piton(map, x, y));
}

static bool has_secure_support(const struct world_spelunk_state *state);
static void prune_inactive_grip(struct world_spelunk_state *state);
static enum world_spelunk_action_event attach_to_rope(
		struct world_spelunk_state *state, int *damage);

static int clamp_int(int value, int low, int high)
{
	if (value < low) return low;
	if (value > high) return high;
	return value;
}

bool world_spelunk_state_init(struct world_spelunk_state *state,
		const struct world_spelunk_map *map,
		const struct world_spelunk_rules *rules, int x, int y, int stamina)
{
	if (!state || !map_is_valid(map) || !rules ||
		!map_in_bounds(map, x, y) || map_is_solid(map, x, y) ||
		stamina <= 0 || rules->action_energy == 0 ||
		rules->safe_fall_tiles < 0 || rules->fall_base_damage < 0 ||
		rules->jump_stamina_cost < 0 ||
		rules->grip_up_stamina_cost < 0 ||
		rules->grip_lateral_stamina_cost < 0 ||
		rules->grip_down_stamina_cost < 0 ||
		rules->rest_stamina_gain < 0 || rules->rope_max_length <= 0 ||
		rules->rope_up_stamina_cost < 0 ||
		rules->rope_lateral_stamina_cost < 0 ||
		rules->rope_down_stamina_cost < 0 || rules->breath_turns <= 0 ||
		rules->drowning_damage <= 0 || rules->swim_turn_stamina_cost < 0) {
		return false;
	}

	memset(state, 0, sizeof(*state));
	state->map = *map;
	state->rules = *rules;
	state->x = x;
	state->y = y;
	state->stamina = stamina;
	state->max_stamina = stamina;
	state->breath = rules->breath_turns;
	state->fall_start_y = y;
	state->grip_target_x = -1;
	state->grip_target_y = -1;
	state->movement = map_is_water(map, x, y) ? WORLD_SPELUNK_SWIMMING :
		(has_secure_support(state) ? WORLD_SPELUNK_STANDING :
		WORLD_SPELUNK_FALLING);
	return true;
}

bool world_spelunk_is_grounded(const struct world_spelunk_state *state)
{
	if (!state || !map_is_valid(&state->map)) return false;
	return map_has_footing(&state->map, state->x, state->y);
}

bool world_spelunk_has_stable_floor(const struct world_spelunk_state *state)
{
	if (!state || !map_is_valid(&state->map)) return false;
	return state->movement == WORLD_SPELUNK_STANDING &&
		!map_is_water(&state->map, state->x, state->y) &&
		map_is_solid(&state->map, state->x, state->y + 1);
}

bool world_spelunk_is_submerged(const struct world_spelunk_state *state)
{
	if (!state || !map_is_valid(&state->map) ||
			!map_is_water(&state->map, state->x, state->y)) {
		return false;
	}
	/* A water cell models the swimmer's body.  Clear air in the cell above
	 * models a head at the surface; water or rock above means submerged. */
	return !map_is_air(&state->map, state->x, state->y - 1);
}

static bool has_secure_support(const struct world_spelunk_state *state)
{
	return world_spelunk_is_grounded(state);
}

static void set_standing(struct world_spelunk_state *state)
{
	state->movement = WORLD_SPELUNK_STANDING;
	state->fall_start_y = state->y;
	state->jump_holding = false;
	prune_inactive_grip(state);
}

static void set_swimming(struct world_spelunk_state *state)
{
	state->movement = WORLD_SPELUNK_SWIMMING;
	state->fall_start_y = state->y;
	state->grip_target_x = -1;
	state->grip_target_y = -1;
	state->jump_holding = false;
}

bool world_spelunk_has_active_grip(const struct world_spelunk_state *state)
{
	int dx;
	int dy;

	if (!state || state->stamina <= 0 || state->grip_target_x < 0 ||
		state->grip_target_y < 0) {
		return false;
	}
	dx = state->grip_target_x - state->x;
	dy = state->grip_target_y - state->y;
	if ((dx == 0 && dy == 0) || dx < -1 || dx > 1 || dy < -1 || dy > 1) {
		return false;
	}
	return map_is_grippable(&state->map, state->grip_target_x,
		state->grip_target_y);
}

static void prune_inactive_grip(struct world_spelunk_state *state)
{
	if (!state || state->grip_target_x < 0 ||
			world_spelunk_has_active_grip(state)) {
		return;
	}
	state->grip_target_x = -1;
	state->grip_target_y = -1;
}

bool world_spelunk_state_is_valid(const struct world_spelunk_state *state)
{
	bool active_grip;

	if (!state || !map_is_valid(&state->map) ||
			!map_in_bounds(&state->map, state->x, state->y) ||
			map_is_solid(&state->map, state->x, state->y) ||
			state->rules.action_energy == 0 ||
			state->rules.safe_fall_tiles < 0 ||
			state->rules.fall_base_damage < 0 ||
			state->rules.jump_stamina_cost < 0 ||
			state->rules.grip_up_stamina_cost < 0 ||
			state->rules.grip_lateral_stamina_cost < 0 ||
			state->rules.grip_down_stamina_cost < 0 ||
			state->rules.rest_stamina_gain < 0 ||
			state->rules.rope_max_length <= 0 ||
			state->rules.rope_up_stamina_cost < 0 ||
			state->rules.rope_lateral_stamina_cost < 0 ||
			state->rules.rope_down_stamina_cost < 0 ||
			state->rules.breath_turns <= 0 ||
			state->rules.drowning_damage <= 0 ||
			state->rules.swim_turn_stamina_cost < 0 ||
			state->max_stamina <= 0 || state->stamina < 0 ||
			state->stamina > state->max_stamina ||
			state->breath < 0 ||
			state->breath > state->rules.breath_turns ||
			!map_in_bounds(&state->map, state->x, state->fall_start_y) ||
			state->movement < WORLD_SPELUNK_STANDING ||
			state->movement > WORLD_SPELUNK_SWIMMING) {
		return false;
	}
	if ((state->grip_target_x < 0) != (state->grip_target_y < 0)) {
		return false;
	}
	if (state->grip_target_x >= 0 &&
			!map_is_grippable(&state->map, state->grip_target_x,
				state->grip_target_y)) {
		return false;
	}
	active_grip = world_spelunk_has_active_grip(state);
	if (state->jump_holding &&
			state->movement != WORLD_SPELUNK_JUMP_APEX) {
		return false;
	}
	switch (state->movement) {
	case WORLD_SPELUNK_STANDING:
		return has_secure_support(state) && !state->jump_holding;
	case WORLD_SPELUNK_JUMP_APEX:
		return !has_secure_support(state);
	case WORLD_SPELUNK_FALLING:
		return !has_secure_support(state) && !state->jump_holding &&
			state->fall_start_y <= state->y;
	case WORLD_SPELUNK_CLIMBING:
		return !has_secure_support(state) &&
			!map_has_rope(&state->map, state->x, state->y) && active_grip &&
			!state->jump_holding;
	case WORLD_SPELUNK_HANGING:
		return !has_secure_support(state) && state->stamina > 0 &&
			map_has_rope(&state->map, state->x, state->y) &&
			!state->jump_holding;
	case WORLD_SPELUNK_SWIMMING:
		return map_is_water(&state->map, state->x, state->y) &&
			!state->jump_holding;
	default:
		return false;
	}
}

static void sync_grip(struct world_spelunk_state *state)
{
	prune_inactive_grip(state);
	if (!map_is_water(&state->map, state->x, state->y) &&
			!has_secure_support(state) &&
			!map_has_rope(&state->map, state->x, state->y) &&
			world_spelunk_has_active_grip(state)) {
		state->movement = WORLD_SPELUNK_CLIMBING;
		state->jump_holding = false;
	}
}

static int damage_for_fall(const struct world_spelunk_rules *rules,
		int fall_tiles)
{
	int damage;
	int tile;

	if (fall_tiles <= rules->safe_fall_tiles || !rules->fall_base_damage) {
		return 0;
	}
	damage = rules->fall_base_damage;
	for (tile = rules->safe_fall_tiles + 1; tile < fall_tiles; tile++) {
		if (damage > INT_MAX / 2) return INT_MAX;
		damage *= 2;
	}
	return damage;
}

static enum world_spelunk_action_event apply_gravity(
		struct world_spelunk_state *state, int *damage_out)
{
	int fall_tiles;
	int fall_damage;

	if (state->movement == WORLD_SPELUNK_CLIMBING ||
			state->movement == WORLD_SPELUNK_HANGING) {
		return WORLD_SPELUNK_EVENT_NONE;
	}
	if (map_is_water(&state->map, state->x, state->y)) {
		set_swimming(state);
		return WORLD_SPELUNK_EVENT_WATER_LANDED;
	}
	if (has_secure_support(state)) {
		set_standing(state);
		return WORLD_SPELUNK_EVENT_LANDED;
	}
	if (map_has_rope(&state->map, state->x, state->y) &&
			state->stamina > 0) {
		return attach_to_rope(state, damage_out);
	}
	if (state->movement != WORLD_SPELUNK_FALLING) {
		state->fall_start_y = state->y;
		state->movement = WORLD_SPELUNK_FALLING;
		state->jump_holding = false;
	}
	while (!world_spelunk_is_grounded(state) &&
			!map_is_water(&state->map, state->x, state->y) &&
			!(map_has_rope(&state->map, state->x, state->y) &&
			state->stamina > 0)) {
		state->y++;
	}
	if (map_is_water(&state->map, state->x, state->y)) {
		set_swimming(state);
		return WORLD_SPELUNK_EVENT_WATER_LANDED;
	}
	if (map_has_rope(&state->map, state->x, state->y) &&
			state->stamina > 0) {
		return attach_to_rope(state, damage_out);
	}

	fall_tiles = state->y - state->fall_start_y;
	fall_damage = damage_for_fall(&state->rules, fall_tiles);
	if (fall_damage > 0) {
		if (damage_out) *damage_out += fall_damage;
		set_standing(state);
		return WORLD_SPELUNK_EVENT_FALL_HURT;
	}
	set_standing(state);
	return WORLD_SPELUNK_EVENT_LANDED;
}

static enum world_spelunk_action_event spend_grip_turn(
		struct world_spelunk_state *state, int dy, int *damage)
{
	int cost = dy < 0 ? state->rules.grip_up_stamina_cost :
		(dy > 0 ? state->rules.grip_down_stamina_cost :
		state->rules.grip_lateral_stamina_cost);

	state->stamina = clamp_int(state->stamina - cost, 0,
		state->max_stamina);
	if (state->stamina == 0) {
		state->movement = WORLD_SPELUNK_FALLING;
		state->fall_start_y = state->y;
		state->jump_holding = false;
		(void)apply_gravity(state, damage);
		return WORLD_SPELUNK_EVENT_GRIP_EXHAUSTED;
	}
	return WORLD_SPELUNK_EVENT_GRIP_DRAINED;
}

static enum world_spelunk_action_event spend_rope_turn(
		struct world_spelunk_state *state, int dy, int *damage)
{
	int cost = dy < 0 ? state->rules.rope_up_stamina_cost :
		(dy > 0 ? state->rules.rope_down_stamina_cost :
		state->rules.rope_lateral_stamina_cost);

	state->stamina = clamp_int(state->stamina - cost, 0,
		state->max_stamina);
	if (state->stamina == 0) {
		state->movement = WORLD_SPELUNK_FALLING;
		state->fall_start_y = state->y;
		state->jump_holding = false;
		(void)apply_gravity(state, damage);
		return WORLD_SPELUNK_EVENT_ROPE_EXHAUSTED;
	}
	return WORLD_SPELUNK_EVENT_ROPE_DRAINED;
}

static enum world_spelunk_action_event spend_swim_turn(
		struct world_spelunk_state *state)
{
	state->stamina = clamp_int(state->stamina -
		state->rules.swim_turn_stamina_cost, 0, state->max_stamina);
	return WORLD_SPELUNK_EVENT_SWAM;
}

static enum world_spelunk_action_event attach_to_rope(
		struct world_spelunk_state *state, int *damage)
{
	state->fall_start_y = state->y;
	state->jump_holding = false;
	prune_inactive_grip(state);
	if (state->stamina == 0) {
		state->movement = WORLD_SPELUNK_FALLING;
		(void)apply_gravity(state, damage);
		return WORLD_SPELUNK_EVENT_ROPE_EXHAUSTED;
	}
	state->movement = WORLD_SPELUNK_HANGING;
	return WORLD_SPELUNK_EVENT_ROPE_ATTACHED;
}

static enum world_spelunk_action_event resolve_after_move(
		struct world_spelunk_state *state, bool drain_grip, bool drain_rope,
		int dy, int *damage)
{
	prune_inactive_grip(state);
	if (map_is_water(&state->map, state->x, state->y)) {
		set_swimming(state);
		return spend_swim_turn(state);
	}
	if (has_secure_support(state)) {
		set_standing(state);
		return WORLD_SPELUNK_EVENT_MOVED;
	}
	if (map_has_rope(&state->map, state->x, state->y)) {
		enum world_spelunk_action_event event =
			attach_to_rope(state, damage);

		return drain_rope && event == WORLD_SPELUNK_EVENT_ROPE_ATTACHED ?
			spend_rope_turn(state, dy, damage) : event;
	}
	sync_grip(state);
	if (state->movement == WORLD_SPELUNK_CLIMBING &&
			world_spelunk_has_active_grip(state)) {
		return drain_grip ? spend_grip_turn(state, dy, damage) :
			WORLD_SPELUNK_EVENT_GRIPPED;
	}
	state->movement = WORLD_SPELUNK_FALLING;
	/* A deliberate downward step is part of the fall, not a free tile. */
	state->fall_start_y = state->y - (dy > 0 ? dy : 0);
	return apply_gravity(state, damage);
}

static enum world_spelunk_action_event apply_move(
		struct world_spelunk_state *state, int dx, int dy, bool *accepted,
		int *damage)
{
	int step;
	int x1;
	int x2;
	int nx;
	int ny;
	bool corner_squeeze = false;
	bool anchor_step = false;

	*accepted = false;
	if (dx < -1 || dx > 1 || dy < -1 || dy > 1 ||
		(dx == 0 && dy == 0)) {
		return WORLD_SPELUNK_EVENT_NONE;
	}
	if (state->movement == WORLD_SPELUNK_FALLING) {
		*accepted = true;
		return apply_gravity(state, damage);
	}
	if (state->movement == WORLD_SPELUNK_JUMP_APEX &&
		state->jump_holding) {
		state->jump_holding = false;
		state->movement = WORLD_SPELUNK_FALLING;
		state->fall_start_y = state->y;
		*accepted = true;
		return apply_gravity(state, damage);
	}
	if (state->movement == WORLD_SPELUNK_SWIMMING) {
		nx = state->x + dx;
		ny = state->y + dy;
		if (map_is_solid(&state->map, nx, ny)) {
			return WORLD_SPELUNK_EVENT_NONE;
		}
		state->x = nx;
		state->y = ny;
		*accepted = true;
		return resolve_after_move(state, false, false, dy, damage);
	}

	if (state->movement == WORLD_SPELUNK_JUMP_APEX) {
		if (dx == 0 && dy == 1) {
			*accepted = true;
			return apply_gravity(state, damage);
		}
		if (dx == 0 || dy != 0) return WORLD_SPELUNK_EVENT_NONE;
		step = dx > 0 ? 1 : -1;
		x1 = state->x + step;
		x2 = state->x + step * 2;
		if (map_is_solid(&state->map, x1, state->y)) {
			return WORLD_SPELUNK_EVENT_NONE;
		}
		state->x = map_is_solid(&state->map, x2, state->y) ? x1 : x2;
		state->fall_start_y = state->y;
		state->jump_holding = false;
		*accepted = true;
		if (has_secure_support(state)) {
			set_standing(state);
			return WORLD_SPELUNK_EVENT_MOVED;
		}
		if (map_has_rope(&state->map, state->x, state->y)) {
			return attach_to_rope(state, damage);
		}
		sync_grip(state);
		if (state->movement == WORLD_SPELUNK_CLIMBING) {
			return spend_grip_turn(state, 0, damage);
		}
		state->jump_holding = true;
		return WORLD_SPELUNK_EVENT_JUMP_HELD;
	}

	if (state->movement == WORLD_SPELUNK_CLIMBING) {
		nx = state->x + dx;
		ny = state->y + dy;
		if (map_is_solid(&state->map, nx, ny)) {
			return WORLD_SPELUNK_EVENT_NONE;
		}
		state->x = nx;
		state->y = ny;
		*accepted = true;
		return resolve_after_move(state, true, false, dy, damage);
	}

	if (state->movement == WORLD_SPELUNK_HANGING) {
		nx = state->x + dx;
		ny = state->y + dy;
		if (map_is_solid(&state->map, nx, ny)) {
			return WORLD_SPELUNK_EVENT_NONE;
		}
		/* Step up off a rope onto an adjoining ledge/anchor, not into
		 * unsupported air. Downward diagonals may attach or commit a fall. */
		if (dx != 0 && dy < 0 && !map_has_footing(&state->map, nx, ny)) {
			return WORLD_SPELUNK_EVENT_NONE;
		}
		state->x = nx;
		state->y = ny;
		*accepted = true;
		return resolve_after_move(state, true, true, dy, damage);
	}

	if (state->movement == WORLD_SPELUNK_STANDING && dx != 0 && dy != 0) {
		corner_squeeze = map_is_solid(&state->map, state->x + dx,
			state->y) && map_is_solid(&state->map, state->x,
			state->y + dy);
		anchor_step = map_has_piton(&state->map, state->x, state->y) &&
			map_has_footing(&state->map, state->x + dx, state->y + dy);
	}
	if (state->movement != WORLD_SPELUNK_STANDING ||
			(dx != 0 && dy < 0 && !anchor_step &&
			!world_spelunk_has_active_grip(state) && !corner_squeeze)) {
		return WORLD_SPELUNK_EVENT_NONE;
	}
	nx = state->x + dx;
	ny = state->y + dy;
	if (map_is_solid(&state->map, nx, ny)) {
		return WORLD_SPELUNK_EVENT_NONE;
	}
	if (dy < 0 && !map_has_rope(&state->map, nx, ny) &&
			!map_has_piton(&state->map, nx, ny) &&
			!map_has_piton(&state->map, state->x, state->y) &&
			!world_spelunk_has_active_grip(state) && !corner_squeeze) {
		return WORLD_SPELUNK_EVENT_NONE;
	}
	state->x = nx;
	state->y = ny;
	*accepted = true;
	return resolve_after_move(state, true, false, dy, damage);
}

static enum world_spelunk_action_event apply_wait(
		struct world_spelunk_state *state, int *damage)
{
	sync_grip(state);
	if (state->movement == WORLD_SPELUNK_FALLING ||
		state->movement == WORLD_SPELUNK_JUMP_APEX) {
		return apply_gravity(state, damage);
	}
	if (state->movement == WORLD_SPELUNK_CLIMBING) {
		return spend_grip_turn(state, 0, damage);
	}
	if (state->movement == WORLD_SPELUNK_HANGING) {
		return spend_rope_turn(state, 0, damage);
	}
	if (state->movement == WORLD_SPELUNK_SWIMMING) {
		return spend_swim_turn(state);
	}
	/* A piton can arrest a fall and support a jump, but it is not a safe
	 * resting ledge.  Otherwise a lone anchor turns free climbing into an
	 * unlimited-stamina route. */
	if (map_has_piton(&state->map, state->x, state->y) &&
			!map_is_solid(&state->map, state->x, state->y + 1)) {
		return WORLD_SPELUNK_EVENT_RESTED;
	}
	state->stamina = clamp_int(state->stamina +
		state->rules.rest_stamina_gain, 0, state->max_stamina);
	return WORLD_SPELUNK_EVENT_RESTED;
}

static bool apply_jump(struct world_spelunk_state *state)
{
	if (state->movement != WORLD_SPELUNK_STANDING ||
		state->stamina < state->rules.jump_stamina_cost ||
		map_is_solid(&state->map, state->x, state->y - 1)) {
		return false;
	}
	state->stamina -= state->rules.jump_stamina_cost;
	state->y--;
	state->movement = WORLD_SPELUNK_JUMP_APEX;
	state->jump_holding = false;
	state->fall_start_y = state->y;
	sync_grip(state);
	return true;
}

static enum world_spelunk_action_event apply_grip(
		struct world_spelunk_state *state)
{
	static const int dx[8] = { -1, 1, 0, 0, -1, 1, -1, 1 };
	static const int dy[8] = { 0, 0, -1, 1, 1, 1, -1, -1 };
	int current = -1;
	int start;
	int step;
	int i;
	int next = -1;

	if (state->movement == WORLD_SPELUNK_SWIMMING) {
		state->grip_target_x = -1;
		state->grip_target_y = -1;
		return WORLD_SPELUNK_EVENT_GRIP_CLEARED;
	}

	if (state->grip_target_x >= 0) {
		int current_dx = state->grip_target_x - state->x;
		int current_dy = state->grip_target_y - state->y;

		for (i = 0; i < 8; i++) {
			if (dx[i] == current_dx && dy[i] == current_dy) {
				current = i;
				break;
			}
		}
	}
	start = current < 0 ? 0 : (current + 1) % 8;
	for (step = 0; step < 8; step++) {
		i = (start + step) % 8;
		if (i == current) break;
		if (map_is_grippable(&state->map, state->x + dx[i],
				state->y + dy[i])) {
			next = i;
			break;
		}
	}
	if (next < 0) {
		state->grip_target_x = -1;
		state->grip_target_y = -1;
		return WORLD_SPELUNK_EVENT_GRIP_CLEARED;
	}
	state->grip_target_x = state->x + dx[next];
	state->grip_target_y = state->y + dy[next];
	sync_grip(state);
	return state->movement == WORLD_SPELUNK_CLIMBING ?
		WORLD_SPELUNK_EVENT_GRIPPED : WORLD_SPELUNK_EVENT_GRIP_SELECTED;
}

static enum world_spelunk_action_outcome finish_turn(
		struct world_spelunk_state *state,
		struct world_spelunk_action_report *report,
		enum world_spelunk_action_event event)
{
	report->event = event;
	report->energy_use = state->rules.action_energy;
	report->outcome = WORLD_SPELUNK_ACTION_TURN;
	return report->outcome;
}

enum world_spelunk_action_outcome world_spelunk_apply_command(
		struct world_spelunk_state *state,
		const struct world_spelunk_command *command,
		struct world_spelunk_action_report *report)
{
	struct world_spelunk_action_report local = { 0 };
	enum world_spelunk_movement movement_before;
	int stamina_before;
	int fall_start_before;
	int y_before;
	int damage = 0;
	bool accepted = false;
	enum world_spelunk_action_event event = WORLD_SPELUNK_EVENT_NONE;

	if (!report) report = &local;
	memset(report, 0, sizeof(*report));
	if (!state || !command || !world_spelunk_state_is_valid(state)) {
		return WORLD_SPELUNK_ACTION_REJECTED;
	}
	stamina_before = state->stamina;
	movement_before = state->movement;
	fall_start_before = state->fall_start_y;
	y_before = state->y;

	switch (command->kind) {
	case WORLD_SPELUNK_COMMAND_MOVE:
		event = apply_move(state, command->dx, command->dy, &accepted,
			&damage);
		break;
	case WORLD_SPELUNK_COMMAND_WAIT:
		event = apply_wait(state, &damage);
		accepted = true;
		break;
	case WORLD_SPELUNK_COMMAND_JUMP:
		accepted = apply_jump(state);
		event = accepted ? (state->movement == WORLD_SPELUNK_CLIMBING ?
			WORLD_SPELUNK_EVENT_GRIPPED : WORLD_SPELUNK_EVENT_JUMPED) :
			WORLD_SPELUNK_EVENT_NONE;
		break;
	case WORLD_SPELUNK_COMMAND_GRIP:
		event = apply_grip(state);
		report->outcome = WORLD_SPELUNK_ACTION_FREE;
		report->event = event;
		report->stamina_delta = state->stamina - stamina_before;
		return report->outcome;
	default:
		break;
	}
	if (!accepted) return WORLD_SPELUNK_ACTION_REJECTED;
	(void)finish_turn(state, report, event);
	report->damage = damage;
	if (damage > 0 && (event == WORLD_SPELUNK_EVENT_FALL_HURT ||
			event == WORLD_SPELUNK_EVENT_GRIP_EXHAUSTED ||
			event == WORLD_SPELUNK_EVENT_ROPE_EXHAUSTED)) {
		int origin_y = movement_before == WORLD_SPELUNK_FALLING ?
			fall_start_before : y_before;

		report->fall_tiles = state->y - origin_y;
	}
	report->stamina_delta = state->stamina - stamina_before;
	return report->outcome;
}

bool world_spelunk_apply_breath_turn(struct world_spelunk_state *state,
		bool air_supply_available,
		struct world_spelunk_breath_report *report)
{
	struct world_spelunk_breath_report local = { 0 };
	int breath_before;

	if (!report) report = &local;
	memset(report, 0, sizeof(*report));
	if (!world_spelunk_state_is_valid(state)) return false;
	breath_before = state->breath;
	if (!world_spelunk_is_submerged(state)) {
		state->breath = state->rules.breath_turns;
		if (state->breath != breath_before) {
			report->event = WORLD_SPELUNK_BREATH_RESTORED;
		}
	} else if (air_supply_available) {
		report->event = WORLD_SPELUNK_BREATH_SCUBA_USED;
		report->air_supply_used = 1;
	} else if (state->breath > 0) {
		state->breath--;
		report->event = WORLD_SPELUNK_BREATH_HELD;
	} else {
		report->event = WORLD_SPELUNK_BREATH_DROWNING;
		report->damage = state->rules.drowning_damage;
	}
	report->breath_delta = state->breath - breath_before;
	return world_spelunk_state_is_valid(state);
}
