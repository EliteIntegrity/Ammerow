/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/** \file world-fishing-discovery.c
 * \brief Derived catch conditions which reveal persistent world topology.
 */

#include "angband.h"
#include "game-world.h"
#include "player.h"
#include "world-fishing-discovery.h"
#include "world-spelunking-system.h"

enum world_fishing_discovery_result world_fishing_apply_discovery(
		struct player *p, const struct world_fishing_runtime *runtime,
		const struct world_fishing_report *report, const char **message)
{
	const struct world_fishing_species *species;
	const struct level *level = world_player_level(p);
	int i;

	if (message) *message = NULL;
	if (!p || !runtime || !report || !level || !p->spelunking_system) {
		return WORLD_FISHING_DISCOVERY_NONE;
	}
	species = world_fishing_species_by_kind(report->landed_kind);
	if (!species) return WORLD_FISHING_DISCOVERY_NONE;
	for (i = 0; i < world_fishing_discovery_count(); i++) {
		const struct world_fishing_discovery *discovery =
			world_fishing_discovery_by_index(i);
		const struct world_spelunk_system_portal *portal;
		bool out_unlocked;
		bool in_unlocked;

		if (!discovery || !streq(discovery->species_id, species->id) ||
				!streq(discovery->rig_id, runtime->rig_id) ||
				!streq(discovery->location_id, level->id) ||
				discovery->habitat != runtime->habitat ||
				report->landed_depth < discovery->minimum_depth ||
				!streq(discovery->system_id, p->spelunking_system->id)) {
			continue;
		}
		portal = world_spelunk_system_portal_by_id(p->spelunking_system,
			discovery->portal_id);
		if (!portal || !streq(portal->node_id,
				p->spelunking_system->active_node_id)) {
			continue;
		}
		out_unlocked = world_player_route_is_unlocked(p,
			discovery->out_route_id);
		in_unlocked = world_player_route_is_unlocked(p,
			discovery->in_route_id);
		if (portal->enabled && out_unlocked && in_unlocked) {
			return WORLD_FISHING_DISCOVERY_ALREADY_RECORDED;
		}
		if ((!out_unlocked && !world_player_unlock_route(p,
				discovery->out_route_id)) ||
				(!in_unlocked && !world_player_unlock_route(p,
					discovery->in_route_id)) ||
				!world_spelunk_system_enable_portal(p->spelunking_system,
					discovery->portal_id)) {
			return WORLD_FISHING_DISCOVERY_ERROR;
		}
		if (message) *message = discovery->message;
		return WORLD_FISHING_DISCOVERY_UNLOCKED;
	}
	return WORLD_FISHING_DISCOVERY_NONE;
}
