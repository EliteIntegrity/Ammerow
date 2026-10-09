/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-object-transfer.c
 * \brief Transfer detached objects to the active location owner.
 *
 *
 */

#include "world-object-transfer.h"

#include "cave.h"
#include "obj-pile.h"
#include "player.h"
#include "world-spelunking-runtime.h"
#include "world-turn.h"

bool world_player_location_accepts_object(const struct player *p,
		struct chunk *native_chunk)
{
	switch (world_turn_context_for_player(p)) {
	case WORLD_TURN_CONTEXT_TOP_DOWN:
		return native_chunk && native_chunk == cave &&
			square_in_bounds(native_chunk, p->grid);
	case WORLD_TURN_CONTEXT_SPELUNKING:
		return world_spelunk_runtime_can_add_ground_object_at(p->spelunking,
			p->spelunking->state.x, p->spelunking->state.y);
	case WORLD_TURN_CONTEXT_INVALID:
	default:
		return false;
	}
}

bool world_player_place_detached_object(struct player *p,
		struct chunk *native_chunk, struct object **object)
{
	if (!p || !object || !*object ||
			!world_player_location_accepts_object(p, native_chunk)) {
		return false;
	}
	switch (world_turn_context_for_player(p)) {
	case WORLD_TURN_CONTEXT_TOP_DOWN:
		drop_near(native_chunk, object, 0, p->grid, false, true);
		/* drop_near() consumes ownership but retains the caller's pointer even
		 * when the object is placed, absorbed or destroyed. */
		*object = NULL;
		return true;
	case WORLD_TURN_CONTEXT_SPELUNKING:
		if (!world_spelunk_runtime_add_ground_object(p->spelunking, *object,
				p->spelunking->state.x, p->spelunking->state.y)) {
			return false;
		}
		*object = NULL;
		return true;
	case WORLD_TURN_CONTEXT_INVALID:
	default:
		return false;
	}
}
