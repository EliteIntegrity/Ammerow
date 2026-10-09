/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-world.c
 * \brief Commands for travel between stable world locations.
 *
 *
 */

#include "angband.h"
#include "cmd-core.h"
#include "game-world.h"
#include "world-location.h"
#include "world-spelunking-transition.h"
#include "world-travel-action.h"

static void report_transition_failure(enum world_transition_result result)
{
	switch (result) {
		case WORLD_TRANSITION_LOCKED:
			msg("That route has not been unlocked.");
			break;
		case WORLD_TRANSITION_SOURCE_NODE_INACTIVE:
			msg("This travel point has not been activated.");
			break;
		case WORLD_TRANSITION_DESTINATION_NODE_INACTIVE:
			msg("You have not activated that destination.");
			break;
		case WORLD_TRANSITION_NOT_AT_SOURCE_NODE:
			msg("You must stand on the route's travel point.");
			break;
		case WORLD_TRANSITION_WRONG_SOURCE:
			msg("That route does not leave from here.");
			break;
		case WORLD_TRANSITION_UNKNOWN_ROUTE:
			msg("That route is not known.");
			break;
		case WORLD_TRANSITION_UNKNOWN_TRAVEL_NODE:
		case WORLD_TRANSITION_SOURCE_NODE_UNAVAILABLE:
			msg("This route has no usable travel point.");
			break;
		default:
			msg("You cannot use that route now.");
			break;
	}
}

static void report_travel_failure(
		const struct world_travel_action_report *report)
{
	switch (report->result) {
		case WORLD_TRAVEL_ACTION_SAFETY_REJECTED:
			switch (report->safety) {
				case WORLD_TRAVEL_SAFETY_THREATENED:
					msg("You cannot travel while a visible threat is nearby.");
					break;
				case WORLD_TRAVEL_SAFETY_RESTRAINED:
					msg("You must free yourself before travelling.");
					break;
				case WORLD_TRAVEL_SAFETY_TEMPORARY_EFFECT:
					msg("You cannot travel while a temporary effect is active.");
					break;
				case WORLD_TRAVEL_SAFETY_PENDING_RELOCATION:
					msg("Another relocation is already pending.");
					break;
				case WORLD_TRAVEL_SAFETY_UNSUPPORTED_LOCATION:
					msg("You cannot travel from this kind of location.");
					break;
				default:
					msg("You cannot travel safely now.");
					break;
			}
			break;
		case WORLD_TRAVEL_ACTION_TRANSITION_REJECTED:
			report_transition_failure(report->transition);
			break;
		case WORLD_TRAVEL_ACTION_TIME_REJECTED:
			msg("That journey cannot be timed safely.");
			break;
		case WORLD_TRAVEL_ACTION_RESOURCES_REJECTED:
			if (report->resources ==
					WORLD_TRAVEL_RESOURCES_UNSAFE_HUNGER) {
				msg("You need to eat before making that journey.");
			} else if (report->resources ==
					WORLD_TRAVEL_RESOURCES_UNSAFE_LIGHT) {
				msg("You need more light fuel, or must remove the light first.");
			} else {
				msg("You lack the resources to make that journey safely.");
			}
			break;
		case WORLD_TRAVEL_ACTION_DESTINATION_REJECTED:
			msg("The destination cannot be reached safely.");
			break;
		case WORLD_TRAVEL_ACTION_INVALID:
		default:
			msg("That is not a valid journey.");
			break;
	}
}

static void report_spelunking_entry_failure(
		const struct world_spelunking_entry_report *report)
{
	switch (report->result) {
	case WORLD_SPELUNKING_ENTRY_SAFETY_REJECTED:
		if (report->safety == WORLD_TRAVEL_SAFETY_RESTRAINED) {
			msg("You must free yourself before descending.");
		} else if (report->safety ==
				WORLD_TRAVEL_SAFETY_PENDING_RELOCATION) {
			msg("Another relocation is already pending.");
		} else {
			msg("You cannot enter the shaft safely now.");
		}
		break;
	case WORLD_SPELUNKING_ENTRY_TRANSITION_REJECTED:
		report_transition_failure(report->transition);
		break;
	case WORLD_SPELUNKING_ENTRY_RESOURCES_REJECTED:
		msg("You cannot safely make the descent now.");
		break;
	case WORLD_SPELUNKING_ENTRY_TIME_REJECTED:
	case WORLD_SPELUNKING_ENTRY_DESTINATION_REJECTED:
	case WORLD_SPELUNKING_ENTRY_INVALID:
	default:
		msg("You cannot enter that shaft now.");
		break;
	}
}

/** Execute a route selected by a frontend. */
void do_cmd_travel(struct command *cmd)
{
	const char *route_id;
	struct world_route *route;
	struct level *destination;
	struct world_travel_action_report report;

	cmd_disable_repeat();
	if (cmd_get_arg_string(cmd, "route", &route_id) != CMD_OK) {
		msg("No travel route was selected.");
		return;
	}
	route = world_route_by_id(route_id);
	destination = route ? level_by_id(route->to) : NULL;
	if (world_travel_execute(player, cave, route_id, &report) !=
			WORLD_TRAVEL_ACTION_OK) {
		report_travel_failure(&report);
		return;
	}

	msgt(MSG_TPLEVEL, "You travel to %s.",
		destination ? destination->name : "your destination");
}

/** Use a selected immediate route: edge, stairs, or one cross-mode shaft. */
bool do_cmd_world_transition_route(const char *route_id)
{
	struct world_route *route;
	struct level *destination;
	struct world_travel_action_report report;

	route = world_route_by_id(route_id);
	destination = route ? level_by_id(route->to) : NULL;
	if (world_route_is_cross_mode(route)) {
		struct world_spelunking_entry_report entry_report;

		if (world_spelunking_entry_execute(player, cave, route_id,
				&entry_report) != WORLD_SPELUNKING_ENTRY_OK) {
			report_spelunking_entry_failure(&entry_report);
			return false;
		}
		if (route->message) {
			msgt(MSG_TPLEVEL, "%s", route->message);
		} else {
			msgt(MSG_TPLEVEL, "You lower yourself into %s.",
				destination ? destination->name : "the chasm");
		}
		return true;
	}
	if (world_route_execute_physical(player, cave, route_id, &report) !=
			WORLD_TRAVEL_ACTION_OK) {
		report_travel_failure(&report);
		return false;
	}

	msgt(MSG_TPLEVEL, streq(route->kind, "stairs") ?
		"You enter %s." : "You continue to %s.",
		destination ? destination->name : "the neighbouring region");
	return true;
}

/** Use one immediate world passage as an ordinary, turn-taking action. */
void do_cmd_world_transition(struct command *cmd)
{
	const char *route_id;

	cmd_disable_repeat();
	if (cmd_get_arg_string(cmd, "route", &route_id) != CMD_OK) {
		msg("No physical route was selected.");
		return;
	}
	(void)do_cmd_world_transition_route(route_id);
}
