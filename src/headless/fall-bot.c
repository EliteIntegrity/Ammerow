/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/fall-bot.c
 * \brief Semantic generated-cave fatal-fall acceptance policy.
 */

#include "angband.h"
#include "headless/fall-bot.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-system.h"

static enum headless_fall_bot_result fall_error(char *error,
		size_t error_size, const char *message)
{
	if (error && error_size) my_strcpy(error, message, error_size);
	return HEADLESS_FALL_BOT_ERROR;
}

static bool prepare_fall(struct headless_fall_bot *bot, char *error,
		size_t error_size)
{
	const struct world_spelunk_system *system = player->spelunking_system;
	const struct world_spelunk_system_node *node;
	const struct world_spelunk_system_portal *portal;
	struct world_spelunk_generated_layout generated;
	const struct world_spelunk_generated_route_station *from;
	const struct world_spelunk_generated_route_station *to;
	const struct world_spelunk_generated_route_segment *segment;
	int route_side;

	if (!system || system->graph_version !=
			WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		if (error && error_size) my_strcpy(error,
			"fatal-fall acceptance requires a generated cave system",
			error_size);
		return false;
	}
	node = world_spelunk_system_node_by_id(system, system->active_node_id);
	portal = world_spelunk_system_portal_by_entry(system,
		player->world_location.entry);
	if (!node || !node->runtime || !portal || !portal->materialized ||
			!streq(node->id, portal->node_id) ||
			world_spelunk_reconstruct_node_candidate(system, node->id,
				node->runtime->state.rules.action_energy, &generated) !=
				WORLD_SPELUNK_RECONSTRUCTION_OK) {
		if (error && error_size) my_strcpy(error,
			"fatal-fall acceptance could not reconstruct the active entrance",
			error_size);
		return false;
	}
	if (!generated.route_segment_count || generated.route_station_count < 2) {
		world_spelunk_generated_layout_dispose(&generated);
		if (error && error_size) my_strcpy(error,
			"generated cave has no upper fall shaft", error_size);
		return false;
	}
	segment = &generated.route_segments[0];
	from = &generated.route_stations[segment->from_station];
	to = &generated.route_stations[segment->to_station];
	route_side = segment->shaft_x < from->x && segment->shaft_x < to->x ?
		1 : -1;
	bot->upper_y = from->y;
	bot->shaft_x = segment->shaft_x;
	bot->lip_x = segment->shaft_x + route_side;
	bot->initialized = portal->y == from->y &&
		player->spelunking->state.y == from->y;
	world_spelunk_generated_layout_dispose(&generated);
	if (!bot->initialized && error && error_size) {
		my_strcpy(error,
			"generated cave portal is not on the upper certified rail",
			error_size);
	}
	return bot->initialized;
}

void headless_fall_bot_init(struct headless_fall_bot *bot)
{
	if (bot) memset(bot, 0, sizeof(*bot));
}

enum headless_fall_bot_result headless_fall_bot_next(
		struct headless_fall_bot *bot, char *action, size_t action_size,
		char *error, size_t error_size)
{
	const struct world_spelunk_state *state;

	if (!bot || !action || !action_size || !player || player->is_dead ||
			!world_spelunk_player_is_active(player)) {
		return fall_error(error, error_size,
			"fatal-fall bot has no live side-view player");
	}
	if (!bot->initialized && !prepare_fall(bot, error, error_size)) {
		return HEADLESS_FALL_BOT_ERROR;
	}
	state = &player->spelunking->state;
	if (state->y != bot->upper_y) {
		return fall_error(error, error_size,
			"fatal-fall bot left the upper rail without dying");
	}
	if (state->x < bot->lip_x) {
		my_strcpy(action, "walk:east", action_size);
	} else if (state->x > bot->lip_x) {
		my_strcpy(action, "walk:west", action_size);
	} else if (bot->shaft_x < bot->lip_x) {
		my_strcpy(action, "walk:west", action_size);
	} else {
		my_strcpy(action, "walk:east", action_size);
	}
	return HEADLESS_FALL_BOT_ACTION;
}
