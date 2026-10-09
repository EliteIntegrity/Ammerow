/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-travel.c
 * \brief Surface shortcut into the unified known-world map.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "ui-travel.h"
#include "ui-world.h"
#include "world-entry.h"

/**
 * Use the otherwise idle '<' key for the known-world map on the surface.  A
 * real ascending stair or west/north edge under the player remains
 * authoritative.
 */
bool textui_travel_uses_up_key_here(const struct player *p, struct chunk *c)
{
	const struct level *current;

	if (!p || !c) return false;
	current = world_player_level(p);
	if (!current || (current->kind != WORLD_LOCATION_HUB &&
			current->kind != WORLD_LOCATION_OUTDOORS)) {
		return false;
	}
	if (world_entry_edge_route_for_stair_key(c, p->grid, false)) return false;
	return !square_isupstairs(c, p->grid);
}

/** Both command menus and the '<' shortcut use the same spatial controller. */
void textui_cmd_travel(void)
{
	do_cmd_world_map();
}
