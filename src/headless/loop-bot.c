/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/loop-bot.c
 * \brief Observation-driven acceptance bot for the first cross-mode loop.
 */

#include "angband.h"
#include "cmd-larder.h"
#include "fishing-data.h"
#include "game-world.h"
#include "headless/loop-bot.h"
#include "init.h"
#include "obj-tval.h"
#include "player-timed.h"
#include "store.h"
#include "world-entry.h"
#include "world-fishing.h"
#include "world-fishing-site.h"
#include "world-larder-data.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-system.h"
#include "world-spelunking-traversal-proof.h"
#include "world-transition.h"

#include <stdint.h>

#define GUIDE_RETRY_MAX 16U

struct headless_loop_route_guide {
	struct world_spelunk_generated_layout generated;
	size_t next_step;
	bool pending;
	enum world_spelunk_witness_step_kind pending_kind;
	int pending_origin_x;
	int pending_origin_y;
	int pending_expected_x;
	int pending_expected_y;
	unsigned int pending_retries;
	char pending_action[48];
	struct loc portal;
};

enum guide_action_result {
	GUIDE_ACTION = 0,
	GUIDE_COMPLETE,
	GUIDE_ERROR
};

static enum headless_loop_bot_result bot_error(char *error,
		size_t error_size, const char *message)
{
	if (error && error_size) my_strcpy(error, message, error_size);
	return HEADLESS_LOOP_BOT_ERROR;
}

static bool witness_command_name(const struct world_spelunk_command *command,
		char *action, size_t action_size)
{
	struct command_name {
		int dx;
		int dy;
		const char *name;
	};
	static const struct command_name moves[] = {
		{ 0, -1, "walk:north" }, { 1, -1, "walk:north-east" },
		{ 1, 0, "walk:east" }, { 1, 1, "walk:south-east" },
		{ 0, 1, "walk:south" }, { -1, 1, "walk:south-west" },
		{ -1, 0, "walk:west" }, { -1, -1, "walk:north-west" }
	};
	size_t i;

	if (!command || !action || !action_size) return false;
	switch (command->kind) {
	case WORLD_SPELUNK_COMMAND_WAIT:
		my_strcpy(action, "hold", action_size);
		return true;
	case WORLD_SPELUNK_COMMAND_JUMP:
		my_strcpy(action, "jump", action_size);
		return true;
	case WORLD_SPELUNK_COMMAND_GRIP:
		my_strcpy(action, "grip", action_size);
		return true;
	case WORLD_SPELUNK_COMMAND_MOVE:
		for (i = 0; i < N_ELEMENTS(moves); i++) {
			if (moves[i].dx == command->dx && moves[i].dy == command->dy) {
				my_strcpy(action, moves[i].name, action_size);
				return true;
			}
		}
		return false;
	default:
		return false;
	}
}

static bool guide_pending_completed(struct headless_loop_route_guide *guide,
		char *action, size_t action_size, char *error, size_t error_size)
{
	struct world_spelunk_runtime *runtime = player->spelunking;
	const struct world_spelunk_witness_step *step =
		&guide->generated.witness_steps[guide->next_step - 1];

	if (!guide->pending) return true;
	if (runtime->state.x != guide->pending_expected_x ||
			runtime->state.y != guide->pending_expected_y) {
		/* A side-view actor can consume an attempted move as an attack.  Retry
		 * the same ordinary command while the player remains at its origin;
		 * the bounded retry keeps a real rules rejection visible. */
		if (guide->pending_kind == WORLD_SPELUNK_WITNESS_COMMAND &&
				step->command.kind == WORLD_SPELUNK_COMMAND_MOVE &&
				runtime->state.x == guide->pending_origin_x &&
				runtime->state.y == guide->pending_origin_y &&
				guide->pending_retries++ < GUIDE_RETRY_MAX) {
			my_strcpy(action, guide->pending_action, action_size);
			return false;
		}
		if (error && error_size) {
			strnfmt(error, error_size,
				"certified cave route diverged at step %u: expected (%d,%d), got (%d,%d)",
				(unsigned int)(guide->next_step - 1),
				guide->pending_expected_x, guide->pending_expected_y,
				runtime->state.x, runtime->state.y);
		}
		return false;
	}
	if (guide->pending_kind == WORLD_SPELUNK_WITNESS_PLACE_PITON &&
			!world_spelunk_runtime_has_piton(runtime,
				guide->pending_expected_x, guide->pending_expected_y)) {
		if (error && error_size) my_strcpy(error,
			"certified cave route failed to place its piton", error_size);
		return false;
	}
	if (guide->pending_kind == WORLD_SPELUNK_WITNESS_DEPLOY_ROPE &&
			!world_spelunk_runtime_has_rope(runtime,
				guide->pending_expected_x, guide->pending_expected_y + 1)) {
		if (error && error_size) my_strcpy(error,
			"certified cave route failed to deploy its rope", error_size);
		return false;
	}
	guide->pending = false;
	return true;
}

