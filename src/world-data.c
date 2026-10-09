/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-data.c
 * \brief Parse and validate the explorable world data model.
 *
 * Copyright (c) 1997 Ben Harrison
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 *
 * World-data ownership is deliberately kept separate from general game
 * initialization so the world schema can evolve behind one narrow parser
 * lifecycle.
 */

#include "angband.h"
#include "datafile.h"
#include "fishing-data.h"
#include "game-world.h"
#include "init.h"
#include "world-data.h"
#include "world-entry.h"
#include "world-overworld.h"
#include "world-story-data.h"

/**
 * ------------------------------------------------------------------------
 * Initialize world map
 * ------------------------------------------------------------------------ */
static enum parser_error parse_world_level(struct parser *p) {
	const int depth = parser_getint(p, "depth");
	const char *name = parser_getsym(p, "name");
	const char *up = parser_getsym(p, "up");
	const char *down = parser_getsym(p, "down");
	struct level *last = parser_priv(p);
	struct level *lev = mem_zalloc(sizeof *lev);

	if (last) {
		last->next = lev;
	} else {
		world = lev;
	}
	lev->depth = depth;
	lev->has_legacy_depth = true;
	lev->name = string_make(name);
	lev->up = streq(up, "None") ? NULL : string_make(up);
	lev->down = streq(down, "None") ? NULL : string_make(down);
	parser_setpriv(p, lev);
	return PARSE_ERROR_NONE;
}

static bool world_kind_from_name(const char *name,
		enum world_location_kind *kind);
static bool world_mode_from_name(const char *name,
		enum world_location_mode *mode);

/** Add a stable location which is not part of the inherited depth chain. */
static enum parser_error parse_world_place(struct parser *p)
{
	const char *name = parser_getsym(p, "name");
	const char *id = parser_getsym(p, "id");
	const char *site = parser_getsym(p, "site");
	const char *kind_name = parser_getsym(p, "kind");
	const char *mode_name = parser_getsym(p, "mode");
	int local_floor = parser_getint(p, "floor");
	int danger = parser_getint(p, "danger");
	unsigned int height = parser_getuint(p, "height");
	unsigned int width = parser_getuint(p, "width");
	struct level *last = parser_priv(p);
	struct level *lev;
	enum world_location_kind kind;
	enum world_location_mode mode;

