/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/world */
/* Exercise parsing used for world.txt. */

#include "unit-test.h"
#include "cave.h"
#include "game-world.h"
#include "init.h"
#include "player.h"
#include "world-data.h"
#include "world-entry.h"
#include "world-map.h"
#include "world-overworld.h"
#include "z-color.h"

NOSETUP
NOTEARDOWN

static int test_complete0(void *state)
{
	const char *lines[] = {
		"level:0:Town:None:Test 1",
		"level:1:Test 1:Town:Test 2",
		"level:2:Test 2:Test 1:None"
	};
	struct parser *p = world_parser.init();
	struct level *lv;
	int i, rtn;

	notnull(p);
	for (i = 0; i < (int) N_ELEMENTS(lines); ++i) {
		enum parser_error r = parser_parse(p, lines[i]);

		eq(r, PARSE_ERROR_NONE);
	}
	rtn = world_parser.finish(p);
	eq(rtn, 0);

	lv = world;
	notnull(lv);
	eq(lv->depth, 0);
	require(lv->has_legacy_depth);
	notnull(lv->name);
	require(streq(lv->name, "Town"));
	require(streq(lv->id, "core.hub"));
	require(streq(lv->site_id, "core.hub"));
	require(lv->kind == WORLD_LOCATION_HUB);
	require(lv->mode == WORLD_MODE_TOP_DOWN);
	require(lv->has_store_services);
	eq(lv->local_floor, 0);
	eq(lv->danger, 0);
	null(lv->up);
	notnull(lv->down);
	require(streq(lv->down, "Test 1"));
	require(streq(lv->down_id, "core.main.001"));
	lv = lv->next;
	notnull(lv);
	eq(lv->depth, 1);
	require(lv->has_legacy_depth);
	notnull(lv->name);
	require(streq(lv->name, "Test 1"));
	require(streq(lv->id, "core.main.001"));
	notnull(lv->up);
	require(streq(lv->up, "Town"));
	require(streq(lv->up_id, "core.hub"));
	notnull(lv->down);
	require(streq(lv->down, "Test 2"));
	lv = lv->next;
	notnull(lv);
	eq(lv->depth, 2);
	require(lv->has_legacy_depth);
	notnull(lv->name);
	require(streq(lv->name, "Test 2"));
	notnull(lv->up);
	require(streq(lv->up, "Test 1"));
	null(lv->down);
	lv = lv->next;
	null(lv);
	notnull(world_site_by_id("core.hub"));
	notnull(world_site_by_id("core.main"));
	require(streq(world_site_by_id("core.hub")->name, "Town"));
	require(streq(world_site_by_id("core.main")->name, "Test"));

	world_parser.cleanup();
	ok;
}

