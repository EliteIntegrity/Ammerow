/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-world.c
 * \brief Interactive known-world map and surface travel controller.
 *
 * The terminal rendering remains the portable fallback.  Frontends receive
 * the same short-lived semantic map and selected-cell cursor.
 */

#include "angband.h"
#include "cmd-core.h"
#include "game-world.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-screen.h"
#include "ui-term.h"
#include "ui-world.h"
#include "world-map.h"
#include "world-overworld.h"
#include "world-travel-action.h"
#include "world-transition.h"

#define UI_WORLD_SURFACE_CELLS WORLD_OVERWORLD_LAUNCH_CELLS
#define UI_WORLD_NODE_LABEL 160
#define UI_WORLD_NODE_DETAIL 80
#define UI_WORLD_MAP_SCALE 4

struct ui_world_surface_map {
	struct ui_screen_row rows[UI_WORLD_SURFACE_CELLS];
	struct ui_screen_map_node nodes[UI_WORLD_SURFACE_CELLS];
	struct ui_screen_map_edge edges[2 * UI_WORLD_SURFACE_CELLS];
	char labels[UI_WORLD_SURFACE_CELLS][UI_WORLD_NODE_LABEL];
	char details[UI_WORLD_SURFACE_CELLS][UI_WORLD_NODE_DETAIL];
	const struct level *levels[UI_WORLD_SURFACE_CELLS];
	bool discovered[UI_WORLD_SURFACE_CELLS];
	int edge_count;
	int current_cell;
	bool below_surface;
};

static const char *location_kind_name(enum world_location_kind kind)
{
	switch (kind) {
		case WORLD_LOCATION_HUB: return "Settlement";
		case WORLD_LOCATION_OUTDOORS: return "Outdoors";
		case WORLD_LOCATION_DUNGEON: return "Delve";
		case WORLD_LOCATION_INTERIOR: return "Interior";
		case WORLD_LOCATION_SPECIAL: return "Special";
		default: return "Location";
	}
}

static void format_site_detail(const struct world_map_site_summary *site,
		char *detail, size_t capacity)
{
	const struct level *lev = site ? site->representative : NULL;

	if (!lev) {
		my_strcpy(detail, "Discovered", capacity);
	} else if (site->location_count > 1) {
		if (site->minimum_floor == site->maximum_floor) {
			strnfmt(detail, capacity, "%u areas | floor %d | danger %d-%d",
				site->location_count, site->minimum_floor,
				site->minimum_danger, site->maximum_danger);
		} else {
			strnfmt(detail, capacity,
				"%u areas | floors %d-%d | danger %d-%d",
				site->location_count, site->minimum_floor,
				site->maximum_floor, site->minimum_danger,
				site->maximum_danger);
		}
	} else if (lev->local_floor > 0) {
		strnfmt(detail, capacity, "Floor %d | danger %d",
			lev->local_floor, lev->danger);
	} else {
		strnfmt(detail, capacity, "%s | danger %d",
			location_kind_name(lev->kind), lev->danger);
	}
}

static void fill_map_stamp(struct ui_screen_map_node *node,
		const struct world_site *site, bool discovered)
{
	static const char *const unknown[UI_SCREEN_MAP_STAMP_HEIGHT] = {
		".....", ".???.", "....."
	};
	int row;

	for (row = 0; row < UI_SCREEN_MAP_STAMP_HEIGHT; row++) {
		const char *source = discovered && site && site->has_map_visual ?
			site->map_stamp[row] : unknown[row];

		my_strcpy(node->stamp[row], source, sizeof(node->stamp[row]));
	}
}

static const struct world_map_site_summary *map_site_summary(
		const struct world_map_model *model, const char *site_id)
{
	unsigned int i;

	if (!model || !site_id) return NULL;
	for (i = 0; i < model->site_count; i++) {
		if (streq(model->sites[i].site->id, site_id)) {
			return &model->sites[i];
		}
	}
	return NULL;
}

static bool sites_have_learned_connection(const struct world_map_model *model,
		const char *first, const char *second)
{
	unsigned int i;

	for (i = 0; model && i < model->route_count; i++) {
		const struct world_map_route_summary *route = &model->routes[i];

		if (!route->source || !route->destination) continue;
		if ((streq(route->source->id, first) &&
				streq(route->destination->id, second)) ||
				(streq(route->source->id, second) &&
				 streq(route->destination->id, first))) {
			return true;
		}
	}
	return false;
}