static enum guide_action_result guide_next_action(
		struct headless_loop_route_guide *guide, char *action,
		size_t action_size, char *error, size_t error_size)
{
	struct world_spelunk_runtime *runtime = player->spelunking;

	if (!guide || !runtime) return GUIDE_ERROR;
	if (guide->pending) {
		bool completed = guide_pending_completed(guide, action, action_size,
			error, error_size);

		if (!completed) return action[0] ? GUIDE_ACTION : GUIDE_ERROR;
	}
	while (guide->next_step < guide->generated.witness_step_count) {
		const struct world_spelunk_witness_step *step =
			&guide->generated.witness_steps[guide->next_step++];

		if (step->kind == WORLD_SPELUNK_WITNESS_CHECKPOINT) {
			if (runtime->state.x != step->expected_x ||
					runtime->state.y != step->expected_y) {
				if (error && error_size) {
					strnfmt(error, error_size,
						"certified cave checkpoint expected (%d,%d), got (%d,%d)",
						step->expected_x, step->expected_y,
						runtime->state.x, runtime->state.y);
				}
				return GUIDE_ERROR;
			}
			continue;
		}
		action[0] = '\0';
		if (step->kind == WORLD_SPELUNK_WITNESS_COMMAND) {
			if (!witness_command_name(&step->command, action, action_size)) {
				if (error && error_size) my_strcpy(error,
					"certified cave route contains an unsupported command",
					error_size);
				return GUIDE_ERROR;
			}
		} else if (step->kind == WORLD_SPELUNK_WITNESS_PLACE_PITON) {
			my_strcpy(action, "piton", action_size);
		} else if (step->kind == WORLD_SPELUNK_WITNESS_DEPLOY_ROPE) {
			my_strcpy(action, "rope", action_size);
		} else {
			if (error && error_size) my_strcpy(error,
				"certified cave route contains an invalid step", error_size);
			return GUIDE_ERROR;
		}
		guide->pending = true;
		guide->pending_kind = step->kind;
		guide->pending_origin_x = runtime->state.x;
		guide->pending_origin_y = runtime->state.y;
		guide->pending_expected_x = step->expected_x;
		guide->pending_expected_y = step->expected_y;
		guide->pending_retries = 0;
		my_strcpy(guide->pending_action, action,
			sizeof(guide->pending_action));
		return GUIDE_ACTION;
	}
	return GUIDE_COMPLETE;
}