static int test_stable_locations0(void *state)
{
	const char *lines[] = {
		"level:0:Home:None:Cave 1",
		"level:1:Cave 1:Home:Cave 2",
		"level:2:Cave 2:Cave 1:None",
		"site:test.hub:Home Base",
		"site:test.cave:Testing Caverns",
		"site-floor-term:test.cave:Test floor",
		"site-visual:test.hub:village",
		"site-map:test.hub:Green:..+..:.H.H.:..+..:Warm lamps and wet roads.",
		"site-position:test.hub:-2:3",
		"site-position:test.cave:4:7",
		"location:Home:test.hub:test.hub:hub:top_down:0:0:44:99",
		"store-services:test.hub",
		"location:Cave 1:test.cave.001:test.cave:dungeon:top_down:1:12",
		"location:Cave 2:test.cave.002:test.cave:special:spelunking:1:18",
		"entry:test.hub:gate.test.cave:grid:2:3",
		"entry:test.cave.001:surface.default:up_stair",
		"travel-node:test.node.hub.cave:test.hub:gate.test.cave",
		"travel-node:test.node.cave.surface:test.cave.001:surface.default",
		"route:test.route.cave:test.hub:gate.test.cave:test.cave.001:"
			"surface.default:trail:250",
		"route-gate:test.route.cave",
		"route-message:test.route.cave:The old road opens.",
		"route:test.route.retired:test.hub:gate.test.cave:test.cave.001:"
			"surface.default:retired:250"
	};
	struct parser *p = world_parser.init();
	struct level *lv;
	struct world_route *route;
	struct world_travel_node *node;
	const struct world_entry *entry;
	struct world_transition transition;
	struct world_transition_coordinator coordinator = { 0 };
	struct world_map_model map;
	struct player pstate = { 0 };
	int32_t saved_turn = turn;
	int i, rtn;

	notnull(p);
	for (i = 0; i < (int)N_ELEMENTS(lines); ++i) {
		enum parser_error r = parser_parse(p, lines[i]);

		eq(r, PARSE_ERROR_NONE);
	}
	rtn = world_parser.finish(p);
	eq(rtn, 0);
	notnull(world_site_by_id("test.hub"));
	notnull(world_site_by_id("test.cave"));
	require(streq(world_site_by_id("test.hub")->name, "Home Base"));
	require(streq(world_site_by_id("test.cave")->name,
		"Testing Caverns"));
	require(streq(world_site_by_id("test.cave")->floor_term,
		"Test floor"));
	require(streq(world_site_by_id("test.hub")->visual_profile, "village"));
	require(world_site_by_id("test.hub")->has_map_visual);
	require(streq(world_site_by_id("test.hub")->map_stamp[1], ".H.H."));
	require(streq(world_site_by_id("test.hub")->map_description,
		"Warm lamps and wet roads."));
	eq(world_site_by_id("test.hub")->map_attr, COLOUR_GREEN);
	require(world_site_by_id("test.hub")->has_map_position);
	eq(world_site_by_id("test.hub")->map_x, -2);
	eq(world_site_by_id("test.hub")->map_y, 3);
	require(world_site_by_id("test.cave")->has_map_position);
	eq(world_site_by_id("test.cave")->map_x, 4);
	eq(world_site_by_id("test.cave")->map_y, 7);
	lv = level_by_id("test.hub");
	notnull(lv);
	eq(lv->height, 44);
	eq(lv->width, 99);
	require(lv->has_store_services);

	lv = level_by_id("test.cave.002");
	notnull(lv);
	require(!lv->has_store_services);
	require(streq(lv->site_id, "test.cave"));
	require(lv->kind == WORLD_LOCATION_SPECIAL);
	require(lv->mode == WORLD_MODE_SPELUNKING);
	eq(lv->local_floor, 1);
	eq(lv->danger, 18);
	eq(lv->height, 0);
	eq(lv->width, 0);
	null(world_entry_by_id(lv, "surface.default"));

	entry = world_entry_by_id(level_by_id("test.hub"), "gate.test.cave");
	notnull(entry);
	require(entry->kind == WORLD_ENTRY_GRID);
	require(loc_eq(entry->grid, loc(3, 2)));
	entry = world_entry_by_id(level_by_id("test.cave.001"),
		"surface.default");
	notnull(entry);
	require(entry->kind == WORLD_ENTRY_UP_STAIR);

	route = world_route_by_id("test.route.cave");
	notnull(route);
	require(streq(route->from, "test.hub"));
	require(streq(route->from_entry, "gate.test.cave"));
	require(streq(route->to, "test.cave.001"));
	require(streq(route->to_entry, "surface.default"));
	require(streq(route->kind, "trail"));
	require(route->requires_unlock);
	require(streq(route->message, "The old road opens."));
	eq(route->travel_turns, 250);
	require(world_route_is_aggregate(route));
	require(!world_route_is_physical(route));
	route = world_route_by_id("test.route.retired");
	notnull(route);
	require(world_route_is_retired(route));
	require(!world_route_is_aggregate(route));
	node = world_travel_node_by_id("test.node.hub.cave");
	notnull(node);
	require(streq(node->location_id, "test.hub"));
	require(streq(node->entry_id, "gate.test.cave"));
	require(world_travel_node_by_endpoint("test.hub", "gate.test.cave") ==
		node);

	/* Legacy saves only have the old linear depth key. */
	pstate.depth = 2;
	require(world_player_restore_location(&pstate));
	require(streq(pstate.world_location.id, "test.cave.002"));
	require(streq(pstate.world_location.entry, "default"));
	require(world_player_has_discovered_location(&pstate, "test.cave.002"));
	eq(pstate.discovered_locations.count, 1);
	world_map_build(&pstate, &map);
	eq(map.site_count, 1);
	eq(map.route_count, 0);
	eq(map.discovered_location_count, 1);
	require(map.sites[0].current);
	require(streq(map.sites[0].site->id, "test.cave"));
	require(world_player_set_location(&pstate, "test.hub", "gate.test.cave"));
	eq(pstate.depth, 0);
	require(streq(pstate.world_location.entry, "gate.test.cave"));
	eq(pstate.discovered_locations.count, 2);

	/* Physical traversal may discover a route before fast travel unlocks. */
	require(world_transition_prepare_route(&pstate, "test.route.cave", true,
		&transition) == WORLD_TRANSITION_LOCKED);
	require(world_transition_prepare_route(&pstate, "test.route.cave", false,
		&transition) == WORLD_TRANSITION_LOCKED);
	require(world_player_unlock_route(&pstate, "test.route.cave"));
	require(world_transition_prepare_route(&pstate, "test.route.cave", false,
		&transition) == WORLD_TRANSITION_OK);
	require(streq(transition.source_id, "test.hub"));
	require(streq(transition.destination_id, "test.cave.001"));
	require(streq(transition.destination_entry, "surface.default"));
	eq(transition.destination_danger, 12);
	eq(transition.travel_turns, 250);
	require(world_player_unlock_route(&pstate, "test.route.retired"));
	require(world_player_route_is_unlocked(&pstate, "test.route.cave"));
	require(world_transition_prepare_route(&pstate, "test.route.retired", true,
		&transition) == WORLD_TRANSITION_LOCKED);
	/* An unlocked route remains hidden until both endpoint locations have
	 * actually been discovered. */
	world_map_build(&pstate, &map);
	eq(map.site_count, 2);
	eq(map.route_count, 0);
	require(world_player_activate_travel_node(&pstate,
		"test.node.hub.cave"));
	require(world_player_activate_travel_node(&pstate,
		"test.node.cave.surface"));
	require(!world_player_activate_travel_node(&pstate,
		"test.node.missing"));
	require(world_player_has_activated_travel_node(&pstate,
		"test.node.hub.cave"));
	eq(pstate.activated_travel_nodes.count, 2);
	require(world_transition_prepare_route(&pstate, "test.route.cave", true,
		&transition) == WORLD_TRANSITION_OK);
	require(world_player_set_location(&pstate, "test.cave.001",
		"surface.default"));
	require(streq(pstate.world_location.id, "test.cave.001"));
	require(streq(pstate.world_location.entry, "surface.default"));
	eq(pstate.depth, 12);
	eq(pstate.discovered_locations.count, 3);
	world_map_build(&pstate, &map);
	eq(map.site_count, 2);
	eq(map.route_count, 1);
	eq(map.discovered_location_count, 3);
	require(map.routes[0].source_active);
	require(map.routes[0].destination_active);
	require(streq(map.routes[0].source->name, "Home Base"));
	require(streq(map.routes[0].destination->name, "Testing Caverns"));

	/* The directed route cannot be reused from its destination. */
	require(world_transition_prepare_route(&pstate, "test.route.cave", false,
		&transition) == WORLD_TRANSITION_WRONG_SOURCE);

	/* A coordinator keeps the source authoritative until the exact prepared
	 * destination has reported ready. */
	require(world_player_set_location(&pstate, "test.hub", "gate.test.cave"));
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_is_active(&coordinator));
	require(!world_transition_coordinator_is_ready(&coordinator));
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_BUSY);
	require(!world_transition_coordinator_commit(&coordinator, &pstate));
	require(!world_transition_coordinator_destination_ready(&coordinator,
		"test.cave.002", "surface.default", 18));
	require(streq(pstate.world_location.id, "test.hub"));
	require(world_transition_coordinator_destination_ready(&coordinator,
		"test.cave.001", "surface.default", 12));
	require(world_transition_coordinator_is_ready(&coordinator));
	world_transition_coordinator_abort(&coordinator);
	require(!world_transition_coordinator_is_active(&coordinator));
	require(streq(pstate.world_location.id, "test.hub"));

	/* A ready transaction cannot move a player whose source changed. */
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_destination_ready(&coordinator,
		"test.cave.001", "surface.default", 12));
	require(world_player_set_location(&pstate, "test.cave.002", "default"));
	require(!world_transition_coordinator_commit(&coordinator, &pstate));
	require(world_transition_coordinator_is_active(&coordinator));
	require(streq(pstate.world_location.id, "test.cave.002"));
	world_transition_coordinator_abort(&coordinator);
	require(world_player_set_location(&pstate, "test.hub", "gate.test.cave"));

	/* Successful commit is single-use and clears the coordinator. */
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_destination_ready(&coordinator,
		"test.cave.001", "surface.default", 12));
	require(world_transition_coordinator_commit(&coordinator, &pstate));
	require(!world_transition_coordinator_is_active(&coordinator));
	require(!world_transition_coordinator_commit(&coordinator, &pstate));
	require(streq(pstate.world_location.id, "test.cave.001"));

	/* Clock staging is abortable, single-use, overflow-safe, and rejects a
	 * world clock that changed before commit. */
	require(world_player_set_location(&pstate, "test.hub", "gate.test.cave"));
	turn = 1000;
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(coordinator.clock_staged);
	eq(coordinator.departure_turn, 1000);
	eq(coordinator.arrival_turn, 1250);
	require(!world_transition_coordinator_stage_clock(&coordinator));
	world_transition_coordinator_abort(&coordinator);
	eq(turn, 1000);
	require(streq(pstate.world_location.id, "test.hub"));

	turn = INT32_MAX - 100;
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_OK);
	require(!world_transition_coordinator_stage_clock(&coordinator));
	world_transition_coordinator_abort(&coordinator);
	eq(turn, INT32_MAX - 100);

	turn = 1000;
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_destination_ready(&coordinator,
		"test.cave.001", "surface.default", 12));
	turn++;
	require(!world_transition_coordinator_commit(&coordinator, &pstate));
	require(world_transition_coordinator_is_active(&coordinator));
	require(streq(pstate.world_location.id, "test.hub"));
	world_transition_coordinator_abort(&coordinator);

	turn = 1000;
	require(world_transition_coordinator_begin_route(&coordinator, &pstate,
		"test.route.cave", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_destination_ready(&coordinator,
		"test.cave.001", "surface.default", 12));
	require(world_transition_coordinator_commit(&coordinator, &pstate));
	eq(turn, 1250);
	require(streq(pstate.world_location.id, "test.cave.001"));
	turn = saved_turn;

	world_player_clear_ledgers(&pstate);
	world_parser.cleanup();
	ok;
}