/** Find the surface cell which contains the current location or its entrance. */
static int current_surface_cell(const struct player *p, bool *below_surface)
{
	const struct level *current = world_player_level(p);
	const struct world_site *site;
	const struct level *lev;
	int x, y;

	if (below_surface) *below_surface = false;
	if (!current) return -1;
	if (world_overworld_position(p, current->id, &x, &y)) {
		return y * p->overworld_layout.width + x;
	}
	site = world_site_by_id(current->site_id);
	if (!site || !site->has_map_anchor) return -1;
	for (lev = world; lev; lev = lev->next) {
		if (!lev->is_overworld_cell ||
				!streq(lev->site_id, site->anchor_site_id)) {
			continue;
		}
		if (world_overworld_position(p, lev->id, &x, &y)) {
			if (below_surface) *below_surface = true;
			return y * p->overworld_layout.width + x;
		}
	}
	return -1;
}

static void add_grid_edge(struct ui_world_surface_map *map,
		const struct world_map_model *model, int first, int second)
{
	struct ui_screen_map_edge *edge;
	bool both_discovered;
	bool learned;

	if (map->edge_count >= (int)N_ELEMENTS(map->edges)) return;
	edge = &map->edges[map->edge_count++];
	both_discovered = map->discovered[first] && map->discovered[second];
	learned = both_discovered && sites_have_learned_connection(model,
		map->levels[first]->site_id, map->levels[second]->site_id);
	edge->from = first;
	edge->to = second;
	edge->ready = learned;
	edge->attr = learned ? COLOUR_L_GREEN :
		both_discovered ? COLOUR_YELLOW : COLOUR_SLATE;
}

/** Build all nine cells, but reveal identity only after physical discovery. */
static bool build_surface_map(const struct player *p,
		const struct world_map_model *model, struct ui_world_surface_map *map)
{
	int x, y;

	if (!p || !model || !map || !world_overworld_layout_valid(p) ||
			p->overworld_layout.count != UI_WORLD_SURFACE_CELLS) {
		return false;
	}
	memset(map, 0, sizeof(*map));
	map->current_cell = current_surface_cell(p, &map->below_surface);
	for (y = 0; y < p->overworld_layout.height; y++) {
		for (x = 0; x < p->overworld_layout.width; x++) {
			int index = y * p->overworld_layout.width + x;
			const struct world_map_site_summary *summary;
			const struct world_site *site;
			struct ui_screen_map_node *node = &map->nodes[index];
			struct ui_screen_row *row = &map->rows[index];

			map->levels[index] = world_overworld_location_at(p, x, y);
			if (!map->levels[index]) return false;
			map->discovered[index] =
				world_player_has_discovered_location(p,
					map->levels[index]->id);
			summary = map->discovered[index] ? map_site_summary(model,
				map->levels[index]->site_id) : NULL;
			site = summary ? summary->site : NULL;
			if (summary) {
				my_strcpy(map->labels[index], summary->site->name,
					sizeof(map->labels[index]));
				format_site_detail(summary, map->details[index],
					sizeof(map->details[index]));
			} else {
				my_strcpy(map->labels[index], "Unexplored",
					sizeof(map->labels[index]));
				my_strcpy(map->details[index],
					"Travel here on foot to reveal it",
					sizeof(map->details[index]));
			}
			node->label = map->labels[index];
			node->detail = map->details[index];
			node->description = site && site->map_description ?
				site->map_description : map->discovered[index] ?
				"No landscape description has been recorded." :
				"Its landscape is unknown until you reach it on foot.";
			fill_map_stamp(node, site, map->discovered[index]);
			node->x = x * UI_WORLD_MAP_SCALE;
			node->y = y * UI_WORLD_MAP_SCALE;
			node->current = index == map->current_cell;
			node->glyph = node->current ?
				(map->below_surface ? L'v' : L'@') :
				(map->discovered[index] ? L'*' : L'?');
			node->attr = site && site->has_map_visual ? site->map_attr :
				map->discovered[index] ? COLOUR_L_BLUE : COLOUR_SLATE;
			row->label = node->label;
			row->prefix = node->current ? "Current" : "Region";
			row->detail = node->detail;
			row->glyph = node->glyph;
			row->attr = node->attr;
			row->enabled = map->discovered[index];
		}
	}
	for (y = 0; y < p->overworld_layout.height; y++) {
		for (x = 0; x < p->overworld_layout.width; x++) {
			int index = y * p->overworld_layout.width + x;

			if (x + 1 < p->overworld_layout.width) {
				add_grid_edge(map, model, index, index + 1);
			}
			if (y + 1 < p->overworld_layout.height) {
				add_grid_edge(map, model, index,
					index + p->overworld_layout.width);
			}
		}
	}
	if (map->current_cell < 0) {
		for (x = 0; x < p->overworld_layout.count; x++) {
			if (map->discovered[x]) {
				map->current_cell = x;
				break;
			}
		}
	}
	return map->current_cell >= 0;
}

