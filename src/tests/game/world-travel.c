/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* game/world-travel.c */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include <stdio.h>
#include "cave.h"
#include "cmd-core.h"
#include "cmd-larder.h"
#include "cmd-spelunking.h"
#include "effects.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-timed.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "savefile.h"
#include "trap.h"
#include "player.h"
#include "player-birth.h"
#include "player-timed.h"
#include "player-util.h"
#include "ui-travel.h"
#include "world-entry.h"
#include "world-map.h"
#include "world-overworld.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-layout.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-visibility.h"
#include "world-travel-action.h"
#include "world-travel-resources.h"
#include "world-turn.h"
#include "world-transition.h"
#include "z-rand.h"
#include "z-util.h"
#include "game-fixture.h"

static int choose_direction(struct chunk *c, struct player *p) {
	int dir = 0;

	while (1) {
		struct loc grid;

		if (dir >= 9) {
			return -1;
		}
		grid.x = p->grid.x + ddx_ddd[dir];
		grid.y = p->grid.y + ddy_ddd[dir];
		if (square_isempty(c, grid)) {
			return ddd[dir];
		}
		++dir;
	}
}
static int test_travel_resources(void *state) {
	struct world_travel_resources resources = { 0 };
	struct object *light;
	int saved_depth, saved_food, saved_light, saved_blind;
	uint32_t saved_update, saved_redraw;
	int full_rate, normal_rate;

	reset_before_load();
	eq(savefile_load("Test1", false), true);
	require(character_dungeon);
	on_new_level();
	light = equipped_item_by_slot_name(player, "light");
	notnull(light);
	require(light->timeout > 20);

	saved_depth = player->depth;
	saved_food = player->timed[TMD_FOOD];
	saved_light = light->timeout;
	saved_blind = player->timed[TMD_BLIND];
	saved_update = player->upkeep->update;
	saved_redraw = player->upkeep->redraw;
	full_rate = 5000 / z_info->food_value;
	normal_rate = (turn_energy(player->state.speed) * 100) /
		z_info->food_value;
	if (player_of_has(player, OF_REGEN)) normal_rate *= 2;
	if (player_of_has(player, OF_SLOW_DIGEST)) normal_rate /= 2;
	if (normal_rate < 1) normal_rate = 1;

	/* Projection is non-mutating.  It accounts for one Full tick, then two
	 * normal hundred-turn boundaries, plus all thirteen light ticks. */
	player->depth = 1;
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL + 25);
	light->timeout = 100;
	require(world_travel_resources_stage(&resources, player, 90, 220) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(resources.staged);
	eq(resources.food_before, PY_FOOD_FULL + 25);
	eq(resources.food_after, PY_FOOD_FULL + 25 - full_rate -
		2 * normal_rate);
	eq(resources.light_before, 100);
	eq(resources.light_after, 87);
	eq(player->timed[TMD_FOOD], PY_FOOD_FULL + 25);
	eq(light->timeout, 100);
	require(world_travel_resources_match(&resources, player));
	of_on(light->flags, OF_NO_FUEL);
	require(!world_travel_resources_match(&resources, player));
	of_off(light->flags, OF_NO_FUEL);
	require(world_travel_resources_match(&resources, player));
	require(world_travel_resources_stage(&resources, player, 90, 220) ==
		WORLD_TRAVEL_RESOURCES_ALREADY_STAGED);
	player->timed[TMD_FOOD]++;
	require(!world_travel_resources_match(&resources, player));
	player->timed[TMD_FOOD]--;
	world_travel_resources_commit(&resources, player);
	eq(player->timed[TMD_FOOD], resources.food_after);
	eq(light->timeout, resources.light_after);

	/* Town light burns only for the nightly portion of the route. */
	memset(&resources, 0, sizeof(resources));
	player->depth = 0;
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL - 1);
	light->timeout = 100;
	require(world_travel_resources_stage(&resources, player, 49990, 50120) ==
		WORLD_TRAVEL_RESOURCES_OK);
	eq(resources.light_after, 88);

	/* The route boundary refuses random fainting or extinguishing a live
	 * light.  An already exhausted light is not a reason to block travel. */
	memset(&resources, 0, sizeof(resources));
	player->depth = 1;
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FAINT + 1);
	light->timeout = 100;
	require(world_travel_resources_stage(&resources, player, 0, 100) ==
		WORLD_TRAVEL_RESOURCES_UNSAFE_HUNGER);
	memset(&resources, 0, sizeof(resources));
	player->timed[TMD_FOOD] = (int16_t)PY_FOOD_FAINT;
	require(world_travel_resources_stage(&resources, player, 1, 20) ==
		WORLD_TRAVEL_RESOURCES_UNSAFE_HUNGER);
	memset(&resources, 0, sizeof(resources));
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL - 1);
	light->timeout = 1;
	require(world_travel_resources_stage(&resources, player, 1, 20) ==
		WORLD_TRAVEL_RESOURCES_UNSAFE_LIGHT);
	memset(&resources, 0, sizeof(resources));
	light->timeout = 0;
	require(world_travel_resources_stage(&resources, player, 1, 20) ==
		WORLD_TRAVEL_RESOURCES_OK);
	eq(resources.light_after, 0);
	memset(&resources, 0, sizeof(resources));
	player->timed[TMD_FOOD] = (int16_t)PY_FOOD_FAINT;
	light->timeout = 1;
	require(world_travel_resources_stage(&resources, player, 10, 10) ==
		WORLD_TRAVEL_RESOURCES_OK);
	eq(resources.food_after, PY_FOOD_FAINT);
	eq(resources.light_after, 1);

	/* A fuel-free light never consumes timeout. */
	memset(&resources, 0, sizeof(resources));
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL - 1);
	light->timeout = 100;
	require(!of_has(light->flags, OF_NO_FUEL));
	of_on(light->flags, OF_NO_FUEL);
	require(world_travel_resources_stage(&resources, player, 1, 100) ==
		WORLD_TRAVEL_RESOURCES_OK);
	eq(resources.light_after, 100);
	of_off(light->flags, OF_NO_FUEL);

	/* Blindness preserves the final unit of fuel, matching ordinary play. */
	memset(&resources, 0, sizeof(resources));
	light->timeout = 1;
	player->timed[TMD_BLIND] = 1;
	require(world_travel_resources_stage(&resources, player, 1, 20) ==
		WORLD_TRAVEL_RESOURCES_OK);
	eq(resources.light_after, 1);

	player->depth = (int16_t)saved_depth;
	player->timed[TMD_FOOD] = (int16_t)saved_food;
	player->timed[TMD_BLIND] = (int16_t)saved_blind;
	light->timeout = (int16_t)saved_light;
	player->upkeep->update = saved_update;
	player->upkeep->redraw = saved_redraw;
	ok;
}

static const struct world_route *test_route_between(const char *from,
		const char *to, bool aggregate)
{
	const struct world_route *route;

	for (route = world_routes; route; route = route->next) {
		if (world_route_is_retired(route) || !streq(route->from, from) ||
				!streq(route->to, to)) {
			continue;
		}
		if (aggregate ? world_route_is_aggregate(route) :
				world_route_is_physical(route)) {
			return route;
		}
	}
	return NULL;
}

static bool test_place_on_entry(struct chunk *actual, const char *entry_id)
{
	struct loc grid;
	struct monster *mon;

	if (world_entry_locate(actual, entry_id, &grid) != WORLD_ENTRY_OK ||
			!square_ispassable(actual, grid)) {
		return false;
	}
	mon = square(actual, grid)->mon > 0 ? square_monster(actual, grid) : NULL;
	if (mon) delete_monster_idx(actual, mon->midx);
	square_set_mon(actual, player->grid, 0);
	player_place(actual, player, grid);
	return loc_eq(player->grid, grid);
}

static bool test_surface_step(const struct level *destination)
{
	struct world_travel_action_report report;
	const struct world_route *route;
	const char *source_id = player->world_location.id;
	struct loc arrival;
	struct loc trigger;

	if (!destination) return false;
	route = test_route_between(source_id, destination->id, false);
	if (!route || !test_place_on_entry(cave, route->from_entry)) return false;
	if (world_entry_locate(cave, route->from_entry, &arrival) !=
			WORLD_ENTRY_OK || world_entry_is_edge_exit_grid(cave, arrival)) {
		return false;
	}
	trigger = arrival;
	if (arrival.x == 2) trigger.x--;
	else if (arrival.x == cave->width - 3) trigger.x++;
	else if (arrival.y == 2) trigger.y--;
	else if (arrival.y == cave->height - 3) trigger.y++;
	else return false;
	if (!world_entry_is_edge_exit_grid(cave, trigger) ||
			world_entry_edge_route_for_step(cave, arrival, trigger) != route) {
		return false;
	}
	if (world_route_execute_physical(player, cave, route->id, &report) !=
			WORLD_TRAVEL_ACTION_OK) {
		return false;
	}
	return streq(player->world_location.id, destination->id) &&
		world_entry_contains(cave, route->to_entry, player->grid) &&
		world_player_route_is_unlocked(player, route->id);
}