static int test_entry_validation0(void *state)
{
	const char *lines[] = {
		"level:0:Home:None:Cave",
		"level:1:Cave:Home:None",
		"location:Home:test.hub:test.hub:hub:top_down:0:0",
		"location:Cave:test.cave:test.cave:dungeon:top_down:1:1",
		"entry:test.hub:gate:grid:2:3"
	};
	struct parser *p = world_parser.init();
	int i;

	notnull(p);
	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "entry:test.hub:gate:grid:4:5"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "entry:test.hub:bad.grid:grid:-1:2"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "entry:test.hub:bad.stair:up_stair:1:2"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "entry:test.missing:gate:grid:1:2"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(world_parser.finish(p), 0);
	world_parser.cleanup();

	/* A route cannot name an entry absent from either endpoint. */
	p = world_parser.init();
	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "route:test.route:test.hub:gate:test.cave:"
		"missing:trail:10"), PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();

	/* A node must name an existing entry, and an endpoint is unique. */
	p = world_parser.init();
	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		if (prefix(lines[i], "travel-node:") || prefix(lines[i], "route:")) {
			continue;
		}
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "travel-node:test.node.one:test.hub:gate"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "travel-node:test.node.two:test.hub:gate"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "travel-node:test.node.missing:test.hub:missing"),
		PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();

	ok;
}

