/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-spelunking.c
 * \brief Semantic commands for the side-view spelunking location mode.
 *
 *
 */

#include "angband.h"
#include "cave.h"
#include "cmd-spelunking.h"

#include "game-world.h"
#include "player.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-combat.h"
#include "world-spelunking-door.h"
#include "world-spelunking-fixture.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "player-resource.h"
#include "world-spelunking-terrain.h"
#include "world-spelunking-rest.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-system.h"

void do_cmd_spelunk_rest(struct command *cmd)
{
	struct world_spelunk_command wait = { WORLD_SPELUNK_COMMAND_WAIT, 0, 0 };
	struct world_spelunk_action_report report;
	const char *reason;
	int n, energy;
	cmd_cancel_repeat();
	cmd_disable_repeat();
	if (!cmd || player->upkeep->energy_use ||
		cmd_get_arg_choice(cmd, "choice", &n) != CMD_OK ||
		n > 9999 || n < INT16_MIN || (n < 0 && !player_resting_is_special(n))) return;
	player->upkeep->redraw |= PR_STATE;
	reason = world_spelunk_rest_block_reason(player);
	if (reason) {
		/* This command has consumed its continuation. Do not leave a stale
		 * disturbance latch which would swallow the next manual Rest. */
		player_resting_cancel(player, false);
		msg("Rest stopped: %s", reason);
		return;
	}
	player_resting_set_count(player, (int16_t)n);
	if (!player_is_resting(player)) return; /* includes a prior disturbance */
	if (world_spelunk_rest_is_complete(player, n)) {
		player_resting_cancel(player, false);
		return;
	}
	/* Use the ordinary cave Wait: search, fixture triggers and resource rules
	 * have one owner. The scheduler still runs creatures, hunger and hazards. */
	if (world_spelunk_player_apply(player, &wait, &report) != WORLD_SPELUNK_PLAYER_TURN) {
		player_resting_cancel(player, false);
		return;
	}
	if (!player_is_resting(player)) {
		/* Wait may itself trigger a trap and disturb. There is no queued
		 * continuation to consume that latch in this case. */
		player_resting_cancel(player, false);
		return;
	}
	energy = player->upkeep->energy_use;
	player_resting_step_turn(player);
	player->upkeep->energy_use = energy;
	if (player_is_resting(player)) {
		if (!cmdq_push(CMD_REST))
			cmd_set_arg_choice(cmdq_peek(), "choice", player_resting_count(player));
		else player_resting_cancel(player, false);
	} else player_resting_cancel(player, false);
}

static bool spelunk_command_is_valid(enum world_spelunk_command_kind kind,
		int dx, int dy)
{
	switch (kind) {
	case WORLD_SPELUNK_COMMAND_MOVE:
		return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 &&
			(dx != 0 || dy != 0);
	case WORLD_SPELUNK_COMMAND_WAIT:
	case WORLD_SPELUNK_COMMAND_JUMP:
	case WORLD_SPELUNK_COMMAND_GRIP:
		return dx == 0 && dy == 0;
	default:
		return false;
	}
}

void do_cmd_spelunk_tunnel(struct command *cmd)
{
	struct world_spelunk_runtime *r;
	enum world_spelunk_cut_result result;
	int dir, x, y, chance, cost;
	cmd_cancel_repeat();
	cmd_disable_repeat();
	if (!cmd || !world_spelunk_player_is_active(player) || player->is_dead ||
		player->upkeep->energy_use) return;
	r = player->spelunking;
	if (r->state.rules.action_energy > INT_MAX) return;
	if (!world_spelunk_has_stable_floor(&r->state)) {
		msg("You need a stable rock floor to tunnel, not a rope or grip.");
		return;
	}
	if (cmd_get_direction(cmd, "direction", &dir, false) || dir < 1 || dir > 9 || dir == 5) return;
	player_confuse_dir(player, &dir, false);
	x = r->state.x + ddgrid[dir].x;
	y = r->state.y + ddgrid[dir].y;
	cost = z_info->stamina_dig_cost ? z_info->stamina_dig_cost : 5;
	result = world_spelunk_cut_check(player,x,y,cost);
	if (result != SPELUNK_CUT_OK) {
		msg("%s", world_spelunk_cut_reason(result));
		return;
	}
	chance = world_spelunk_dig_chance(r,x,y,calc_digging_skill(player,NULL));
	if (!chance) {
		msg("You need a better digging tool or more digging skill for %s.", world_spelunk_rock_name(r,x,y));
		return;
	}
	if (!player_stamina_spend(player,cost)) {
		msg("You are too exhausted to dig. Wait on stable ground to recover.");
		return;
	}
	r->state.stamina = player_stamina_current(player);
	player->upkeep->energy_use = (int)r->state.rules.action_energy;
	if (randint0(1600) < chance) {
		const char *name = world_spelunk_rock_name(r,x,y);
		/* The preflight and edit share one transaction boundary. Spending
		 * stamina cannot remove stable footing before the cut is committed. */
		result = world_spelunk_cut_rock(player,x,y);
		if (result == SPELUNK_CUT_OK) {
			msgt(MSG_DIG, "You finish cutting through %s (-%d stamina).", name, cost);
		} else {
			msg("%s", world_spelunk_cut_reason(result));
		}
	} else {
		msgt(MSG_DIG, "You chip at %s (-%d stamina).", world_spelunk_rock_name(r,x,y), cost);
	}
}