static bool test_walk_surface_to(const struct level *destination)
{
	int x, y, target_x, target_y;

	if (!destination || !world_overworld_position(player, destination->id,
			&target_x, &target_y)) {
		return false;
	}
	while (!streq(player->world_location.id, destination->id)) {
		const struct level *next;

		if (!world_overworld_position(player, player->world_location.id,
				&x, &y)) {
			return false;
		}
		if (x != target_x) x += x < target_x ? 1 : -1;
		else if (y != target_y) y += y < target_y ? 1 : -1;
		next = world_overworld_location_at(player, x, y);
		if (!test_surface_step(next)) return false;
	}
	return true;
}

static void test_hide_monsters(struct chunk *actual)
{
	int i;

	for (i = 1; i < cave_monster_max(actual); i++) {
		struct monster *mon = cave_monster(actual, i);

		if (!mon || !mon->race) continue;
		mflag_off(mon->mflag, MFLAG_VISIBLE);
		mflag_off(mon->mflag, MFLAG_VIEW);
	}
}

static int test_horizontal_world_loop(void *state)
{
	struct world_travel_action_report report;
	struct world_map_model map;
	struct player legacy = { 0 };
	const struct level *hub;
	const struct level *cavewood;
	const struct level *current;
	const struct world_route *route;
	const struct world_entry *shaft_entry;
	struct chunk *cavewood_actual;
	struct chunk *cavewood_known;
	struct chunk *satellite_actual;
	struct chunk *satellite_known;
	struct chunk *stored_ruins;
	struct loc cave_mouth;
	struct loc cave_scar;
	struct loc trap_probe;
	struct loc ruins_scar = loc(0, 0);
	struct loc scan;
	char ruins_id[WORLD_ID_LEN] = "";
	bool seen_slot[WORLD_OVERWORLD_LAUNCH_CELLS] = { false };
	bool seen_biome[WORLD_BIOME_SCRUB + 1] = { false };
	uint32_t layout_seed;
	uint16_t stable_chunk_count;
	uint16_t stable_discovery_count;
	uint16_t stable_route_count;
	uint16_t stable_node_count;
	int saved_x[WORLD_OVERWORLD_LAUNCH_CELLS];
	int saved_y[WORLD_OVERWORLD_LAUNCH_CELLS];
	int current_x, current_y, hub_x, hub_y;
	int32_t departure_turn;
	int i, x, y, circuit;

	/* Old zero-time route IDs remain sufficient evidence for migrating exact
	 * endpoint activation, but every old line-shaped journey is inert. */
	require(world_player_unlock_route(&legacy,
		"core.route.approach.east.out"));
	require(world_player_unlock_route(&legacy, "core.route.cave.east.in"));
	eq(world_player_backfill_travel_nodes(&legacy), 4);
	require(world_player_has_activated_travel_node(&legacy,
		"core.node.hub.east"));
	require(world_player_has_activated_travel_node(&legacy,
		"core.node.cave.east.out"));
	eq(world_player_unlock_activated_travel_routes(&legacy), 0);
	require(world_route_is_retired(world_route_by_id(
		"core.route.travel.cave.east")));
	world_player_clear_ledgers(&legacy);

	reset_before_load();
	require(savefile_load("Test1", false));
	require(character_dungeon);
	on_new_level();
	require(world_overworld_layout_valid(player));
	require(player->overworld_layout.seed != 0);
	eq(player->overworld_layout.width, 3);
	eq(player->overworld_layout.height, 3);
	eq(player->overworld_layout.count, 9);
	layout_seed = player->overworld_layout.seed;
	hub = level_by_id("core.hub");
	cavewood = level_by_id("core.approach.east");
	notnull(hub);
	notnull(cavewood);
	require(world_overworld_position(player, hub->id, &hub_x, &hub_y));

	/* Stable roles are shuffled into nine unique, reversible coordinates.
	 * The save stores only the compact layout description, never nine chunks
	 * of permanently simulated background world. */
	for (y = 0; y < 3; y++) {
		for (x = 0; x < 3; x++) {
			const struct level *lev = world_overworld_location_at(player, x, y);
			int reverse_x, reverse_y;

			notnull(lev);
			require(lev->is_overworld_cell);
			require(lev->overworld_slot < 9);
			require(!seen_slot[lev->overworld_slot]);
			seen_slot[lev->overworld_slot] = true;
			require(lev->biome >= WORLD_BIOME_VILLAGE &&
				lev->biome <= WORLD_BIOME_SCRUB);
			require(!seen_biome[lev->biome]);
			seen_biome[lev->biome] = true;
			require(world_overworld_position(player, lev->id,
				&reverse_x, &reverse_y));
			eq(reverse_x, x);
			eq(reverse_y, y);
			saved_x[lev->overworld_slot] = x;
			saved_y[lev->overworld_slot] = y;
		}
	}

	/* Walk real cardinal routes until every cell has generated.  Each biome
	 * must retain its semantic terrain identity underneath ASCII rendering. */
	for (y = 0; y < 3; y++) {
		for (x = 0; x < 3; x++) {
			const struct level *lev = world_overworld_location_at(player, x, y);
			int water = 0, wood = 0, rubble = 0;

			require(test_walk_surface_to(lev));
			require(streq(cave->location_id, lev->id));
			eq(cave->height, 66);
			if (lev->kind == WORLD_LOCATION_OUTDOORS) eq(cave->width, 132);
			for (scan.y = 1; scan.y < cave->height - 1; scan.y++) {
				for (scan.x = 1; scan.x < cave->width - 1; scan.x++) {
					if (square_iswater(cave, scan)) water++;
					if (square_iswood(cave, scan)) wood++;
					if (square_isrubble(cave, scan)) rubble++;
				}
			}
			if (lev->biome == WORLD_BIOME_LAKE) require(water > 500);
			if (lev->biome == WORLD_BIOME_MARSH) require(water > 100);
			if (lev->biome == WORLD_BIOME_CAVEWOOD ||
					lev->biome == WORLD_BIOME_PINE_WOOD ||
					lev->biome == WORLD_BIOME_SCRUB) {
				require(wood > 20);
			}
			if (lev->biome == WORLD_BIOME_RUINS) {
				require(rubble > 0);
				require(find_empty(cave, &ruins_scar));
				square_set_feat(cave, ruins_scar, FEAT_RUBBLE);
				my_strcpy(ruins_id, lev->id, sizeof(ruins_id));
			}
			require(world_player_has_activated_travel_node(player,
				format("core.node.overworld.%02u", lev->overworld_slot)));
		}
	}
	require(ruins_id[0]);
	eq(player->discovered_locations.count, 9);
	world_map_build(player, &map);
	eq(map.site_count, 9);
	require(map.route_count >= 8);
	for (i = 0; i < (int)map.site_count; i++) {
		require(map.sites[i].has_map_position);
	}

	/* Finish outside the village, then prove direct travel uses Manhattan
	 * distance rather than the old one-link special case. */
	current = world_player_level(player);
	if (current == hub) {
		current = world_overworld_neighbor(player, hub->id,
			hub_x < 2 ? 1 : -1, 0);
		require(test_surface_step(current));
	}
	current = world_player_level(player);
	require(world_overworld_position(player, current->id,
		&current_x, &current_y));
	route = test_route_between(current->id, hub->id, true);
	notnull(route);
	require(world_player_route_is_unlocked(player, route->id));
	eq(route->travel_turns,
		(unsigned)(ABS(current_x - hub_x) + ABS(current_y - hub_y)) * 375);
	for (i = 0; i < TMD_MAX; i++) {
		if (i != TMD_FOOD) player->timed[i] = 0;
	}
	test_hide_monsters(cave);
	departure_turn = turn;
	require(world_travel_execute(player, cave, route->id, &report) ==
		WORLD_TRAVEL_ACTION_OK);
	require(streq(player->world_location.id, hub->id));
	eq(turn, departure_turn + (int32_t)route->travel_turns);
	shaft_entry = world_entry_by_id(hub, "shaft.practice");
	notnull(shaft_entry);
	require(shaft_entry->kind == WORLD_ENTRY_GRID);
	require(square(cave, shaft_entry->grid)->feat != FEAT_HOLE);
	null(world_entry_cross_mode_route_here(cave, shaft_entry->grid));
	require(world_route_is_retired(world_route_by_id(
		"core.route.spelunk.practice.in")));
	/* Simulate a saved actual/remembered development entrance. */
	square_set_feat(cave, shaft_entry->grid, FEAT_HOLE);
	square_set_feat(player->cave, shaft_entry->grid, FEAT_HOLE);
	on_new_level();
	eq(square(cave, shaft_entry->grid)->feat, FEAT_FLOOR);
	eq(square(player->cave, shaft_entry->grid)->feat, FEAT_FLOOR);

	/* The cave follows the shuffled Cavewood role, retains its own chunk, and
	 * never exposes surface fast travel while underground. */
	require(test_walk_surface_to(cavewood));
	cavewood_actual = cave;
	cavewood_known = player->cave;
	require(world_entry_locate(cave, "cave.east", &cave_mouth) ==
		WORLD_ENTRY_OK);
	square_memorize(cave, cave_mouth);
	require(streq(square_apparent_name(player->cave, cave_mouth), "Cavewood Cavern"));
	require(streq(square_apparent_look_prefix(player->cave, cave_mouth), "an entrance to "));
	square_forget(cave, cave_mouth);
	null(world_entry_known_destination(player->cave, cave_mouth));
	require(test_place_on_entry(cave, "cave.east"));
	require(world_route_execute_physical(player, cave,
		"core.route.cave.east.in", &report) == WORLD_TRAVEL_ACTION_OK);
	require(streq(player->world_location.id, "core.cave.east.001"));
	require(!textui_travel_uses_up_key_here(player, cave));
	shaft_entry = world_entry_by_id(world_player_level(player),
		"shaft.spelunk");
	notnull(shaft_entry);
	require(shaft_entry->kind == WORLD_ENTRY_GRID);
	eq(square(cave, shaft_entry->grid)->feat, FEAT_HOLE);
	require(square_isfloor(cave, shaft_entry->grid));
	square_memorize(cave, shaft_entry->grid);
	require(streq(square_apparent_name(player->cave, shaft_entry->grid), "The Deepwell"));
	require(streq(square_apparent_look_prefix(player->cave, shaft_entry->grid), "a shaft into "));
	require(world_entry_cross_mode_route_here(cave, shaft_entry->grid) ==
		world_route_by_id("core.route.spelunk.east.in"));
	/* A satellite cave has no implicit stack of deeper dungeon levels.
	 * Ordinary local traps remain legal, but the legacy trap door must not be
	 * generated or explicitly placed here. */
	for (scan.y = 1; scan.y < cave->height - 1; scan.y++) {
		for (scan.x = 1; scan.x < cave->width - 1; scan.x++) {
			struct trap *trap;
			require(!square_isdownstairs(cave, scan));

			for (trap = square_trap(cave, scan); trap; trap = trap->next) {
				require(!trf_has(trap->kind->flags, TRF_DOWN));
			}
		}
	}
	require(find_empty(cave, &trap_probe));
	require(square_player_trap_allowed(cave, trap_probe));
	place_trap(cave, trap_probe, lookup_trap("trap door")->tidx,
		cave->depth);
	null(square_trap(cave, trap_probe));
	satellite_actual = cave;
	satellite_known = player->cave;
	require(find_empty(cave, &cave_scar));
	square_set_feat(cave, cave_scar, FEAT_RUBBLE);
	/* Learn both directed cave-mouth routes before measuring the repeated
	 * circuit's stable ledger and chunk high-water marks. */
	require(test_place_on_entry(cave, "stair.out"));
	require(world_route_execute_physical(player, cave,
		"core.route.cave.east.out", &report) == WORLD_TRAVEL_ACTION_OK);
	require(test_place_on_entry(cave, "cave.east"));
	require(world_route_execute_physical(player, cave,
		"core.route.cave.east.in", &report) == WORLD_TRAVEL_ACTION_OK);
	stable_chunk_count = chunk_list_max;
	stable_discovery_count = player->discovered_locations.count;
	stable_route_count = player->unlocked_routes.count;
	stable_node_count = player->activated_travel_nodes.count;
	for (circuit = 0; circuit < 8; circuit++) {
		require(test_place_on_entry(cave, "stair.out"));
		require(world_route_execute_physical(player, cave,
			"core.route.cave.east.out", &report) == WORLD_TRAVEL_ACTION_OK);
		require(cave == cavewood_actual);
		require(player->cave == cavewood_known);
		require(test_place_on_entry(cave, "cave.east"));
		require(world_route_execute_physical(player, cave,
			"core.route.cave.east.in", &report) == WORLD_TRAVEL_ACTION_OK);
		require(cave == satellite_actual);
		require(player->cave == satellite_known);
		require(square_isrubble(cave, cave_scar));
		eq(chunk_list_max, stable_chunk_count);
		eq(player->discovered_locations.count, stable_discovery_count);
		eq(player->unlocked_routes.count, stable_route_count);
		eq(player->activated_travel_nodes.count, stable_node_count);
	}
	require(savefile_save("TestWorldCircuit"));

	/* Reloading preserves geography and changed terrain in both the active
	 * cave and an inactive surface cell. */
	reset_before_load();
	require(savefile_load("TestWorldCircuit", false));
	require(character_dungeon);
	on_new_level();
	require(world_overworld_layout_valid(player));
	eq(player->overworld_layout.seed, layout_seed);
	for (i = 0; i < 9; i++) {
		const struct level *lev = NULL;
		const struct level *candidate;
		int loaded_x, loaded_y;

		for (candidate = world; candidate; candidate = candidate->next) {
			if (candidate->is_overworld_cell &&
					candidate->overworld_slot == i) {
				lev = candidate;
				break;
			}
		}
		notnull(lev);
		require(world_overworld_position(player, lev->id,
			&loaded_x, &loaded_y));
		eq(loaded_x, saved_x[i]);
		eq(loaded_y, saved_y[i]);
	}
	require(streq(player->world_location.id, "core.cave.east.001"));
	require(square_isrubble(cave, cave_scar));
	stored_ruins = chunk_find_location(ruins_id, false);
	notnull(stored_ruins);
	require(square_isrubble(stored_ruins, ruins_scar));
	eq(chunk_list_max, stable_chunk_count);
	eq(player->discovered_locations.count, stable_discovery_count);
	eq(player->unlocked_routes.count, stable_route_count);
	eq(player->activated_travel_nodes.count, stable_node_count);

	ok;
}