static int test_site_validation0(void *state)
{
	struct parser *p = world_parser.init();

	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site-visual:test.home:home-stone"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "site-visual:test.home:other"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "site-map:test.home:Green:..+..:.H.H.:..+..:Home"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "site-map:test.home:Blue:~~~~~:~~~~~:~~~~~:Other"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "site-position:test.home:-3:5"), PARSE_ERROR_NONE);
	require(world_site_by_id("test.home")->has_map_position);
	eq(world_site_by_id("test.home")->map_x, -3);
	eq(world_site_by_id("test.home")->map_y, 5);
	eq(parser_parse(p, "site-position:test.home:1:2"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "site-position:test.missing:1:2"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "site:test.home:Duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "location:Home:test.home:test.missing:hub:"
		"top_down:0:0"), PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();

	/* Diagram positions must be unique, but sites may omit them so legacy
	 * world files continue to receive a non-spatial list fallback. */
	p = world_parser.init();
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.east:Eastern Approach"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "site-position:test.home:0:0"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site-position:test.east:0:0"),
		PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();
	ok;
}

static int test_dimension_validation0(void *state)
{
	struct parser *p = world_parser.init();

	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:"
		"top_down:0:0:44"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:"
		"top_down:0:0:2:99"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:"
		"top_down:0:0:44:199"), PARSE_ERROR_INVALID_VALUE);
	eq(world_parser.finish(p), 0);
	world_parser.cleanup();

	/* A fixed entry cannot lie beyond an explicitly sized location. */
	p = world_parser.init();
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:"
		"top_down:0:0:10:10"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "entry:test.home:valid:grid:9:9"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "entry:test.home:outside:grid:10:2"),
		PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();
	ok;
}

