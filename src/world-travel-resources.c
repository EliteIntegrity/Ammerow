/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-travel-resources.c
 * \brief Bounded player-resource effects for aggregate route travel.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "init.h"
#include "obj-gear.h"
#include "obj-tval.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "world-travel-resources.h"

/** Count period boundaries in the half-open interval [start, end). */
static uint64_t count_period_boundaries(int32_t start, int32_t end,
		uint32_t period)
{
	uint64_t first, after_last;

	assert(start >= 0);
	assert(end >= start);
	assert(period > 0);
	if (start == end) return 0;
	first = ((uint64_t)start + period - 1) / period;
	after_last = ((uint64_t)end + period - 1) / period;
	return after_last - first;
}

/** Count the nightly ten-turn world ticks in [start, end). */
static uint64_t count_town_night_ticks(int32_t start, int32_t end)
{
	uint64_t first_step, after_last_step, cycle, night_start;
	uint64_t complete, remainder, before_first, before_after_last;

	assert(z_info->day_length > 0);
	first_step = ((uint64_t)start + 9) / 10;
	after_last_step = ((uint64_t)end + 9) / 10;
	cycle = z_info->day_length;
	night_start = (cycle + 1) / 2;

	complete = first_step / cycle;
	remainder = first_step % cycle;
	before_first = complete * (cycle - night_start);
	if (remainder > night_start) before_first += remainder - night_start;

	complete = after_last_step / cycle;
	remainder = after_last_step % cycle;
	before_after_last = complete * (cycle - night_start);
	if (remainder > night_start) {
		before_after_last += remainder - night_start;
	}
	return before_after_last - before_first;
}

/** Project ordinary digestion without replaying local world processing. */
static int project_food(struct player *p, int32_t departure_turn,
		int32_t arrival_turn)
{
	uint64_t world_ticks = count_period_boundaries(departure_turn,
		arrival_turn, 10);
	uint64_t full_ticks = 0;
	int64_t food = p->timed[TMD_FOOD];
	int64_t full_rate = 5000 / z_info->food_value;
	int64_t normal_rate = turn_energy(p->state.speed);
	int32_t normal_start = departure_turn;
	bool normal_phase = food <= PY_FOOD_FULL;

	normal_rate = (normal_rate * 100) / z_info->food_value;
	if (player_of_has(p, OF_REGEN)) normal_rate *= 2;
	if (player_of_has(p, OF_SLOW_DIGEST)) normal_rate /= 2;
	if (normal_rate < 1) normal_rate = 1;

	if (food > PY_FOOD_FULL && full_rate > 0 && world_ticks > 0) {
		uint64_t needed = (uint64_t)((food - PY_FOOD_FULL + full_rate - 1) /
			full_rate);

		full_ticks = MIN(world_ticks, needed);
		food -= (int64_t)full_ticks * full_rate;
		if (full_ticks < world_ticks) {
			uint64_t first_tick = ((uint64_t)departure_turn + 9) / 10 * 10;
			uint64_t after_full = first_tick + full_ticks * 10;

			normal_start = (int32_t)after_full;
			normal_phase = true;
		}
	}
	if (normal_phase) {
		uint64_t normal_ticks = count_period_boundaries(normal_start,
			arrival_turn, 100);

		food -= (int64_t)normal_ticks * normal_rate;
	}
	return (int)MAX(food, 1);
}

enum world_travel_resources_result world_travel_resources_stage(
		struct world_travel_resources *resources, struct player *p,
		int32_t departure_turn, int32_t arrival_turn)
{
	struct world_travel_resources staged = { 0 };
	uint64_t fuel_ticks;

	if (!resources || !p || !p->upkeep || !z_info ||
			z_info->food_value == 0 || z_info->day_length == 0 ||
			departure_turn < 0 || arrival_turn < departure_turn) {
		return WORLD_TRAVEL_RESOURCES_INVALID;
	}
	if (resources->staged) return WORLD_TRAVEL_RESOURCES_ALREADY_STAGED;

	staged.departure_turn = departure_turn;
	staged.arrival_turn = arrival_turn;
	staged.source_depth = p->depth;
	staged.food_before = p->timed[TMD_FOOD];
	staged.food_after = project_food(p, departure_turn, arrival_turn);
	if (staged.food_after <= PY_FOOD_FAINT &&
			(staged.food_after < staged.food_before ||
			 count_period_boundaries(departure_turn, arrival_turn, 10) > 0)) {
		return WORLD_TRAVEL_RESOURCES_UNSAFE_HUNGER;
	}

	staged.light = equipped_item_by_slot_name(p, "light");
	if (staged.light) {
		staged.light_before = staged.light->timeout;
		staged.light_after = staged.light_before;
	}
	if (staged.light && tval_is_light(staged.light) &&
			!of_has(staged.light->flags, OF_NO_FUEL) &&
			staged.light_before > 0) {
		fuel_ticks = (p->depth == 0) ?
			count_town_night_ticks(departure_turn, arrival_turn) :
			count_period_boundaries(departure_turn, arrival_turn, 10);
		if (fuel_ticks >= (uint64_t)staged.light_before &&
				!p->timed[TMD_BLIND]) {
			return WORLD_TRAVEL_RESOURCES_UNSAFE_LIGHT;
		}
		staged.light_after = (fuel_ticks >=
				(uint64_t)staged.light_before) ? 1 :
			staged.light_before - (int)fuel_ticks;
	}

	staged.staged = true;
	*resources = staged;
	return WORLD_TRAVEL_RESOURCES_OK;
}

bool world_travel_resources_match(
		const struct world_travel_resources *resources, struct player *p)
{
	struct world_travel_resources projected = { 0 };

	if (!resources || !resources->staged || !p || !p->upkeep ||
			p->depth != resources->source_depth ||
			p->timed[TMD_FOOD] != resources->food_before ||
			equipped_item_by_slot_name(p, "light") != resources->light) {
		return false;
	}
	if (resources->light &&
			resources->light->timeout != resources->light_before) {
		return false;
	}
	if (world_travel_resources_stage(&projected, p,
			resources->departure_turn, resources->arrival_turn) !=
			WORLD_TRAVEL_RESOURCES_OK) {
		return false;
	}
	return projected.food_after == resources->food_after &&
		projected.light == resources->light &&
		projected.light_after == resources->light_after;
}

void world_travel_resources_commit(
		const struct world_travel_resources *resources, struct player *p)
{
	assert(resources && resources->staged && p && p->upkeep);
	assert(p->timed[TMD_FOOD] == resources->food_before);
	assert(equipped_item_by_slot_name(p, "light") == resources->light);
	assert(!resources->light ||
		resources->light->timeout == resources->light_before);
	p->timed[TMD_FOOD] = (int16_t)resources->food_after;
	if (resources->light) {
		resources->light->timeout = (int16_t)resources->light_after;
	}
	p->upkeep->update |= (PU_BONUS | PU_TORCH);
	p->upkeep->redraw |= (PR_STATUS | PR_EQUIP);
}
