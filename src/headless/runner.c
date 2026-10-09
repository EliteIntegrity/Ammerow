/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/runner.c
 * \brief Deterministic bots, transcripts, reports, and off-screen captures.
 */

#include "angband.h"
#include "cmd-fishing.h"
#include "cmd-larder.h"
#include "cmd-spelunking.h"
#include "effects.h"
#include "game-world.h"
#include "generate.h"
#include "headless/fall-bot.h"
#include "headless/loop-bot.h"
#include "headless/runner.h"
#include "init.h"
#include "main.h"
#include "mon-make.h"
#include "mon-util.h"
#include "obj-gear.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-resource.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "store.h"
#include "ui-command.h"
#include "ui-help.h"
#include "ui-context.h"
#include "ui-birth-screen.h"
#include "ui-menu.h"
#include "ui-options.h"
#include "ui-output.h"
#include "ui-player.h"
#include "ui-relic-broker.h"
#include "ui-store-screen.h"
#include "world-entry.h"
#include "world-larder.h"
#include "world-larder-data.h"
#include "world-location.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-traversal-proof.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-visibility.h"
#include "world-story-data.h"
#include "world-turn.h"

#include <stdint.h>

struct headless_output {
	ang_file *report;
	ang_file *replay;
	char initial_capture[HEADLESS_SCENARIO_PATH];
	char final_capture[HEADLESS_SCENARIO_PATH];
	char frame_directory[HEADLESS_SCENARIO_PATH];
	unsigned int step;
};

static bool runner_error(char *error, size_t error_size, const char *message,
		const char *value)
{
	if (error && error_size) {
		if (value) {
			strnfmt(error, error_size, "%s '%s'", message, value);
		} else {
			my_strcpy(error, message, error_size);
		}
	}
	return false;
}

static bool give_scenario_item(const char *definition, uint8_t quantity,
		bool equip, char *error, size_t error_size)
{
	char buffer[HEADLESS_SCENARIO_TEXT];
	char *name;
	struct object_kind *kind;
	struct object *object;
	int tval;
	int sval;

	if (!definition || !quantity || strlen(definition) >= sizeof(buffer)) {
		return runner_error(error, error_size,
			"invalid scenario inventory item", definition);
	}
	my_strcpy(buffer, definition, sizeof(buffer));
	name = strchr(buffer, ':');
	if (!name || name == buffer || !name[1]) {
		return runner_error(error, error_size,
			"invalid scenario inventory item", definition);
	}
	*name++ = '\0';
	tval = tval_find_idx(buffer);
	sval = tval > 0 ? lookup_sval(tval, name) : -1;
	kind = sval >= 0 ? lookup_kind(tval, sval) : NULL;
	if (!kind) {
		return runner_error(error, error_size,
			"unknown scenario inventory item", definition);
	}
	object = object_new();
	object_prep(object, kind, player ? player->depth : 0, AVERAGE);
	object->number = quantity;
	object->known = object_new();
	object_copy(object->known, object);
	object->known->known = NULL;
	inven_carry(player, object, !equip, false);
	if (equip) {
		int slot = wield_slot(object);

		if (slot < 0) {
			return runner_error(error, error_size,
				"scenario item cannot be equipped", definition);
		}
		inven_wield(object, slot);
		if (!object_is_equipped(player->body, object)) {
			return runner_error(error, error_size,
				"could not equip scenario item", definition);
		}
	}
	return true;
}

/** Put a development capture at the first certified piton stance by replaying
 * the generator's exact witness prefix through the ordinary movement rules.
 * This deliberately stops before placing infrastructure: the scripted trailer
 * actions still have to hammer the piton and deploy the rope for themselves. */
static bool stage_first_rope_anchor(
		const struct world_spelunk_generated_layout *generated,
		char *error, size_t error_size)
{
	struct world_spelunk_runtime *runtime = player ? player->spelunking : NULL;
	struct world_spelunk_state before;
	size_t i;

	if (!runtime || !generated || !generated->witness_steps ||
			!generated->witness_step_count) {
		return runner_error(error, error_size,
			"generated cave has no certified rope anchor", NULL);
	}
	before = runtime->state;
	if (!generated->route_station_count ||
			world_spelunk_runtime_actor_at(runtime,
				generated->route_stations[0].x,
				generated->route_stations[0].y)) {
		return runner_error(error, error_size,
			"generated cave has no safe certified route start", NULL);
	}
	if (!world_spelunk_player_sync_resources(player)) {
		runtime->state = before;
		return runner_error(error, error_size,
			"could not synchronize route-start resources", NULL);
	}
	/* A portal may enter beside the generator's formal proof station.  Scenario
	 * setup may relocate there because that exact tile has already passed the
	 * publication proof as a safe standing start. */
	runtime->state.x = generated->route_stations[0].x;
	runtime->state.y = generated->route_stations[0].y;
	runtime->state.movement = WORLD_SPELUNK_STANDING;
	runtime->state.fall_start_y = runtime->state.y;
	runtime->state.grip_target_x = -1;
	runtime->state.grip_target_y = -1;
	runtime->state.jump_holding = false;
	if (!world_spelunk_runtime_is_valid(runtime) ||
			!world_spelunk_is_grounded(&runtime->state)) {
		runtime->state = before;
		return runner_error(error, error_size,
			"certified cave route start is not grounded", NULL);
	}
	for (i = 0; i < generated->witness_step_count; i++) {
		const struct world_spelunk_witness_step *step =
			&generated->witness_steps[i];

		if (step->kind == WORLD_SPELUNK_WITNESS_PLACE_PITON) {
			if (runtime->state.x != step->expected_x ||
					runtime->state.y != step->expected_y ||
					world_spelunk_runtime_actor_at(runtime,
						step->expected_x, step->expected_y) ||
					!world_spelunk_runtime_is_valid(runtime)) {
				runtime->state = before;
				return runner_error(error, error_size,
					"certified rope anchor is not safely occupiable", NULL);
			}
			/* Scenario staging is not elapsed play.  Start the photographed
			 * action with the real character's current resource values. */
			if (!world_spelunk_player_sync_resources(player)) {
				runtime->state = before;
				return runner_error(error, error_size,
					"could not synchronize rope-anchor resources", NULL);
			}
			world_spelunk_visibility_follow_player(runtime);
			event_signal_point(EVENT_MAP, -1, -1);
			return true;
		}
		if (step->kind == WORLD_SPELUNK_WITNESS_CHECKPOINT) {
			if (runtime->state.x != step->expected_x ||
					runtime->state.y != step->expected_y) {
				runtime->state = before;
				return runner_error(error, error_size,
					"certified route checkpoint diverged", NULL);
			}
			continue;
		}
		if (step->kind != WORLD_SPELUNK_WITNESS_COMMAND) {
			runtime->state = before;
			return runner_error(error, error_size,
				"certified route needs infrastructure before its first piton",
				NULL);
		}
		{
			struct world_spelunk_action_report report;
			enum world_spelunk_action_outcome outcome =
				world_spelunk_apply_command(&runtime->state, &step->command,
					&report);

			if (outcome == WORLD_SPELUNK_ACTION_REJECTED || report.damage > 0 ||
					runtime->state.x != step->expected_x ||
					runtime->state.y != step->expected_y) {
				runtime->state = before;
				return runner_error(error, error_size,
					"certified route diverged before its first piton", NULL);
			}
		}
	}
	runtime->state = before;
	return runner_error(error, error_size,
		"generated cave route contains no piton", NULL);
}

