/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-fishing.c
 * \brief Bounded turn-based fishing activity state and rules.
 *
 * This is an independent C adaptation of selected fishing behaviour from the
 * owner's authorised NetHook prototype (see AUTHORS.md). It preserves bounded
 * water-grid movement, evasion, exact-bite, strike, and winding while leaving
 * generated taxonomy, research, processing, crafting, and dependencies out.
 *
 */

#include "world-fishing.h"
#include "z-util.h"

#include <string.h>

static uint32_t fishing_random(struct world_fishing_runtime *runtime)
{
	uint32_t value = runtime->rng_state;

	if (!value) value = 0x9e3779b9U;
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	runtime->rng_state = value;
	return value;
}

static int fishing_random_range(struct world_fishing_runtime *runtime,
		int minimum, int maximum)
{
	uint32_t span;

	if (maximum <= minimum) return minimum;
	span = (uint32_t)(maximum - minimum + 1);
	return minimum + (int)(fishing_random(runtime) % span);
}

static int clamp_int(int value, int minimum, int maximum)
{
	if (value < minimum) return minimum;
	if (value > maximum) return maximum;
	return value;
}

static bool hooked_fish_is_valid(const struct world_fishing_runtime *runtime)
{
	return runtime && runtime->hooked_index >= 0 &&
		runtime->hooked_index < runtime->fish_count;
}

static world_fishing_kind random_fish_kind(
		struct world_fishing_runtime *runtime)
{
	int weights[WORLD_FISHING_MAX_SPECIES];
	int count = world_fishing_species_count();
	int total = 0;
	int roll;
	int i;

	for (i = 0; i < count; i++) {
		weights[i] = world_fishing_habitat_weight(runtime->habitat,
			runtime->location_danger, (world_fishing_kind)i);
	}
	for (i = 0; i < count; i++) total += weights[i];
	roll = fishing_random_range(runtime, 1, MAX(1, total));
	for (i = 0; i < count; i++) {
		roll -= weights[i];
		if (roll <= 0) return (world_fishing_kind)i;
	}
	return count ? 0 : WORLD_FISHING_KIND_NONE;
}

static void configure_fish(struct world_fishing_runtime *runtime,
		struct world_fishing_fish *fish, int index)
{
	const struct world_fishing_species *species;

	memset(fish, 0, sizeof(*fish));
	fish->kind = random_fish_kind(runtime);
	species = world_fishing_species_by_kind(fish->kind);
	if (!species) return;
	fish->glyph = species->glyph;
	fish->attr = species->attr;
	fish->depth_min = MAX(1, runtime->depth_rows * species->depth_min /
		WORLD_FISHING_DEPTH_SCALE);
	fish->depth_max = MAX(fish->depth_min,
		runtime->depth_rows * species->depth_max /
		WORLD_FISHING_DEPTH_SCALE);
	fish->run_chance = species->run_chance;
	fish->wind_needed = species->wind_needed;
	fish->bite_timeout = species->bite_timeout;
	fish->row = fishing_random_range(runtime, fish->depth_min,
		fish->depth_max);
	fish->col = fishing_random_range(runtime, 0, runtime->columns - 1);
	/* Avoid exact initial stacking without an unbounded retry loop. */
	if (index > 0 && fish->col == runtime->fish[index - 1].col &&
			fish->row == runtime->fish[index - 1].row) {
		fish->col = clamp_int(fish->col - 1, 0, runtime->columns - 1);
	}
}