static int test_place_validation0(void *state)
{
	struct parser *p = world_parser.init();
	struct level *place;
	struct player pstate = { 0 };
	char position[32];

	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.east:Eastern Approach"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.cave:Approach Cave"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:top_down:"
		"0:0:44:99"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "place:Eastern Approach:test.east:test.east:"
		"outdoors:top_down:0:7:44:99"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "place:Approach Cave 1:test.cave.001:test.cave:"
		"dungeon:top_down:1:3:44:99"), PARSE_ERROR_NONE);
	place = level_by_id("test.east");
	notnull(place);
	require(!place->has_legacy_depth);
	require(place->kind == WORLD_LOCATION_OUTDOORS);
	require(place->mode == WORLD_MODE_TOP_DOWN);
	eq(place->local_floor, 0);
	eq(place->danger, 7);
	eq(place->height, 44);
	eq(place->width, 99);
	require(streq(world_level_area_name(place), "Eastern Approach"));
	world_level_format_position(place, position, sizeof(position));
	require(streq(position, "Danger 7"));
	require(!world_level_tracks_dungeon_depth(place));
	place = level_by_id("test.cave.001");
	notnull(place);
	world_level_format_position(place, position, sizeof(position));
	require(streq(position, "L1 Danger 3"));
	null(level_by_depth(7));
	eq(parser_parse(p, "place:Duplicate name:test.east.two:test.east:"
		"outdoors:top_down:0:7:44:99"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "place:Duplicate name:test.east.three:test.east:"
		"outdoors:top_down:0:7:44:99"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "place:Duplicate id:test.east:test.east:"
		"outdoors:top_down:0:7:44:99"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "place:Too short:test.short:test.east:outdoors:"
		"top_down:0:7:2:99"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "place:Unsafe outdoor:test.unsafe:test.east:outdoors:"
		"top_down:0:7:11:99"), PARSE_ERROR_INVALID_VALUE);
	eq(world_parser.finish(p), 0);
	require(world_player_set_location(&pstate, "test.east", "default"));
	pstate.depth = 1; /* Stale danger from an older authored balance pass. */
	require(world_player_restore_location(&pstate));
	eq(pstate.depth, 7);
	world_player_clear_ledgers(&pstate);
	world_parser.cleanup();
	ok;
}

static int test_fishing_habitat_validation0(void *state)
{
	struct parser *p = world_parser.init();
	struct level *home;

	(void)state;
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:top_down:"
		"0:0:44:99"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "fishing-habitat:test.home:rainwater"),
		PARSE_ERROR_NONE);
	home = level_by_id("test.home");
	notnull(home);
	eq(home->fishing_habitat, WORLD_FISHING_HABITAT_RAINWATER);
	eq(parser_parse(p, "fishing-habitat:test.home:open-lake"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "fishing-habitat:test.missing:rainwater"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(world_parser.finish(p), 0);
	world_parser.cleanup();

	p = world_parser.init();
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:top_down:"
		"0:0:44:99"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "fishing-habitat:test.home:lava"),
		PARSE_ERROR_INVALID_VALUE);
	eq(world_parser.finish(p), 0);
	world_parser.cleanup();
	ok;
}

