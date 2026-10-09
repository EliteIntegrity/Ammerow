/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-overworld.h
 * \brief Save-stable layout and derived routes for the explorable surface.
 */

#ifndef WORLD_OVERWORLD_H
#define WORLD_OVERWORLD_H

#include "world-location.h"

struct level;
struct player;
struct world_site;

bool world_overworld_layout_create(struct player *p, bool legacy);
bool world_overworld_layout_set(struct player *p, uint8_t version,
		uint8_t width, uint8_t height, uint8_t count, uint32_t seed);
bool world_overworld_layout_valid(const struct player *p);
bool world_overworld_rebuild_routes(struct player *p);
const struct level *world_overworld_location_at(const struct player *p,
		int x, int y);
bool world_overworld_position(const struct player *p,
		const char *location_id, int *x, int *y);
const struct level *world_overworld_neighbor(const struct player *p,
		const char *location_id, int dx, int dy);
bool world_overworld_site_position(const struct player *p,
		const struct world_site *site, int *x, int *y);
unsigned int world_overworld_activate_current_stop(struct player *p);
unsigned int world_overworld_backfill_discovered_stops(struct player *p);
const char *world_overworld_biome_name(enum world_outdoor_biome biome);
bool world_overworld_biome_from_name(const char *name,
		enum world_outdoor_biome *biome);

#endif /* !WORLD_OVERWORLD_H */