static int global_monster_population(void) {
	int count = 0;
	int i;

	for (i = 0; i < z_info->r_max; i++) count += r_info[i].cur_num;
	return count;
}

static int created_artifact_count(void) {
	int count = 0;
	int i;

	for (i = 0; i < z_info->a_max; i++) {
		if (is_artifact_created(&a_info[i])) count++;
	}
	return count;
}

struct destination_lifecycle_test {
	struct chunk *source_actual;
	struct chunk *source_known;
	struct chunk *destination_actual;
	struct chunk *destination_known;
	struct loc source_grid;
	struct loc arrival;
	const struct world_destination_lifecycle *delegate;
	int step;
	bool valid;
};

static bool lifecycle_transition_is_expected(
		const struct world_transition *transition)
{
	return transition && streq(transition->route_id,
		"core.route.main.down") && streq(transition->source_id, "core.hub") &&
		streq(transition->destination_id, "core.main.001") &&
		streq(transition->destination_entry, "stair.up") &&
		transition->destination_danger == 1;
}

static void test_lifecycle_leave(
		const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		void *user)
{
	struct destination_lifecycle_test *test = user;

	test->valid = test->valid && test->step == 0 &&
		lifecycle_transition_is_expected(transition) &&
		actual == test->source_actual && known == test->source_known &&
		cave == test->source_actual && p->cave == test->source_known &&
		streq(p->world_location.id, "core.main.001") && p->depth == 1 &&
		loc_eq(p->grid, test->source_grid) &&
		square(actual, test->source_grid)->mon == -1;
	test->step = 1;
	if (test->delegate && test->delegate->leave_source) {
		test->delegate->leave_source(transition, actual, known, p,
			test->delegate->leave_user);
	}
}

static void test_lifecycle_enter(
		const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		void *user)
{
	struct destination_lifecycle_test *test = user;

	test->valid = test->valid && test->step == 1 &&
		lifecycle_transition_is_expected(transition) &&
		actual == test->destination_actual &&
		known == test->destination_known && cave == actual && p->cave == known &&
		loc_eq(p->grid, test->arrival) && square(actual, p->grid)->mon == -1 &&
		square(test->source_actual, test->source_grid)->mon == 0;
	test->step = 2;
	if (test->delegate && test->delegate->enter_destination) {
		test->delegate->enter_destination(transition, actual, known, p,
			test->delegate->enter_user);
	}
}

