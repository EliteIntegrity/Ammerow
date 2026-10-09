/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-adapter.c
 * \brief Commit spelunking actions to the authoritative Angband player.
 *
 *
 */

#include "world-spelunking-adapter.h"

#include "game-world.h"
#include "player.h"
#include "player-calcs.h"
#include "player-resource.h"
#include "player-util.h"
#include "world-entry.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-visibility.h"
#include "z-util.h"

#include <limits.h>
#include <string.h>

static enum world_spelunk_player_result reject_invalid(
		struct world_spelunk_action_report *report)
{
	memset(report, 0, sizeof(*report));
	return WORLD_SPELUNK_PLAYER_INVALID;
}

bool world_spelunk_player_is_active(const struct player *p)
{
	const struct level *level;

	if (!p || !p->spelunking ||
			!world_id_is_valid(p->world_location.id) ||
			!world_id_is_valid(p->world_location.entry) ||
			!world_spelunk_runtime_is_valid(p->spelunking)) {
		return false;
	}
	level = world_player_level(p);
	return level && level->mode == WORLD_MODE_SPELUNKING &&
		streq(level->id, p->world_location.id) &&
		world_entry_by_id(level, p->world_location.entry) &&
		streq(level->id, p->spelunking->location_id);
}

bool world_spelunk_player_sync_resources(struct player *p)
{
	if (!p || !p->spelunking ||
			!world_spelunk_runtime_is_valid(p->spelunking)) {
		return false;
	}
	player_resources_ensure(p);
	p->spelunking->state.max_stamina = player_stamina_maximum(p);
	p->spelunking->state.stamina = player_stamina_current(p);
	p->spelunking->state.rules.breath_turns = player_air_maximum(p);
	p->spelunking->state.breath = player_air_current(p);
	return world_spelunk_runtime_is_valid(p->spelunking);
}

enum world_spelunk_player_result world_spelunk_player_apply(
		struct player *p, const struct world_spelunk_command *command,
		struct world_spelunk_action_report *report)
{
	struct world_spelunk_action_report local = { 0 };
	struct world_spelunk_action_report *result = report ? report : &local;
	struct world_spelunk_state before;
	struct player_resources resources_before;
	enum world_spelunk_action_outcome outcome;

	memset(result, 0, sizeof(*result));
	if (!p || !p->upkeep || !p->spelunking || !command || p->is_dead ||
			p->upkeep->energy_use != 0 ||
			!world_spelunk_player_is_active(p) ||
			p->spelunking->state.rules.action_energy > INT_MAX) {
		return WORLD_SPELUNK_PLAYER_INVALID;
	}
	player_resources_ensure(p);
	if (!world_spelunk_player_sync_resources(p)) {
		return WORLD_SPELUNK_PLAYER_INVALID;
	}

	/* A shallow snapshot is sufficient: the rules core never mutates terrain. */
	before = p->spelunking->state;
	resources_before = p->resources;
	outcome = world_spelunk_apply_command(&p->spelunking->state, command,
		result);
	if (!world_spelunk_runtime_is_valid(p->spelunking)) {
		p->spelunking->state = before;
		p->resources = resources_before;
		return reject_invalid(result);
	}

	switch (outcome) {
	case WORLD_SPELUNK_ACTION_REJECTED:
		/* Rejection is a strict no-op even if a future rules change regresses. */
		p->spelunking->state = before;
		p->resources = resources_before;
		memset(result, 0, sizeof(*result));
		return WORLD_SPELUNK_PLAYER_REJECTED;
	case WORLD_SPELUNK_ACTION_FREE:
		if (result->outcome != outcome || result->energy_use != 0 ||
				result->damage != 0) {
			p->spelunking->state = before;
			p->resources = resources_before;
			return reject_invalid(result);
		}
		world_spelunk_visibility_follow_player(p->spelunking);
		p->upkeep->redraw |= PR_STATUS;
		return WORLD_SPELUNK_PLAYER_FREE;
	case WORLD_SPELUNK_ACTION_TURN:
		if (result->outcome != outcome ||
				result->energy_use !=
					p->spelunking->state.rules.action_energy ||
				result->energy_use > INT_MAX || result->damage < 0) {
			p->spelunking->state = before;
			p->resources = resources_before;
			return reject_invalid(result);
		}
		p->resources.stamina.current = (int16_t)
			p->spelunking->state.stamina;
		if (result->stamina_delta < 0) {
			player_stamina_mark_exerted(p);
		} else if (command->kind == WORLD_SPELUNK_COMMAND_WAIT ||
				!world_spelunk_has_stable_floor(&p->spelunking->state)) {
			player_stamina_mark_resolved(p);
		}
		world_spelunk_visibility_follow_player(p->spelunking);
		p->upkeep->energy_use = (int)result->energy_use;
		p->upkeep->redraw |= PR_STATUS;
		/* Falling is an environmental hazard.  Its rules-core damage bypasses
		 * armour reduction, but take_hit() remains the sole HP/death authority.
		 * Exponential overkill is capped just beyond current HP before entering
		 * Angband's secondary damage arithmetic; the report retains its exact
		 * deterministic value. */
		if (result->damage > 0) {
			int committed_damage = result->damage;

			if (p->chp >= 0 && committed_damage > p->chp) {
				committed_damage = p->chp + 1;
			}
			take_hit(p, committed_damage, "a fall");
		}
		return p->is_dead ? WORLD_SPELUNK_PLAYER_DEAD :
			WORLD_SPELUNK_PLAYER_TURN;
	default:
		p->spelunking->state = before;
		p->resources = resources_before;
		return reject_invalid(result);
	}
}