enum cave_interaction { CAVE_OPEN, CAVE_CLOSE, CAVE_DISARM };

static bool interaction_candidate(const struct world_spelunk_runtime *r,
		int x, int y, enum cave_interaction action)
{
	const struct world_spelunk_door *d = world_spelunk_door_at(r, x, y);
	const struct world_spelunk_fixture *f = world_spelunk_fixture_at(r, x, y);
	const struct world_spelunk_fixture_definition *def = f ?
		world_spelunk_fixture_by_id(f->id) : NULL;
	if (action == CAVE_DISARM)
		return f && f->known && f->trap_known == SPELUNK_TRAP_ARMED;
	if (action == CAVE_OPEN && f && f->known && def && def->kind == SPELUNK_CHEST)
		return f->known != SPELUNK_FIXTURE_SPENT;
	return d && d->known && (action == CAVE_OPEN ?
		d->known != WORLD_SPELUNK_DOOR_OPEN : d->known == WORLD_SPELUNK_DOOR_OPEN);
}

static void cave_interact(struct command *cmd, enum cave_interaction action)
{
	struct world_spelunk_runtime *r;
	struct world_spelunk_door *door;
	struct world_spelunk_fixture *fixture;
	const struct world_spelunk_door_definition *def;
	int dir, x, y, i, candidates = 0, candidate_dir = 0;
	bool open = action == CAVE_OPEN;

	/* Native Open/Close/Disarm install automatic repeats. Cave attempts are
	 * explicit, including failure; one keypress cannot silently pick a lock. */
	cmd_cancel_repeat();
	cmd_disable_repeat();
	if (!cmd || !world_spelunk_player_is_active(player) || player->is_dead ||
		player->upkeep->energy_use || !world_spelunk_runtime_is_valid(player->spelunking)) return;
	r = player->spelunking;
	if (r->state.rules.action_energy > INT_MAX) return;
	if (!world_spelunk_has_stable_floor(&r->state)) {
		msg("You need stable footing to work on a cave fixture.");
		return;
	}
	if (cmd_get_arg_direction(cmd, "direction", &dir) != CMD_OK || dir == DIR_UNKNOWN) {
		for (i = -1; i <= 1; i++) {
			if (interaction_candidate(r, r->state.x + i, r->state.y, action)) {
				candidates++;
				candidate_dir = i < 0 ? 4 : i > 0 ? 6 : 5;
			}
		}
		if (candidates == 1) dir = candidate_dir;
		else if (cmd_get_direction(cmd, "direction", &dir, action != CAVE_CLOSE)) return;
	}
	if (dir < 1 || dir > 9) return;
	player_confuse_dir(player, &dir, false);
	x = r->state.x + ddgrid[dir].x;
	y = r->state.y + ddgrid[dir].y;
	if (y != r->state.y) {
		msg("Stand beside the fixture, or on a trap or chest, to reach it.");
		return;
	}
	fixture = world_spelunk_fixture_at_mutable(r, x, y);
	if (action == CAVE_DISARM || (open && fixture && fixture->known)) {
		if (!fixture || !fixture->known) {
			msg("There is no known trap or chest there.");
			return;
		}
		if (world_spelunk_runtime_actor_at(r, x, y)) {
			msg("A creature is in the way.");
			return;
		}
		if (world_spelunk_fixture_interact(player, fixture, action == CAVE_DISARM)) {
			player->upkeep->energy_use = (int)r->state.rules.action_energy;
			player->upkeep->redraw |= PR_MAP | PR_ITEMLIST;
			event_signal_point(EVENT_MAP, -1, -1);
		}
		return;
	}
	door = world_spelunk_door_at_mutable(r, x, y);
	if (!door || !door->known) {
		msg("There is no known cave door there.");
		return;
	}
	if (x == r->state.x) {
		msg("Stand beside the door to reach its latch.");
		return;
	}
	if (open == (door->state == WORLD_SPELUNK_DOOR_OPEN)) {
		msg(open ? "That door is already open." : "That door is already closed.");
		return;
	}
	if (!open && (world_spelunk_runtime_actor_at(r, x, y) ||
		world_spelunk_runtime_ground_object_at(r, x, y))) {
		msg("Something is in the doorway.");
		return;
	}
	def = world_spelunk_door_by_id(door->id);
	if (!def) return;
	if (open && door->state == WORLD_SPELUNK_DOOR_LOCKED) {
		int chance = calc_unlocking_chance(player, def->lock_power,
			player->timed[TMD_BLIND] != 0);
		door->known = WORLD_SPELUNK_DOOR_LOCKED;
		player->upkeep->energy_use = (int)r->state.rules.action_energy;
		if (randint0(100) >= chance) {
			msgt(MSG_LOCKPICK_FAIL, "You fail to pick the %s's lock.", def->name);
			return;
		}
		sound(MSG_LOCKPICK);
	}
	if (!world_spelunk_door_set_open(r, x, y, open)) return;
	player->upkeep->energy_use = (int)r->state.rules.action_energy;
	msgt(open ? MSG_OPENDOOR : MSG_SHUTDOOR, "You %s the %s.", open ? "open" : "close", def->name);
	event_signal_point(EVENT_MAP, -1, -1);
}