static const struct world_route *travel_route_to(const struct level *destination)
{
	const struct world_route *route;

	if (!destination) return NULL;
	for (route = world_routes; route; route = route->next) {
		if (world_route_is_aggregate(route) &&
				world_player_route_is_unlocked(player, route->id) &&
				streq(route->from, player->world_location.id) &&
				streq(route->to, destination->id)) {
			return route;
		}
	}
	return NULL;
}

static const char *travel_status(const struct world_route *route,
		enum world_travel_safety_result safety, bool *ready)
{
	struct world_transition transition;
	enum world_transition_result result;

	if (ready) *ready = false;
	if (!route) return "no route from here";
	result = world_transition_prepare_travel(player, cave, route->id,
		&transition);
	if (result != WORLD_TRANSITION_OK) {
		switch (result) {
			case WORLD_TRANSITION_LOCKED: return "route locked";
			case WORLD_TRANSITION_DESTINATION_NODE_INACTIVE:
				return "destination not activated";
			case WORLD_TRANSITION_SOURCE_NODE_INACTIVE:
				return "departure not activated";
			case WORLD_TRANSITION_WRONG_SOURCE:
				return "return to the surface first";
			default: return "travel unavailable";
		}
	}
	switch (safety) {
		case WORLD_TRAVEL_SAFETY_OK:
			if (ready) *ready = true;
			return "Enter to travel";
		case WORLD_TRAVEL_SAFETY_THREATENED:
			return "visible threat nearby";
		case WORLD_TRAVEL_SAFETY_RESTRAINED: return "restrained";
		case WORLD_TRAVEL_SAFETY_TEMPORARY_EFFECT:
			return "temporary effect active";
		case WORLD_TRAVEL_SAFETY_PENDING_RELOCATION:
			return "relocation pending";
		case WORLD_TRAVEL_SAFETY_UNSUPPORTED_LOCATION:
			return "return to the surface first";
		default: return "travel unsafe now";
	}
}

static void format_selection_status(const struct ui_world_surface_map *map,
		int selected, char *status, size_t capacity,
		const struct world_route **ready_route)
{
	const struct world_route *route = NULL;
	const char *route_state;
	bool ready = false;

	if (ready_route) *ready_route = NULL;
	if (selected < 0 || selected >= UI_WORLD_SURFACE_CELLS) {
		my_strcpy(status, "Move across the map to inspect a region.", capacity);
		return;
	}
	if (!map->discovered[selected]) {
		strnfmt(status, capacity, "%s | reach this region on foot to reveal it",
			map->labels[selected]);
		return;
	}
	if (selected == map->current_cell) {
		strnfmt(status, capacity, "%s | %s", map->labels[selected],
			map->below_surface ? "you are below this region" : "you are here");
		return;
	}
	if (map->below_surface) {
		strnfmt(status, capacity, "%s | return to the surface before travelling",
			map->labels[selected]);
		return;
	}
	route = travel_route_to(map->levels[selected]);
	route_state = travel_status(route,
		world_travel_check_safety(player, cave), &ready);
	if (route) {
		strnfmt(status, capacity, "%s | %u turns | %s",
			map->labels[selected], route->travel_turns, route_state);
	} else {
		strnfmt(status, capacity, "%s | %s",
			map->labels[selected], route_state);
	}
	if (ready && ready_route) *ready_route = route;
}

static int move_surface_cursor(const struct player *p, int cursor,
		int dx, int dy)
{
	int x, y;

	if (!p || cursor < 0 || cursor >= p->overworld_layout.count) {
		return cursor;
	}
	x = cursor % p->overworld_layout.width + dx;
	y = cursor / p->overworld_layout.width + dy;
	if (x < 0 || x >= p->overworld_layout.width || y < 0 ||
			y >= p->overworld_layout.height) {
		return cursor;
	}
	return y * p->overworld_layout.width + x;
}