static int test_stored_destination_candidate(void *state) {
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct chunk *actual;
	struct chunk *known;
	struct chunk *source_actual;
	struct chunk *source_known;
	struct monster *restored_monster;
	struct loc arrival = loc(3, 2);
	struct loc trail = loc(1, 1);
	struct loc monster_grid = loc(2, 2);
	struct loc source_grid;
	int32_t inactive_turn;
	uint16_t initial_count;

	reset_before_load();
	eq(savefile_load("Test1", false), true);
	initial_count = chunk_list_max;

	actual = cave_new(5, 7);
	actual->depth = 1;
	require(world_chunk_set_location(actual, "core.main.001", false));
	known = cave_new(5, 7);
	known->depth = 1;
	require(world_chunk_set_location(known, "core.main.001", true));
	square_set_feat(actual, arrival, FEAT_LESS);
	square_set_feat(known, arrival, FEAT_LESS);

	/* An incomplete pair cannot be published or consume the transaction. */
	chunk_list_add(actual);
	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_prepare_stored(&candidate, &coordinator) ==
		WORLD_DESTINATION_NOT_FOUND);
	require(!world_destination_candidate_is_ready(&candidate));
	require(!world_transition_coordinator_is_ready(&coordinator));
	eq(chunk_list_max, initial_count + 1);

	/* A complete, matching pair becomes ready without leaving stored state. */
	chunk_list_add(known);
	require(world_destination_prepare_stored(&candidate, &coordinator) ==
		WORLD_DESTINATION_OK);
	require(world_destination_candidate_is_ready(&candidate));
	require(world_transition_coordinator_is_ready(&coordinator));
	require(candidate.actual == actual);
	require(candidate.known == known);
	eq(chunk_list_max, initial_count + 2);
	require(streq(player->world_location.id, "core.hub"));

	/* Abort forgets only runtime references; both stored chunks remain. */
	actual->noise.grids[trail.y][trail.x] = 7;
	actual->scent.grids[trail.y][trail.x] = 2;
	require(world_destination_candidate_abort(&candidate, &coordinator,
		player));
	require(!world_destination_candidate_is_ready(&candidate));
	require(!world_transition_coordinator_is_active(&coordinator));
	require(chunk_find_location("core.main.001", false) == actual);
	require(chunk_find_location("core.main.001", true) == known);
	eq(chunk_list_max, initial_count + 2);
	eq(actual->noise.grids[trail.y][trail.x], 7);
	eq(actual->scent.grids[trail.y][trail.x], 2);

	/* Stable IDs, not the optional display names, survive a disk round trip. */
	eq(savefile_save("TestChunks"), true);
	reset_before_load();
	eq(savefile_load("TestChunks", false), true);
	eq(chunk_list_max, initial_count + 2);
	actual = chunk_find_location("core.main.001", false);
	known = chunk_find_location("core.main.001", true);
	notnull(actual);
	notnull(known);
	eq(actual->depth, 1);
	eq(known->depth, 1);
	require(!actual->is_known);
	require(known->is_known);
	require(actual->height == known->height);
	require(actual->width == known->width);
	if (turn < 1000) turn = 1000;
	inactive_turn = turn - 1000;
	actual->turn = inactive_turn;
	known->turn = inactive_turn;
	actual->noise.grids[trail.y][trail.x] = 17;
	actual->scent.grids[trail.y][trail.x] = 1;
	square_set_feat(actual, monster_grid, FEAT_FLOOR);
	square_set_feat(known, monster_grid, FEAT_FLOOR);
	restored_monster = t_add_monster(actual, monster_grid,
		"giant white mouse");
	restored_monster->maxhp = 100;
	restored_monster->hp = 10;
	restored_monster->m_timed[MON_TMD_SLEEP] = 50;

	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_destination_prepare_stored(&candidate, &coordinator) ==
		WORLD_DESTINATION_OK);
	require(candidate.actual == actual);
	require(candidate.known == known);
	require(world_destination_candidate_set_arrival(&candidate, &coordinator,
		"wrong.entry", arrival) == WORLD_DESTINATION_ENTRY_MISMATCH);
	require(!world_destination_candidate_is_publishable(&candidate));
	square_set_feat(actual, arrival, FEAT_FLOOR);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_ENTRY_UNRESOLVED);
	square_set_feat(actual, arrival, FEAT_LESS);
	square_set_mon(actual, arrival, 1);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_INVALID_ARRIVAL);
	square_set_mon(actual, arrival, 0);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);
	require(loc_eq(candidate.arrival, arrival));
	require(world_destination_candidate_is_publishable(&candidate));
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_STORE, NULL) == WORLD_DESTINATION_TIME_NOT_STAGED);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_STORE, NULL) ==
		WORLD_DESTINATION_RESOURCES_NOT_STAGED);
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	turn++;
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_STORE, NULL) == WORLD_DESTINATION_CLOCK_STALE);
	turn--;
	player->timed[TMD_FOOD]++;
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_STORE, NULL) == WORLD_DESTINATION_RESOURCES_STALE);
	player->timed[TMD_FOOD]--;
	actual->turn++;
	known->turn++;
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_STORE, NULL) == WORLD_DESTINATION_AGE_STALE);
	actual->turn = inactive_turn;
	known->turn = inactive_turn;

	/* A changed stored list rejects publication without changing the source. */
	source_actual = cave;
	source_known = player->cave;
	source_grid = player->grid;
	require(chunk_list_remove_chunk(known));
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_STORE, NULL) == WORLD_DESTINATION_STALE);
	require(cave == source_actual);
	require(player->cave == source_known);
	require(streq(player->world_location.id, "core.hub"));
	require(square(source_actual, source_grid)->mon == -1);
	chunk_list_add(known);

	/* Publication commits once and exchanges destination for stored source. */
	require(world_destination_publish_with_lifecycle(&candidate, &coordinator,
		player, WORLD_SOURCE_STORE, NULL,
		world_top_down_route_lifecycle()) == WORLD_DESTINATION_OK);
	require(cave == actual);
	require(player->cave == known);
	require(loc_eq(player->grid, arrival));
	require(square(cave, arrival)->mon == -1);
	require(square(source_actual, source_grid)->mon == 0);
	require(streq(player->world_location.id, "core.main.001"));
	require(streq(player->world_location.entry, "stair.up"));
	eq(player->depth, 1);
	require(chunk_find_location("core.hub", false) == source_actual);
	require(chunk_find_location("core.hub", true) == source_known);
	require(!chunk_find_location("core.main.001", false));
	require(!chunk_find_location("core.main.001", true));
	require(!world_destination_candidate_is_ready(&candidate));
	require(!world_transition_coordinator_is_active(&coordinator));
	eq(chunk_list_max, initial_count + 2);
	eq(actual->noise.grids[trail.y][trail.x], 0);
	eq(actual->scent.grids[trail.y][trail.x], 101);
	eq(restored_monster->hp, 20);
	eq(restored_monster->m_timed[MON_TMD_SLEEP], 0);

	/* The published destination and retained source survive another restart. */
	eq(savefile_save("TestPublished"), true);
	reset_before_load();
	eq(savefile_load("TestPublished", false), true);
	require(streq(player->world_location.id, "core.main.001"));
	require(streq(player->world_location.entry, "stair.up"));
	require(streq(cave->location_id, "core.main.001"));
	require(streq(player->cave->location_id, "core.main.001"));
	notnull(chunk_find_location("core.hub", false));
	notnull(chunk_find_location("core.hub", true));
	eq(chunk_list_max, initial_count + 2);

	ok;
}

static int test_travel_action(void *state) {
	struct world_travel_action_report report;
	struct chunk *source_actual;
	struct chunk *source_known;
	struct chunk *destination_actual;
	struct chunk *destination_known;
	struct monster *threat;
	struct loc source_grid;
	struct loc arrival = loc(3, 2);
	struct loc threat_grid;
	int dir, i;
	int32_t source_turn;
	uint16_t initial_count;

	reset_before_load();
	eq(savefile_load("Test1", false), true);
	require(character_dungeon);
	on_new_level();
	/* Town generation may place an ordinary monster in view.  Keep the actors
	 * but hide that random perception state so the test controls the exact
	 * visible threat it is exercising. */
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);

		if (!mon || !mon->race) continue;
		mflag_off(mon->mflag, MFLAG_VISIBLE);
		mflag_off(mon->mflag, MFLAG_VIEW);
	}
	require(world_player_activate_travel_nodes_here(player, cave) == 0);
	require(world_travel_check_safety(player, cave) ==
		WORLD_TRAVEL_SAFETY_OK);

	source_actual = cave;
	source_known = player->cave;
	source_grid = player->grid;
	source_turn = turn;
	initial_count = chunk_list_max;

	/* Safety failures occur before destination preparation and leave the
	 * authoritative world pair, position, and clock untouched. */
	player->timed[TMD_FAST] = 1;
	require(world_travel_execute(player, cave, "core.route.main.down",
		&report) == WORLD_TRAVEL_ACTION_SAFETY_REJECTED);
	require(report.safety == WORLD_TRAVEL_SAFETY_TEMPORARY_EFFECT);
	require(report.transition == WORLD_TRANSITION_OK);
	player->timed[TMD_FAST] = 0;
	require(cave == source_actual);
	require(player->cave == source_known);
	require(loc_eq(player->grid, source_grid));
	eq(turn, source_turn);
	eq(chunk_list_max, initial_count);

	player->word_recall = 1;
	require(world_travel_execute(player, cave, "core.route.main.down",
		&report) == WORLD_TRAVEL_ACTION_SAFETY_REJECTED);
	require(report.safety == WORLD_TRAVEL_SAFETY_PENDING_RELOCATION);
	player->word_recall = 0;

	dir = choose_direction(cave, player);
	require(dir >= 0);
	threat_grid = loc_sum(player->grid, ddgrid[dir]);
	threat = t_add_monster(cave, threat_grid, "giant white mouse");
	mflag_on(threat->mflag, MFLAG_VISIBLE);
	mflag_on(threat->mflag, MFLAG_VIEW);
	require(world_travel_execute(player, cave, "core.route.main.down",
		&report) == WORLD_TRAVEL_ACTION_SAFETY_REJECTED);
	require(report.safety == WORLD_TRAVEL_SAFETY_THREATENED);
	delete_monster_idx(cave, threat->midx);
	require(cave == source_actual);
	require(player->cave == source_known);
	require(loc_eq(player->grid, source_grid));
	eq(turn, source_turn);
	eq(chunk_list_max, initial_count);

	/* A complete stored destination is published once at its named entry,
	 * with the source retained for the return journey. */
	destination_actual = cave_new(5, 7);
	destination_actual->depth = 1;
	require(world_chunk_set_location(destination_actual, "core.main.001",
		false));
	destination_known = cave_new(5, 7);
	destination_known->depth = 1;
	require(world_chunk_set_location(destination_known, "core.main.001",
		true));
	square_set_feat(destination_actual, arrival, FEAT_LESS);
	square_set_feat(destination_known, arrival, FEAT_LESS);
	chunk_list_add(destination_actual);
	chunk_list_add(destination_known);

	require(world_travel_execute(player, cave, "core.route.main.down",
		&report) == WORLD_TRAVEL_ACTION_OK);
	require(report.result == WORLD_TRAVEL_ACTION_OK);
	require(report.safety == WORLD_TRAVEL_SAFETY_OK);
	require(report.transition == WORLD_TRANSITION_OK);
	require(report.resources == WORLD_TRAVEL_RESOURCES_OK);
	require(report.destination == WORLD_DESTINATION_OK);
	require(cave == destination_actual);
	require(player->cave == destination_known);
	require(streq(player->world_location.id, "core.main.001"));
	require(streq(player->world_location.entry, "stair.up"));
	require(loc_eq(player->grid, arrival));
	eq(turn, source_turn);
	eq(chunk_list_max, initial_count + 2);
	require(chunk_find_location("core.hub", false) == source_actual);
	require(chunk_find_location("core.hub", true) == source_known);

	ok;
}