static bool prepare_generated_guide(struct headless_loop_bot *bot,
		char *error, size_t error_size)
{
	const struct world_spelunk_system *system = player->spelunking_system;
	const struct world_spelunk_system_node *node;
	const struct world_spelunk_system_portal *portal;
	struct headless_loop_route_guide *guide;
	size_t i;

	if (bot->guide) return true;
	if (!system || system->graph_version !=
			WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		if (error && error_size) my_strcpy(error,
			"long-loop acceptance requires a generated cave system", error_size);
		return false;
	}
	node = world_spelunk_system_node_by_id(system, system->active_node_id);
	portal = world_spelunk_system_portal_by_entry(system,
		player->world_location.entry);
	if (!node || !node->runtime || !portal || !portal->materialized ||
			!streq(portal->node_id, node->id)) {
		if (error && error_size) my_strcpy(error,
			"active generated cave has no matching return portal", error_size);
		return false;
	}
	guide = mem_zalloc(sizeof(*guide));
	if (world_spelunk_reconstruct_node_candidate(system, node->id,
			node->runtime->state.rules.action_energy, &guide->generated) !=
			WORLD_SPELUNK_RECONSTRUCTION_OK) {
		mem_free(guide);
		if (error && error_size) my_strcpy(error,
			"generated cave could not reconstruct its certified route",
			error_size);
		return false;
	}
	for (i = 0; i < guide->generated.witness_step_count; i++) {
		const struct world_spelunk_witness_step *step =
			&guide->generated.witness_steps[i];

		if (step->kind == WORLD_SPELUNK_WITNESS_CHECKPOINT &&
				step->expected_x == portal->x &&
				step->expected_y == portal->y) {
			guide->next_step = i + 1;
			break;
		}
	}
	if (i == guide->generated.witness_step_count ||
			player->spelunking->state.x != portal->x ||
			player->spelunking->state.y != portal->y) {
		world_spelunk_generated_layout_dispose(&guide->generated);
		mem_free(guide);
		if (error && error_size) my_strcpy(error,
			"generated cave arrival is not a certified portal checkpoint",
			error_size);
		return false;
	}
	guide->portal = loc(portal->x, portal->y);
	bot->guide = guide;
	return true;
}

static const struct world_fishing_rig *best_carried_rig(void)
{
	const struct world_fishing_rig *best = NULL;
	const struct object *obj;

	for (obj = player ? player->gear : NULL; obj; obj = obj->next) {
		const struct world_fishing_rig *rig =
			world_fishing_rig_for_kind(obj->kind);

		if (rig && (!best || rig->priority > best->priority)) best = rig;
	}
	return best;
}

static struct object *best_ground_rig(struct world_spelunk_runtime *runtime)
{
	struct object *best = NULL;
	struct object *obj;

	for (obj = runtime ? runtime->ground_objects : NULL; obj; obj = obj->next) {
		const struct world_fishing_rig *rig =
			world_fishing_rig_for_kind(obj->kind);
		const struct world_fishing_rig *current = best ?
			world_fishing_rig_for_kind(best->kind) : NULL;

		if (rig && (!current || rig->priority > current->priority)) best = obj;
	}
	return best;
}

static uint32_t carried_catch_value(void)
{
	const struct object *obj;
	uint32_t total = 0;

	for (obj = player ? player->gear : NULL; obj; obj = obj->next) {
		total += world_larder_object_value(obj) * (uint32_t)obj->number;
	}
	return total;
}

static const char *fishing_action_for_state(void)
{
	struct world_fishing_runtime *runtime = player->fishing;
	const struct world_fishing_fish *target = NULL;
	uint32_t best_value = 0;
	int best_distance = INT_MAX;
	int i;

	if (!runtime) return "fish:start";
	if (runtime->phase == WORLD_FISHING_BITE) return "fish:strike";
	if (runtime->phase == WORLD_FISHING_WINDING) return "fish:reel";
	if (!runtime->fish_count) return "fish:stop";
	for (i = 0; i < runtime->fish_count; i++) {
		const struct world_fishing_fish *fish = &runtime->fish[i];
		uint32_t value = world_fishing_kind_larder_value(fish->kind);
		int desired_reach = runtime->columns - 1 - fish->col;
		int distance;

		if (desired_reach < WORLD_FISHING_INITIAL_REACH ||
				desired_reach > runtime->maximum_reach ||
				fish->row > runtime->maximum_depth) {
			continue;
		}
		distance = ABS(runtime->rod_reach - desired_reach) +
			ABS(runtime->depth - fish->row);
		if (!target || value > best_value ||
				(value == best_value && distance < best_distance)) {
			target = fish;
			best_value = value;
			best_distance = distance;
		}
	}
	if (!target) return "fish:stop";
	if (runtime->depth < target->row) return "fish:lower";
	if (runtime->depth > target->row) return "fish:raise";
	if (world_fishing_hook_col(runtime) < target->col) return "fish:retract";
	if (world_fishing_hook_col(runtime) > target->col) return "fish:extend";
	return "fish:hold";
}