/** Select the lip of the longest certified shaft which descends to the right.
 * The following scripted south-east peek can therefore remain semantic and
 * stable when procedural cave coordinates move. */
static bool resolve_downward_peek_right(
		const struct world_spelunk_generated_layout *generated,
		struct loc *target, char *error, size_t error_size)
{
	int best_drop = -1;
	uint16_t i;

	if (!generated || !target || !generated->cells) {
		return runner_error(error, error_size,
			"generated cave has no downward-peek anchor", NULL);
	}
	for (i = 0; i < generated->route_segment_count; i++) {
		const struct world_spelunk_generated_route_segment *segment =
			&generated->route_segments[i];
		const struct world_spelunk_generated_route_station *from =
			&generated->route_stations[segment->from_station];
		const struct world_spelunk_generated_route_station *to =
			&generated->route_stations[segment->to_station];
		int lip_x = segment->shaft_x - 1;
		int drop = to->y - from->y;
		size_t lip;
		size_t support;
		size_t shaft;

		if (drop <= best_drop || segment->shaft_x <= from->x ||
				segment->shaft_x <= to->x || lip_x < 0 || from->y < 0 ||
				from->y + 1 >= generated->height ||
				segment->shaft_x >= generated->width) {
			continue;
		}
		lip = (size_t)from->y * (size_t)generated->width +
			(size_t)lip_x;
		support = (size_t)(from->y + 1) * (size_t)generated->width +
			(size_t)lip_x;
		shaft = (size_t)from->y * (size_t)generated->width +
			(size_t)segment->shaft_x;
		if (generated->cells[lip] != WORLD_SPELUNK_AIR ||
				generated->cells[support] != WORLD_SPELUNK_ROCK ||
				generated->cells[shaft] != WORLD_SPELUNK_AIR) {
			continue;
		}
		*target = loc(lip_x, from->y);
		best_drop = drop;
	}
	if (best_drop < 0) {
		return runner_error(error, error_size,
			"generated cave has no right-hand downward-peek lip", NULL);
	}
	return true;
}

static bool relocate_start_position(const struct headless_scenario *scenario,
		char *error, size_t error_size)
{
	struct loc target = loc(scenario->start_x, scenario->start_y);
	bool has_target = scenario->has_start_position;

	if (streq(scenario->start_anchor, "open-floor")) {
		int best_score = -1;
		int y;

		if (world_spelunk_player_is_active(player) || !cave) {
			return runner_error(error, error_size,
				"open-floor anchor requires a top-down location",
				scenario->location);
		}
		for (y = 1; y < cave->height - 1; y++) {
			int x;

			for (x = 1; x < cave->width - 1; x++) {
				struct loc candidate = loc(x, y);
				int score = 0;
				int dy;

				if (!square_isempty(cave, candidate)) continue;
				for (dy = -4; dy <= 4; dy++) {
					int dx;

					for (dx = -4; dx <= 4; dx++) {
						struct loc nearby = loc(x + dx, y + dy);

						if (square_in_bounds(cave, nearby) &&
								square_ispassable(cave, nearby)) {
							score++;
						}
					}
				}
				if (score > best_score) {
					best_score = score;
					target = candidate;
				}
			}
		}
		if (best_score < 0) {
			return runner_error(error, error_size,
				"could not resolve open-floor start anchor",
				scenario->location);
		}
		has_target = true;
	} else if (scenario->start_anchor[0]) {
		const struct world_spelunk_system *system = player ?
			player->spelunking_system : NULL;
		const struct world_spelunk_system_node *node = system ?
			world_spelunk_system_node_by_id(system, system->active_node_id) : NULL;
		struct world_spelunk_generated_layout generated = { 0 };

		if (!node || !node->runtime ||
				world_spelunk_reconstruct_node_candidate(system, node->id,
					node->runtime->state.rules.action_energy, &generated) !=
				WORLD_SPELUNK_RECONSTRUCTION_OK) {
			return runner_error(error, error_size,
				"could not resolve generated cave start anchor",
				scenario->start_anchor);
		}
		if (streq(scenario->start_anchor, "first-rope-anchor")) {
			bool staged = stage_first_rope_anchor(&generated, error,
				error_size);

			world_spelunk_generated_layout_dispose(&generated);
			return staged;
		} else if (streq(scenario->start_anchor, "downward-peek-right")) {
			if (!resolve_downward_peek_right(&generated, &target, error,
					error_size)) {
				world_spelunk_generated_layout_dispose(&generated);
				return false;
			}
			has_target = true;
		} else {
			target = loc(generated.fishing_stance_x,
				generated.fishing_stance_y);
			has_target = true;
		}
		world_spelunk_generated_layout_dispose(&generated);
	}
	if (!has_target) return true;
	if (world_spelunk_player_is_active(player)) {
		struct world_spelunk_runtime *runtime = player->spelunking;
		struct world_spelunk_state before = runtime->state;
		size_t index;

		if (target.x < 0 || target.y < 0 ||
				target.x >= runtime->state.map.width ||
				target.y >= runtime->state.map.height ||
				world_spelunk_runtime_actor_at(runtime, target.x, target.y)) {
			return runner_error(error, error_size,
				"side-view start position is not available", scenario->name);
		}
		index = (size_t)target.y * (size_t)runtime->state.map.width +
			(size_t)target.x;
		if (runtime->cells[index] != WORLD_SPELUNK_AIR) {
			return runner_error(error, error_size,
				"side-view start position is not open air", scenario->name);
		}
		runtime->state.x = target.x;
		runtime->state.y = target.y;
		runtime->state.movement = WORLD_SPELUNK_STANDING;
		runtime->state.fall_start_y = target.y;
		runtime->state.grip_target_x = -1;
		runtime->state.grip_target_y = -1;
		runtime->state.jump_holding = false;
		if (!world_spelunk_runtime_is_valid(runtime) ||
				!world_spelunk_is_grounded(&runtime->state)) {
			runtime->state = before;
			return runner_error(error, error_size,
				"side-view start position is not grounded", scenario->name);
		}
		world_spelunk_visibility_follow_player(runtime);
		/* Relocation mutates the runtime outside the command loop.  Keep the
		 * frontend's cached presentation in lockstep before an initial capture. */
		event_signal_point(EVENT_MAP, -1, -1);
		return true;
	}
	if (!cave || !square_in_bounds(cave, target) ||
			(!loc_eq(target, player->grid) && !square_isempty(cave, target))) {
		return runner_error(error, error_size,
			"top-down start position is not safely occupiable", scenario->name);
	}
	if (!loc_eq(target, player->grid)) {
		square_set_mon(cave, player->grid, 0);
		player_place(cave, player, target);
		handle_stuff(player);
		Term_activate(angband_term[0]);
		verify_panel();
		event_signal_point(EVENT_MAP, -1, -1);
	}
	return true;
}

