/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-generation.c
 * \brief Outdoor world generation behind a world-specific interface.
 *
 *
 */

#include "angband.h"

#include "cave.h"
#include "game-world.h"
#include "generate.h"
#include "mon-make.h"
#include "player-util.h"
#include "world-entry.h"
#include "world-generation.h"
#include "world-location.h"

#define OUTDOOR_BASE_AREA (44 * 99)

/** Preserve authored biome density as location dimensions grow. */
static int outdoor_scaled_count(const struct chunk *c, int base_count)
{
	return MAX(1, (base_count * c->height * c->width +
		OUTDOOR_BASE_AREA / 2) / OUTDOOR_BASE_AREA);
}

/** Mark a passable packed-earth road without consuming a feature ID. */
static void outdoor_build_road(struct chunk *c, int y1, int x1, int y2,
		int x2)
{
	struct loc grid;

	fill_rectangle(c, y1, x1, y2, x2, FEAT_FLOOR, SQUARE_ROAD);
	for (grid.y = y1; grid.y <= y2; grid.y++) {
		for (grid.x = x1; grid.x <= x2; grid.x++) {
			sqinfo_off(square(c, grid)->info, SQUARE_WATER);
			sqinfo_off(square(c, grid)->info, SQUARE_WOOD);
		}
	}
}

/** Place semantic cave-mouth entries and join them to the approach road. */
static void place_outdoor_stair_entries(struct chunk *c,
		const struct level *lev, int road_y)
{
	const struct world_entry *entry;
	int slot = 0;

	for (entry = lev ? lev->entries : NULL; entry; entry = entry->next) {
		struct loc grid;
		int offset;

		if (entry->kind != WORLD_ENTRY_UP_STAIR &&
				entry->kind != WORLD_ENTRY_DOWN_STAIR) {
			continue;
		}
		offset = 7 + (slot % 3) * 3;
		grid.x = MAX(3, c->width - 14 - slot * 11);
		grid.y = road_y + ((slot % 2) ? offset : -offset);
		grid.y = MAX(2, MIN(c->height - 3, grid.y));
		outdoor_build_road(c, MIN(road_y, grid.y), grid.x,
			MAX(road_y, grid.y), grid.x);
		square_set_feat(c, grid, entry->kind == WORLD_ENTRY_UP_STAIR ?
			FEAT_LESS : FEAT_MORE);
		sqinfo_off(square(c, grid)->info, SQUARE_ROAD);
		sqinfo_off(square(c, grid)->info, SQUARE_WATER);
		slot++;
	}
}

static void outdoor_set_tree(struct chunk *c, struct loc grid)
{
	if (!square_in_bounds_fully(c, grid)) return;
	square_set_feat(c, grid, FEAT_TREE);
	sqinfo_off(square(c, grid)->info, SQUARE_ROAD);
	sqinfo_off(square(c, grid)->info, SQUARE_WATER);
	sqinfo_on(square(c, grid)->info, SQUARE_WOOD);
}

static void outdoor_set_water(struct chunk *c, struct loc grid)
{
	if (!square_in_bounds_fully(c, grid)) return;
	square_set_feat(c, grid, FEAT_FLOOR);
	sqinfo_off(square(c, grid)->info, SQUARE_ROAD);
	sqinfo_off(square(c, grid)->info, SQUARE_WOOD);
	sqinfo_on(square(c, grid)->info, SQUARE_WATER);
}

static void outdoor_build_water_ellipse(struct chunk *c, struct loc centre,
		int radius_x, int radius_y)
{
	struct loc grid;
	int limit = radius_x * radius_x * radius_y * radius_y;

	for (grid.y = centre.y - radius_y; grid.y <= centre.y + radius_y;
			grid.y++) {
		for (grid.x = centre.x - radius_x; grid.x <= centre.x + radius_x;
				grid.x++) {
			int dx = grid.x - centre.x;
			int dy = grid.y - centre.y;
			int scaled = dx * dx * radius_y * radius_y +
				dy * dy * radius_x * radius_x;

			if (scaled <= limit && (!one_in_(8) || scaled < limit * 3 / 4)) {
				outdoor_set_water(c, grid);
			}
		}
	}
}

