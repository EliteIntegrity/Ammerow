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
#include "world-spelunking-transition.h"

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
	result = world_spelunk_player_apply(player, &action, &report);
	if (result != WORLD_SPELUNK_PLAYER_TURN &&
			result != WORLD_SPELUNK_PLAYER_DEAD) {
		if (action.kind == WORLD_SPELUNK_COMMAND_MOVE &&
				result == WORLD_SPELUNK_PLAYER_REJECTED) {
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
		} else if (action == WORLD_SPELUNK_LOCAL_PICKUP &&
				report.completion_message > MSG_GENERIC) {
			sound(report.completion_message);
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