void do_cmd_spelunk_door(struct command *cmd, bool open)
{
	cave_interact(cmd, open ? CAVE_OPEN : CAVE_CLOSE);
}

void do_cmd_spelunk_disarm(struct command *cmd)
{
	cave_interact(cmd, CAVE_DISARM);
}

errr cmdq_push_spelunk_action(enum world_spelunk_command_kind kind,
		int dx, int dy)
{
	struct command *queued;

	if (!spelunk_command_is_valid(kind, dx, dy) ||
			cmdq_push(CMD_SPELUNK_ACTION)) {
		return 1;
	}
	queued = cmdq_peek();
	cmd_set_arg_choice(queued, "kind", kind);
	cmd_set_arg_number(queued, "dx", dx);
	cmd_set_arg_number(queued, "dy", dy);
	return 0;
}

errr cmdq_push_spelunk_local(enum world_spelunk_local_action action)
{
	struct command *queued;

	if (action < WORLD_SPELUNK_LOCAL_PITON ||
			action > WORLD_SPELUNK_LOCAL_PICKUP ||
			cmdq_push(CMD_SPELUNK_LOCAL)) {
		return 1;
	}
	queued = cmdq_peek();
	cmd_set_arg_choice(queued, "action", action);
	return 0;
}

errr cmdq_push_spelunk_passage(
		enum world_spelunk_passage_direction direction)
{
	struct command *queued;

	if ((direction != WORLD_SPELUNK_PASSAGE_UP &&
			direction != WORLD_SPELUNK_PASSAGE_DOWN) ||
			cmdq_push(CMD_SPELUNK_PASSAGE)) {
		return 1;
	}
	queued = cmdq_peek();
	cmd_set_arg_choice(queued, "direction", direction);
	return 0;
}

errr cmdq_push_spelunk_exit(void)
{
	return cmdq_push(CMD_SPELUNK_EXIT);
}