static int test_store_services_validation0(void *state)
{
	struct parser *p = world_parser.init();
	struct level *home;

	(void)state;
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.side:Side Cave"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:top_down:"
		"0:0:44:99"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "place:Side Cave:test.side:test.side:special:"
		"spelunking:1:3:12:20"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "store-services:test.home"), PARSE_ERROR_NONE);
	home = level_by_id("test.home");
	notnull(home);
	require(home->has_store_services);
	eq(parser_parse(p, "store-services:test.home"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "store-services:test.missing"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "store-services:test.side"),
		PARSE_ERROR_INVALID_VALUE);
	require(!level_by_id("test.side")->has_store_services);
	eq(world_parser.finish(p), 0);
	world_parser.cleanup();
	ok;
}

static int test_edge_validation0(void *state)
{
	struct parser *p = world_parser.init();
	const char *lines[] = {
		"level:0:Home:None:None",
		"site:test.home:Home Base",
		"site:test.east:Eastern Approach",
		"location:Home:test.home:test.home:hub:top_down:0:0:44:99",
		"place:Eastern Approach:test.east:test.east:outdoors:top_down:"
			"0:1:44:99",
		"entry:test.home:road.east:grid:22:97",
		"entry:test.east:road.west:grid:22:1",
		"route:test.edge:test.home:road.east:test.east:road.west:edge:1"
	};
	int i;

	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();
	ok;
}

static int test_stair_validation0(void *state)
{
	struct parser *p = world_parser.init();
	const char *lines[] = {
		"level:0:Home:None:None",
		"site:test.home:Home Base",
		"site:test.cave:Test Cave",
		"location:Home:test.home:test.home:hub:top_down:0:0:44:99",
		"place:Test Cave:test.cave.001:test.cave:dungeon:top_down:1:3:"
			"44:99",
		"entry:test.home:cave:down_stair",
		"entry:test.cave.001:exit:down_stair",
		"route:test.stairs:test.home:cave:test.cave.001:exit:stairs:0"
	};
	int i;

	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();
	ok;
}

static int test_trail_validation0(void *state)
{
	const char *lines[] = {
		"level:0:Home:None:None",
		"site:test.home:Home Base",
		"site:test.east:Eastern Camp",
		"location:Home:test.home:test.home:hub:top_down:0:0:44:99",
		"place:Eastern Camp:test.east:test.east:outdoors:top_down:"
			"0:1:44:99",
		"entry:test.home:gate:grid:22:97",
		"entry:test.east:gate:grid:22:1",
		"travel-node:test.node.home:test.home:gate",
		"travel-node:test.node.east:test.east:gate"
	};
	struct parser *p = world_parser.init();
	int i;

	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "route:test.trail:test.home:gate:test.east:"
		"gate:trail:0"), PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();

	/* Aggregate routes require a travel node at both endpoints. */
	p = world_parser.init();
	for (i = 0; i < (int)N_ELEMENTS(lines) - 2; i++) {
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "route:test.trail:test.home:gate:test.east:"
		"gate:trail:10"), PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();

	/* Unknown kinds cannot silently acquire unspecified travel semantics. */
	p = world_parser.init();
	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "route:test.unknown:test.home:gate:test.east:"
		"gate:ferry:10"), PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();

	ok;
}

