/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-map.c
 * \brief Read-only, discovery-filtered summaries for the world screen.
 *
 *
 */

#include "angband.h"
#include "game-world.h"
#include "world-location.h"
#include "world-map.h"
#include "world-overworld.h"
#include "world-transition.h"

static struct world_map_site_summary *find_site(
		struct world_map_model *model, const char *site_id)
{
	unsigned int i;

	for (i = 0; i < model->site_count; i++) {
		if (streq(model->sites[i].site->id, site_id)) {
			return &model->sites[i];
		}
	}
	return NULL;
}

static void add_location(const struct player *p,
		struct world_map_model *model, const struct level *lev)
{
	struct world_map_site_summary *summary;
	const struct world_site *site = world_site_by_id(lev->site_id);

	if (!site) return;
	model->discovered_location_count++;
	summary = find_site(model, lev->site_id);
	if (!summary) {
		if (model->site_count >= WORLD_MAP_SITE_LIMIT) {
			model->sites_truncated = true;
			return;
		}
		summary = &model->sites[model->site_count++];
		summary->site = site;
		summary->representative = lev;
		summary->minimum_floor = lev->local_floor;
		summary->maximum_floor = lev->local_floor;
		summary->minimum_danger = lev->danger;
		summary->maximum_danger = lev->danger;
		summary->has_map_position = world_overworld_site_position(p, site,
			&summary->map_x, &summary->map_y);
	}
	summary->location_count++;
	summary->minimum_floor = MIN(summary->minimum_floor, lev->local_floor);
	summary->maximum_floor = MAX(summary->maximum_floor, lev->local_floor);
	summary->minimum_danger = MIN(summary->minimum_danger, lev->danger);
	summary->maximum_danger = MAX(summary->maximum_danger, lev->danger);
	if (streq(p->world_location.id, lev->id)) {
		summary->current = true;
		summary->representative = lev;
	}
}

static bool endpoint_is_active(const struct player *p, const char *location,
		const char *entry)
{
	const struct world_travel_node *node =
		world_travel_node_by_endpoint(location, entry);

	return node && world_player_has_activated_travel_node(p, node->id);
}

static void add_route(const struct player *p, struct world_map_model *model,
		const struct world_route *route)
{
	const struct level *from = level_by_id(route->from);
	const struct level *to = level_by_id(route->to);
	struct world_map_route_summary *summary;

	/* Retired identities exist only so development saves remain readable. */
	if (world_route_is_retired(route) ||
			(world_route_is_aggregate(route) && route->runtime_layout)) {
		return;
	}
	/* Unlocking is the durable discovery fact for a route.  Require both
	 * endpoint locations as a defensive no-spoiler boundary as well. */
	if (!world_player_route_is_unlocked(p, route->id) || !from || !to ||
			!world_player_has_discovered_location(p, from->id) ||
			!world_player_has_discovered_location(p, to->id)) {
		return;
	}
	if (model->route_count >= WORLD_MAP_ROUTE_LIMIT) {
		model->routes_truncated = true;
		return;
	}
	summary = &model->routes[model->route_count++];
	summary->route = route;
	summary->source = world_site_by_id(from->site_id);
	summary->destination = world_site_by_id(to->site_id);
	summary->source_active = world_route_is_physical(route) ||
		endpoint_is_active(p, route->from, route->from_entry);
	summary->destination_active = world_route_is_physical(route) ||
		endpoint_is_active(p, route->to, route->to_entry);
}

void world_map_build(const struct player *p, struct world_map_model *model)
{
	const struct level *lev;
	const struct world_route *route;

	if (!model) return;
	memset(model, 0, sizeof(*model));
	if (!p) return;
	for (lev = world; lev; lev = lev->next) {
		if (world_player_has_discovered_location(p, lev->id)) {
			add_location(p, model, lev);
		}
	}
	for (route = world_routes; route; route = route->next) {
		add_route(p, model, route);
	}
}
