/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-fishing-site.c
 * \brief Mode-owned access to the player's current fishing site.
 *
 */

#include "angband.h"
#include "cave.h"
#include "game-world.h"
#include "init.h"
#include "obj-pile.h"
#include "player.h"
#include "world-fishing-site.h"
#include "world-object-transfer.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-runtime.h"
#include "world-turn.h"

#include <limits.h>

static const int adjacent_dx[] = { -1, 1, 0, 0 };
static const int adjacent_dy[] = { 0, 0, -1, 1 };

static bool top_down_has_water(struct chunk *chunk, struct loc grid)
{
	static const int dx[] = { 0, -1, 1, 0, 0 };
	static const int dy[] = { 0, 0, 0, -1, 1 };
	int i;

	if (!chunk) return false;
	for (i = 0; i < (int)N_ELEMENTS(dx); i++) {
		struct loc candidate = loc(grid.x + dx[i], grid.y + dy[i]);

		if (square_in_bounds(chunk, candidate) &&
				square_iswater(chunk, candidate)) {
			return true;
		}
	}
	return false;
}

static bool spelunking_has_adjacent_water(
		const struct world_spelunk_runtime *runtime)
{
	const struct world_spelunk_state *state;
	int i;

	if (!world_spelunk_runtime_is_valid(runtime)) return false;
	state = &runtime->state;
	for (i = 0; i < (int)N_ELEMENTS(adjacent_dx); i++) {
		int x = state->x + adjacent_dx[i];
		int y = state->y + adjacent_dy[i];

		if (x >= 0 && y >= 0 && x < state->map.width &&
				y < state->map.height &&
				state->map.cells[y * state->map.stride + x] ==
					WORLD_SPELUNK_WATER) {
			return true;
		}
	}
	return false;
}

enum world_fishing_site_result world_fishing_site_query(
		const struct player *p, struct chunk *chunk,
		struct world_fishing_site *site)
{
	const struct level *level;
	enum world_turn_context context;
	struct world_fishing_site candidate;

	if (site) memset(site, 0, sizeof(*site));
	if (!p || !site || p->is_dead) return WORLD_FISHING_SITE_INVALID;
	level = world_player_level(p);
	if (!level || level->fishing_habitat == WORLD_FISHING_HABITAT_NONE) {
		return WORLD_FISHING_SITE_NO_HABITAT;
	}
	context = world_turn_context_for_player(p);
	candidate.habitat = level->fishing_habitat;
	candidate.danger = level->danger;
	switch (context) {
	case WORLD_TURN_CONTEXT_TOP_DOWN:
		candidate.origin = p->grid;
		if (!z_info || z_info->move_energy <= 0) {
			return WORLD_FISHING_SITE_INVALID;
		}
		candidate.action_energy = z_info->move_energy;
		if (!top_down_has_water(chunk, candidate.origin)) {
			return WORLD_FISHING_SITE_NO_WATER;
		}
		break;
	case WORLD_TURN_CONTEXT_SPELUNKING:
		if (!world_spelunk_player_is_active(p)) {
			return WORLD_FISHING_SITE_INVALID;
		}
		candidate.origin = loc(p->spelunking->state.x,
			p->spelunking->state.y);
		if (!p->spelunking->state.rules.action_energy ||
				p->spelunking->state.rules.action_energy > INT_MAX) {
			return WORLD_FISHING_SITE_INVALID;
		}
		candidate.action_energy =
			(int)p->spelunking->state.rules.action_energy;
		/* Casting while falling, hanging, climbing, or swimming would make the
		 * fishing overlay a way to suspend the side-view hazard model. */
		if (p->spelunking->state.movement != WORLD_SPELUNK_STANDING) {
			return WORLD_FISHING_SITE_UNSTABLE;
		}
		if (!spelunking_has_adjacent_water(p->spelunking)) {
			return WORLD_FISHING_SITE_NO_WATER;
		}
		break;
	case WORLD_TURN_CONTEXT_INVALID:
	default:
		return WORLD_FISHING_SITE_INVALID;
	}
	*site = candidate;
	return WORLD_FISHING_SITE_OK;
}

bool world_fishing_site_matches(const struct player *p,
		struct chunk *chunk, const struct world_fishing_site *site)
{
	struct world_fishing_site current;

	return site && world_fishing_site_query(p, chunk, &current) ==
		WORLD_FISHING_SITE_OK && loc_eq(current.origin, site->origin) &&
		current.habitat == site->habitat && current.danger == site->danger &&
		current.action_energy == site->action_energy;
}

bool world_fishing_site_drop_catch(struct player *p, struct chunk *chunk,
		struct object **object)
{
	struct world_fishing_site site;

	if (!object || !*object ||
			world_fishing_site_query(p, chunk, &site) != WORLD_FISHING_SITE_OK) {
		return false;
	}
	/* The queried site origin is the player's current mode-owned position. */
	return world_player_place_detached_object(p, chunk, object);
}