static bool top_down_target_step(struct loc target, char *action,
		size_t action_size)
{
	static const int dx[] = { 0, 1, 0, -1 };
	static const int dy[] = { -1, 0, 1, 0 };
	static const char *names[] = {
		"walk:north", "walk:east", "walk:south", "walk:west"
	};
	int *previous;
	uint8_t *direction;
	struct loc *queue;
	int cells;
	int head = 0;
	int tail = 0;
	int target_index;
	int first;
	int i;

	if (!cave || !square_in_bounds(cave, target)) return false;
	if (loc_eq(player->grid, target)) {
		action[0] = '\0';
		return true;
	}
	cells = cave->width * cave->height;
	previous = mem_alloc((size_t)cells * sizeof(*previous));
	direction = mem_alloc((size_t)cells * sizeof(*direction));
	queue = mem_alloc((size_t)cells * sizeof(*queue));
	for (i = 0; i < cells; i++) previous[i] = -2;
	previous[player->grid.y * cave->width + player->grid.x] = -1;
	queue[tail++] = player->grid;
	while (head < tail) {
		struct loc here = queue[head++];

		for (i = 0; i < 4; i++) {
			struct loc next = loc(here.x + dx[i], here.y + dy[i]);
			int index;

			if (!square_in_bounds(cave, next) ||
					!square_ispassable(cave, next)) {
				continue;
			}
			index = next.y * cave->width + next.x;
			if (previous[index] != -2) continue;
			previous[index] = here.y * cave->width + here.x;
			direction[index] = (uint8_t)i;
			queue[tail++] = next;
			if (loc_eq(next, target)) {
				head = tail;
				break;
			}
		}
	}
	target_index = target.y * cave->width + target.x;
	if (previous[target_index] == -2) {
		mem_free(queue);
		mem_free(direction);
		mem_free(previous);
		return false;
	}
	first = target_index;
	while (previous[first] >= 0 && previous[first] !=
			player->grid.y * cave->width + player->grid.x) {
		first = previous[first];
	}
	my_strcpy(action, names[direction[first]], action_size);
	mem_free(queue);
	mem_free(direction);
	mem_free(previous);
	return true;
}

static bool larder_store_grid(struct loc *target)
{
	int x;
	int y;

	if (!cave || !target) return false;
	for (y = 0; y < cave->height; y++) {
		for (x = 0; x < cave->width; x++) {
			struct loc grid = loc(x, y);

			if (store_accepts_larder_donations(store_at(cave, grid))) {
				*target = grid;
				return true;
			}
		}
	}
	return false;
}

static bool carried_larder_reward(void)
{
	const struct object *obj;
	int i;

	for (obj = player ? player->gear : NULL; obj; obj = obj->next) {
		for (i = 0; i < z_info->store_max; i++) {
			size_t j;

			for (j = 0; j < stores[i].larder_num; j++) {
				if (stores[i].larder_table[j].kind == obj->kind &&
						world_larder_has_milestone(&player->larder,
							stores[i].larder_table[j].milestone->id)) {
					return true;
				}
			}
		}
	}
	return false;
}

static const struct world_route *first_meridian_route(void)
{
	const struct world_route *route;

	for (route = world_routes; route; route = route->next) {
		const struct level *destination;

		if (!streq(route->from, player->world_location.id) ||
				world_route_is_retired(route) ||
				world_route_is_cross_mode(route)) {
			continue;
		}
		destination = level_by_id(route->to);
		if (destination && destination->mode == WORLD_MODE_TOP_DOWN &&
				destination->depth > 0) {
			return route;
		}
	}
	return NULL;
}

void headless_loop_bot_init(struct headless_loop_bot *bot)
{
	if (!bot) return;
	memset(bot, 0, sizeof(*bot));
	bot->stage = HEADLESS_LOOP_FIND_RIG;
}