	if (!name || !name[0] || !world_id_is_valid(id) ||
			!world_id_is_valid(site) ||
			local_floor < 0 || danger < 0 || danger > INT16_MAX ||
			height < 3 || width < 3 ||
			height > WORLD_LOCATION_HEIGHT_MAX ||
			width > WORLD_LOCATION_WIDTH_MAX ||
			!world_kind_from_name(kind_name, &kind) ||
			!world_mode_from_name(mode_name, &mode)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	/* The stock outdoor builder reserves a permanent border, irregular rock
	 * outcrops, and a bounded pool.  Reject dimensions too small for those
	 * guarantees at data-load time rather than failing during generation. */
	if (kind == WORLD_LOCATION_OUTDOORS && (height < 12 || width < 21)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (level_by_name(name) || level_by_id(id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}

	lev = mem_zalloc(sizeof(*lev));
	lev->name = string_make(name);
	lev->id = string_make(id);
	lev->site_id = string_make(site);
	lev->kind = kind;
	lev->mode = mode;
	lev->local_floor = local_floor;
	lev->danger = danger;
	lev->height = (uint16_t)height;
	lev->width = (uint16_t)width;
	if (last) {
		last->next = lev;
	} else {
		world = lev;
	}
	parser_setpriv(p, lev);
	return PARSE_ERROR_NONE;
}

static bool world_kind_from_name(const char *name,
		enum world_location_kind *kind)
{
	if (streq(name, "hub")) {
		*kind = WORLD_LOCATION_HUB;
	} else if (streq(name, "outdoors")) {
		*kind = WORLD_LOCATION_OUTDOORS;
	} else if (streq(name, "dungeon")) {
		*kind = WORLD_LOCATION_DUNGEON;
	} else if (streq(name, "interior")) {
		*kind = WORLD_LOCATION_INTERIOR;
	} else if (streq(name, "special")) {
		*kind = WORLD_LOCATION_SPECIAL;
	} else {
		return false;
	}
	return true;
}

static bool world_mode_from_name(const char *name,
		enum world_location_mode *mode)
{
	if (streq(name, "top_down")) {
		*mode = WORLD_MODE_TOP_DOWN;
	} else if (streq(name, "spelunking")) {
		*mode = WORLD_MODE_SPELUNKING;
	} else {
		return false;
	}
	return true;
}

/** Add stable presentation metadata for one world site. */
static enum parser_error parse_world_site(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *name = parser_getstr(p, "name");
	struct world_site *site;
	struct world_site **tail = &world_sites;

	if (!world_id_is_valid(id) || !name || !name[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (world_site_by_id(id)) return PARSE_ERROR_REPEATED_DIRECTIVE;

	site = mem_zalloc(sizeof(*site));
	site->id = string_make(id);
	site->name = string_make(name);
	while (*tail) tail = &(*tail)->next;
	*tail = site;
	return PARSE_ERROR_NONE;
}

/** Assign an authored terrain recipe to every location at one stable site. */
static enum parser_error parse_world_site_visual(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *profile = parser_getsym(p, "profile");
	struct world_site *site = world_site_by_id(id);

	if (!site) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (site->visual_profile) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (!datafile_art_id_is_valid(profile)) return PARSE_ERROR_INVALID_VALUE;
	site->visual_profile = string_make(profile);
	return PARSE_ERROR_NONE;
}

/** Assign the player-facing term used for numbered floors at one site. */
static enum parser_error parse_world_site_floor_term(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *term = parser_getstr(p, "term");
	struct world_site *site = world_site_by_id(id);

	if (!site) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!term || !term[0]) return PARSE_ERROR_INVALID_VALUE;
	if (site->floor_term) return PARSE_ERROR_REPEATED_DIRECTIVE;
	site->floor_term = string_make(term);
	return PARSE_ERROR_NONE;
}

/** Assign the one opening brief shown when a run begins at this site. */
static enum parser_error parse_world_site_opening(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *scene_id = parser_getsym(p, "scene");
	struct world_site *site = world_site_by_id(id);
	const struct world_story_scene *scene =
		world_story_scene_by_id(scene_id);

	if (!site) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!scene) return PARSE_ERROR_INVALID_VALUE;
	if (site->opening_scene) return PARSE_ERROR_REPEATED_DIRECTIVE;
	site->opening_scene = scene;
	return PARSE_ERROR_NONE;
}

/** Assign a post-victory environment recipe and arrival observation. */
static enum parser_error parse_world_site_victory(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *profile = parser_getsym(p, "profile");
	const char *message = parser_getstr(p, "message");
	struct world_site *site = world_site_by_id(id);

	if (!site) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!datafile_art_id_is_valid(profile) || !message[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (site->victory_visual_profile || site->victory_message) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	site->victory_visual_profile = string_make(profile);
	site->victory_message = string_make(message);
	return PARSE_ERROR_NONE;
}

static bool valid_map_stamp_row(const char *row)
{
	size_t i;

	if (!row || strlen(row) != WORLD_SITE_MAP_STAMP_WIDTH) return false;
	for (i = 0; i < WORLD_SITE_MAP_STAMP_WIDTH; i++) {
		if ((unsigned char)row[i] < 32 || (unsigned char)row[i] > 126) {
			return false;
		}
	}
	return true;
}

/** Assign a compact world-map stamp and non-spoiler description to a site. */
static enum parser_error parse_world_site_map(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *color = parser_getsym(p, "color");
	const char *rows[WORLD_SITE_MAP_STAMP_HEIGHT] = {
		parser_getsym(p, "row1"), parser_getsym(p, "row2"),
		parser_getsym(p, "row3")
	};
	const char *description = parser_getstr(p, "description");
	struct world_site *site = world_site_by_id(id);
	int attr;
	int i;

	if (!site) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (site->has_map_visual) return PARSE_ERROR_REPEATED_DIRECTIVE;
	attr = color_text_to_attr(color);
	if (!description || !description[0] ||
			my_stricmp(attr_to_text((uint8_t)attr), color) != 0) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < WORLD_SITE_MAP_STAMP_HEIGHT; i++) {
		if (!valid_map_stamp_row(rows[i])) return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < WORLD_SITE_MAP_STAMP_HEIGHT; i++) {
		my_strcpy(site->map_stamp[i], rows[i], sizeof(site->map_stamp[i]));
	}
	site->map_description = string_make(description);
	site->map_attr = (uint8_t)attr;
	site->has_map_visual = true;
	return PARSE_ERROR_NONE;
}

/** Give a stable site a presentation-only position on the world diagram. */
static enum parser_error parse_world_site_position(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	int x = parser_getint(p, "x");
	int y = parser_getint(p, "y");
	struct world_site *site = world_site_by_id(id);
	struct world_site *other;

	if (!site) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (site->has_map_position || site->has_map_anchor) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (x < INT16_MIN || x > INT16_MAX || y < INT16_MIN || y > INT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (other = world_sites; other; other = other->next) {
		if (other != site && other->has_map_position &&
				other->map_x == x && other->map_y == y) {
			return PARSE_ERROR_INVALID_VALUE;
		}
	}
	site->map_x = (int16_t)x;
	site->map_y = (int16_t)y;
	site->has_map_position = true;
	return PARSE_ERROR_NONE;
}

/** Position a subordinate site relative to a shuffled overworld site. */
static enum parser_error parse_world_site_anchor(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *anchor_id = parser_getsym(p, "anchor");
	int dx = parser_getint(p, "dx");
	int dy = parser_getint(p, "dy");
	struct world_site *site = world_site_by_id(id);
	struct world_site *anchor = world_site_by_id(anchor_id);

	if (!site || !anchor) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (site == anchor || site->has_map_anchor || site->has_map_position) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (dx < INT16_MIN || dx > INT16_MAX || dy < INT16_MIN ||
			dy > INT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	site->anchor_site_id = string_make(anchor_id);
	site->anchor_dx = (int16_t)dx;
	site->anchor_dy = (int16_t)dy;
	site->has_map_anchor = true;
	return PARSE_ERROR_NONE;
}

/** Attach stable identity and gameplay-independent metadata to a level. */
static enum parser_error parse_world_location(struct parser *p)
{
	const char *level_name = parser_getsym(p, "level");
	const char *id = parser_getsym(p, "id");
	const char *site = parser_getsym(p, "site");
	const char *kind_name = parser_getsym(p, "kind");
	const char *mode_name = parser_getsym(p, "mode");
	int local_floor = parser_getint(p, "floor");
	int danger = parser_getint(p, "danger");
	bool has_height = parser_hasval(p, "height");
	bool has_width = parser_hasval(p, "width");
	unsigned int height = has_height ? parser_getuint(p, "height") : 0;
	unsigned int width = has_width ? parser_getuint(p, "width") : 0;
	struct level *lev = level_by_name(level_name);
	struct level *cursor;
	enum world_location_kind kind;
	enum world_location_mode mode;

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (lev->id) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (!world_id_is_valid(id) || !world_id_is_valid(site) ||
			local_floor < 0 || danger < 0 || danger > INT16_MAX ||
			has_height != has_width ||
			(has_height && (height < 3 || width < 3 ||
				height > WORLD_LOCATION_HEIGHT_MAX ||
				width > WORLD_LOCATION_WIDTH_MAX)) ||
			!world_kind_from_name(kind_name, &kind) ||
			!world_mode_from_name(mode_name, &mode)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (cursor = world; cursor; cursor = cursor->next) {
		if (cursor->id && streq(cursor->id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}

	lev->id = string_make(id);
	lev->site_id = string_make(site);
	lev->kind = kind;
	lev->mode = mode;
	lev->local_floor = local_floor;
	lev->danger = danger;
	lev->height = (uint16_t)height;
	lev->width = (uint16_t)width;
	return PARSE_ERROR_NONE;
}

/** Add one named arrival rule to a stable location. */
static enum parser_error parse_world_entry(struct parser *p)
{
	const char *location_id = parser_getsym(p, "location");
	const char *id = parser_getsym(p, "id");
	const char *kind_name = parser_getsym(p, "kind");
	struct level *lev = level_by_id(location_id);
	struct world_entry *entry;
	struct world_entry **tail;
	bool has_y = parser_hasval(p, "y");
	bool has_x = parser_hasval(p, "x");

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!world_id_is_valid(id)) return PARSE_ERROR_INVALID_VALUE;
	if (world_entry_by_id(lev, id)) return PARSE_ERROR_REPEATED_DIRECTIVE;

	entry = mem_zalloc(sizeof(*entry));
	if (streq(kind_name, "grid")) {
		int y, x;

		if (!has_y || !has_x) {
			mem_free(entry);
			return PARSE_ERROR_INVALID_VALUE;
		}
		y = parser_getint(p, "y");
		x = parser_getint(p, "x");
		if (y < 0 || x < 0) {
			mem_free(entry);
			return PARSE_ERROR_INVALID_VALUE;
		}
		entry->kind = WORLD_ENTRY_GRID;
		entry->grid = loc(x, y);
	} else if (streq(kind_name, "up_stair") ||
			streq(kind_name, "down_stair")) {
		if (has_y || has_x) {
			mem_free(entry);
			return PARSE_ERROR_INVALID_VALUE;
		}
		entry->kind = streq(kind_name, "up_stair") ?
			WORLD_ENTRY_UP_STAIR : WORLD_ENTRY_DOWN_STAIR;
	} else {
		mem_free(entry);
		return PARSE_ERROR_INVALID_VALUE;
	}

	entry->id = string_make(id);
	tail = &lev->entries;
	while (*tail) tail = &(*tail)->next;
	*tail = entry;
	return PARSE_ERROR_NONE;
}

/** Add one cell of an entry's small, data-authored rock surround. */
static enum parser_error parse_world_entry_rock(struct parser *p)
{
	struct level *lev = level_by_id(parser_getsym(p, "location"));
	struct world_entry *entry;
	struct world_entry_rock **tail;
	int y = parser_getint(p, "y"), x = parser_getint(p, "x");
	int feat = lookup_feat_code(parser_getsym(p, "feature"));

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	for (entry = lev->entries; entry; entry = entry->next) {
		if (streq(entry->id, parser_getsym(p, "id"))) break;
	}
	if (!entry) return PARSE_ERROR_MISSING_RECORD_HEADER;
	/* Bound both the visual footprint and the amount of decoration data. */
	if (x < -4 || x > 4 || y < -4 || y > 4 || (!x && !y) || feat < 0)
		return PARSE_ERROR_INVALID_VALUE;
	tail = &entry->rocks;
	while (*tail) {
		if (loc_eq((*tail)->offset, loc(x, y)))
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		tail = &(*tail)->next;
	}
	*tail = mem_zalloc(sizeof(**tail));
	(*tail)->offset = loc(x, y);
	(*tail)->feat = feat;
	return PARSE_ERROR_NONE;
}

/** Add a named, directed route between two stable locations. */
static enum parser_error parse_world_route(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *from = parser_getsym(p, "from");
	const char *from_entry = parser_getsym(p, "fromentry");
	const char *to = parser_getsym(p, "to");
	const char *to_entry = parser_getsym(p, "toentry");
	const char *kind = parser_getsym(p, "kind");
	unsigned int turns = parser_getuint(p, "turns");
	struct world_route *route;
	struct world_route **tail = &world_routes;

	if (!world_id_is_valid(id) || !world_id_is_valid(from) ||
			!world_id_is_valid(from_entry) || !world_id_is_valid(to) ||
			!world_id_is_valid(to_entry) || !world_id_is_valid(kind)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (world_route_by_id(id)) return PARSE_ERROR_REPEATED_DIRECTIVE;

	route = mem_zalloc(sizeof(*route));
	route->id = string_make(id);
	route->from = string_make(from);
	route->from_entry = string_make(from_entry);
	route->to = string_make(to);
	route->to_entry = string_make(to_entry);
	route->kind = string_make(kind);
	route->travel_turns = turns;
	while (*tail) tail = &(*tail)->next;
	*tail = route;
	return PARSE_ERROR_NONE;
}

/** Require an explicitly discovered ledger entry before traversing a route. */
static enum parser_error parse_world_route_gate(struct parser *p)
{
	struct world_route *route = world_route_by_id(parser_getsym(p, "id"));

	if (!route) return PARSE_ERROR_INVALID_VALUE;
	if (route->requires_unlock) return PARSE_ERROR_REPEATED_DIRECTIVE;
	route->requires_unlock = true;
	return PARSE_ERROR_NONE;
}

/** Set optional authored narration for a route. */
static enum parser_error parse_world_route_message(struct parser *p)
{
	struct world_route *route = world_route_by_id(parser_getsym(p, "id"));

	if (!route) return PARSE_ERROR_INVALID_VALUE;
	if (route->message) return PARSE_ERROR_REPEATED_DIRECTIVE;
	route->message = string_make(parser_getstr(p, "message"));
	return PARSE_ERROR_NONE;
}

/** Add a stable fast-travel node attached to one named location entry. */
static enum parser_error parse_world_travel_node(struct parser *p)
{
	const char *id = parser_getsym(p, "id");
	const char *location_id = parser_getsym(p, "location");
	const char *entry_id = parser_getsym(p, "entry");
	struct world_travel_node *node;
	struct world_travel_node **tail = &world_travel_nodes;

	if (!world_id_is_valid(id) || !world_id_is_valid(location_id) ||
			!world_id_is_valid(entry_id)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (world_travel_node_by_id(id)) return PARSE_ERROR_REPEATED_DIRECTIVE;

	node = mem_zalloc(sizeof(*node));
	node->id = string_make(id);
	node->location_id = string_make(location_id);
	node->entry_id = string_make(entry_id);
	while (*tail) tail = &(*tail)->next;
	*tail = node;
	return PARSE_ERROR_NONE;
}

static bool add_overworld_entry(struct level *lev, const char *id,
		struct loc grid)
{
	struct world_entry *entry;
	struct world_entry **tail;

	if (!lev || world_entry_by_id(lev, id) || grid.x < 1 || grid.y < 1 ||
			grid.x >= lev->width - 1 || grid.y >= lev->height - 1) {
		return false;
	}
	entry = mem_zalloc(sizeof(*entry));
	entry->id = string_make(id);
	entry->kind = WORLD_ENTRY_GRID;
	entry->grid = grid;
	tail = &lev->entries;
	while (*tail) tail = &(*tail)->next;
	*tail = entry;
	return true;
}

/** Mark a location as one role in the save-shuffled surface grid. */
static enum parser_error parse_world_overworld_cell(struct parser *p)
{
	unsigned int slot = parser_getuint(p, "slot");
	const char *location_id = parser_getsym(p, "location");
	const char *biome_name = parser_getsym(p, "biome");
	unsigned int gate_width = parser_hasval(p, "gate") ?
		parser_getuint(p, "gate") : WORLD_OVERWORLD_GATE_WIDTH_DEFAULT;
	unsigned int population = parser_hasval(p, "population") ?
		parser_getuint(p, "population") : 0;
	struct level *lev = level_by_id(location_id);
	struct level *other;
	struct world_travel_node *node;
	struct world_travel_node **tail = &world_travel_nodes;
	enum world_outdoor_biome biome;
	char node_id[WORLD_ID_LEN];

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!population && lev->kind == WORLD_LOCATION_OUTDOORS) {
		population = 3 + MIN(3, lev->danger);
	}
	if (lev->is_overworld_cell || slot >= WORLD_OVERWORLD_CELL_LIMIT ||
			gate_width < 1 || gate_width > WORLD_OVERWORLD_GATE_WIDTH_MAX ||
			!(gate_width & 1) ||
			gate_width > (unsigned int)(lev->width - 4) ||
			gate_width > (unsigned int)(lev->height - 4) ||
			population > UINT16_MAX ||
			!world_overworld_biome_from_name(biome_name, &biome) ||
			(lev->kind != WORLD_LOCATION_HUB &&
			 lev->kind != WORLD_LOCATION_OUTDOORS) ||
			lev->mode != WORLD_MODE_TOP_DOWN || !lev->height || !lev->width ||
			(lev->kind == WORLD_LOCATION_HUB) !=
				(biome == WORLD_BIOME_VILLAGE)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (other = world; other; other = other->next) {
		if (other->is_overworld_cell && other->overworld_slot == slot) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	if (!add_overworld_entry(lev, "edge.north",
			loc(lev->width / 2, 2)) ||
			!add_overworld_entry(lev, "edge.east",
				loc(lev->width - 3, lev->height / 2)) ||
			!add_overworld_entry(lev, "edge.south",
				loc(lev->width / 2, lev->height - 3)) ||
			!add_overworld_entry(lev, "edge.west",
				loc(2, lev->height / 2)) ||
			!add_overworld_entry(lev, "travel.stop",
				loc(lev->width / 2, lev->height / 2))) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	strnfmt(node_id, sizeof(node_id), "core.node.overworld.%02u", slot);
	if (!world_id_is_valid(node_id) || world_travel_node_by_id(node_id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	node = mem_zalloc(sizeof(*node));
	node->id = string_make(node_id);
	node->location_id = string_make(lev->id);
	node->entry_id = string_make("travel.stop");
	while (*tail) tail = &(*tail)->next;
	*tail = node;
	lev->is_overworld_cell = true;
	lev->overworld_slot = (uint8_t)slot;
	lev->biome = biome;
	lev->overworld_gate_width = (uint8_t)gate_width;
	lev->overworld_population = (uint16_t)population;
	return PARSE_ERROR_NONE;
}

/** Attach one explicit fishing encounter identity to a stable location. */
static enum parser_error parse_world_fishing_habitat(struct parser *p)
{
	const char *location_id = parser_getsym(p, "location");
	const char *habitat_name = parser_getsym(p, "habitat");
	struct level *lev = level_by_id(location_id);
	enum world_fishing_habitat habitat;

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (lev->fishing_habitat != WORLD_FISHING_HABITAT_NONE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!world_fishing_habitat_from_name(habitat_name, &habitat) ||
			habitat == WORLD_FISHING_HABITAT_NONE) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	lev->fishing_habitat = habitat;
	return PARSE_ERROR_NONE;
}

/** Mark a top-down location as a place where the global shops are present. */
static enum parser_error parse_world_store_services(struct parser *p)
{
	const char *location_id = parser_getsym(p, "location");
	struct level *lev = level_by_id(location_id);

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (lev->has_store_services) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (lev->mode != WORLD_MODE_TOP_DOWN) return PARSE_ERROR_INVALID_VALUE;
	lev->has_store_services = true;
	return PARSE_ERROR_NONE;
}

/** Add a restrained first-descent observation to one stable location. */
static enum parser_error parse_world_arrival_message(struct parser *p)
{
	struct level *lev = level_by_id(parser_getsym(p, "location"));
	const char *message = parser_getstr(p, "message");

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!message[0]) return PARSE_ERROR_INVALID_VALUE;
	if (lev->arrival_message) return PARSE_ERROR_REPEATED_DIRECTIVE;
	lev->arrival_message = string_make(message);
	return PARSE_ERROR_NONE;
}

/** Attach a modal first-descent scene to one stable location. */
static enum parser_error parse_world_arrival_scene(struct parser *p)
{
	struct level *lev = level_by_id(parser_getsym(p, "location"));
	const struct world_story_scene *scene = world_story_scene_by_id(
		parser_getsym(p, "scene"));

	if (!lev) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!scene) return PARSE_ERROR_INVALID_VALUE;
	if (lev->arrival_scene) return PARSE_ERROR_REPEATED_DIRECTIVE;
	lev->arrival_scene = scene;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_world(void) {
	struct parser *p = parser_new();

	parser_reg(p, "level int depth sym name sym up sym down",
			   parse_world_level);
	parser_reg(p, "site sym id str name", parse_world_site);
	parser_reg(p, "site-visual sym id sym profile", parse_world_site_visual);
	parser_reg(p, "site-floor-term sym id str term",
		parse_world_site_floor_term);
	parser_reg(p, "site-opening sym id sym scene", parse_world_site_opening);
	parser_reg(p, "site-victory sym id sym profile str message",
			parse_world_site_victory);
	parser_reg(p, "site-map sym id sym color sym row1 sym row2 sym row3 "
			"str description", parse_world_site_map);
	parser_reg(p, "site-position sym id int x int y",
			parse_world_site_position);
	parser_reg(p, "site-anchor sym id sym anchor int dx int dy",
			parse_world_site_anchor);
	parser_reg(p, "location sym level sym id sym site sym kind sym mode "
			"int floor int danger ?uint height ?uint width",
			parse_world_location);
	parser_reg(p, "place sym name sym id sym site sym kind sym mode int floor "
			"int danger uint height uint width", parse_world_place);
	parser_reg(p, "entry sym location sym id sym kind ?int y ?int x",
			parse_world_entry);
	parser_reg(p, "entry-rock sym location sym id int y int x sym feature",
			parse_world_entry_rock);
	parser_reg(p, "route sym id sym from sym fromentry sym to sym toentry "
			"sym kind uint turns", parse_world_route);
	parser_reg(p, "route-gate sym id", parse_world_route_gate);
	parser_reg(p, "route-message sym id str message", parse_world_route_message);
	parser_reg(p, "travel-node sym id sym location sym entry",
			parse_world_travel_node);
	parser_reg(p, "overworld-cell uint slot sym location sym biome "
			"?uint gate ?uint population",
			parse_world_overworld_cell);
	parser_reg(p, "fishing-habitat sym location sym habitat",
			parse_world_fishing_habitat);
	parser_reg(p, "store-services sym location",
			parse_world_store_services);
	parser_reg(p, "arrival-message sym location str message",
			parse_world_arrival_message);
	parser_reg(p, "arrival-scene sym location sym scene",
			parse_world_arrival_scene);
	return p;
}

/** Derive a tolerable display name for an entirely legacy world file. */
static char *legacy_world_site_name(const struct level *lev)
{
	char name[80];
	size_t length;

	my_strcpy(name, lev && lev->name ? lev->name : "Unknown site",
		sizeof(name));
	if (!lev || lev->kind == WORLD_LOCATION_HUB) return string_make(name);
	length = strlen(name);
	while (length && name[length - 1] >= '0' && name[length - 1] <= '9') {
		name[--length] = '\0';
	}
	while (length && name[length - 1] == ' ') name[--length] = '\0';
	return string_make(name[0] ? name : lev->name);
}

/** Supply site records only when an old file has no explicit site section. */
static void assign_legacy_world_sites(void)
{
	struct level *lev;
	struct world_site **tail = &world_sites;

	if (world_sites) return;
	for (lev = world; lev; lev = lev->next) {
		struct world_site *site;

		if (world_site_by_id(lev->site_id)) continue;
		site = mem_zalloc(sizeof(*site));
		site->id = string_make(lev->site_id);
		site->name = legacy_world_site_name(lev);
		while (*tail) tail = &(*tail)->next;
		*tail = site;
	}
}

/** Validate the complete launch layout before a player can depend on it. */
static errr validate_overworld_records(void)
{
	bool overworld_slots[WORLD_OVERWORLD_LAUNCH_CELLS] = { false };
	struct level *lev;
	struct world_site *site;
	unsigned int count = 0;
	unsigned int villages = 0;
	unsigned int cavewoods = 0;
	errr result = PARSE_ERROR_NONE;

	for (lev = world; lev; lev = lev->next) {
		if (!lev->is_overworld_cell) continue;
		count++;
		if (lev->overworld_slot >= WORLD_OVERWORLD_LAUNCH_CELLS ||
				overworld_slots[lev->overworld_slot]) {
			plog_fmt("Invalid overworld slot for location %s", lev->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else {
			overworld_slots[lev->overworld_slot] = true;
		}
		if (lev->biome == WORLD_BIOME_VILLAGE) villages++;
		if (lev->biome == WORLD_BIOME_CAVEWOOD) cavewoods++;
	}
	if (count && (count != WORLD_OVERWORLD_LAUNCH_CELLS || villages != 1 ||
			cavewoods != 1)) {
		plog_fmt("The launch overworld requires %u cells, one village, and "
			"one cavewood", WORLD_OVERWORLD_LAUNCH_CELLS);
		result = PARSE_ERROR_INVALID_VALUE;
	}
	for (site = world_sites; site; site = site->next) {
		bool anchored = false;

		if (!site->has_map_anchor) continue;
		for (lev = world; lev; lev = lev->next) {
			if (lev->is_overworld_cell &&
					streq(lev->site_id, site->anchor_site_id)) {
				anchored = true;
				break;
			}
		}
		if (!anchored) {
			plog_fmt("World site %s is not anchored to an overworld site",
				site->id);
			result = PARSE_ERROR_INVALID_VALUE;
		}
	}
	return result;
}

static errr run_parse_world(struct parser *p) {
	return parse_file_quit_not_found(p, "world");
}

/** Supply stable compatibility IDs when reading an unextended world file. */
static void assign_legacy_world_metadata(struct level *lev)
{
	char id[WORLD_ID_LEN];

	if (lev->id) return;
	if (lev->depth == 0) {
		my_strcpy(id, "core.hub", sizeof(id));
		lev->site_id = string_make("core.hub");
		lev->kind = WORLD_LOCATION_HUB;
		/* Compatibility for unextended Angband world files.  Modern Ammerow
		 * data declares this role explicitly with store-services. */
		lev->has_store_services = true;
	} else {
		strnfmt(id, sizeof(id), "core.main.%03d", lev->depth);
		lev->site_id = string_make("core.main");
		lev->kind = WORLD_LOCATION_DUNGEON;
	}
	lev->id = string_make(id);
	lev->mode = WORLD_MODE_TOP_DOWN;
	lev->local_floor = lev->depth;
	lev->danger = lev->depth;
}

static errr finish_parse_world(struct parser *p) {
	struct level *level_check;
	struct world_route *route;
	struct world_travel_node *node;
	errr result = PARSE_ERROR_NONE;
	int maxe = get_parser_error_limit(), counte = 0;

	/* Old world files receive deterministic identities for save migration. */
	for (level_check = world; level_check; level_check = level_check->next) {
		assign_legacy_world_metadata(level_check);
	}
	assign_legacy_world_sites();
	if (validate_overworld_records() != PARSE_ERROR_NONE) {
		result = PARSE_ERROR_INVALID_VALUE;
	}

	/* Stable identities must be unique even when explicit and legacy records
	 * are mixed. */
	for (level_check = world; level_check; level_check = level_check->next) {
		struct level *other = level_check->next;
		struct world_entry *entry = level_check->entries;

		if (!world_site_by_id(level_check->site_id)) {
			plog_fmt("Unknown world site %s for location %s",
				level_check->site_id, level_check->id);
			result = PARSE_ERROR_INVALID_VALUE;
		}
		/* Fixed entries in explicitly sized locations must be representable by
		 * the generated chunk.  Stair-based entries are resolved at runtime. */
		while (entry) {
			if (entry->kind == WORLD_ENTRY_GRID && level_check->height &&
					(entry->grid.y >= level_check->height ||
					entry->grid.x >= level_check->width)) {
				plog_fmt("World entry %s lies outside location %s",
					entry->id, level_check->id);
				result = PARSE_ERROR_INVALID_VALUE;
			}
			entry = entry->next;
		}

		while (other) {
			if (streq(level_check->id, other->id)) {
				plog_fmt("Duplicate world location ID, %s", level_check->id);
				result = PARSE_ERROR_INVALID_VALUE;
			}
			other = other->next;
		}
	}

	/* Check that all levels referred to exist */
	for (level_check = world; level_check; level_check = level_check->next) {
		struct level *level_find = world;

		/* Check upwards */
		if (level_check->up) {
			while (level_find && !streq(level_check->up, level_find->name)) {
				level_find = level_find->next;
			}
			if (!level_find) {
				if (result == PARSE_ERROR_NONE) {
					result = PARSE_ERROR_INVALID_VALUE;
				}
				plog_fmt("Invalid up level reference, %s, "
					"for level %s", level_check->up,
					level_check->name);
				if (maxe) {
					if (counte >= maxe - 1) {
						break;
					}
					++counte;
				}
			} else {
				level_check->up_id = string_make(level_find->id);
			}
		}

		/* Check downwards */
		level_find = world;
		if (level_check->down) {
			while (level_find && !streq(level_check->down, level_find->name)) {
				level_find = level_find->next;
			}
			if (!level_find) {
				if (result == PARSE_ERROR_NONE) {
					result = PARSE_ERROR_INVALID_VALUE;
				}
				plog_fmt("Invalid down level reference, %s, "
					"for level %s", level_check->down,
					level_check->name);
				if (maxe) {
					if (counte >= maxe - 1) {
						break;
					}
					++counte;
				}
			} else {
				level_check->down_id = string_make(level_find->id);
			}
		}
	}

	/* Routes use stable identities and can be declared in any order after the
	 * levels they connect. */
	for (route = world_routes; route; route = route->next) {
		struct level *from = level_by_id(route->from);
		struct level *to = level_by_id(route->to);
		const struct world_entry *from_entry = from ?
			world_entry_by_id(from, route->from_entry) : NULL;
		const struct world_entry *to_entry = to ?
			world_entry_by_id(to, route->to_entry) : NULL;
		bool from_stair = from_entry &&
			(from_entry->kind == WORLD_ENTRY_UP_STAIR ||
			 from_entry->kind == WORLD_ENTRY_DOWN_STAIR);
		bool to_stair = to_entry &&
			(to_entry->kind == WORLD_ENTRY_UP_STAIR ||
			 to_entry->kind == WORLD_ENTRY_DOWN_STAIR);

		if (!from || !to) {
			plog_fmt("Invalid endpoint for world route %s", route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (!from_entry || !to_entry) {
			plog_fmt("Invalid named entry for world route %s", route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (streq(route->kind, "edge") &&
				(route->travel_turns != 0 ||
				from->mode != WORLD_MODE_TOP_DOWN ||
				to->mode != WORLD_MODE_TOP_DOWN ||
				from_entry->kind != WORLD_ENTRY_GRID ||
				to_entry->kind != WORLD_ENTRY_GRID)) {
			plog_fmt("Invalid physical edge route %s", route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (streq(route->kind, "stairs") &&
				(route->travel_turns != 0 ||
				from->mode != WORLD_MODE_TOP_DOWN ||
				to->mode != WORLD_MODE_TOP_DOWN || !from_stair || !to_stair ||
				from_entry->kind == to_entry->kind)) {
			plog_fmt("Invalid physical stair route %s", route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (streq(route->kind, "shaft") &&
				(!world_route_is_cross_mode(route) ||
				from_entry->kind != WORLD_ENTRY_GRID ||
				to_entry->kind != WORLD_ENTRY_GRID)) {
			plog_fmt("Invalid cross-mode shaft route %s", route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (streq(route->kind, "recall") &&
				(!world_route_is_recall(route) || route->requires_unlock)) {
			plog_fmt("Invalid magical recall route %s", route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (streq(route->kind, "trail") &&
				(route->travel_turns == 0 ||
				from->mode != WORLD_MODE_TOP_DOWN ||
				to->mode != WORLD_MODE_TOP_DOWN)) {
			plog_fmt("Invalid aggregate trail route %s", route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (!streq(route->kind, "edge") &&
				!streq(route->kind, "stairs") &&
				!streq(route->kind, "shaft") &&
				!streq(route->kind, "recall") &&
				!streq(route->kind, "trail") &&
				!streq(route->kind, "retired")) {
			plog_fmt("Unknown world route kind %s for %s", route->kind,
				route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		} else if (streq(route->kind, "trail") &&
				(!world_travel_node_by_endpoint(route->from,
					route->from_entry) ||
				 !world_travel_node_by_endpoint(route->to,
					route->to_entry))) {
			plog_fmt("Missing travel node for aggregate trail route %s",
				route->id);
			result = PARSE_ERROR_INVALID_VALUE;
		}
	}

	/* Travel nodes must map one stable ID to one unique declared entry. */
	for (node = world_travel_nodes; node; node = node->next) {
		struct level *location = level_by_id(node->location_id);
		struct world_travel_node *other = node->next;

		if (!location || !world_entry_by_id(location, node->entry_id)) {
			plog_fmt("Invalid endpoint for world travel node %s", node->id);
			result = PARSE_ERROR_INVALID_VALUE;
		}
		while (other) {
			if (streq(node->location_id, other->location_id) &&
					streq(node->entry_id, other->entry_id)) {
				plog_fmt("Duplicate endpoint for world travel nodes %s and %s",
					node->id, other->id);
				result = PARSE_ERROR_INVALID_VALUE;
			}
			other = other->next;
		}
	}

	parser_destroy(p);
	return result;
}

static void cleanup_world(void)
{
	struct level *level = world;
	struct world_site *site = world_sites;
	struct world_route *route = world_routes;
	struct world_travel_node *node = world_travel_nodes;

	while (level) {
		struct level *old = level;
		struct world_entry *entry = level->entries;

		while (entry) {
			struct world_entry *old_entry = entry;
			struct world_entry_rock *rock = entry->rocks;

			while (rock) {
				struct world_entry_rock *old_rock = rock;
				rock = rock->next;
				mem_free(old_rock);
			}

			string_free(entry->id);
			entry = entry->next;
			mem_free(old_entry);
		}
		string_free(level->name);
		string_free(level->up);
		string_free(level->down);
		string_free(level->id);
		string_free(level->site_id);
		string_free(level->up_id);
		string_free(level->down_id);
		string_free(level->arrival_message);
		level = level->next;
		mem_free(old);
	}
	world = NULL;

	while (site) {
		struct world_site *old = site;

		string_free(site->id);
		string_free(site->name);
		string_free(site->visual_profile);
		string_free(site->victory_visual_profile);
		string_free(site->victory_message);
		string_free(site->floor_term);
		string_free(site->map_description);
		string_free(site->anchor_site_id);
		site = site->next;
		mem_free(old);
	}
	world_sites = NULL;

	while (route) {
		struct world_route *old = route;
		string_free(route->id);
		string_free(route->from);
		string_free(route->from_entry);
		string_free(route->to);
		string_free(route->to_entry);
		string_free(route->kind);
		string_free(route->message);
		route = route->next;
		mem_free(old);
	}
	world_routes = NULL;

	while (node) {
		struct world_travel_node *old = node;

		string_free(node->id);
		string_free(node->location_id);
		string_free(node->entry_id);
		node = node->next;
		mem_free(old);
	}
	world_travel_nodes = NULL;
}

struct file_parser world_parser = {
	"world",
	init_parse_world,
	run_parse_world,
	finish_parse_world,
	cleanup_world
};