static int test_retired_source_publication(void *state) {
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct world_retired_chunks retired = { 0 };
	struct chunk *actual;
	struct chunk *known;
	struct loc arrival = loc(3, 2);
	uint16_t initial_count;

	reset_before_load();
	eq(savefile_load("Test1", false), true);
	initial_count = chunk_list_max;
	actual = cave_new(5, 7);
	actual->depth = 1;
	require(world_chunk_set_location(actual, "core.main.001", false));
	known = cave_new(5, 7);
	known->depth = 1;
	require(world_chunk_set_location(known, "core.main.001", true));
	square_set_feat(actual, arrival, FEAT_LESS);
	square_set_feat(known, arrival, FEAT_LESS);
	chunk_list_add(actual);
	chunk_list_add(known);

	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_prepare_stored(&candidate, &coordinator) ==
		WORLD_DESTINATION_OK);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_RETIRE, &retired) == WORLD_DESTINATION_OK);
	require(cave == actual);
	require(player->cave == known);
	notnull(retired.actual);
	notnull(retired.known);
	eq(chunk_list_max, initial_count);
	require(world_retired_chunks_dispose(&retired, player));
	null(retired.actual);
	null(retired.known);

	ok;
}

static int test_owned_destination_candidate(void *state) {
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct chunk *actual;
	struct chunk *known;
	struct chunk *source_actual;
	struct chunk *source_known;
	struct loc arrival = loc(3, 2);
	struct loc source_grid;
	uint16_t initial_count;

	reset_before_load();
	eq(savefile_load("Test1", false), true);
	initial_count = chunk_list_max;
	source_actual = cave;
	source_known = player->cave;
	source_grid = player->grid;

	actual = cave_new(5, 7);
	actual->depth = 1;
	require(world_chunk_set_location(actual, "core.main.001", false));
	known = cave_new(5, 7);
	known->depth = 1;
	require(world_chunk_set_location(known, "core.main.001", true));
	known->depth = 2;
	square_set_feat(actual, arrival, FEAT_LESS);
	square_set_feat(known, arrival, FEAT_LESS);

	/* Failed adoption leaves the caller's pair and source untouched. */
	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_prepare_owned(&candidate, &coordinator, actual,
		known) == WORLD_DESTINATION_INCONSISTENT);
	require(!world_destination_candidate_is_ready(&candidate));
	require(!world_transition_coordinator_is_ready(&coordinator));
	require(cave == source_actual);
	require(player->cave == source_known);
	eq(chunk_list_max, initial_count);

	/* Successful adoption transfers ownership, and abort destroys off-list
	 * chunks without changing the active pair or stored list. */
	known->depth = 1;
	require(world_destination_prepare_owned(&candidate, &coordinator, actual,
		known) == WORLD_DESTINATION_OK);
	require(candidate.storage == WORLD_DESTINATION_CANDIDATE_OWNED);
	require(!candidate.rollback_artifacts);
	require(!candidate.generated_for_transition);
	require(!chunk_find(actual));
	require(!chunk_find(known));
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);
	require(world_destination_candidate_abort(&candidate, &coordinator,
		player));
	require(!world_destination_candidate_is_ready(&candidate));
	require(!world_transition_coordinator_is_active(&coordinator));
	require(cave == source_actual);
	require(player->cave == source_known);
	require(square(source_actual, source_grid)->mon == -1);
	eq(chunk_list_max, initial_count);

	/* A second generated pair publishes directly; capacity for retaining the
	 * source is reserved before commit, so publication only appends pointers. */
	actual = cave_new(5, 7);
	actual->depth = 1;
	require(world_chunk_set_location(actual, "core.main.001", false));
	known = cave_new(5, 7);
	known->depth = 1;
	require(world_chunk_set_location(known, "core.main.001", true));
	square_set_feat(actual, arrival, FEAT_LESS);
	square_set_feat(known, arrival, FEAT_LESS);
	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_prepare_owned(&candidate, &coordinator, actual,
		known) == WORLD_DESTINATION_OK);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_STORE, NULL) == WORLD_DESTINATION_OK);
	require(cave == actual);
	require(player->cave == known);
	require(loc_eq(player->grid, arrival));
	require(square(cave, arrival)->mon == -1);
	require(square(source_actual, source_grid)->mon == 0);
	require(chunk_find_location("core.hub", false) == source_actual);
	require(chunk_find_location("core.hub", true) == source_known);
	require(!chunk_find_location("core.main.001", false));
	require(!chunk_find_location("core.main.001", true));
	eq(chunk_list_max, initial_count + 2);

	/* Both ownership sides remain reconstructible after a restart. */
	eq(savefile_save("TestOwned"), true);
	reset_before_load();
	eq(savefile_load("TestOwned", false), true);
	require(streq(player->world_location.id, "core.main.001"));
	require(streq(cave->location_id, "core.main.001"));
	require(streq(player->cave->location_id, "core.main.001"));
	notnull(chunk_find_location("core.hub", false));
	notnull(chunk_find_location("core.hub", true));
	eq(chunk_list_max, initial_count + 2);

	ok;
}

static int test_owned_destination_source_retirement(void *state) {
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct world_retired_chunks retired = { 0 };
	struct chunk *actual;
	struct chunk *known;
	struct chunk *source_actual;
	struct chunk *source_known;
	struct loc arrival = loc(3, 2);
	uint16_t initial_count;

	reset_before_load();
	eq(savefile_load("Test1", false), true);
	initial_count = chunk_list_max;
	source_actual = cave;
	source_known = player->cave;
	actual = cave_new(5, 7);
	actual->depth = 1;
	require(world_chunk_set_location(actual, "core.main.001", false));
	known = cave_new(5, 7);
	known->depth = 1;
	require(world_chunk_set_location(known, "core.main.001", true));
	square_set_feat(actual, arrival, FEAT_LESS);
	square_set_feat(known, arrival, FEAT_LESS);

	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_prepare_owned(&candidate, &coordinator, actual,
		known) == WORLD_DESTINATION_OK);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);
	require(world_destination_publish(&candidate, &coordinator, player,
		WORLD_SOURCE_RETIRE, &retired) == WORLD_DESTINATION_OK);
	require(cave == actual);
	require(player->cave == known);
	require(retired.actual == source_actual);
	require(retired.known == source_known);
	eq(chunk_list_max, initial_count);
	require(world_retired_chunks_dispose(&retired, player));
	null(retired.actual);
	null(retired.known);

	ok;
}