errr cmdq_push_spelunk_peek(enum world_spelunk_peek_action action,
		int dx, int dy)
{
	struct command *queued;
	bool move = action == WORLD_SPELUNK_PEEK_MOVE;

	if (action < WORLD_SPELUNK_PEEK_BEGIN ||
			action > WORLD_SPELUNK_PEEK_TOGGLE ||
			(move ? (dx < -1 || dx > 1 || dy < -1 || dy > 1 ||
			(dx == 0 && dy == 0)) : (dx != 0 || dy != 0)) ||
			cmdq_push(CMD_SPELUNK_PEEK)) {
		return 1;
	}
	queued = cmdq_peek();
	cmd_set_arg_choice(queued, "action", action);
	cmd_set_arg_number(queued, "dx", dx);
	cmd_set_arg_number(queued, "dy", dy);
	return 0;
}

void do_cmd_spelunk_action(struct command *cmd)
{
	struct world_spelunk_command action;
	struct world_spelunk_action_report report;
	struct world_spelunk_combat_report combat_report;
	enum world_spelunk_player_result result;
	enum world_spelunk_combat_result combat_result;
	int kind;
	int old_x, old_y;

	if (!cmd || cmd_get_arg_choice(cmd, "kind", &kind) != CMD_OK ||
			cmd_get_arg_number(cmd, "dx", &action.dx) != CMD_OK ||
			cmd_get_arg_number(cmd, "dy", &action.dy) != CMD_OK) {
		return;
	}
	action.kind = (enum world_spelunk_command_kind)kind;
	if (!spelunk_command_is_valid(action.kind, action.dx, action.dy) ||
			!world_spelunk_player_is_active(player)) {
		return;
	}
	if (action.kind == WORLD_SPELUNK_COMMAND_MOVE) {
		combat_result = world_spelunk_player_bump_attack(player,
			action.dx, action.dy, &combat_report);
		switch (combat_result) {
		case WORLD_SPELUNK_COMBAT_UNSTABLE:
			msg("You need stable footing to attack.");
			return;
		case WORLD_SPELUNK_COMBAT_TURN:
			if (!combat_report.hit) {
				msg("You miss the %s.",
					world_spelunk_actor_name(combat_report.actor_id));
			} else if (combat_report.damage > 0) {
				msg("You hit the %s for %d damage.",
					world_spelunk_actor_name(combat_report.actor_id),
					combat_report.damage);
			} else {
				msg("You fail to harm the %s.",
					world_spelunk_actor_name(combat_report.actor_id));
			}
			event_signal_point(EVENT_MAP, -1, -1);
			return;
		case WORLD_SPELUNK_COMBAT_KILLED:
			msg("You kill the %s.",
				world_spelunk_actor_name(combat_report.actor_id));
			event_signal_point(EVENT_MAP, -1, -1);
			return;
		case WORLD_SPELUNK_COMBAT_NO_TARGET:
			break;
		case WORLD_SPELUNK_COMBAT_INVALID:
		default:
			return;
		}
	}
	old_x = player->spelunking->state.x;
	old_y = player->spelunking->state.y;
	result = world_spelunk_player_apply(player, &action, &report);
	if (result != WORLD_SPELUNK_PLAYER_TURN &&
			result != WORLD_SPELUNK_PLAYER_DEAD) {
		if (action.kind == WORLD_SPELUNK_COMMAND_MOVE &&
				result == WORLD_SPELUNK_PLAYER_REJECTED) {
			const struct world_spelunk_door *door = world_spelunk_door_at(player->spelunking,
				player->spelunking->state.x + action.dx, player->spelunking->state.y + action.dy);
			if (door && door->known) msg("A closed cave door blocks the way. Use o to open it.");
			sound(MSG_HITWALL);
		}
		return;
	}
	if (action.kind == WORLD_SPELUNK_COMMAND_MOVE) {
		if (player->spelunking->state.movement == WORLD_SPELUNK_CLIMBING ||
				player->spelunking->state.movement == WORLD_SPELUNK_HANGING) {
			sound(MSG_SPELUNK_CLIMB);
		} else if (report.event == WORLD_SPELUNK_EVENT_MOVED ||
				report.event == WORLD_SPELUNK_EVENT_LANDED ||
				report.event == WORLD_SPELUNK_EVENT_WATER_LANDED ||
				report.event == WORLD_SPELUNK_EVENT_SWAM) {
			sound(MSG_WALK);
		}
	} else if (action.kind == WORLD_SPELUNK_COMMAND_JUMP) {
		/* The authored climb cue already has the short body-and-gear effort
		 * needed here; keep jump audio within the existing sound manifest. */
		sound(MSG_SPELUNK_CLIMB);
	}
	/* Only arrivals schedule pickup: not blocked steps, attacks, peek, rest
	 * or dropping. Bind the background command to this exact destination. */
	if (!player->is_dead && (old_x != player->spelunking->state.x ||
			old_y != player->spelunking->state.y) &&
			world_spelunk_runtime_ground_object_at(player->spelunking,
				player->spelunking->state.x, player->spelunking->state.y) &&
			!cmdq_push(CMD_AUTOPICKUP)) {
		struct command *pickup = cmdq_peek();
		cmd_set_arg_string(pickup, "cave-location", player->spelunking->location_id);
		cmd_set_arg_string(pickup, "cave-node", player->spelunking_system ?
			player->spelunking_system->active_node_id : "");
		cmd_set_arg_number(pickup, "x", player->spelunking->state.x);
		cmd_set_arg_number(pickup, "y", player->spelunking->state.y);
		pickup->background_command = 2;
	}
	combat_result = world_spelunk_player_resolve_collision(player,
		&combat_report);
	if (combat_result == WORLD_SPELUNK_COMBAT_KILLED) {
		msg("You crash into and kill the %s.",
			world_spelunk_actor_name(combat_report.actor_id));
	} else if (combat_result != WORLD_SPELUNK_COMBAT_NO_TARGET) {
		return;
	}
	/* Commit the destination before the normal death UI can take over.  A
	 * fatal fall therefore remains visibly resolved at its landing cell. */
	event_signal_point(EVENT_MAP, -1, -1);
	switch (report.event) {
	case WORLD_SPELUNK_EVENT_ROPE_ATTACHED:
		msg("You catch the rope.");
		break;
	case WORLD_SPELUNK_EVENT_ROPE_EXHAUSTED:
		msg("Your arms give out and you fall!");
		break;
	case WORLD_SPELUNK_EVENT_GRIP_EXHAUSTED:
		msg("Your grip fails and you fall!");
		break;
	case WORLD_SPELUNK_EVENT_FALL_HURT:
		msg("You fall %d tile%s and hit the ground at %d,%d.",
			report.fall_tiles, PLURAL(report.fall_tiles),
			player->spelunking->state.x, player->spelunking->state.y);
		break;
	default:
		break;
	}
}