static void outdoor_scatter_trees(struct chunk *c, int clusters,
		int radius_max)
{
	int i;

	for (i = 0; i < clusters; i++) {
		struct loc centre = loc(rand_range(3, c->width - 4),
			rand_range(3, c->height - 4));
		int radius = rand_range(1, radius_max);
		struct loc grid;

		for (grid.y = centre.y - radius; grid.y <= centre.y + radius;
				grid.y++) {
			for (grid.x = centre.x - radius; grid.x <= centre.x + radius;
					grid.x++) {
				if (distance(centre, grid) <= radius && !one_in_(4)) {
					outdoor_set_tree(c, grid);
				}
			}
		}
	}
}

static void outdoor_scatter_rock(struct chunk *c, int clusters)
{
	int i;

	for (i = 0; i < clusters; i++) {
		struct loc centre = loc(rand_range(3, c->width - 4),
			rand_range(3, c->height - 4));
		int radius = rand_range(1, 3);
		struct loc grid;

		for (grid.y = centre.y - radius; grid.y <= centre.y + radius;
				grid.y++) {
			for (grid.x = centre.x - radius; grid.x <= centre.x + radius;
					grid.x++) {
				if (square_in_bounds_fully(c, grid) &&
						distance(centre, grid) <= radius && !one_in_(4)) {
					square_set_feat(c, grid, FEAT_GRANITE);
				}
			}
		}
	}
}

static void outdoor_build_ruins(struct chunk *c, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		int left = rand_range(3, c->width - 12);
		int top = rand_range(3, c->height - 9);
		int width = rand_range(5, 10);
		int height = rand_range(4, 7);
		int x, y;

		for (x = left; x < left + width; x++) {
			if (!one_in_(5)) square_set_feat(c, loc(x, top), FEAT_GRANITE);
			if (!one_in_(5)) square_set_feat(c, loc(x, top + height - 1),
				FEAT_GRANITE);
		}
		for (y = top + 1; y < top + height - 1; y++) {
			if (!one_in_(5)) square_set_feat(c, loc(left, y), FEAT_GRANITE);
			if (!one_in_(5)) square_set_feat(c,
				loc(left + width - 1, y), FEAT_GRANITE);
		}
		square_set_feat(c, loc(left + width / 2, top + height / 2),
			FEAT_RUBBLE);
	}
}

/** Apply one cheap, data-selected terrain identity before paths are cleared. */
static void outdoor_apply_biome(struct chunk *c,
		enum world_outdoor_biome biome)
{
	switch (biome) {
		case WORLD_BIOME_CAVEWOOD:
			outdoor_scatter_trees(c, outdoor_scaled_count(c, 24), 3);
			outdoor_scatter_rock(c, outdoor_scaled_count(c, 6));
			break;
		case WORLD_BIOME_LAKE:
			outdoor_build_water_ellipse(c, loc(c->width / 2, c->height / 2),
				c->width / 3, c->height / 3);
			outdoor_scatter_trees(c, outdoor_scaled_count(c, 8), 2);
			break;
		case WORLD_BIOME_PINE_WOOD:
			outdoor_scatter_trees(c, outdoor_scaled_count(c, 38), 3);
			break;
		case WORLD_BIOME_MARSH:
			outdoor_build_water_ellipse(c, loc(c->width / 4, c->height / 3),
				c->width / 8, c->height / 6);
			outdoor_build_water_ellipse(c,
				loc(c->width * 2 / 3, c->height * 2 / 3),
				c->width / 7, c->height / 7);
			outdoor_scatter_trees(c, outdoor_scaled_count(c, 10), 2);
			break;
		case WORLD_BIOME_HEATH:
			outdoor_scatter_rock(c, outdoor_scaled_count(c, 10));
			break;
		case WORLD_BIOME_MEADOW:
			outdoor_scatter_trees(c, outdoor_scaled_count(c, 5), 1);
			break;
		case WORLD_BIOME_RUINS:
			outdoor_build_ruins(c, outdoor_scaled_count(c, 10));
			outdoor_scatter_trees(c, outdoor_scaled_count(c, 6), 1);
			break;
		case WORLD_BIOME_SCRUB:
			outdoor_scatter_trees(c, outdoor_scaled_count(c, 16), 2);
			outdoor_scatter_rock(c, outdoor_scaled_count(c, 5));
			break;
		default:
			outdoor_scatter_rock(c, outdoor_scaled_count(c, 8));
			break;
	}
}