void headless_loop_bot_dispose(struct headless_loop_bot *bot)
{
	if (!bot || !bot->guide) return;
	world_spelunk_generated_layout_dispose(&bot->guide->generated);
	mem_free(bot->guide);
	bot->guide = NULL;
}

const char *headless_loop_bot_stage_name(const struct headless_loop_bot *bot)
{
	if (!bot) return "invalid";
	switch (bot->stage) {
	case HEADLESS_LOOP_FIND_RIG: return "find-rig";
	case HEADLESS_LOOP_FIND_BANK: return "find-bank";
	case HEADLESS_LOOP_FISH: return "fish";
	case HEADLESS_LOOP_RETURN: return "return";
	case HEADLESS_LOOP_DONATE: return "donate";
	case HEADLESS_LOOP_BUY_REWARD: return "buy-reward";
	case HEADLESS_LOOP_ENTER_MERIDIAN: return "enter-meridian";
	case HEADLESS_LOOP_USE_REWARD: return "use-reward";
	case HEADLESS_LOOP_DONE: return "done";
	default: return "invalid";
	}
}

enum headless_loop_bot_result headless_loop_bot_next(
		struct headless_loop_bot *bot, char *action, size_t action_size,
		char *error, size_t error_size)
{
	if (!bot || !action || !action_size || !player || player->is_dead) {
		return bot_error(error, error_size, "loop bot has no live player");
	}
	action[0] = '\0';
	for (;;) {
		switch (bot->stage) {
		case HEADLESS_LOOP_FIND_RIG:
			if (!world_spelunk_player_is_active(player)) {
				return bot_error(error, error_size,
					"loop bot did not begin in side-view mode");
			}
			if (!prepare_generated_guide(bot, error, error_size)) {
				return HEADLESS_LOOP_BOT_ERROR;
			}
			if (best_carried_rig()) {
				bot->stage = HEADLESS_LOOP_FIND_BANK;
				continue;
			} else {
				struct object *rig = best_ground_rig(player->spelunking);
				enum guide_action_result result;

				if (!rig) return bot_error(error, error_size,
					"no authored fishing rig remains in the cave");
				if (rig->grid.x == player->spelunking->state.x &&
						rig->grid.y == player->spelunking->state.y) {
					my_strcpy(action, "pickup", action_size);
					break;
				}
				result = guide_next_action(bot->guide, action, action_size,
					error, error_size);
				if (result == GUIDE_ERROR) return HEADLESS_LOOP_BOT_ERROR;
				if (result == GUIDE_COMPLETE) return bot_error(error, error_size,
					"certified cave route ended before reaching the authored rig");
				break;
			}
		case HEADLESS_LOOP_FIND_BANK:
			if (!world_spelunk_player_is_active(player)) {
				return bot_error(error, error_size,
					"loop bot left the cave before reaching water");
			} else {
				struct world_fishing_site site;
				enum guide_action_result result;

				if (world_fishing_site_query(player, NULL, &site) ==
						WORLD_FISHING_SITE_OK) {
					bot->stage = HEADLESS_LOOP_FISH;
					continue;
				}
				result = guide_next_action(bot->guide, action, action_size,
					error, error_size);
				if (result == GUIDE_ERROR) return HEADLESS_LOOP_BOT_ERROR;
				if (result == GUIDE_COMPLETE) return bot_error(error, error_size,
					"certified cave route ended before reaching a fishing bank");
				break;
			}
		case HEADLESS_LOOP_FISH:
			{
				const struct world_larder_milestone_definition *next =
					world_larder_next_milestone(&player->larder);
				uint32_t available = player->larder.food_points +
					carried_catch_value();

				if (!next || available >= next->target) {
					if (player->fishing) {
						my_strcpy(action, "fish:stop", action_size);
						break;
					}
					bot->stage = HEADLESS_LOOP_RETURN;
					continue;
				}
				my_strcpy(action, fishing_action_for_state(), action_size);
				break;
			}
		case HEADLESS_LOOP_RETURN:
			if (player->word_recall) {
				my_strcpy(action, "hold", action_size);
				break;
			}
			if (!world_spelunk_player_is_active(player)) {
				bot->stage = HEADLESS_LOOP_DONATE;
				continue;
			} else {
				enum guide_action_result result;

				result = guide_next_action(bot->guide, action, action_size,
					error, error_size);
				if (result == GUIDE_ERROR) return HEADLESS_LOOP_BOT_ERROR;
				if (result == GUIDE_ACTION) break;
				if (player->spelunking->state.x == bot->guide->portal.x &&
						player->spelunking->state.y == bot->guide->portal.y) {
					my_strcpy(action, "read-recall", action_size);
					break;
				}
				if (player->spelunking->state.y != bot->guide->portal.y) {
					return bot_error(error, error_size,
						"certified cave route ended off the return portal rail");
				}
				my_strcpy(action,
					player->spelunking->state.x < bot->guide->portal.x ?
						"walk:east" : "walk:west", action_size);
				break;
			}
		case HEADLESS_LOOP_DONATE:
			if (world_spelunk_player_is_active(player)) {
				return bot_error(error, error_size,
					"loop bot did not return to top-down play");
			}
			if (world_larder_next_milestone(&player->larder)) {
				struct loc store_grid;

				if (!larder_store_grid(&store_grid)) return bot_error(error,
					error_size, "loaded location has no larder facility");
				if (!loc_eq(player->grid, store_grid)) {
					if (!top_down_target_step(store_grid, action, action_size)) {
						return bot_error(error, error_size,
							"no top-down path reaches the larder facility");
					}
					break;
				}
				if (!carried_catch_value()) return bot_error(error, error_size,
					"the bot reached the larder without enough catches");
				my_strcpy(action, "donate-catch", action_size);
				break;
			}
			bot->stage = HEADLESS_LOOP_BUY_REWARD;
			continue;
		case HEADLESS_LOOP_BUY_REWARD:
			if (!carried_larder_reward()) {
				my_strcpy(action, "buy-larder-reward", action_size);
				break;
			}
			bot->stage = HEADLESS_LOOP_ENTER_MERIDIAN;
			continue;
		case HEADLESS_LOOP_ENTER_MERIDIAN:
			{
				const struct world_route *route = first_meridian_route();

				if (!route) {
					const struct level *level = world_player_level(player);

					if (level && level->depth > 0) {
						bot->stage = HEADLESS_LOOP_USE_REWARD;
						continue;
					}
					return bot_error(error, error_size,
						"no authored route leads from the larder location to the Meridian");
				} else {
					struct loc route_grid;

					if (world_entry_locate(cave, route->from_entry, &route_grid) !=
							WORLD_ENTRY_OK) {
						return bot_error(error, error_size,
							"the Meridian route source could not be located");
					}
					if (!loc_eq(player->grid, route_grid)) {
						if (!top_down_target_step(route_grid, action, action_size)) {
							return bot_error(error, error_size,
								"no top-down path reaches the Meridian route");
						}
						break;
					}
					/* Legacy-depth dungeon stairs still use Angband's ordinary
					 * ascend/descend commands.  World routes identify and locate
					 * their endpoints; only native physical routes execute through
					 * the route command itself. */
					if (streq(route->kind, "stairs") &&
							!world_route_is_physical(route)) {
						const struct level *source = level_by_id(route->from);
						const struct level *destination = level_by_id(route->to);

						my_strcpy(action, source && destination &&
							destination->depth < source->depth ? "ascend" :
							"descend", action_size);
					} else {
						strnfmt(action, action_size, "route:%s", route->id);
					}
					break;
				}
			}
		case HEADLESS_LOOP_USE_REWARD:
			if (player->timed[TMD_OPP_POIS] > 0) {
				bot->stage = HEADLESS_LOOP_DONE;
				continue;
			}
			if (!carried_larder_reward()) return bot_error(error, error_size,
				"the larder reward was lost before use");
			my_strcpy(action, "eat-larder-reward", action_size);
			break;
		case HEADLESS_LOOP_DONE:
			return HEADLESS_LOOP_BOT_COMPLETE;
		default:
			return bot_error(error, error_size, "loop bot entered an invalid stage");
		}
		bot->actions++;
		return HEADLESS_LOOP_BOT_ACTION;
	}
}