static int test_shaft_validation0(void *state)
{
	const char *valid_lines[] = {
		"level:0:Home:None:None",
		"site:test.home:Home Base",
		"site:test.shaft:Test Shaft",
		"location:Home:test.home:test.home:hub:top_down:0:0:20:20",
		"place:Test Shaft:test.shaft.001:test.shaft:special:spelunking:"
			"1:3:12:20",
		"entry:test.home:shaft:grid:10:10",
		"entry:test.shaft.001:surface:grid:2:2",
		"route:test.shaft.in:test.home:shaft:test.shaft.001:surface:"
			"shaft:0"
	};
	struct parser *p = world_parser.init();
	struct world_route *route;
	int i;

	(void)state;
	for (i = 0; i < (int)N_ELEMENTS(valid_lines); i++) {
		eq(parser_parse(p, valid_lines[i]), PARSE_ERROR_NONE);
	}
	eq(world_parser.finish(p), 0);
	route = world_route_by_id("test.shaft.in");
	notnull(route);
	require(world_route_is_cross_mode(route));
	require(!world_route_is_physical(route));
	require(!world_route_is_aggregate(route));
	world_parser.cleanup();

	/* A shaft is immediate and must cross exactly one mode boundary. */
	p = world_parser.init();
	for (i = 0; i < (int)N_ELEMENTS(valid_lines) - 1; i++) {
		eq(parser_parse(p, valid_lines[i]), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "route:test.shaft.slow:test.home:shaft:"
		"test.shaft.001:surface:shaft:1"), PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();

	p = world_parser.init();
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home Base"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.other:Other Cave"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:top_down:"
		"0:0:20:20"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "place:Other Cave:test.other:test.other:special:"
		"top_down:1:3:12:20"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "entry:test.home:shaft:grid:10:10"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "entry:test.other:surface:grid:2:2"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "route:test.shaft.same:test.home:shaft:test.other:"
		"surface:shaft:0"), PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();
	ok;
}

static int test_overworld0(void *state)
{
	static const char *const biomes[] = {
		"village", "cavewood", "lake", "pine-wood", "marsh",
		"heath", "meadow", "ruins", "scrub"
	};
	struct parser *p = world_parser.init();
	struct player pstate = { 0 };
	const struct level *lev;
	const struct world_route *route;
	const struct world_entry *entry;
	int physical = 0, aggregate = 0, runtime = 0;
	int map_x, map_y, cell_x, cell_y;
	int i;
	(void)state;

	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	for (i = 0; i < 9; i++) {
		char line[160];

		strnfmt(line, sizeof(line), "site:test.cell.%d:Test Cell %d", i, i);
		eq(parser_parse(p, line), PARSE_ERROR_NONE);
		if (i == 0) {
			eq(parser_parse(p, "location:Home:test.cell.0:test.cell.0:"
				"hub:top_down:0:0:44:99"), PARSE_ERROR_NONE);
		} else {
			strnfmt(line, sizeof(line), "place:Cell %d:test.cell.%d:"
				"test.cell.%d:outdoors:top_down:0:%d:44:99",
				i, i, i, i);
			eq(parser_parse(p, line), PARSE_ERROR_NONE);
		}
	}
	eq(parser_parse(p, "site:test.child:Child Site"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site-anchor:test.child:test.cell.1:1:1"),
		PARSE_ERROR_NONE);
	for (i = 0; i < 9; i++) {
		char line[120];

		strnfmt(line, sizeof(line),
			"overworld-cell:%d:test.cell.%d:%s:5:%d",
			i, i, biomes[i], i);
		eq(parser_parse(p, line), PARSE_ERROR_NONE);
	}
	lev = level_by_id("test.cell.0");
	notnull(lev);
	entry = world_entry_by_id(lev, "edge.east");
	notnull(entry);
	require(loc_eq(entry->grid, loc(96, 22)));
	eq(lev->overworld_gate_width, 5);
	eq(lev->overworld_population, 0);
	eq(level_by_id("test.cell.1")->overworld_population, 1);
	entry = world_entry_by_id(lev, "travel.stop");
	notnull(entry);
	require(loc_eq(entry->grid, loc(49, 22)));
	eq(world_parser.finish(p), 0);
	require(world_overworld_layout_set(&pstate, WORLD_OVERWORLD_VERSION,
		3, 3, 9, 123456));
	require(world_overworld_rebuild_routes(&pstate));
	for (route = world_routes; route; route = route->next) {
		if (route->runtime_layout) runtime++;
		if (world_route_is_physical(route)) physical++;
		if (world_route_is_aggregate(route)) aggregate++;
	}
	eq(runtime, 96);
	eq(physical, 24);
	eq(aggregate, 72);
	require(world_overworld_position(&pstate, "test.cell.1",
		&cell_x, &cell_y));
	require(world_overworld_site_position(&pstate,
		world_site_by_id("test.child"), &map_x, &map_y));
	eq(map_x, cell_x * 4 + 1);
	eq(map_y, cell_y * 4 + 1);
	world_parser.cleanup();

	/* Partial launch layouts fail at data load rather than at character birth. */
	p = world_parser.init();
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "site:test.home:Home"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:top_down:"
		"0:0:44:99"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "overworld-cell:0:test.home:village"),
		PARSE_ERROR_NONE);
	eq(world_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	world_parser.cleanup();
	ok;
}