static bool stage_scenario_encounter(
		const struct headless_scenario *scenario, char *error,
		size_t error_size)
{
	static const struct loc directions[] = {
		{ 1, 0 }, { 1, -1 }, { 1, 1 }, { 0, -1 },
		{ 0, 1 }, { -1, -1 }, { -1, 1 }, { -1, 0 }
	};
	struct monster_race *race;
	struct monster_group_info group = { 0, 0 };
	struct loc target = loc(0, 0);
	bool found = false;
	size_t i;

	if (!scenario->encounter[0]) return true;
	if (!player || !cave || world_spelunk_player_is_active(player)) {
		return runner_error(error, error_size,
			"scenario encounter requires a top-down location",
			scenario->encounter);
	}
	race = lookup_monster(scenario->encounter);
	if (!race) {
		return runner_error(error, error_size,
			"unknown scenario encounter monster", scenario->encounter);
	}
	for (i = 0; i < N_ELEMENTS(directions); i++) {
		struct loc candidate = loc(player->grid.x + directions[i].x * 2,
			player->grid.y + directions[i].y * 2);

		if (square_in_bounds(cave, candidate) &&
				square_isempty(cave, candidate) &&
				projectable(cave, player->grid, candidate, PROJECT_NONE)) {
			target = candidate;
			found = true;
			break;
		}
	}
	if (!found || !place_new_monster(cave, target, race, false, false,
			group, ORIGIN_DROP)) {
		return runner_error(error, error_size,
			"could not place scenario encounter monster", scenario->encounter);
	}
	player->upkeep->update |= PU_UPDATE_VIEW | PU_MONSTERS;
	handle_stuff(player);
	event_signal_point(EVENT_MAP, target.x, target.y);
	return true;
}

static void json_string(ang_file *file, const char *text)
{
	const unsigned char *cursor = (const unsigned char *)(text ? text : "");

	file_put(file, "\"");
	while (*cursor) {
		switch (*cursor) {
		case '\\': file_put(file, "\\\\"); break;
		case '"': file_put(file, "\\\""); break;
		case '\b': file_put(file, "\\b"); break;
		case '\f': file_put(file, "\\f"); break;
		case '\n': file_put(file, "\\n"); break;
		case '\r': file_put(file, "\\r"); break;
		case '\t': file_put(file, "\\t"); break;
		default:
			if (*cursor < 0x20) {
				file_putf(file, "\\u%04x", (unsigned int)*cursor);
			} else {
				file_putf(file, "%c", *cursor);
			}
			break;
		}
		cursor++;
	}
	file_put(file, "\"");
}

static const char *bot_name(enum headless_bot_kind bot)
{
	switch (bot) {
	case HEADLESS_BOT_WAIT: return "wait";
	case HEADLESS_BOT_RANDOM_WALK: return "random-walk";
	case HEADLESS_BOT_SCRIPTED: return "scripted";
	case HEADLESS_BOT_CAVE_LARDER_LOOP: return "cave-larder-loop";
	case HEADLESS_BOT_CAVE_FATAL_FALL: return "cave-fatal-fall";
	default: return "invalid";
	}
}

/* Read-only birth preview uses the engine's live point budget, not an
 * independently authored set of numbers. Interactive flow is covered by the
 * birth-screen integration test; these actions check the actual SDL pixels. */
static void capture_birth_points(game_event_type type, game_event_data *data,
		void *user)
{
	int allowed[STAT_MAX] = { 0 };
	(void)type;
	(void)user;
	for (int i = 0; i < STAT_MAX; i++) {
		if (data->birthpoints.points[i] > 0) allowed[i] |= 1;
		if (data->birthpoints.inc_points[i] <= data->birthpoints.remaining)
			allowed[i] |= 2;
	}
	ui_birth_screen_present_points(0, data->birthpoints.points,
		data->birthpoints.inc_points, data->birthpoints.remaining, allowed);
}

static const char *capture_name(enum headless_capture_kind capture)
{
	switch (capture) {
	case HEADLESS_CAPTURE_NONE: return "none";
	case HEADLESS_CAPTURE_INITIAL: return "initial";
	case HEADLESS_CAPTURE_FINAL: return "final";
	case HEADLESS_CAPTURE_BOTH: return "both";
	case HEADLESS_CAPTURE_SEQUENCE: return "sequence";
	default: return "invalid";
	}
}

static const char *expected_result_name(enum headless_expected_result expected)
{
	switch (expected) {
	case HEADLESS_EXPECT_COMPLETE: return "complete";
	case HEADLESS_EXPECT_DEAD: return "dead";
	case HEADLESS_EXPECT_ANY: return "any";
	default: return "invalid";
	}
}