void world_fishing_runtime_start(struct world_fishing_runtime *runtime,
		uint32_t seed, enum world_fishing_habitat habitat,
		int location_danger, int maximum_reach, int maximum_depth,
		const char *rig_id, const char *rig_name, struct loc origin)
{
	const struct world_fishing_rules *rules = world_fishing_rules();
	size_t rig_name_length;
	int i;

	if (!runtime) return;
	memset(runtime, 0, sizeof(*runtime));
	runtime->hooked_index = -1;
	if (!rules || !world_fishing_species_count()) return;
	runtime->active = true;
	runtime->phase = WORLD_FISHING_WAITING;
	runtime->columns = WORLD_FISHING_COLUMNS;
	runtime->depth_rows = WORLD_FISHING_DEPTH_ROWS;
	runtime->maximum_reach = clamp_int(maximum_reach,
		WORLD_FISHING_INITIAL_REACH, runtime->columns - 1);
	runtime->rod_reach = WORLD_FISHING_INITIAL_REACH;
	runtime->maximum_depth = clamp_int(maximum_depth, 1,
		runtime->depth_rows);
	runtime->habitat = habitat > WORLD_FISHING_HABITAT_NONE &&
		habitat < WORLD_FISHING_HABITAT_MAX ? habitat :
		WORLD_FISHING_HABITAT_NONE;
	runtime->location_danger = clamp_int(location_danger, 0, INT16_MAX);
	if (!rig_id || !world_id_is_valid(rig_id)) rig_id = "village-rig";
	my_strcpy(runtime->rig_id, rig_id, sizeof(runtime->rig_id));
	if (!rig_name || !rig_name[0]) rig_name = "Village rig";
	rig_name_length = MIN(strlen(rig_name), sizeof(runtime->rig_name) - 1);
	memcpy(runtime->rig_name, rig_name, rig_name_length);
	runtime->rig_name[rig_name_length] = '\0';
	runtime->fish_move_chance_percent = rules->move_chance;
	runtime->fish_depth_move_chance_percent = rules->depth_move_chance;
	runtime->moving_hook_evasion_chance_percent = rules->evasion_chance;
	runtime->moving_hook_evasion_radius = rules->evasion_radius;
	runtime->patience_turns = rules->patience_turns;
	runtime->attraction_horizontal = rules->attraction_horizontal;
	runtime->attraction_vertical = rules->attraction_vertical;
	runtime->origin = origin;
	runtime->rng_state = seed ? seed : 0x9e3779b9U;
	runtime->fish_count = fishing_random_range(runtime,
		rules->minimum_fish, rules->maximum_fish);
	for (i = 0; i < runtime->fish_count; i++) {
		configure_fish(runtime, &runtime->fish[i], i);
	}
}

void world_fishing_runtime_cancel(struct world_fishing_runtime *runtime)
{
	if (!runtime) return;
	memset(runtime, 0, sizeof(*runtime));
	runtime->hooked_index = -1;
}

bool world_fishing_runtime_is_valid(const struct world_fishing_runtime *runtime)
{
	int i;

	if (!runtime || !runtime->active ||
			runtime->phase < WORLD_FISHING_WAITING ||
			runtime->phase > WORLD_FISHING_WINDING ||
			runtime->columns != WORLD_FISHING_COLUMNS ||
			runtime->depth_rows != WORLD_FISHING_DEPTH_ROWS ||
			runtime->maximum_reach < WORLD_FISHING_INITIAL_REACH ||
			runtime->maximum_reach >= runtime->columns ||
			runtime->rod_reach < WORLD_FISHING_INITIAL_REACH ||
			runtime->rod_reach > runtime->maximum_reach ||
			runtime->maximum_depth < 1 ||
			runtime->maximum_depth > runtime->depth_rows ||
			runtime->depth < 0 || runtime->depth > runtime->maximum_depth ||
			runtime->habitat <= WORLD_FISHING_HABITAT_NONE ||
			runtime->habitat >= WORLD_FISHING_HABITAT_MAX ||
			runtime->location_danger < 0 || !runtime->rig_name[0] ||
			runtime->fish_move_chance_percent < 0 ||
			runtime->fish_move_chance_percent > 100 ||
			runtime->fish_depth_move_chance_percent < 0 ||
			runtime->fish_depth_move_chance_percent > 100 ||
			runtime->moving_hook_evasion_chance_percent < 0 ||
			runtime->moving_hook_evasion_chance_percent > 100 ||
			runtime->moving_hook_evasion_radius < 0 ||
			runtime->moving_hook_evasion_radius >= runtime->columns ||
			runtime->patience_turns < 1 || runtime->wait_counter < 0 ||
			runtime->wait_counter > runtime->patience_turns ||
			runtime->attraction_horizontal < 1 ||
			runtime->attraction_horizontal >= runtime->columns ||
			runtime->attraction_vertical < 1 ||
			runtime->attraction_vertical > runtime->depth_rows ||
			runtime->fish_count < 0 ||
			runtime->fish_count > WORLD_FISHING_MAX_FISH) {
		return false;
	}
	for (i = 0; i < runtime->fish_count; i++) {
		const struct world_fishing_fish *fish = &runtime->fish[i];

		if (!world_fishing_kind_is_valid(fish->kind) || fish->col < 0 ||
				fish->col >= runtime->columns || fish->depth_min < 1 ||
				fish->depth_max > runtime->depth_rows ||
				fish->depth_min > fish->depth_max ||
				fish->row < fish->depth_min || fish->row > fish->depth_max ||
				fish->run_chance < 0 || fish->run_chance > 95 ||
				fish->wind_needed < 1 || fish->bite_timeout < 1) {
			return false;
		}
	}
	if (runtime->phase == WORLD_FISHING_WAITING) {
		return runtime->hooked_index == -1;
	}
	return hooked_fish_is_valid(runtime);
}