static int test_isolated_destination_generation(void *state) {
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct destination_lifecycle_test lifecycle_test = { 0 };
	struct world_destination_lifecycle lifecycle = {
		.leave_source = test_lifecycle_leave,
		.leave_user = &lifecycle_test,
		.enter_destination = test_lifecycle_enter,
		.enter_user = &lifecycle_test
	};
	struct chunk *source_actual;
	struct chunk *source_known;
	struct chunk *destination_actual;
	struct chunk *destination_known;
	struct loc source_grid;
	int source_depth;
	int32_t departure_turn;
	int monster_population;
	int artifact_count;
	uint32_t source_update, source_redraw;
	bool source_ready;

	reset_before_load();
	eq(savefile_load("Test1", false), true);
	source_actual = cave;
	source_known = player->cave;
	source_grid = player->grid;
	source_depth = player->depth;
	departure_turn = turn;
	monster_population = global_monster_population();
	artifact_count = created_artifact_count();
	source_update = player->upkeep->update;
	source_redraw = player->upkeep->redraw;
	source_ready = character_dungeon;

	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_generate_owned(&candidate, &coordinator, player,
		0, 0) == WORLD_DESTINATION_OK);
	require(candidate.storage == WORLD_DESTINATION_CANDIDATE_OWNED);
	require(candidate.rollback_artifacts);
	require(candidate.generated_for_transition);
	require(world_destination_candidate_is_ready(&candidate));
	require(!world_destination_candidate_is_publishable(&candidate));
	require(cave == source_actual);
	require(player->cave == source_known);
	require(player->depth == source_depth);
	require(streq(player->world_location.id, "core.hub"));
	require(loc_eq(player->grid, source_grid));
	require(square(source_actual, source_grid)->mon == -1);
	require(character_dungeon == source_ready);
	require(player->upkeep->update == source_update);
	require(player->upkeep->redraw == source_redraw);
	require(!chunk_find(candidate.actual));
	require(!chunk_find(candidate.known));
	require(streq(candidate.actual->location_id, "core.main.001"));
	require(streq(candidate.known->location_id, "core.main.001"));
	require(candidate.actual->depth == 1);
	require(candidate.known->depth == 1);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);

	/* Abort rolls back candidate monsters and artifacts as well as memory. */
	require(world_destination_candidate_abort(&candidate, &coordinator,
		player));
	require(global_monster_population() == monster_population);
	require(created_artifact_count() == artifact_count);
	require(cave == source_actual);
	require(player->cave == source_known);
	require(square(source_actual, source_grid)->mon == -1);
	require(!world_transition_coordinator_is_active(&coordinator));
	require(turn == departure_turn);

	/* A second isolated pair can be resolved and transactionally published. */
	require(world_transition_coordinator_begin_route(&coordinator, player,
		"core.route.main.down", true) == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_generate_owned(&candidate, &coordinator, player,
		0, 0) == WORLD_DESTINATION_OK);
	destination_actual = candidate.actual;
	destination_known = candidate.known;
	lifecycle_test.source_actual = source_actual;
	lifecycle_test.source_known = source_known;
	lifecycle_test.destination_actual = destination_actual;
	lifecycle_test.destination_known = destination_known;
	lifecycle_test.source_grid = source_grid;
	lifecycle_test.delegate = world_top_down_route_lifecycle();
	lifecycle_test.valid = true;
	require(player_set_timed(player, TMD_COMMAND, 1, false, false));
	player->energy = 0;

	/* Rejection occurs before either irreversible lifecycle hook. */
	require(world_destination_publish_with_lifecycle(&candidate, &coordinator,
		player, WORLD_SOURCE_STORE, NULL, &lifecycle) ==
		WORLD_DESTINATION_NOT_PREPARED);
	require(lifecycle_test.step == 0);
	require(lifecycle_test.valid);
	require(player->timed[TMD_COMMAND] == 1);
	require(cave == source_actual);
	require(player->cave == source_known);

	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);
	lifecycle_test.arrival = candidate.arrival;
	require(world_destination_publish_with_lifecycle(&candidate, &coordinator,
		player, WORLD_SOURCE_STORE, NULL, &lifecycle) == WORLD_DESTINATION_OK);
	require(lifecycle_test.step == 2);
	require(lifecycle_test.valid);
	require(cave == destination_actual);
	require(player->cave == destination_known);
	require(player->depth == 1);
	require(streq(player->world_location.id, "core.main.001"));
	require(square(cave, player->grid)->mon == -1);
	require(player->timed[TMD_COMMAND] == 0);
	require(player->energy >= z_info->move_energy);
	require(player->max_depth >= 1);
	require(player->upkeep->autosave);
	require(chunk_find_location("core.hub", false) == source_actual);
	require(chunk_find_location("core.hub", true) == source_known);
	require(source_actual->turn == departure_turn);
	require(source_known->turn == departure_turn);
	require(destination_actual->turn == turn);
	require(destination_known->turn == turn);
	require(!world_transition_coordinator_is_active(&coordinator));

	ok;
}

static int test_gate_approaches_and_stair_repair(void *state)
{
	const struct level *level;
	struct loc grid, original, repaired, trigger = loc(0, 0);
	int approaches = 0;
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	level = world_chunk_level(cave);
	notnull(level);
	eq(world_entry_locate(cave, "stair.main", &original), WORLD_ENTRY_OK);
	require(!world_entry_is_overworld_gate_grid(cave, original));
	for (grid.y = 1; grid.y < cave->height - 1; grid.y++) {
		for (grid.x = 1; grid.x < cave->width - 1; grid.x++) {
			if (!world_entry_is_edge_exit_grid(cave, grid)) continue;
			trigger = grid;
			for (int dir = 1; dir <= 9; dir++) {
				struct loc from = loc_diff(grid, ddgrid[dir]);
				const struct world_route *route;
				struct world_transition transition;
				struct player probe = *player;
				if (dir == 5 || !square_in_bounds_fully(cave, from)) continue;
				route = world_entry_edge_route_for_step(cave, from, grid);
				notnull(route);
				require(world_entry_contains(cave, route->from_entry, from));
				probe.grid = from;
				eq(world_transition_prepare_physical(&probe, cave, route->id,
					&transition), WORLD_TRANSITION_OK);
				approaches++;
			}
		}
	}
	require(approaches > 0);
	/* Reproduce the old collision: the down-stair is in a travel band. */
	square_set_feat(cave, original, FEAT_FLOOR);
	square_set_feat(player->cave, original, FEAT_FLOOR);
	square_set_feat(cave, trigger, FEAT_MORE);
	square_set_feat(player->cave, trigger, FEAT_MORE);
	require(world_entry_materialize_overworld_gates(cave, level));
	require(square_isdownstairs(cave, trigger)); /* never silently erase it */
	require(town_ensure_main_stair(cave, player->cave));
	eq(world_entry_locate(cave, "stair.main", &repaired), WORLD_ENTRY_OK);
	require(!world_entry_is_overworld_gate_grid(cave, repaired));
	require(!square_isdownstairs(player->cave, trigger));
	require(world_entry_materialize_overworld_gates(cave, level));
	require(square_isdownstairs(cave, repaired));
	/* An already erased stair is repaired through the normal load boundary. */
	square_set_feat(cave, repaired, FEAT_FLOOR);
	square_set_feat(player->cave, repaired, FEAT_FLOOR);
	require(world_reconcile_location_services(cave, player->cave));
	eq(world_entry_locate(cave, "stair.main", &repaired), WORLD_ENTRY_OK);
	require(!world_entry_is_overworld_gate_grid(cave, repaired));
	/* Idempotent, and the repaired town survives a real save/reload. */
	require(world_reconcile_location_services(cave, player->cave));
	require(square_isdownstairs(cave, repaired));
	require(savefile_save("TestEdge"));
	reset_before_load();
	require(savefile_load("TestEdge", false));
	on_new_level();
	require(square_isdownstairs(cave, repaired));
	/* Load an unrepaired active town, rather than only a repaired save. */
	square_set_feat(cave, repaired, FEAT_FLOOR);
	square_set_feat(player->cave, repaired, FEAT_FLOOR);
	require(savefile_save("TestEdge"));
	reset_before_load();
	require(savefile_load("TestEdge", false));
	eq(world_entry_locate(cave, "stair.main", &repaired), WORLD_ENTRY_OK);
	require(!world_entry_is_overworld_gate_grid(cave, repaired));
	ok;
}

static void hide_monsters_for_travel(void)
{
	for (int i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		if (!mon || !mon->race) continue;
		mflag_off(mon->mflag, MFLAG_VISIBLE);
		mflag_off(mon->mflag, MFLAG_VIEW);
	}
}

static int test_stored_town_stair_repair(void *state)
{
	struct world_travel_action_report report;
	struct chunk *town, *known;
	struct loc stair;
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	/* The fixture has discovered both stair nodes; isolate fast-travel safety. */
	require(world_player_unlock_route(player, "core.route.main.up"));
	hide_monsters_for_travel();
	require(world_travel_execute(player, cave, "core.route.main.down",
		&report) == WORLD_TRAVEL_ACTION_OK);
	town = chunk_find_location("core.hub", false);
	known = chunk_find_location("core.hub", true);
	notnull(town);
	notnull(known);
	eq(world_entry_locate(town, "stair.main", &stair), WORLD_ENTRY_OK);
	square_set_feat(town, stair, FEAT_FLOOR);
	square_set_feat(known, stair, FEAT_FLOOR);
	require(savefile_save("TestEdge"));
	reset_before_load();
	require(savefile_load("TestEdge", false));
	town = chunk_find_location("core.hub", false);
	eq(world_entry_locate(town, "stair.main", &stair), WORLD_ENTRY_OK);
	require(!world_entry_is_overworld_gate_grid(town, stair));
	/* Return resolution happens before the town becomes the active chunk. */
	hide_monsters_for_travel();
	require(world_travel_execute(player, cave, "core.route.main.up",
		&report) == WORLD_TRAVEL_ACTION_OK);
	require(cave == town);
	require(loc_eq(player->grid, stair));
	ok;
}