static const char *mode_name(void)
{
	if (world_player_mode_has_capability(player,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		return "spelunking";
	}
	if (world_player_mode_has_capability(player,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		return "top-down";
	}
	return "invalid";
}

static uint64_t hash_byte(uint64_t hash, unsigned int value)
{
	hash ^= (uint8_t)value;
	return hash * UINT64_C(1099511628211);
}

static uint64_t hash_u32(uint64_t hash, uint32_t value)
{
	int i;

	for (i = 0; i < 4; i++) {
		hash = hash_byte(hash, value & 0xffU);
		value >>= 8;
	}
	return hash;
}

static uint64_t hash_text(uint64_t hash, const char *text)
{
	while (text && *text) hash = hash_byte(hash, (unsigned char)*text++);
	return hash_byte(hash, 0U);
}

static uint64_t state_hash(void)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	int x;
	int y;

	hash = hash_text(hash, player->world_location.id);
	hash = hash_text(hash, player->world_location.entry);
	hash = hash_u32(hash, (uint32_t)turn);
	hash = hash_u32(hash, (uint32_t)player->chp);
	hash = hash_u32(hash, (uint32_t)player->csp);
	hash = hash_u32(hash, (uint32_t)player->lev);
	hash = hash_u32(hash, (uint32_t)player_stamina_current(player));
	hash = hash_u32(hash, (uint32_t)player_stamina_maximum(player));
	hash = hash_u32(hash, (uint32_t)player_air_current(player));
	hash = hash_u32(hash, (uint32_t)player_air_maximum(player));
	hash = hash_u32(hash, player->larder.food_points);
	hash = hash_byte(hash, player->larder.milestone);
	hash = hash_u32(hash, (uint32_t)player->au);
	hash = hash_byte(hash, player->broker.initialized ? 1 : 0);
	for (x = 0; x < player->broker.count; x++) {
		const struct relic_broker_lot *lot = &player->broker.lots[x];
		hash = hash_u32(hash, lot->artifact);
		hash = hash_byte(hash, lot->band);
		hash = hash_u32(hash, (uint32_t)lot->price);
		hash = hash_byte(hash, lot->sold ? 1 : 0);
	}
	if (player->fishing) {
		hash = hash_u32(hash, (uint32_t)player->fishing->phase);
		hash = hash_u32(hash, (uint32_t)player->fishing->rod_reach);
		hash = hash_u32(hash, (uint32_t)player->fishing->depth);
		hash = hash_u32(hash, player->fishing->rng_state);
	}
	if (world_spelunk_player_is_active(player)) {
		const struct world_spelunk_runtime *runtime = player->spelunking;
		size_t cells = (size_t)runtime->state.map.width *
			(size_t)runtime->state.map.height;
		size_t i;

		hash = hash_u32(hash, (uint32_t)runtime->state.x);
		hash = hash_u32(hash, (uint32_t)runtime->state.y);
		hash = hash_u32(hash, (uint32_t)runtime->state.stamina);
		hash = hash_u32(hash, (uint32_t)runtime->state.breath);
		hash = hash_u32(hash, (uint32_t)runtime->state.movement);
		hash = hash_u32(hash, (uint32_t)runtime->actor_count);
		for (i = 0; i < runtime->actor_count; i++) {
			const struct world_spelunk_actor *actor = &runtime->actors[i];

			hash = hash_text(hash, actor->id);
			hash = hash_u32(hash, (uint32_t)actor->x);
			hash = hash_u32(hash, (uint32_t)actor->y);
			hash = hash_u32(hash, (uint32_t)actor->hp);
			hash = hash_u32(hash, (uint32_t)actor->max_hp);
			hash = hash_u32(hash, actor->energy);
		}
		for (i = 0; i < cells; i++) {
			hash = hash_byte(hash, (unsigned int)runtime->cells[i]);
			hash = hash_byte(hash, runtime->pitons[i]);
			hash = hash_byte(hash, runtime->ropes[i]);
		}
	} else if (cave) {
		hash = hash_u32(hash, (uint32_t)player->grid.x);
		hash = hash_u32(hash, (uint32_t)player->grid.y);
		for (y = 0; y < cave->height; y++) {
			for (x = 0; x < cave->width; x++) {
				const struct square *grid = square(cave, loc(x, y));

				hash = hash_byte(hash, grid->feat);
				hash = hash_u32(hash, (uint32_t)(int32_t)grid->mon);
			}
		}
	}
	return hash;
}

static void state_position(int *x, int *y)
{
	if (world_spelunk_player_is_active(player)) {
		*x = player->spelunking->state.x;
		*y = player->spelunking->state.y;
	} else {
		*x = player->grid.x;
		*y = player->grid.y;
	}
}

static void report_state(struct headless_output *output, const char *phase)
{
	int x;
	int y;
	size_t actor_count = world_spelunk_player_is_active(player) ?
		player->spelunking->actor_count : 0;
	uint64_t hash = state_hash();

	state_position(&x, &y);
	file_put(output->report, "{\"type\":\"state\",\"phase\":");
	json_string(output->report, phase);
	file_putf(output->report,
		",\"step\":%u,\"turn\":%ld,\"mode\":", output->step,
		(long)turn);
	json_string(output->report, mode_name());
	file_put(output->report, ",\"location\":");
	json_string(output->report, player->world_location.id);
	file_put(output->report, ",\"entry\":");
	json_string(output->report, player->world_location.entry);
	file_putf(output->report,
		",\"x\":%d,\"y\":%d,\"depth\":%d,\"level\":%d,"
		"\"hp\":%d,\"max_hp\":%d,\"mana\":%d,\"max_mana\":%d,"
		"\"energy\":%d,\"speed\":%d,\"actors\":%u,"
		"\"gold\":%ld,\"poison_resistance\":%d,"
		"\"larder_points\":%lu,\"larder_milestone\":%u",
		x, y, player->depth, player->lev, player->chp, player->mhp,
		player->csp, player->msp, player->energy, player->state.speed,
		(unsigned int)actor_count, (long)player->au,
		player->timed[TMD_OPP_POIS],
		(unsigned long)player->larder.food_points,
		(unsigned int)player->larder.milestone);
	if (player->fishing) {
		file_putf(output->report,
			",\"fishing\":true,\"fishing_phase\":%d,"
			"\"hook_depth\":%d,\"hook_reach\":%d",
			(int)player->fishing->phase, player->fishing->depth,
			player->fishing->rod_reach);
	} else {
		file_put(output->report, ",\"fishing\":false");
	}
	file_putf(output->report,
		",\"dead\":%s,\"playing\":%s,\"state_hash\":\"%08lx%08lx\"}\n",
		player->is_dead ? "true" : "false",
		player->upkeep->playing ? "true" : "false",
		(unsigned long)(hash >> 32), (unsigned long)(hash & UINT32_MAX));
}

static void report_message(game_event_type type, game_event_data *data,
		void *user)
{
	struct headless_output *output = user;

	(void)type;
	if (!output || !output->report || !data || !data->message.msg) return;
	file_putf(output->report,
		"{\"type\":\"message\",\"step\":%u,\"message_type\":%d,"
		"\"text\":", output->step, data->message.type);
	json_string(output->report, data->message.msg);
	file_put(output->report, "}\n");
}

static void report_story(game_event_type type, game_event_data *data,
		void *user)
{
	struct headless_output *output = user;
	const struct world_story_scene *scene;

	(void)type;
	if (!output || !output->report || !data || !data->story) return;
	scene = data->story;
	file_putf(output->report,
		"{\"type\":\"story\",\"step\":%u,\"id\":", output->step);
	json_string(output->report, scene->id);
	file_put(output->report, ",\"title\":");
	json_string(output->report, scene->title);
	file_put(output->report, ",\"subtitle\":");
	json_string(output->report, scene->subtitle);
	file_put(output->report, ",\"body\":");
	json_string(output->report, scene->body);
	file_put(output->report, ",\"footer\":");
	json_string(output->report, scene->footer);
	file_put(output->report, "}\n");
}

static bool write_capture(struct headless_output *output, const char *phase,
		const char *path, char *error, size_t error_size)
{
	Term_activate(angband_term[0]);
	Term_redraw();
	if (!sdl3_capture_bmp(path)) {
		return runner_error(error, error_size,
			"could not write off-screen capture", path);
	}
	file_put(output->report, "{\"type\":\"capture\",\"phase\":");
	json_string(output->report, phase);
	file_put(output->report, ",\"path\":");
	json_string(output->report, path);
	file_put(output->report, "}\n");
	return true;
}

static bool direction(const char *name, int *keypad, int *dx, int *dy)
{
	struct direction_definition {
		const char *name;
		int keypad;
		int dx;
		int dy;
	};
	static const struct direction_definition definitions[] = {
		{ "north", 8, 0, -1 }, { "north-east", 9, 1, -1 },
		{ "east", 6, 1, 0 }, { "south-east", 3, 1, 1 },
		{ "south", 2, 0, 1 }, { "south-west", 1, -1, 1 },
		{ "west", 4, -1, 0 }, { "north-west", 7, -1, -1 }
	};
	size_t i;

	for (i = 0; i < N_ELEMENTS(definitions); i++) {
		if (streq(name, definitions[i].name)) {
			*keypad = definitions[i].keypad;
			*dx = definitions[i].dx;
			*dy = definitions[i].dy;
			return true;
		}
	}
	return false;
}

static bool fishing_action(const char *name,
		enum world_fishing_action *action)
{
	struct fishing_action_definition {
		const char *name;
		enum world_fishing_action action;
	};
	static const struct fishing_action_definition definitions[] = {
		{ "extend", WORLD_FISHING_EXTEND },
		{ "retract", WORLD_FISHING_RETRACT },
		{ "lower", WORLD_FISHING_LOWER },
		{ "raise", WORLD_FISHING_RAISE },
		{ "hold", WORLD_FISHING_WAIT },
		{ "strike", WORLD_FISHING_STRIKE },
		{ "reel", WORLD_FISHING_REEL },
		{ "stop", WORLD_FISHING_CANCEL }
	};
	size_t i;

	if (!name || !action) return false;
	for (i = 0; i < N_ELEMENTS(definitions); i++) {
		if (streq(name, definitions[i].name)) {
			*action = definitions[i].action;
			return true;
		}
	}
	return false;
}

static struct object *first_carried_catch(void)
{
	struct object *obj;

	for (obj = player ? player->gear : NULL; obj; obj = obj->next) {
		if (world_larder_object_is_catch(obj)) return obj;
	}
	return NULL;
}

static bool milestone_is_reached(
		const struct world_larder_milestone_definition *milestone)
{
	return milestone && player &&
		world_larder_has_milestone(&player->larder, milestone->id);
}

static struct object *current_larder_reward_stock(void)
{
	struct store *store = store_at(cave, player->grid);
	size_t i;

	if (!store_accepts_larder_donations(store)) return NULL;
	for (i = 0; i < store->larder_num; i++) {
		struct object *obj;

		if (!milestone_is_reached(store->larder_table[i].milestone)) continue;
		for (obj = store->stock; obj; obj = obj->next) {
			if (obj->kind == store->larder_table[i].kind) return obj;
		}
	}
	return NULL;
}

static struct object *carried_larder_reward(void)
{
	struct object *obj;
	int i;

	for (obj = player ? player->gear : NULL; obj; obj = obj->next) {
		for (i = 0; z_info && i < z_info->store_max; i++) {
			size_t j;

			for (j = 0; j < stores[i].larder_num; j++) {
				if (obj->kind == stores[i].larder_table[j].kind &&
						milestone_is_reached(
							stores[i].larder_table[j].milestone)) {
					return obj;
				}
			}
		}
	}
	return NULL;
}

static bool queue_action(const char *action, char *error, size_t error_size)
{
	bool spelunking = world_spelunk_player_is_active(player);
	const char *direction_name = prefix(action, "walk:") ? action + 5 : NULL;
	const char *peek_direction_name = prefix(action, "peek:") ? action + 5 :
		NULL;
	const char *fishing_name = prefix(action, "fish:") ? action + 5 : NULL;
	enum world_fishing_action fishing;
	int keypad = 0;
	int dx = 0;
	int dy = 0;
	errr r = 0;

	if (streq(action, "hold")) {
		r = spelunking ?
			cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0) :
			cmdq_push(CMD_HOLD);
	} else if (prefix(action, "quaff:")) {
		struct object *obj;
		int sval = lookup_sval(TV_POTION, action + strlen("quaff:"));
		for (obj = player->gear; obj; obj = obj->next) {
			if (tval_is_potion(obj) && obj->sval == sval) break;
		}
		if (!obj) return runner_error(error, error_size,
			"requested potion is not carried", action);
		r = cmdq_push(CMD_QUAFF);
		if (!r) cmd_set_arg_item(cmdq_peek(), "item", obj);
	} else if (streq(action, "read-recall")) {
		struct object *obj;
		for (obj = player->gear; obj; obj = obj->next) {
			const struct effect *effect = object_effect(obj);
			if (tval_is_scroll(obj) && effect && effect->index == EF_RECALL) break;
		}
		if (!obj) return runner_error(error, error_size,
			"no carried recall scroll is available", action);
		r = cmdq_push(CMD_READ_SCROLL);
		if (!r) cmd_set_arg_item(cmdq_peek(), "item", obj);
	} else if (direction_name &&
			direction(direction_name, &keypad, &dx, &dy)) {
		if (spelunking) {
			r = cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, dx, dy);
		} else {
			r = cmdq_push(CMD_WALK);
			if (!r) cmd_set_arg_direction(cmdq_peek(), "direction", keypad);
		}
	} else if (peek_direction_name && spelunking &&
			direction(peek_direction_name, &keypad, &dx, &dy)) {
		/* Model held P plus one direction as the same two semantic commands used
		 * by the interactive frontend.  Both are free observations. */
		r = cmdq_push_spelunk_peek(WORLD_SPELUNK_PEEK_BEGIN, 0, 0);
		if (!r) {
			r = cmdq_push_spelunk_peek(WORLD_SPELUNK_PEEK_MOVE, dx, dy);
		}
	} else if (streq(action, "peek:end") && spelunking) {
		r = cmdq_push_spelunk_peek(WORLD_SPELUNK_PEEK_END, 0, 0);
	} else if (streq(action, "ascend")) {
		r = spelunking ?
			cmdq_push_spelunk_passage(WORLD_SPELUNK_PASSAGE_UP) :
			cmdq_push(CMD_GO_UP);
	} else if (streq(action, "descend")) {
		r = spelunking ?
			cmdq_push_spelunk_passage(WORLD_SPELUNK_PASSAGE_DOWN) :
			cmdq_push(CMD_GO_DOWN);
	} else if (streq(action, "explore") && !spelunking) {
		r = cmdq_push(CMD_EXPLORE);
	} else if (streq(action, "jump") && spelunking) {
		r = cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_JUMP, 0, 0);
	} else if (streq(action, "grip") && spelunking) {
		r = cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_GRIP, 0, 0);
	} else if (streq(action, "piton") && spelunking) {
		r = cmdq_push_spelunk_local(WORLD_SPELUNK_LOCAL_PITON);
	} else if (streq(action, "rope") && spelunking) {
		r = cmdq_push_spelunk_local(WORLD_SPELUNK_LOCAL_ROPE);
	} else if (streq(action, "pickup") && spelunking) {
		r = cmdq_push_spelunk_local(WORLD_SPELUNK_LOCAL_PICKUP);
	} else if (streq(action, "fish:start") && !player->fishing) {
		r = cmdq_push(CMD_FISHING_START);
	} else if (fishing_name && player->fishing &&
			fishing_action(fishing_name, &fishing)) {
		r = cmdq_push_fishing_action(fishing);
	} else if (streq(action, "exit") && spelunking) {
		r = cmdq_push_spelunk_exit();
	} else if (streq(action, "donate-catch") && !spelunking) {
		struct object *obj = first_carried_catch();

		if (!obj) return runner_error(error, error_size,
			"no carried fishing catch is available", action);
		r = cmdq_push(CMD_LARDER_DONATE);
		if (!r) {
			cmd_set_arg_item(cmdq_peek(), "item", obj);
			cmd_set_arg_number(cmdq_peek(), "quantity", obj->number);
		}
	} else if (prefix(action, "buy-relic:") && !spelunking) {
		const char *number = action + strlen("buy-relic:");
		char *end;
		long index = strtol(number, &end, 10);
		if (end == number || *end || index < 0 || index >= RELIC_BROKER_MAX_LOTS)
			return runner_error(error, error_size, "invalid relic lot", action);
		r = cmdq_push(CMD_BUY_RELIC);
		if (!r) cmd_set_arg_number(cmdq_peek(), "lot", (int)index);
	} else if (streq(action, "buy-larder-reward") && !spelunking) {
		struct object *obj = current_larder_reward_stock();

		if (!obj) return runner_error(error, error_size,
			"no unlocked larder reward is stocked here", action);
		r = cmdq_push(CMD_BUY);
		if (!r) {
			cmd_set_arg_item(cmdq_peek(), "item", obj);
			cmd_set_arg_number(cmdq_peek(), "quantity", 1);
		}
	} else if (streq(action, "eat-larder-reward") && !spelunking) {
		struct object *obj = carried_larder_reward();

		if (!obj) return runner_error(error, error_size,
			"no carried larder reward is available", action);
		r = cmdq_push(CMD_EAT);
		if (!r) cmd_set_arg_item(cmdq_peek(), "item", obj);
	} else if (prefix(action, "route:") && action[6]) {
		r = cmdq_push(CMD_WORLD_TRANSITION);
		if (!r) cmd_set_arg_string(cmdq_peek(), "route", action + 6);
	} else {
		return runner_error(error, error_size,
			"action is unknown or invalid in the active mode", action);
	}
	if (r) return runner_error(error, error_size,
		"could not queue action", action);
	return true;
}