int world_fishing_hook_col(const struct world_fishing_runtime *runtime)
{
	if (!runtime || runtime->columns <= 0) return 0;
	return MAX(0, runtime->columns - 1 - runtime->rod_reach);
}

static void remove_fish(struct world_fishing_runtime *runtime, int index)
{
	int i;

	if (!runtime || index < 0 || index >= runtime->fish_count) return;
	for (i = index; i + 1 < runtime->fish_count; i++) {
		runtime->fish[i] = runtime->fish[i + 1];
	}
	runtime->fish_count--;
	if (runtime->fish_count >= 0 &&
			runtime->fish_count < WORLD_FISHING_MAX_FISH) {
		memset(&runtime->fish[runtime->fish_count], 0,
			sizeof(runtime->fish[runtime->fish_count]));
	}
	runtime->hooked_index = -1;
}

static enum world_fishing_event tick_fish(
		struct world_fishing_runtime *runtime, bool hook_moved,
		bool allow_bites)
{
	int hook_col = world_fishing_hook_col(runtime);
	int i;

	for (i = 0; i < runtime->fish_count; i++) {
		struct world_fishing_fish *fish = &runtime->fish[i];
		bool near_hook;
		bool evades;

		if (fish->biting || i == runtime->hooked_index) continue;
		near_hook = hook_moved && runtime->depth > 0 &&
			ABS(fish->col - hook_col) <=
				runtime->moving_hook_evasion_radius &&
			ABS(fish->row - runtime->depth) <= 1;
		evades = near_hook && fishing_random_range(runtime, 0, 99) <
			runtime->moving_hook_evasion_chance_percent;
		if (evades) {
			fish->col += fish->col <= hook_col ? -1 : 1;
			if (fish->row == runtime->depth) {
				fish->row += fish->row < runtime->depth_rows ? 1 : -1;
			}
		} else if (fishing_random_range(runtime, 0, 99) <
				runtime->fish_move_chance_percent) {
			fish->col += fishing_random_range(runtime, -1, 1);
		}
		fish->col = clamp_int(fish->col, 0, runtime->columns - 1);
		if (!evades && fishing_random_range(runtime, 0, 99) <
				runtime->fish_depth_move_chance_percent) {
			fish->row += fishing_random_range(runtime, -1, 1);
		}
		fish->row = clamp_int(fish->row, fish->depth_min, fish->depth_max);
	}
	/* Only a stationary wait permits a bite.  Fish move first, so the player
	 * watches them enter the exact hook cell rather than sweeping the hook. */
	if (allow_bites && runtime->phase == WORLD_FISHING_WAITING &&
			runtime->depth > 0) {
		for (i = 0; i < runtime->fish_count; i++) {
			struct world_fishing_fish *fish = &runtime->fish[i];

			if (fish->col == hook_col && fish->row == runtime->depth) {
				fish->biting = true;
				runtime->hooked_index = i;
				runtime->bite_counter = 0;
				runtime->phase = WORLD_FISHING_BITE;
				return WORLD_FISHING_EVENT_BITE;
			}
		}
	}
	if (runtime->phase == WORLD_FISHING_WINDING &&
			hooked_fish_is_valid(runtime) &&
			fishing_random_range(runtime, 0, 99) <
				runtime->fish[runtime->hooked_index].run_chance) {
		runtime->wind_turns = MAX(0, runtime->wind_turns - 1);
		return WORLD_FISHING_EVENT_PULL;
	}
	return WORLD_FISHING_EVENT_NONE;
}

static void clear_report(struct world_fishing_report *report)
{
	if (!report) return;
	memset(report, 0, sizeof(*report));
	report->landed_kind = WORLD_FISHING_KIND_NONE;
}

/** End an otherwise unbounded quiet spell by attracting the nearest
 * depth-compatible fish toward a stationary hook.  The fish still has to
 * cross visible water and can bite only after occupying the exact hook cell. */
static enum world_fishing_event patience_attract(
		struct world_fishing_runtime *runtime)
{
	int hook_col = world_fishing_hook_col(runtime);
	int best = -1;
	int best_distance = INT_MAX;
	int i;