static int test_new_towns_keep_the_dungeon_entrance(void *state)
{
	(void)state;
	for (uint32_t seed = 2026100600; seed < 2026100624; seed++) {
		struct loc stair;
		reset_before_load();
		/* Unlike loading a save, fresh birth starts with no complete character. */
		character_generated = false;
		Rand_state_init(seed);
		require(player_make_simple(NULL, NULL, "Town Tester"));
		prepare_next_level(player);
		on_new_level();
		eq(world_entry_locate(cave, "stair.main", &stair), WORLD_ENTRY_OK);
		require(!world_entry_is_overworld_gate_grid(cave, stair));
		require(!world_entry_is_overworld_gate_grid(cave, player->grid));
		require(square_isdownstairs(cave, stair));
		require(cave->feat_count[FEAT_MERIDIAN_STONE] >= 3);
		/* The road-facing doorway and approach remain three tiles wide. */
		for (int dy = 1; dy <= 2; dy++) {
			for (int dx = -1; dx <= 1; dx++)
				require(square_ispassable(cave, loc(stair.x + dx, stair.y + dy)));
		}
	}
	ok;
}

static int test_entrance_landmark_safety(void *state)
{
	struct chunk *actual, *known;
	struct object object = { 0 };
	struct trap trap = { 0 };
	struct loc stair = loc(12, 10);
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	actual = cave_new(21, 25);
	known = cave_new(21, 25);
	require(world_chunk_set_location(actual, "core.hub", false));
	require(world_chunk_set_location(known, "core.hub", true));
	for (int y = 1; y < 20; y++) {
		for (int x = 1; x < 24; x++) {
			square_set_feat(actual, loc(x, y), FEAT_FLOOR);
		}
	}
	square_set_feat(actual, stair, FEAT_MORE);
	square_set_feat(known, loc(10, 9), FEAT_FLOOR);
	require(world_entry_materialize_landmarks(actual, known));
	eq(actual->feat_count[FEAT_MERIDIAN_STONE], 9);
	eq(square(known, loc(10, 9))->feat, FEAT_MERIDIAN_STONE);
	eq(square(known, loc(11, 9))->feat, FEAT_NONE);
	require(square_isdownstairs(actual, stair));
	require(!square_ispassable(actual, loc(10, 9)));
	require(square_ispassable(actual, loc(12, 11)));
	require(world_entry_materialize_landmarks(actual, known));
	eq(actual->feat_count[FEAT_MERIDIAN_STONE], 9);

	/* Content, residents (including the player), roads and water win. */
	for (int y = 9; y <= 11; y++) {
		for (int x = 10; x <= 14; x++) {
			if (square(actual, loc(x, y))->feat == FEAT_MERIDIAN_STONE)
				square_set_feat(actual, loc(x, y), FEAT_FLOOR);
		}
	}
	sqinfo_on(square(actual, loc(10, 9))->info, SQUARE_ROAD);
	sqinfo_on(square(actual, loc(11, 9))->info, SQUARE_WATER);
	square_set_feat(actual, loc(12, 9), FEAT_TREE);
	square_set_feat(actual, loc(13, 9), FEAT_STORE_GENERAL);
	square_set_obj(actual, loc(14, 9), &object);
	square_set_mon(actual, loc(10, 10), 1);
	square_set_mon(actual, loc(14, 10), -1);
	actual->squares[11][10].trap = &trap;
	require(world_entry_materialize_landmarks(actual, known));
	eq(actual->feat_count[FEAT_MERIDIAN_STONE], 1);
	eq(square(actual, loc(14, 11))->feat, FEAT_MERIDIAN_STONE);
	square_set_obj(actual, loc(14, 9), NULL);
	square_set_mon(actual, loc(10, 10), 0);
	square_set_mon(actual, loc(14, 10), 0);
	actual->squares[11][10].trap = NULL;
	cave_free(actual);
	cave_free(known);

	/* Never seal a one-tile corridor in a pre-existing village. */
	actual = cave_new(21, 25);
	require(world_chunk_set_location(actual, "core.hub", false));
	for (int y = 1; y < 20; y++) {
		for (int x = 1; x < 24; x++)
			square_set_feat(actual, loc(x, y), FEAT_GRANITE);
		square_set_feat(actual, loc(12, y), FEAT_FLOOR);
	}
	square_set_feat(actual, stair, FEAT_MORE);
	require(world_entry_materialize_landmarks(actual, NULL));
	eq(square(actual, loc(12, 9))->feat, FEAT_FLOOR);
	eq(actual->feat_count[FEAT_MERIDIAN_STONE], 8);

	/* Gate activation/arrival bands cannot acquire decorative walls. */
	square_set_feat(actual, stair, FEAT_FLOOR);
	{
		const struct world_entry *entry = world_entry_by_id(
			world_chunk_level(actual), "stair.main");
		struct loc gate = loc(0, 0);
		for (int y = 1; y < 20 && !gate.x; y++) {
			for (int x = 1; x < 24 && !gate.x; x++) {
				const struct world_entry_rock *rock;
				if (!world_entry_is_overworld_gate_grid(actual, loc(x, y))) continue;
				for (rock = entry->rocks; rock; rock = rock->next) {
					struct loc origin = loc(x - rock->offset.x, y - rock->offset.y);
					if (!square_in_bounds_fully(actual, origin)) continue;
					gate = loc(x, y);
					square_set_feat(actual, origin, FEAT_MORE);
					break;
				}
			}
		}
		require(gate.x != 0);
		require(world_entry_materialize_landmarks(actual, NULL));
		require(square(actual, gate)->feat != FEAT_MERIDIAN_STONE);
	}
	cave_free(actual);
	ok;
}

static int test_saved_village_gets_landmark(void *state)
{
	struct loc stair, remembered;
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	on_new_level();
	eq(world_entry_locate(cave, "stair.main", &stair), WORLD_ENTRY_OK);
	remembered = loc(0, 0);
	/* Recreate an old, undecorated village without regenerating its map. */
	for (int y = 1; y < cave->height - 1; y++) {
		for (int x = 1; x < cave->width - 1; x++) {
			struct loc grid = loc(x, y);
			if (square(cave, grid)->feat != FEAT_MERIDIAN_STONE) continue;
			square_set_feat(cave, grid, FEAT_GRANITE);
			square_set_feat(player->cave, grid, FEAT_GRANITE);
			remembered = grid;
		}
	}
	require(remembered.x != 0);
	eq(cave->feat_count[FEAT_MERIDIAN_STONE], 0);
	require(savefile_save("TestLandmark"));
	reset_before_load();
	require(savefile_load("TestLandmark", false));
	require(cave->feat_count[FEAT_MERIDIAN_STONE] >= 3);
	eq(square(player->cave, remembered)->feat, FEAT_MERIDIAN_STONE);
	require(square_isdownstairs(cave, stair));
	require(loc_eq(player->grid, stair));
	/* The new feature also survives an ordinary save/load round trip. */
	require(savefile_save("TestLandmark"));
	reset_before_load();
	require(savefile_load("TestLandmark", false));
	eq(square(cave, remembered)->feat, FEAT_MERIDIAN_STONE);
	ok;
}

const char *suite_name = "game/world-travel";
static int test_woodland_migration_and_physics(void *state)
{
	struct loc tree, rock, unknown;
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	on_new_level();
	tree = player->grid;
	rock = loc(tree.x + 1, tree.y);
	unknown = loc(tree.x - 1, tree.y);
	square_set_feat(cave, tree, FEAT_TREE);
	square_memorize(cave, tree);
	require(square_ispassable(cave, tree));
	require(!square_allowslos(cave, tree));
	require(!square_isprojectable(cave, tree));
	require(!square_isdiggable(cave, tree));
	require(streq(square_apparent_name(player->cave, tree), "tree"));
	/* Recreate the old saved representation after setting the feature. */
	square_set_feat(cave, tree, FEAT_GRANITE);
	sqinfo_on(square(cave, tree)->info, SQUARE_WOOD);
	square_set_feat(player->cave, tree, FEAT_GRANITE);
	sqinfo_on(square(player->cave, tree)->info, SQUARE_WOOD);
	square_set_feat(cave, rock, FEAT_GRANITE);
	square_set_feat(player->cave, unknown, FEAT_NONE);
	sqinfo_on(square(player->cave, unknown)->info, SQUARE_WOOD);
	require(savefile_save("TestWoodland"));
	reset_before_load();
	require(savefile_load("TestWoodland", false));
	eq(square(cave, tree)->feat, FEAT_TREE);
	eq(square(player->cave, tree)->feat, FEAT_TREE);
	eq(square(cave, rock)->feat, FEAT_GRANITE);
	eq(square(player->cave, unknown)->feat, FEAT_NONE);
	square_set_feat(cave, tree, FEAT_FLOOR);
	require(!square_iswood(cave, tree));
	ok;
}