static uint32_t bot_random(uint32_t *state)
{
	*state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
	return *state;
}

static const char *action_for_step(const struct headless_scenario *scenario,
		unsigned int step, uint32_t *bot_state, char *buffer, size_t size)
{
	static const char *directions[] = {
		"north", "north-east", "east", "south-east", "south",
		"south-west", "west", "north-west"
	};

	if (scenario->bot == HEADLESS_BOT_SCRIPTED) {
		return scenario->actions[step];
	}
	if (scenario->bot == HEADLESS_BOT_WAIT) return "hold";
	if ((bot_random(bot_state) % 9U) == 8U) return "hold";
	strnfmt(buffer, size, "walk:%s",
		directions[bot_random(bot_state) % N_ELEMENTS(directions)]);
	return buffer;
}

static bool initialize_player(const struct headless_scenario *scenario,
		char *error, size_t error_size)
{
	const struct level *level;
	struct loc requested_entry;
	size_t i;

	Rand_state_init(scenario->seed);
	if (!player_make_simple(scenario->race, scenario->player_class,
			scenario->player_name)) {
		return runner_error(error, error_size,
			"unknown race or class in scenario", scenario->name);
	}
	for (i = 0; i < scenario->inventory_count; i++) {
		if (!give_scenario_item(scenario->inventory[i],
				scenario->inventory_quantity[i], false, error, error_size)) {
			return false;
		}
	}
	for (i = 0; i < scenario->equipment_count; i++) {
		if (!give_scenario_item(scenario->equipment[i], 1, true,
				error, error_size)) {
			return false;
		}
	}
	/* Wielding setup equipment uses the production API, which records an
	 * action cost.  Scenario fixtures are initial state, not elapsed play;
	 * leaving that cost pending also rejects an otherwise valid entry route. */
	player->upkeep->energy_use = 0;
	player->opts.opt[OPT_use_sound] = false;
	level = level_by_id(scenario->location);
	if (!level) return runner_error(error, error_size,
		"unknown world location", scenario->location);
	if (!streq(scenario->entry, "default") &&
			!world_entry_by_id(level, scenario->entry)) {
		return runner_error(error, error_size,
			"unknown entry for world location", scenario->entry);
	}
	if (!world_player_set_location(player, scenario->location,
			scenario->entry)) {
		return runner_error(error, error_size,
			"could not set world location", scenario->location);
	}
	event_signal(EVENT_LEAVE_INIT);
	/* The interactive EVENT_ENTER_GAME message handler owns the blocking
	 * "-more-" prompt.  Headless mode records EVENT_MESSAGE directly and must
	 * never install a prompt that waits for keyboard input. */
	event_signal(EVENT_ENTER_WORLD);
	prepare_next_level(player);
	if (!on_enter_world_location()) {
		return runner_error(error, error_size,
			"world location mode could not be entered", scenario->location);
	}
	/* Some inherited generators choose their traditional arrival square.  The
	 * scenario contract is stronger: an authored entry names the exact start
	 * fixture, so relocate only after generation has materialized all entries. */
	if (!streq(scenario->entry, "default")) {
		if (world_entry_locate(cave, scenario->entry, &requested_entry) !=
				WORLD_ENTRY_OK ||
				(!loc_eq(requested_entry, player->grid) &&
				!square_isempty(cave, requested_entry))) {
			return runner_error(error, error_size,
				"scenario entry is not safely occupiable", scenario->entry);
		}
		if (!loc_eq(requested_entry, player->grid)) {
			square_set_mon(cave, player->grid, 0);
			player_place(cave, player, requested_entry);
			handle_stuff(player);
			event_signal_point(EVENT_MAP, -1, -1);
		}
	}
	if (scenario->enter_route[0]) {
		struct world_spelunking_entry_report report;

		if (world_spelunking_entry_execute(player, cave,
				scenario->enter_route, &report) != WORLD_SPELUNKING_ENTRY_OK) {
			if (error && error_size) {
				strnfmt(error, error_size,
					"could not enter scenario route '%s' (result %d, "
					"transition %d, safety %d, resources %d, destination %d)",
					scenario->enter_route, (int)report.result,
					(int)report.transition, (int)report.safety,
					(int)report.resources, (int)report.destination);
			}
			return false;
		}
		/* A scenario describes its initial state after arrival.  The route API
		 * records an action cost for the interactive loop; consume neither that
		 * setup cost nor a second player turn when the first scripted action is
		 * submitted. */
		player->upkeep->energy_use = 0;
		if (player->energy < z_info->move_energy) {
			player->energy = z_info->move_energy;
		}
	}
	if (!relocate_start_position(scenario, error, error_size)) return false;
	return stage_scenario_encounter(scenario, error, error_size);
}