static int test_recall_validation(void *state)
{
	const char *lines[] = {
		"level:0:Home:None:None", "site:test.home:Home", "site:test.cave:Cave",
		"location:Home:test.home:test.home:hub:top_down:0:0:20:20",
		"place:Cave:test.cave:test.cave:special:spelunking:1:3:12:20",
		"entry:test.home:arrival:down_stair",
		"entry:test.cave:surface:grid:2:2"
	};
	const char *routes[] = {
		"route:test.recall:test.cave:surface:test.home:arrival:recall:0",
		"route:test.recall:test.cave:surface:test.home:arrival:recall:1",
		"route:test.recall:test.home:arrival:test.cave:surface:recall:0"
	};
	int i, j;
	(void)state;
	for (j = 0; j < (int)N_ELEMENTS(routes); j++) {
		struct parser *p = world_parser.init();
		for (i = 0; i < (int)N_ELEMENTS(lines); i++)
			eq(parser_parse(p, lines[i]), PARSE_ERROR_NONE);
		eq(parser_parse(p, routes[j]), PARSE_ERROR_NONE);
		eq(world_parser.finish(p), j ? PARSE_ERROR_INVALID_VALUE : PARSE_ERROR_NONE);
		if (!j) {
			const struct world_route *route = world_route_by_id("test.recall");
			require(world_route_is_recall(route));
			require(!world_route_is_cross_mode(route));
			require(!world_route_is_physical(route));
			require(!world_route_is_aggregate(route));
		}
		world_parser.cleanup();
	}
	ok;
}

static int test_entry_rock_validation(void *state)
{
	struct parser *p = world_parser.init();
	const struct world_entry *entry;
	(void)state;
	eq(parser_parse(p, "entry-rock:test.home:stair:-1:0:MERIDIAN_STONE"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "level:0:Home:None:None"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "location:Home:test.home:test.home:hub:top_down:0:0"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "entry-rock:test.home:stair:-1:0:MERIDIAN_STONE"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "entry:test.home:stair:down_stair"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "entry-rock:test.home:stair:0:0:MERIDIAN_STONE"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "entry-rock:test.home:stair:5:0:MERIDIAN_STONE"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "entry-rock:test.home:stair:-1:0:NO_SUCH_ROCK"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "entry-rock:test.home:stair:-1:0:MERIDIAN_STONE"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "entry-rock:test.home:stair:-1:0:GRANITE"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	entry = world_entry_by_id(level_by_id("test.home"), "stair");
	notnull(entry);
	notnull(entry->rocks);
	eq(entry->rocks->feat, FEAT_MERIDIAN_STONE);
	require(loc_eq(entry->rocks->offset, loc(0, -1)));
	eq(world_parser.finish(p), PARSE_ERROR_NONE);
	world_parser.cleanup();
	ok;
}

static int test_stable_id_validation0(void *state)
{
	require(world_id_is_valid("core.main.001"));
	require(world_id_is_valid("gate.cave-a_1"));
	require(!world_id_is_valid("Core.Main"));
	require(!world_id_is_valid("core..main"));
	require(!world_id_is_valid(".core.main"));
	require(!world_id_is_valid("core main"));
	ok;
}

const char *suite_name = "parse/world";
struct test tests[] = {
	{ "complete0", test_complete0 },
	{ "stable locations", test_stable_locations0 },
	{ "stable ID validation", test_stable_id_validation0 },
	{ "entry validation", test_entry_validation0 },
	{ "entry rock validation", test_entry_rock_validation },
	{ "site validation", test_site_validation0 },
	{ "dimension validation", test_dimension_validation0 },
	{ "place validation", test_place_validation0 },
	{ "fishing habitat validation", test_fishing_habitat_validation0 },
	{ "store services validation", test_store_services_validation0 },
	{ "edge validation", test_edge_validation0 },
	{ "stair validation", test_stair_validation0 },
	{ "trail validation", test_trail_validation0 },
	{ "shaft validation", test_shaft_validation0 },
	{ "recall validation", test_recall_validation },
	{ "overworld", test_overworld0 },
	{ NULL, NULL }
};