void do_cmd_spelunk_local(struct command *cmd)
{
	struct world_spelunk_local_report report;
	enum world_spelunk_local_result result;
	int action;

	if (!cmd || cmd_get_arg_choice(cmd, "action", &action) != CMD_OK ||
			action < WORLD_SPELUNK_LOCAL_PITON ||
			action > WORLD_SPELUNK_LOCAL_PICKUP ||
			!world_spelunk_player_is_active(player)) {
		return;
	}
	result = world_spelunk_player_apply_local(player,
		(enum world_spelunk_local_action)action, &report);
	switch (result) {
	case WORLD_SPELUNK_LOCAL_TURN:
		if (action == WORLD_SPELUNK_LOCAL_PITON) {
			msgt(MSG_PITON_HAMMER, "You hammer a piton into the rock.");
		} else if (action == WORLD_SPELUNK_LOCAL_ROPE) {
			msgt(MSG_ROPE_DEPLOY,
				"Whirrp! You deploy the rope down %d tile%s.", report.affected,
				PLURAL(report.affected));
		} else if (action == WORLD_SPELUNK_LOCAL_PICKUP) {
			sound(report.completion_message > MSG_GENERIC ?
				report.completion_message : MSG_PICKUP);
		}
		break;
	case WORLD_SPELUNK_LOCAL_NO_TOOL:
		msg(action == WORLD_SPELUNK_LOCAL_PITON ?
			"You have no pitons." : "You have no rope.");
		break;
	case WORLD_SPELUNK_LOCAL_UNSTABLE:
		msg("You need a stable hold before using that tool.");
		break;
	case WORLD_SPELUNK_LOCAL_ALREADY_PRESENT:
		msg("There is already fixed equipment here.");
		break;
	case WORLD_SPELUNK_LOCAL_NO_BRACE:
		msg("There is no rock here to brace a piton against.");
		break;
	case WORLD_SPELUNK_LOCAL_NO_ANCHOR:
		msg("You need a piton here to anchor the rope.");
		break;
	case WORLD_SPELUNK_LOCAL_NO_SHAFT:
		msg("There is no open shaft below for the rope.");
		break;
	case WORLD_SPELUNK_LOCAL_NOTHING_HERE:
		msg("There is nothing here to pick up.");
		break;
	case WORLD_SPELUNK_LOCAL_NO_ROOM:
		msg("You have no room for anything here.");
		break;
	case WORLD_SPELUNK_LOCAL_INVALID:
	default:
		msg("That side-view action is not available now.");
		break;
	}
}