static void write_scenario_record(struct headless_output *output,
		const struct headless_scenario *scenario)
{
	file_put(output->report, "{\"type\":\"scenario\",\"name\":");
	json_string(output->report, scenario->name);
	file_putf(output->report, ",\"seed\":%lu,\"bot\":",
		(unsigned long)scenario->seed);
	json_string(output->report, bot_name(scenario->bot));
	file_putf(output->report, ",\"steps\":%u,\"capture\":",
		scenario->steps);
	json_string(output->report, capture_name(scenario->capture));
	file_put(output->report, ",\"expect\":");
	json_string(output->report,
		expected_result_name(scenario->expected_result));
	if (scenario->capture == HEADLESS_CAPTURE_SEQUENCE) {
		file_putf(output->report,
			",\"capture_fps\":%u,\"capture_preroll_ms\":%u,"
			"\"capture_action_ms\":%u,\"capture_postroll_ms\":%u",
			scenario->capture_fps, scenario->capture_preroll_ms,
			scenario->capture_action_ms, scenario->capture_postroll_ms);
	}
	file_put(output->report, "}\n");
}

static void write_replay_header(struct headless_output *output,
		const struct headless_scenario *scenario)
{
	file_putf(output->replay,
		"# Materialized by Ammerow headless mode.\n"
		"name:%s-replay\nseed:%lu\nrace:%s\nclass:%s\nplayer:%s\n"
		"location:%s\nentry:%s\n",
		scenario->name, (unsigned long)scenario->seed, scenario->race,
		scenario->player_class, scenario->player_name, scenario->location,
		scenario->entry);
	if (scenario->enter_route[0]) {
		file_putf(output->replay, "enter-route:%s\n",
			scenario->enter_route);
	}
	file_putf(output->replay, "bot:scripted\ncapture:%s\nexpect:%s\n",
		capture_name(scenario->capture),
		expected_result_name(scenario->expected_result));
	if (scenario->interface_font[0]) {
		file_putf(output->replay, "interface-font:%s\n",
			scenario->interface_font);
	}
	if (scenario->map_font[0]) {
		file_putf(output->replay, "map-font:%s\n", scenario->map_font);
	}
	if (scenario->theme[0]) {
		file_putf(output->replay, "theme:%s\n", scenario->theme);
	}
	if (scenario->map_style[0]) {
		file_putf(output->replay, "map-style:%s\n", scenario->map_style);
	}
	if (scenario->map_presentation[0]) {
		file_putf(output->replay, "map-presentation:%s\n",
			scenario->map_presentation);
	}
	file_putf(output->replay, "map-zoom:%d\n",
		scenario->map_zoom_percent);
	if (scenario->screen_width && scenario->screen_height) {
		file_putf(output->replay, "screen-width:%d\nscreen-height:%d\n",
			scenario->screen_width, scenario->screen_height);
	}
	if (scenario->capture == HEADLESS_CAPTURE_SEQUENCE) {
		file_putf(output->replay,
			"capture-fps:%u\ncapture-preroll-ms:%u\n"
			"capture-action-ms:%u\ncapture-postroll-ms:%u\n",
			scenario->capture_fps, scenario->capture_preroll_ms,
			scenario->capture_action_ms, scenario->capture_postroll_ms);
	}
	if (scenario->has_start_position) {
		file_putf(output->replay, "start-x:%d\nstart-y:%d\n",
			scenario->start_x, scenario->start_y);
	}
	if (scenario->start_anchor[0]) {
		file_putf(output->replay, "start-anchor:%s\n",
			scenario->start_anchor);
	}
	if (scenario->encounter[0]) {
		file_putf(output->replay, "encounter:%s\n", scenario->encounter);
	}
	{
		size_t i;

		for (i = 0; i < scenario->inventory_count; i++) {
			if (scenario->inventory_quantity[i] > 1) {
				file_putf(output->replay, "inventory:%u:%s\n",
					(unsigned int)scenario->inventory_quantity[i],
					scenario->inventory[i]);
			} else {
				file_putf(output->replay, "inventory:%s\n",
					scenario->inventory[i]);
			}
		}
		for (i = 0; i < scenario->equipment_count; i++) {
			file_putf(output->replay, "equipment:%s\n",
				scenario->equipment[i]);
		}
	}
}