static void outdoor_connect_path(struct chunk *c, struct loc from,
		struct loc centre)
{
	struct loc bend = loc(
		MAX(2, MIN(c->width - 3, rand_spread((from.x + centre.x) / 2,
			MAX(2, c->width / 12)))),
		MAX(2, MIN(c->height - 3, rand_spread((from.y + centre.y) / 2,
			MAX(2, c->height / 12)))));

	outdoor_build_road(c, MIN(from.y, bend.y), from.x,
		MAX(from.y, bend.y), from.x);
	outdoor_build_road(c, bend.y, MIN(from.x, bend.x), bend.y,
		MAX(from.x, bend.x));
	outdoor_build_road(c, MIN(bend.y, centre.y), bend.x,
		MAX(bend.y, centre.y), bend.x);
	outdoor_build_road(c, centre.y, MIN(bend.x, centre.x), centre.y,
		MAX(bend.x, centre.x));
}

/** Generate one retained top-down overworld cell without dungeon semantics. */
struct chunk *world_generate_outdoor(struct player *p, int min_height,
		int min_width,
		const char **p_error)
{
	const struct level *lev = world_player_level(p);
	const struct world_entry *entry;
	struct chunk *c;
	struct loc arrival = loc(0, 0);
	struct loc stop = loc(0, 0);
	int height, width, i;

	(void)min_height;
	(void)min_width;
	if (!lev || lev->kind != WORLD_LOCATION_OUTDOORS ||
			lev->mode != WORLD_MODE_TOP_DOWN || !lev->height || !lev->width) {
		if (p_error) *p_error = "invalid outdoor location";
		return NULL;
	}
	height = lev->height;
	width = lev->width;
	c = cave_new(height, width);
	c->depth = lev->danger;
	if (!world_chunk_set_location(c, lev->id, false)) {
		if (p_error) *p_error = "outdoor location identity failed";
		cave_free(c);
		return NULL;
	}
	fill_rectangle(c, 0, 0, height - 1, width - 1,
		FEAT_FLOOR, SQUARE_NONE);
	draw_rectangle(c, 0, 0, height - 1, width - 1,
		FEAT_PERM, SQUARE_NONE, true);

	entry = world_entry_by_id(lev, p->world_location.entry);
	if (!entry || entry->kind != WORLD_ENTRY_GRID ||
			!square_in_bounds_fully(c, entry->grid)) {
		for (entry = lev->entries; entry; entry = entry->next) {
			if (entry->kind == WORLD_ENTRY_GRID &&
					square_in_bounds_fully(c, entry->grid)) {
				break;
			}
		}
	}
	if (!entry) {
		if (p_error) *p_error = "outdoor location has no fixed entry";
		cave_free(c);
		return NULL;
	}
	if (world_entry_locate(c, entry->id, &arrival) != WORLD_ENTRY_OK) {
		if (p_error) *p_error = "outdoor arrival cannot be resolved";
		cave_free(c);
		return NULL;
	}
	entry = world_entry_by_id(lev, "travel.stop");
	if (!entry || entry->kind != WORLD_ENTRY_GRID) {
		if (p_error) *p_error = "outdoor location has no travel stop";
		cave_free(c);
		return NULL;
	}
	stop = entry->grid;
	outdoor_apply_biome(c, lev->biome);
	for (entry = lev->entries; entry; entry = entry->next) {
		struct loc path_start;

		if (entry->kind == WORLD_ENTRY_GRID &&
				(streq(entry->id, "travel.stop") ||
				 world_entry_is_edge_route_entry(lev, entry)) &&
				world_entry_locate(c, entry->id, &path_start) == WORLD_ENTRY_OK) {
			outdoor_connect_path(c, path_start, stop);
		}
	}
	if (!world_entry_materialize_overworld_gates(c, lev)) {
		if (p_error) *p_error = "outdoor travel gates could not be carved";
		cave_free(c);
		return NULL;
	}
	place_outdoor_stair_entries(c, lev, stop.y);

	player_place(c, p, arrival);
	cave_illuminate(c, is_daytime());
	for (i = 0; i < lev->overworld_population; i++) {
		pick_and_place_distant_monster(c, p->grid, 8, true, c->depth);
	}
	return c;
}