static void execute_spelunk_exit(bool passage_fallback)
{
	struct world_spelunking_exit_report report;

	switch (world_spelunking_exit_execute(player, cave, &report)) {
	case WORLD_SPELUNKING_EXIT_OK:
		{
			const struct world_route *route = world_route_by_id(report.route_id);

			msgt(MSG_TPLEVEL, "%s", route && route->message ? route->message :
				"You climb out of the shaft.");
		}
		break;
	case WORLD_SPELUNKING_EXIT_UNSTABLE:
		msg("You need stable footing before leaving.");
		break;
	case WORLD_SPELUNKING_EXIT_NOT_HERE:
		msg(passage_fallback ?
			"There is no upward passage or shaft exit here." :
			"There is no shaft exit here.");
		break;
	case WORLD_SPELUNKING_EXIT_RESOURCES_REJECTED:
		msg("You cannot safely make the climb now.");
		break;
	case WORLD_SPELUNKING_EXIT_INVALID:
	case WORLD_SPELUNKING_EXIT_TRANSITION_REJECTED:
	case WORLD_SPELUNKING_EXIT_TIME_REJECTED:
	case WORLD_SPELUNKING_EXIT_DESTINATION_REJECTED:
	default:
		msg("You cannot leave by that shaft now.");
		break;
	}
}

void do_cmd_spelunk_passage(struct command *cmd)
{
	struct world_spelunk_player_passage_report report;
	int direction;

	cmd_disable_repeat();
	if (!cmd || cmd_get_arg_choice(cmd, "direction", &direction) != CMD_OK ||
			(direction != WORLD_SPELUNK_PASSAGE_UP &&
			 direction != WORLD_SPELUNK_PASSAGE_DOWN)) {
		return;
	}
	switch (world_spelunk_player_use_passage(player,
			(enum world_spelunk_passage_direction)direction, &report)) {
	case WORLD_SPELUNK_PLAYER_PASSAGE_OK:
		msg(direction == WORLD_SPELUNK_PASSAGE_DOWN ?
			(report.first_visit ?
			 "You descend into an uncharted cave section." :
			 "You descend into the next cave section.") :
			"You ascend into the previous cave section.");
		event_signal_point(EVENT_MAP, -1, -1);
		break;
	case WORLD_SPELUNK_PLAYER_PASSAGE_NOT_HERE:
		if (direction == WORLD_SPELUNK_PASSAGE_UP) {
			execute_spelunk_exit(true);
		} else {
			msg("There is no downward passage here.");
		}
		break;
	case WORLD_SPELUNK_PLAYER_PASSAGE_UNSTABLE:
		msg("You need stable footing before using the passage.");
		break;
	case WORLD_SPELUNK_PLAYER_PASSAGE_GENERATION_FAILED:
		msg("The passage ahead cannot be entered safely.");
		break;
	case WORLD_SPELUNK_PLAYER_PASSAGE_BLOCKED:
		msg("Something blocks the far end of the passage.");
		break;
	case WORLD_SPELUNK_PLAYER_PASSAGE_INVALID:
	default:
		msg("You cannot use that cave passage now.");
		break;
	}
}

void do_cmd_spelunk_exit(struct command *cmd)
{
	(void)cmd;
	cmd_disable_repeat();
	execute_spelunk_exit(false);
}

void do_cmd_spelunk_peek(struct command *cmd)
{
	int action;
	int dx;
	int dy;

	cmd_disable_repeat();
	if (!cmd || cmd_get_arg_choice(cmd, "action", &action) != CMD_OK ||
			cmd_get_arg_number(cmd, "dx", &dx) != CMD_OK ||
			cmd_get_arg_number(cmd, "dy", &dy) != CMD_OK ||
			!world_spelunk_player_is_active(player)) {
		return;
	}
	(void)world_spelunk_visibility_apply_peek(player->spelunking,
		(enum world_spelunk_peek_action)action, dx, dy);
}