bool headless_run_scenario(const struct headless_scenario *scenario,
		const char *output_dir, char *error, size_t error_size)
{
	struct headless_output output = { 0 };
	char report_path[HEADLESS_SCENARIO_PATH];
	char replay_path[HEADLESS_SCENARIO_PATH];
	char action_buffer[HEADLESS_SCENARIO_ACTION];
	uint32_t bot_state;
	struct headless_loop_bot loop_bot = { 0 };
	struct headless_fall_bot fall_bot = { 0 };
	unsigned int i;
	bool handler_registered = false;
	bool loop_complete = false;
	bool expectation_met;
	bool okay = false;
	bool sequence_active = false;
	unsigned int sequence_frames = 0;

	if (!scenario || !output_dir || !dir_exists(output_dir)) {
		return runner_error(error, error_size,
			"output directory does not exist", output_dir);
	}
	path_build(report_path, sizeof(report_path), output_dir, "report.jsonl");
	path_build(replay_path, sizeof(replay_path), output_dir, "replay.scn");
	path_build(output.initial_capture, sizeof(output.initial_capture),
		output_dir, "initial.bmp");
	path_build(output.final_capture, sizeof(output.final_capture),
		output_dir, "final.bmp");
	path_build(output.frame_directory, sizeof(output.frame_directory),
		output_dir, "frames");
	output.report = file_open(report_path, MODE_WRITE, FTYPE_TEXT);
	output.replay = file_open(replay_path, MODE_WRITE, FTYPE_TEXT);
	if (!output.report || !output.replay) {
		runner_error(error, error_size, "could not create headless outputs",
			output_dir);
		goto cleanup;
	}
	event_add_handler(EVENT_MESSAGE, report_message, &output);
	event_add_handler(EVENT_STORY, report_story, &output);
	handler_registered = true;
	write_scenario_record(&output, scenario);
	write_replay_header(&output, scenario);
	if (!initialize_player(scenario, error, error_size)) goto cleanup;
	/*
	 * A headless run has no input loop for modal store presentation.  Store
	 * interactions are exposed below as semantic commands, so keep the normal
	 * movement trigger but detach only the interactive frontend handlers.
	 */
	event_remove_handler_type(EVENT_ENTER_STORE);
	event_remove_handler_type(EVENT_USE_STORE);
	event_remove_handler_type(EVENT_LEAVE_STORE);
	report_state(&output, "initial");
	if ((scenario->capture == HEADLESS_CAPTURE_INITIAL ||
			scenario->capture == HEADLESS_CAPTURE_BOTH) &&
			!write_capture(&output, "initial", output.initial_capture,
				error, error_size)) {
		goto cleanup;
	}
	if (scenario->capture == HEADLESS_CAPTURE_SEQUENCE) {
		if (!dir_create(output.frame_directory) ||
				!sdl3_capture_sequence_begin(output.frame_directory,
					scenario->capture_fps)) {
			runner_error(error, error_size,
				"could not start off-screen capture sequence",
				output.frame_directory);
			goto cleanup;
		}
		sequence_active = true;
		if (!sdl3_capture_sequence_hold(scenario->capture_preroll_ms)) {
			runner_error(error, error_size,
				"could not write off-screen capture sequence",
				output.frame_directory);
			goto cleanup;
		}
	}
	bot_state = scenario->seed ^ UINT32_C(0xa511e9b3);
	headless_loop_bot_init(&loop_bot);
	headless_fall_bot_init(&fall_bot);
	for (i = 0; i < scenario->steps; i++) {
		const char *action;

		if (scenario->bot == HEADLESS_BOT_CAVE_LARDER_LOOP) {
			enum headless_loop_bot_result bot_result = headless_loop_bot_next(
				&loop_bot, action_buffer, sizeof(action_buffer), error,
				error_size);

			if (bot_result == HEADLESS_LOOP_BOT_COMPLETE) {
				loop_complete = true;
				break;
			}
			if (bot_result == HEADLESS_LOOP_BOT_ERROR) goto cleanup;
			action = action_buffer;
		} else if (scenario->bot == HEADLESS_BOT_CAVE_FATAL_FALL) {
			if (headless_fall_bot_next(&fall_bot, action_buffer,
					sizeof(action_buffer), error, error_size) ==
					HEADLESS_FALL_BOT_ERROR) {
				goto cleanup;
			}
			action = action_buffer;
		} else {
			action = action_for_step(scenario, i, &bot_state,
				action_buffer, sizeof(action_buffer));
		}

		output.step = i + 1;
		file_putf(output.replay, "action:%s\n", action);
		file_putf(output.report,
			"{\"type\":\"action\",\"step\":%u,\"command\":",
			output.step);
		json_string(output.report, action);
		if (scenario->bot == HEADLESS_BOT_CAVE_LARDER_LOOP) {
			file_put(output.report, ",\"bot_stage\":");
			json_string(output.report, headless_loop_bot_stage_name(&loop_bot));
		}
		file_put(output.report, "}\n");
		(void)sdl3_capture_introduction(NULL);
		sdl3_capture_clear_inspection();
		(void)sdl3_capture_store(NULL);
		if (Term && Term->screen_hook) Term->screen_hook(NULL);
		if (streq(action, "inspect:refresh")) {
			Term_activate(angband_term[0]);
			do_cmd_redraw();
			Term_redraw_section(0, 0, Term->wid - 1, Term->hgt - 1);
		} else if (prefix(action, "inspect:settings:")) {
			if (!sdl3_capture_settings(action + strlen("inspect:settings:"))) {
				runner_error(error, error_size, "unknown settings action", action);
				goto cleanup;
			}
		} else if (prefix(action, "inspect:messages:")) {
			if (!sdl3_capture_message_position(action + strlen("inspect:messages:"))) {
				runner_error(error, error_size, "unknown message position", action);
				goto cleanup;
			}
		} else if (streq(action, "inspect:home")) {
			if (!sdl3_capture_home()) {
				runner_error(error, error_size, "could not inspect home", action);
				goto cleanup;
			}
		} else if (streq(action, "inspect:legal")) {
			if (!sdl3_capture_legal()) {
				runner_error(error, error_size, "could not inspect legal notices", action);
				goto cleanup;
			}
		} else if (streq(action, "inspect:quest-briefing")) {
			const struct store *shop = NULL;
			for (int index = 0; index < z_info->store_max; index++) {
				if (stores[index].objective_briefing) shop = &stores[index];
			}
			if (!shop) {
				runner_error(error, error_size, "no quest briefing is authored", action);
				goto cleanup;
			}
			ui_player_present_fishing_quest_briefing(shop->objective_briefing, false);
		} else if (streq(action, "inspect:donation") ||
				streq(action, "inspect:donation-complete")) {
			/* Read-only UI fixture: calculate the normal command's report on a
			 * copy of larder state. Neither inventory nor XP is changed. */
			struct world_larder_state preview = player->larder;
			struct world_larder_report receipt;
			const struct world_larder_milestone_definition *next =
				world_larder_next_milestone(&preview);
			uint32_t value = world_larder_fish_value(0);
			int quantity = 1;
			if (next && value && streq(action, "inspect:donation-complete"))
				quantity = (int)((next->target - preview.food_points + value - 1) / value);
			if (!world_larder_donate(&preview, 0, quantity, &receipt)) {
				runner_error(error, error_size, "could not preview donation", action);
				goto cleanup;
			}
			ui_store_screen_present_donation(NULL, "your catch", &preview,
				&receipt, false);
		} else if (prefix(action, "inspect:help:")) {
			if (!ui_help_preview(action + strlen("inspect:help:"), 0)) {
				runner_error(error, error_size, "could not inspect help", action);
				goto cleanup;
			}
		} else if (streq(action, "inspect:class-titles")) {
			if (!sdl3_capture_class_titles()) {
				runner_error(error, error_size, "could not inspect class titles", action);
				goto cleanup;
			}
		} else if (prefix(action, "inspect:intro:")) {
			if (!sdl3_capture_introduction(action + strlen("inspect:intro:"))) {
				runner_error(error, error_size, "unknown introduction scene", action);
				goto cleanup;
			}
		} else if (prefix(action, "inspect:store:")) {
			if (!sdl3_capture_store(action + strlen("inspect:store:"))) {
				runner_error(error, error_size, "could not inspect store", action);
				goto cleanup;
			}
		} else if (streq(action, "inspect:commands") ||
				prefix(action, "inspect:commands:")) {
			const char *family = streq(action, "inspect:commands") ? NULL :
				action + strlen("inspect:commands:");
			if (!textui_action_menu_present(family, 0)) {
				runner_error(error, error_size, "unknown command family", action);
				goto cleanup;
			}
		} else if (streq(action, "inspect:birth-options")) {
			struct menu menu = { 0 };
			menu.title = "Birth options";
			menu.filter_list = option_page[OPT_PAGE_BIRTH];
			while (menu.filter_list[menu.filter_count] != OPT_none)
				menu.filter_count++;
			ui_options_present(&menu);
		} else if (streq(action, "inspect:birth-points")) {
			event_add_handler(EVENT_BIRTHPOINTS, capture_birth_points, NULL);
			do_cmd_refresh_stats(NULL);
			event_remove_handler(EVENT_BIRTHPOINTS, capture_birth_points, NULL);
		} else if (streq(action, "inspect:birth-roll")) {
			ui_birth_screen_present_roll(false);
		} else if (streq(action, "inspect:broker")) {
			int store_index;
			if (!relic_broker_ensure(player)) goto cleanup;
			for (store_index = 0; store_index < z_info->store_max; store_index++) {
				if (stores[store_index].relic_broker) {
					ui_relic_broker_present(&stores[store_index], 0, 0);
					break;
				}
			}
		} else if (streq(action, "inspect:closest-monster")) {
			if (!sdl3_capture_inspect_closest_monster()) {
				runner_error(error, error_size,
					"could not stage a visible monster inspection", action);
				goto cleanup;
			}
		} else {
			if (!queue_action(action, error, error_size)) goto cleanup;
			run_game_loop();
		}
		report_state(&output, "after-action");
		if (sequence_active &&
				!sdl3_capture_sequence_hold(scenario->capture_action_ms)) {
			runner_error(error, error_size,
				"could not write off-screen capture sequence",
				output.frame_directory);
			goto cleanup;
		}
		if (player->is_dead || !player->upkeep->playing) break;
	}
	if (sequence_active) {
		if (!sdl3_capture_sequence_hold(scenario->capture_postroll_ms) ||
				!sdl3_capture_sequence_end(&sequence_frames)) {
			sequence_active = false;
			runner_error(error, error_size,
				"could not finish off-screen capture sequence",
				output.frame_directory);
			goto cleanup;
		}
		sequence_active = false;
		file_put(output.report,
			"{\"type\":\"capture-sequence\",\"path\":");
		json_string(output.report, output.frame_directory);
		file_putf(output.report, ",\"fps\":%u,\"frames\":%u}\n",
			scenario->capture_fps, sequence_frames);
	}
	if ((scenario->capture == HEADLESS_CAPTURE_FINAL ||
			scenario->capture == HEADLESS_CAPTURE_BOTH) &&
			!write_capture(&output, "final", output.final_capture,
				error, error_size)) {
		goto cleanup;
	}
	expectation_met = scenario->expected_result == HEADLESS_EXPECT_ANY ||
		(scenario->expected_result == HEADLESS_EXPECT_DEAD &&
		player->is_dead) ||
		(scenario->expected_result == HEADLESS_EXPECT_COMPLETE &&
		!player->is_dead && player->upkeep->playing &&
		(scenario->bot == HEADLESS_BOT_CAVE_LARDER_LOOP ? loop_complete :
		output.step == scenario->steps));
	file_putf(output.report,
		"{\"type\":\"result\",\"status\":\"%s\",\"steps_executed\":%u,"
		"\"bot_complete\":%s,\"expectation_met\":%s}\n",
		player->is_dead ? "dead" :
		(player->upkeep->playing ? "complete" : "stopped"), output.step,
		loop_complete ? "true" : "false",
		expectation_met ? "true" : "false");
	printf("Headless scenario '%s': %u step%s, final state %016llx\n",
		scenario->name, output.step, output.step == 1 ? "" : "s",
		(unsigned long long)state_hash());
	printf("Report: %s\nReplay: %s\n", report_path, replay_path);
	if (!expectation_met) {
		runner_error(error, error_size,
			"scenario result did not match its expectation", scenario->name);
		goto cleanup;
	}
	okay = true;

cleanup:
	if (sequence_active) sdl3_capture_sequence_end(NULL);
	headless_loop_bot_dispose(&loop_bot);
	if (handler_registered) {
		event_remove_handler(EVENT_MESSAGE, report_message, &output);
		event_remove_handler(EVENT_STORY, report_story, &output);
	}
	if (output.replay) file_close(output.replay);
	if (output.report) file_close(output.report);
	return okay;
}