static int test_descent_and_recall_from_cavewood(void *state)
{
	struct world_travel_action_report report;
	const struct world_route *route;
	struct loc cave_scar;
	char travel_id[WORLD_ID_LEN] = "";
	uint16_t discoveries, routes, nodes;
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	on_new_level();
	require(!OPT(player, birth_levels_persist));
	require(test_walk_surface_to(level_by_id("core.approach.east")));
	require(test_place_on_entry(cave, "cave.east"));
	require(world_route_execute_physical(player, cave,
		"core.route.cave.east.in", &report) == WORLD_TRAVEL_ACTION_OK);
	require(find_empty(cave, &cave_scar));
	square_set_feat(cave, cave_scar, FEAT_RUBBLE);
	/* Keep one genuinely learned surface trail for the return-home check. */
	for (route = world_routes; route; route = route->next) {
		if (streq(route->from, "core.hub") && world_route_is_aggregate(route) &&
				world_player_route_is_unlocked(player, route->id)) {
			my_strcpy(travel_id, route->id, sizeof(travel_id));
			break;
		}
	}
	require(travel_id[0]);
	discoveries = player->discovered_locations.count;
	routes = player->unlocked_routes.count;
	nodes = player->activated_travel_nodes.count;
	/* Exercise the real delayed scroll effects.  The deepest visited Meridian
	 * floor controls the descent, not Cavewood's danger rating. */
	player->max_depth = 9;
	effect_simple(EF_DEEP_DESCENT, source_player(), "0", 0, 0, 0, 0, 0, NULL);
	require(player->deep_descent > 0);
	player->deep_descent = 1;
	turn = ((turn / 10) + 1) * 10;
	process_world(cave);
	require(player->upkeep->generate_level);
	require(streq(player->world_location.id, "core.main.014"));
	on_leave_level();
	prepare_next_level(player);
	on_new_level();
	require(chunk_find_location("core.cave.east.001", false));
	require(chunk_find_location("core.cave.east.001", true));
	require(savefile_save("TestScrollTravel"));
	/* Match the report's working L14 autosave, then homeward recall. */
	reset_before_load();
	require(savefile_load("TestScrollTravel", false));
	on_new_level();
	effect_simple(EF_RECALL, source_player(), "0", 0, 0, 0, 0, 0, NULL);
	require(player->word_recall > 0);
	player->word_recall = 1;
	turn = ((turn / 10) + 1) * 10;
	process_world(cave);
	require(player->upkeep->generate_level);
	require(streq(player->world_location.id, "core.hub"));
	on_leave_level();
	prepare_next_level(player);
	on_new_level();
	eq(player->discovered_locations.count, discoveries + 1);
	eq(player->unlocked_routes.count, routes);
	eq(player->activated_travel_nodes.count, nodes);
	null(chunk_find_location("core.hub", false));
	null(chunk_find_location("core.hub", true));
	require(savefile_save("TestScrollTravel"));
	test_hide_monsters(cave);
	require(world_travel_execute(player, cave, travel_id, &report) ==
		WORLD_TRAVEL_ACTION_OK);
	reset_before_load();
	require(savefile_load("TestScrollTravel", false));
	on_new_level();
	require(streq(player->world_location.id, "core.hub"));
	test_hide_monsters(cave);
	require(world_travel_execute(player, cave, travel_id, &report) ==
		WORLD_TRAVEL_ACTION_OK);
	require(test_walk_surface_to(level_by_id("core.approach.east")));
	require(test_place_on_entry(cave, "cave.east"));
	require(world_route_execute_physical(player, cave,
		"core.route.cave.east.in", &report) == WORLD_TRAVEL_ACTION_OK);
	require(square_isrubble(cave, cave_scar));
	require(savefile_save("TestScrollTravel"));
	ok;
}

static int test_legacy_relocation_stored_pairs(void *state)
{
	int persistent, circuit;
	(void)state;
	for (persistent = 0; persistent < 2; persistent++) {
		struct chunk *town, *town_known;
		struct loc scar;
		reset_before_load();
		require(savefile_load("Test1", false));
		on_new_level();
		player->opts.opt[OPT_birth_levels_persist] = persistent != 0;
		town = cave;
		town_known = player->cave;
		require(find_empty(cave, &scar));
		square_set_feat(cave, scar, FEAT_RUBBLE);
		for (circuit = 0; circuit < 3; circuit++) {
			/* Depth-based arrivals include recall, teleport-level and normal
			 * stairs; they must not invent a second copy of a retained town. */
			dungeon_change_level(player, 14);
			on_leave_level();
			prepare_next_level(player);
			on_new_level();
			require(chunk_find_location("core.hub", false) == town);
			require(chunk_find_location("core.hub", true) == town_known);
			require(savefile_save("TestScrollTravel"));
			dungeon_change_level(player, 0);
			on_leave_level();
			prepare_next_level(player);
			on_new_level();
			require(cave == town);
			require(player->cave == town_known);
			require(square_isrubble(cave, scar));
			null(chunk_find_location("core.hub", false));
			null(chunk_find_location("core.hub", true));
			require(savefile_save("TestScrollTravel"));
		}
	}
	ok;
}

static void capture_ambient_event(game_event_type type,
		game_event_data *data, void *user)
{
	int *last = user;
	(void)type;
	if (data->message.type >= MSG_AMBIENT_DAY &&
			data->message.type <= MSG_AMBIENT_DNG5) *last = data->message.type;
}

static int ambient_for_location(const char *id)
{
	int last = -1;
	if (!world_player_set_location(player, id, NULL)) return -1;
	event_add_handler(EVENT_SOUND, capture_ambient_event, &last);
	play_ambient_sound();
	event_remove_handler(EVENT_SOUND, capture_ambient_event, &last);
	return last;
}

static int test_surface_ambient_routing(void *state)
{
	const struct level *level;
	int surfaces = 0;
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	player->opts.opt[OPT_use_sound] = true;
	for (level = world; level; level = level->next) {
		if (!level->is_overworld_cell) continue;
		surfaces++;
		turn = 0;
		eq(ambient_for_location(level->id), MSG_AMBIENT_DAY);
		turn = 5L * z_info->day_length;
		eq(ambient_for_location(level->id), MSG_AMBIENT_NITE);
	}
	eq(surfaces, WORLD_OVERWORLD_LAUNCH_CELLS);
	eq(ambient_for_location("core.main.014"), MSG_AMBIENT_DNG1);
	eq(ambient_for_location("core.main.021"), MSG_AMBIENT_DNG2);
	eq(ambient_for_location("core.main.041"), MSG_AMBIENT_DNG3);
	eq(ambient_for_location("core.main.061"), MSG_AMBIENT_DNG4);
	eq(ambient_for_location("core.main.081"), MSG_AMBIENT_DNG5);
	eq(ambient_for_location("core.cave.east.001"), MSG_AMBIENT_DNG1);
	eq(ambient_for_location("core.spelunk.east.001"), MSG_AMBIENT_DNG1);
	/* The village must replace dungeon ambience in either direction/time. */
	eq(ambient_for_location("core.hub"), MSG_AMBIENT_NITE);
	turn = 0;
	eq(ambient_for_location("core.hub"), MSG_AMBIENT_DAY);
	ok;
}

static int test_return_ambient_event(void *state)
{
	int last = -1, dungeon, village;
	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	player->opts.opt[OPT_use_sound] = true;
	dungeon_change_level(player, 14);
	on_leave_level();
	prepare_next_level(player);
	event_add_handler(EVENT_SOUND, capture_ambient_event, &last);
	on_new_level();
	dungeon = last;
	event_remove_handler(EVENT_SOUND, capture_ambient_event, &last);
	eq(dungeon, MSG_AMBIENT_DNG1);
	dungeon_change_level(player, 0);
	on_leave_level();
	prepare_next_level(player);
	last = -1;
	event_add_handler(EVENT_SOUND, capture_ambient_event, &last);
	on_new_level();
	village = last;
	event_remove_handler(EVENT_SOUND, capture_ambient_event, &last);
	eq(village, is_daytime() ? MSG_AMBIENT_DAY : MSG_AMBIENT_NITE);
	ok;
}

struct test tests[] = {
	{ "surface ambience routing", test_surface_ambient_routing },
	{ "village return ambience event", test_return_ambient_event },
	{ "Cavewood descent and recall", test_descent_and_recall_from_cavewood },
	{ "legacy relocation stored pairs", test_legacy_relocation_stored_pairs },
	{ "woodland migration and physics", test_woodland_migration_and_physics },
	{ "resources", test_travel_resources },
	{ "horizontal world loop", test_horizontal_world_loop },
	{ "stored destination candidate", test_stored_destination_candidate },
	{ "travel action", test_travel_action },
	{ "retired source publication", test_retired_source_publication },
	{ "owned destination candidate", test_owned_destination_candidate },
	{ "owned destination source retirement",
		test_owned_destination_source_retirement },
	{ "isolated destination generation", test_isolated_destination_generation },
	{ "gate approaches and stair repair", test_gate_approaches_and_stair_repair },
	{ "stored town stair repair", test_stored_town_stair_repair },
	{ "new towns keep dungeon entrance", test_new_towns_keep_the_dungeon_entrance },
	{ "entrance landmark safety", test_entrance_landmark_safety },
	{ "saved village gets landmark", test_saved_village_gets_landmark },
	{ NULL, NULL }
};