static void draw_terminal_map(const struct ui_world_surface_map *map,
		int selected, const char *status)
{
	int node_x[UI_WORLD_SURFACE_CELLS];
	int node_y[UI_WORLD_SURFACE_CELLS];
	int map_left = 2;
	int map_right = MAX(map_left + 42, Term->wid - 3);
	int map_top = 7;
	int detail_top = MAX(map_top + 9, Term->hgt - 8);
	int map_bottom = MAX(map_top + 6, detail_top - 3);
	int i;

	prt("KNOWN WORLD", 1, 2);
	prt(status, 3, 2);
	for (i = 0; i < UI_WORLD_SURFACE_CELLS; i++) {
		int x = i % 3;
		int y = i / 3;

		node_x[i] = map_left + 8 + x * (map_right - map_left - 16) / 2;
		node_y[i] = map_top + y * (map_bottom - map_top) / 2;
	}
	for (i = 0; i < map->edge_count; i++) {
		const struct ui_screen_map_edge *edge = &map->edges[i];
		int x1 = node_x[edge->from];
		int y1 = node_y[edge->from];
		int x2 = node_x[edge->to];
		int y2 = node_y[edge->to];
		int attr = edge->attr;

		if (y1 == y2) {
			int x;
			for (x = x1 + 3; x < x2 - 2; x++) {
				Term_putch(x, y1, attr, edge->ready ? L'=' : L'.');
			}
		} else {
			int y;
			for (y = y1 + 2; y < y2 - 1; y++) {
				Term_putch(x1, y, attr, edge->ready ? L'|' : L':');
			}
		}
	}
	for (i = 0; i < UI_WORLD_SURFACE_CELLS; i++) {
		uint8_t attr = i == selected ? COLOUR_YELLOW : map->nodes[i].attr;
		int row;

		for (row = 0; row < UI_SCREEN_MAP_STAMP_HEIGHT; row++) {
			c_put_str(attr, map->nodes[i].stamp[row],
				node_y[i] + row - 1, node_x[i] - 2);
		}
		if (map->nodes[i].current) {
			Term_putch(node_x[i], node_y[i], COLOUR_L_GREEN,
				map->nodes[i].glyph);
		}
		if (i == selected && node_x[i] >= 4) {
			Term_putch(node_x[i] - 4, node_y[i], COLOUR_YELLOW, L'>');
		}
	}
	if (selected >= 0 && selected < UI_WORLD_SURFACE_CELLS) {
		const struct ui_screen_map_node *node = &map->nodes[selected];

		c_put_str(COLOUR_YELLOW, node->label, detail_top, 2);
		c_put_str(COLOUR_SLATE, node->detail, detail_top + 1, 2);
		c_put_str(COLOUR_WHITE, node->description, detail_top + 2, 2);
	}
	prt("Arrows move | Enter travels | Escape returns", Term->hgt - 2, 2);
}

void do_cmd_world_map(void)
{
	struct world_map_model model;
	struct ui_world_surface_map map;
	struct ui_screen screen = { 0 };
	char status[256];
	int selected;
	bool done = false;

	if (!player || !character_dungeon) {
		msg("There is no known world for this character yet.");
		return;
	}
	world_player_activate_travel_nodes_here(player, cave);
	(void)world_overworld_activate_current_stop(player);
	world_map_build(player, &model);
	if (!build_surface_map(player, &model, &map)) {
		msg("The known-world map is unavailable for this world layout.");
		return;
	}
	selected = map.current_cell;
	screen.kind = UI_SCREEN_WORLD_MAP;
	screen.title = "Known World";
	screen.help = "Arrows move    Enter travels    Escape returns";
	screen.content_col = 4;
	screen.content_row = 7;
	screen.content_cols = MAX(20, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - 10);
	screen.row_count = UI_WORLD_SURFACE_CELLS;
	screen.rows = map.rows;
	screen.map_node_count = UI_WORLD_SURFACE_CELLS;
	screen.map_nodes = map.nodes;
	screen.map_edge_count = map.edge_count;
	screen.map_edges = map.edges;

	screen_save();
	while (!done) {
		const struct world_route *ready_route = NULL;
		ui_event event;

		format_selection_status(&map, selected, status, sizeof(status),
			&ready_route);
		screen.subtitle = status;
		screen.cursor = selected;
		Term_clear();
		if (Term->screen_hook) Term->screen_hook(&screen);
		draw_terminal_map(&map, selected, status);
		Term_fresh();
		event = inkey_ex();
		if (event.type == EVT_RESIZE) continue;
		if (event.type == EVT_ESCAPE || event.type == EVT_DISCONNECT) break;
		if (event.type != EVT_KBRD && event.type != EVT_BUTTON) continue;
		switch (event.key.code) {
			case ESCAPE:
				done = true;
				break;
			case ARROW_LEFT:
			case '4':
			case 'h':
				selected = move_surface_cursor(player, selected, -1, 0);
				break;
			case ARROW_RIGHT:
			case '6':
			case 'l':
				selected = move_surface_cursor(player, selected, 1, 0);
				break;
			case ARROW_UP:
			case '8':
			case 'k':
				selected = move_surface_cursor(player, selected, 0, -1);
				break;
			case ARROW_DOWN:
			case '2':
			case 'j':
				selected = move_surface_cursor(player, selected, 0, 1);
				break;
			case KC_ENTER:
			case '\r':
			case '\n':
				if (ready_route && cmdq_push(CMD_TRAVEL) == 0) {
					cmd_set_arg_string(cmdq_peek(), "route", ready_route->id);
					done = true;
				}
				break;
			default:
				break;
		}
	}
	if (Term->screen_hook) Term->screen_hook(NULL);
	screen_load();
}
