/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-map.h
 * \brief Read-only, discovery-filtered summaries for the world screen.
 */

#ifndef WORLD_MAP_H
#define WORLD_MAP_H

#include "h-basic.h"

#define WORLD_MAP_SITE_LIMIT 64
#define WORLD_MAP_ROUTE_LIMIT 128

struct level;
struct player;
struct world_route;
struct world_site;

struct world_map_site_summary {
	const struct world_site *site;
	const struct level *representative;
	unsigned int location_count;
	int minimum_floor;
	int maximum_floor;
	int minimum_danger;
	int maximum_danger;
	bool current;
	bool has_map_position;
	int map_x;
	int map_y;
};

struct world_map_route_summary {
	const struct world_route *route;
	const struct world_site *source;
	const struct world_site *destination;
	bool source_active;
	bool destination_active;
};

struct world_map_model {
	struct world_map_site_summary sites[WORLD_MAP_SITE_LIMIT];
	struct world_map_route_summary routes[WORLD_MAP_ROUTE_LIMIT];
	unsigned int site_count;
	unsigned int route_count;
	unsigned int discovered_location_count;
	bool sites_truncated;
	bool routes_truncated;
};

void world_map_build(const struct player *p, struct world_map_model *model);

#endif /* !WORLD_MAP_H */