	if (!runtime || runtime->depth <= 0) return WORLD_FISHING_EVENT_NONE;
	for (i = 0; i < runtime->fish_count; i++) {
		struct world_fishing_fish *fish = &runtime->fish[i];
		int distance;

		if (fish->biting || runtime->depth < fish->depth_min ||
				runtime->depth > fish->depth_max) {
			continue;
		}
		distance = ABS(fish->col - hook_col) +
			ABS(fish->row - runtime->depth);
		if (distance < best_distance) {
			best = i;
			best_distance = distance;
		}
	}
	if (best < 0) return WORLD_FISHING_EVENT_NONE;
	if (runtime->fish[best].col < hook_col) {
		runtime->fish[best].col = MIN(hook_col,
			runtime->fish[best].col + runtime->attraction_horizontal);
	} else if (runtime->fish[best].col > hook_col) {
		runtime->fish[best].col = MAX(hook_col,
			runtime->fish[best].col - runtime->attraction_horizontal);
	}
	if (runtime->fish[best].row < runtime->depth) {
		runtime->fish[best].row = MIN(runtime->depth,
			runtime->fish[best].row + runtime->attraction_vertical);
	} else if (runtime->fish[best].row > runtime->depth) {
		runtime->fish[best].row = MAX(runtime->depth,
			runtime->fish[best].row - runtime->attraction_vertical);
	}
	if (runtime->fish[best].col != hook_col ||
			runtime->fish[best].row != runtime->depth) {
		return WORLD_FISHING_EVENT_APPROACH;
	}
	runtime->fish[best].biting = true;
	runtime->hooked_index = best;
	runtime->bite_counter = 0;
	runtime->wait_counter = 0;
	runtime->phase = WORLD_FISHING_BITE;
	return WORLD_FISHING_EVENT_BITE;
}

bool world_fishing_apply(struct world_fishing_runtime *runtime,
		enum world_fishing_action action, struct world_fishing_report *report)
{
	enum world_fishing_event tick_event;
	bool moved = false;

	clear_report(report);
	if (!runtime || !report || !world_fishing_runtime_is_valid(runtime) ||
			action < WORLD_FISHING_EXTEND || action > WORLD_FISHING_CANCEL) {
		if (report) report->event = WORLD_FISHING_EVENT_BLOCKED;
		return false;
	}
	if (action == WORLD_FISHING_CANCEL) {
		world_fishing_runtime_cancel(runtime);
		report->event = WORLD_FISHING_EVENT_CANCELLED;
		return true;
	}
	if (runtime->phase == WORLD_FISHING_WINDING) {
		if (action != WORLD_FISHING_REEL) {
			report->event = WORLD_FISHING_EVENT_BLOCKED;
			return false;
		}
		tick_event = tick_fish(runtime, false, false);
		runtime->wind_turns++;
		report->consumes_turn = true;
		if (runtime->wind_turns >= runtime->wind_needed) {
			int hooked = runtime->hooked_index;

			report->landed_kind = runtime->fish[hooked].kind;
			report->landed_depth = runtime->depth;
			remove_fish(runtime, hooked);
			runtime->phase = WORLD_FISHING_WAITING;
			runtime->depth = 0;
			runtime->wind_turns = 0;
			runtime->wind_needed = 0;
			report->event = WORLD_FISHING_EVENT_LANDED;
		} else {
			report->event = tick_event == WORLD_FISHING_EVENT_PULL ?
				WORLD_FISHING_EVENT_PULL : WORLD_FISHING_EVENT_MOVED;
		}
		return true;
	}
	if (runtime->phase == WORLD_FISHING_BITE) {
		if (action == WORLD_FISHING_STRIKE && hooked_fish_is_valid(runtime) &&
				runtime->fish[runtime->hooked_index].col ==
					world_fishing_hook_col(runtime) &&
				runtime->fish[runtime->hooked_index].row == runtime->depth) {
			runtime->phase = WORLD_FISHING_WINDING;
			runtime->bite_counter = 0;
			runtime->wind_turns = 0;
			runtime->wind_needed =
				runtime->fish[runtime->hooked_index].wind_needed;
			(void)tick_fish(runtime, false, false);
			report->event = WORLD_FISHING_EVENT_STRUCK;
			report->consumes_turn = true;
			return true;
		}
		if (action == WORLD_FISHING_WAIT && hooked_fish_is_valid(runtime)) {
			int timeout = runtime->fish[runtime->hooked_index].bite_timeout;

			runtime->bite_counter++;
			(void)tick_fish(runtime, false, false);
			report->consumes_turn = true;
			if (runtime->bite_counter > timeout) {
				remove_fish(runtime, runtime->hooked_index);
				runtime->phase = WORLD_FISHING_WAITING;
				runtime->bite_counter = 0;
				report->event = WORLD_FISHING_EVENT_ESCAPED;
			} else {
				report->event = WORLD_FISHING_EVENT_WAITED;
			}
			return true;
		}
		report->event = WORLD_FISHING_EVENT_BLOCKED;
		return false;
	}
	switch (action) {
	case WORLD_FISHING_EXTEND:
		if (runtime->rod_reach < runtime->maximum_reach) {
			runtime->rod_reach++;
			moved = true;
		}
		break;
	case WORLD_FISHING_RETRACT:
		if (runtime->rod_reach > WORLD_FISHING_INITIAL_REACH) {
			runtime->rod_reach--;
			moved = true;
		}
		break;
	case WORLD_FISHING_LOWER:
		if (runtime->depth < runtime->maximum_depth) {
			runtime->depth++;
			moved = true;
		}
		break;
	case WORLD_FISHING_RAISE:
		if (runtime->depth > 0) {
			runtime->depth--;
			moved = true;
		}
		break;
	case WORLD_FISHING_WAIT:
		tick_event = tick_fish(runtime, false, true);
		if (tick_event != WORLD_FISHING_EVENT_BITE) {
			runtime->wait_counter = MIN(runtime->patience_turns,
				runtime->wait_counter + 1);
			if (runtime->wait_counter >= runtime->patience_turns) {
				tick_event = patience_attract(runtime);
			}
		} else {
			runtime->wait_counter = 0;
		}
		report->consumes_turn = true;
		report->event = tick_event == WORLD_FISHING_EVENT_BITE ?
			WORLD_FISHING_EVENT_BITE :
			tick_event == WORLD_FISHING_EVENT_APPROACH ?
			WORLD_FISHING_EVENT_APPROACH : WORLD_FISHING_EVENT_WAITED;
		return true;
	case WORLD_FISHING_STRIKE:
	case WORLD_FISHING_REEL:
	case WORLD_FISHING_CANCEL:
	default:
		break;
	}
	if (!moved) {
		report->event = WORLD_FISHING_EVENT_BLOCKED;
		return false;
	}
	runtime->wait_counter = 0;
	(void)tick_fish(runtime, true, false);
	report->event = WORLD_FISHING_EVENT_MOVED;
	report->consumes_turn = true;
	return true;
}

const char *world_fishing_phase_name(enum world_fishing_phase phase)
{
	switch (phase) {
	case WORLD_FISHING_WAITING:
		return "POSITIONING";
	case WORLD_FISHING_BITE:
		return "BITE";
	case WORLD_FISHING_WINDING:
		return "REELING";
	case WORLD_FISHING_INACTIVE:
	default:
		return "INACTIVE";
	}
}

bool world_fishing_kind_is_valid(world_fishing_kind kind)
{
	return world_fishing_species_by_kind(kind) != NULL;
}

const char *world_fishing_kind_name(world_fishing_kind kind)
{
	const struct world_fishing_species *species =
		world_fishing_species_by_kind(kind);

	return species ? species->name : "Unknown Fish";
}

char world_fishing_kind_glyph(world_fishing_kind kind)
{
	const struct world_fishing_species *species =
		world_fishing_species_by_kind(kind);

	return species ? species->glyph : '?';
}

uint32_t world_fishing_kind_larder_value(world_fishing_kind kind)
{
	const struct world_fishing_species *species =
		world_fishing_species_by_kind(kind);

	return species ? species->larder_value : 0;
}

int world_fishing_kind_catch_experience(world_fishing_kind kind)
{
	const struct world_fishing_species *species =
		world_fishing_species_by_kind(kind);

	return species ? species->catch_experience : 0;
}

int world_fishing_kind_donation_experience(world_fishing_kind kind)
{
	const struct world_fishing_species *species =
		world_fishing_species_by_kind(kind);

	return species ? species->donation_experience : 0;
}

int world_fishing_habitat_weight(enum world_fishing_habitat habitat,
		int location_danger, world_fishing_kind kind)
{
	const struct world_fishing_species *species =
		world_fishing_species_by_kind(kind);
	int danger;
	int weight;

	if (habitat <= WORLD_FISHING_HABITAT_NONE ||
			habitat >= WORLD_FISHING_HABITAT_MAX || !species) {
		return 0;
	}
	danger = clamp_int(location_danger, 0, INT16_MAX);
	weight = species->habitat_weights[habitat];
	/* Local danger gently favours tougher, more valuable catches without
	 * replacing the authored habitat identity. */
	weight += danger * species->danger_weight;
	return weight;
}
