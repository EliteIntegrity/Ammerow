/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* game/basic.c */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include <stdio.h>
#include "cave.h"
#include "cmd-core.h"
#include "cmd-larder.h"
#include "cmd-spelunking.h"
#include "game-event.h"
#include "game-world.h"
#include "fishing-data.h"
#include "generate.h"
#include "init.h"
#include "message.h"
#include "mon-make.h"
#include "mon-timed.h"
#include "mon-util.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "savefile.h"
#include "store.h"
#include "trap.h"
#include "player.h"
#include "player-birth.h"
#include "player-timed.h"
#include "player-util.h"
#include "ui-travel.h"
#include "world-entry.h"
#include "world-map.h"
#include "world-overworld.h"
#include "world-objective-data.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-layout.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-visibility.h"
#include "world-travel-action.h"
#include "world-travel-resources.h"
#include "world-turn.h"
#include "z-file.h"
#include "z-rand.h"
#include "z-util.h"
#define GAME_TEST_SKIP_BASELINE
#include "game-fixture.h"

static uint32_t expected_random_after_save;

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

static int reverse_direction(int dir) {
	int rdir[10] = { 5, 9, 8, 7, 6, 5, 4, 3, 2, 1 };

	return (dir >= 0 && dir <= 9) ? rdir[dir] : -1;
}

static bool store_has_kind(const struct store *store,
		const struct object_kind *kind)
{
	const struct object *obj;

	for (obj = store->stock; obj; obj = obj->next) {
		if (obj->kind == kind) return true;
	}
	return false;
}

static int store_kind_quantity(const struct store *store,
		const struct object_kind *kind)
{
	const struct object *obj;

	for (obj = store->stock; obj; obj = obj->next) {
		if (obj->kind == kind) return obj->number;
	}
	return 0;
}

static struct object *carried_object_of_kind(const struct player *p,
		const struct object_kind *kind)
{
	struct object *obj;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->kind == kind) return obj;
	}
	return NULL;
}

static struct object *carried_object_of_artifact(const struct player *p,
		const struct artifact *artifact)
{
	struct object *obj;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->artifact == artifact) return obj;
	}
	return NULL;
}

static struct object *carried_object_of_ego(const struct player *p,
		const struct ego_item *ego)
{
	struct object *obj;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->ego == ego) return obj;
	}
	return NULL;
}

struct expected_store_identity {
	int feat;
	unsigned int owner_count;
	const char *welcome;
};

struct expected_store_owner {
	int feat;
	unsigned int oidx;
	int32_t max_cost;
	const char *name;
};

static const struct expected_store_identity expected_store_identities[] = {
	{ FEAT_STORE_GENERAL, 4,
		"Supplies from the Nine Near Lands, weighed fairly and kept dry." },
	{ FEAT_STORE_ARMOR, 4,
		"Repairs cost less than funerals. Try every strap before you trust it." },
	{ FEAT_STORE_WEAPON, 4,
		"Every edge is tested on Meridian stone; every string is waxed against the rain." },
	{ FEAT_STORE_BOOK, 4,
		"Route journals, formulae and inherited manuals share the shelves." },
	{ FEAT_STORE_ALCHEMY, 4,
		"Field mixtures are labelled by batch. Unknown reactions are bought at your risk." },
	{ FEAT_STORE_MAGIC, 4,
		"Seam-touched instruments, charged devices and peculiar adornments." },
	{ FEAT_STORE_BLACK, 4,
		"No questions about origin. No promises about destination." },
	{ FEAT_HOME, 4, NULL },
	{ FEAT_STORE_FISHING, 1,
		"My lost yew longline lies in the Deepwell; it reaches Glassfin water. Twenty larder portions unlock Meridian provisions." }
};

static const struct expected_store_owner expected_store_owners[] = {
	{ FEAT_STORE_GENERAL, 0, 5000,
		"Bramble Fen the Provisioner" },
	{ FEAT_STORE_GENERAL, 1, 10000,
		"Orren Vale the Cautious" },
	{ FEAT_STORE_GENERAL, 2, 20000, "Kett Brassbuttons" },
	{ FEAT_STORE_GENERAL, 3, 30000,
		"Selra Reed the Even-Handed" },
	{ FEAT_STORE_ARMOR, 0, 5000, "Varruk Ironhide" },
	{ FEAT_STORE_ARMOR, 1, 10000, "Halen Ward the Exacting" },
	{ FEAT_STORE_ARMOR, 2, 25000, "Tavin Marchguard" },
	{ FEAT_STORE_ARMOR, 3, 30000, "Borra Flint the Patient" },
	{ FEAT_STORE_WEAPON, 0, 5000, "Grath Coilbreaker" },
	{ FEAT_STORE_WEAPON, 1, 10000, "Cael Ashhand" },
	{ FEAT_STORE_WEAPON, 2, 25000, "Tibbin Farshot" },
	{ FEAT_STORE_WEAPON, 3, 30000, "Dorren Forgevoice" },
	{ FEAT_STORE_BOOK, 0, 15000, "Edran Quill" },
	{ FEAT_STORE_BOOK, 1, 20000, "Sabren Fieldward" },
	{ FEAT_STORE_BOOK, 2, 25000, "Eilra Quietleaf" },
	{ FEAT_STORE_BOOK, 3, 30000, "Korrin Deepprint" },
	{ FEAT_STORE_ALCHEMY, 0, 10000, "Iven Silt the Assayer" },
	{ FEAT_STORE_ALCHEMY, 1, 10000, "Nibbin Glass" },
	{ FEAT_STORE_ALCHEMY, 2, 15000, "Vezra Pinch" },
	{ FEAT_STORE_ALCHEMY, 3, 15000, "Merra Voss the Measured" },
	{ FEAT_STORE_MAGIC, 0, 15000, "Vaeth Coilreader" },
	{ FEAT_STORE_MAGIC, 1, 20000, "Cobbin Spark" },
	{ FEAT_STORE_MAGIC, 2, 25000, "Joren Gauge" },
	{ FEAT_STORE_MAGIC, 3, 30000, "Saevra Riftglass" },
	{ FEAT_STORE_BLACK, 0, 15000, "Rusk Underbridge" },
	{ FEAT_STORE_BLACK, 1, 20000, "Kebb Salt-Eye" },
	{ FEAT_STORE_BLACK, 2, 25000, "Varran Side-Door" },
	{ FEAT_STORE_BLACK, 3, 30000, "Orra Greyhand" },
	{ FEAT_HOME, 0, 0, "Your home" },
	{ FEAT_HOME, 1, 0, "Your home" },
	{ FEAT_HOME, 2, 0, "Your home" },
	{ FEAT_HOME, 3, 0, "Your home" },
	{ FEAT_STORE_FISHING, 0, 5000,
		"Mara Venn the Outfitter" }
};

static int test_newgame(void *state) {
	struct world_transition_coordinator coordinator = { 0 };
	struct world_transition transition;
	struct world_larder_report larder_report;
	struct loc node_grid;
	struct loc off_node_grid;
	struct loc scan;
	struct loc road_grid = loc(0, 0);
	struct loc water_grid = loc(0, 0);
	struct grid_data grid_data;
	int shop_count = 0;
	int road_count = 0;
	int water_count = 0;
	int min_shop_x = INT_MAX, max_shop_x = 0;
	int min_shop_y = INT_MAX, max_shop_y = 0;
	int dir;
	int tool_tval;
	struct store *general;
	struct store *fishing_store;
	struct store *identity_store;
	struct owner *identity_owner;
	struct owner *owner_cursor;
	struct object_kind *piton_kind;
	struct object_kind *rope_kind;
	struct object_kind *scuba_kind;
	struct object_kind *reed_rod_kind;
	struct object_kind *ash_rod_kind;
	struct object_kind *yew_rod_kind;
	const struct world_fishing_rig *yew_rig;
	const struct world_objective_definition *longline_objective;
	const struct world_objective_definition *reserve_objective;
	const struct world_objective_definition *underflow_objective;
	struct world_objective_progress objective_progress;
	struct player objective_player = { 0 };
	struct object objective_item = { 0 };
	size_t identity_index;

	/* Try making a new game */
	eq(player_make_simple(NULL, NULL, "Tester"), true);

	eq(player->is_dead, false);
	require(streq(player->world_location.id, "core.hub"));
	require(streq(player->world_location.entry, "default"));
	require(world_player_has_discovered_location(player, "core.hub"));
	eq(player->discovered_locations.count, 1);
	prepare_next_level(player);
	on_new_level();
	notnull(cave);
	require(streq(cave->location_id, "core.hub"));
	require(!cave->is_known);
	eq(cave->height, 66);
	eq(cave->width, 198);
	require(streq(player->cave->location_id, "core.hub"));
	require(player->cave->is_known);
	eq(player->cave->height, cave->height);
	eq(player->cave->width, cave->width);
	for (scan.y = 0; scan.y < cave->height; scan.y++) {
		for (scan.x = 0; scan.x < cave->width; scan.x++) {
			if (square_isroad(cave, scan)) {
				if (!world_entry_is_edge_exit_grid(cave, scan)) {
					road_grid = scan;
				}
				road_count++;
			}
			if (square_iswater(cave, scan)) {
				water_grid = scan;
				water_count++;
			}
			if (!feat_is_shop(square(cave, scan)->feat)) continue;
			shop_count++;
			min_shop_x = MIN(min_shop_x, scan.x);
			max_shop_x = MAX(max_shop_x, scan.x);
			min_shop_y = MIN(min_shop_y, scan.y);
			max_shop_y = MAX(max_shop_y, scan.y);
		}
	}
	eq(shop_count, z_info->store_max);
	eq(z_info->store_max, 10);
	for (identity_index = 0;
			identity_index < N_ELEMENTS(expected_store_identities);
			identity_index++) {
		const struct expected_store_identity *expected =
			&expected_store_identities[identity_index];
		unsigned int owner_count = 0;

		identity_store = &stores[f_info[expected->feat].shopnum - 1];
		for (owner_cursor = identity_store->owners; owner_cursor;
				owner_cursor = owner_cursor->next) {
			owner_count++;
		}
		eq(owner_count, expected->owner_count);
		if (expected->welcome) {
			require(identity_store->welcome != NULL);
			require(streq(identity_store->welcome, expected->welcome));
		} else {
			require(identity_store->welcome == NULL);
		}
	}
	for (identity_index = 0;
			identity_index < N_ELEMENTS(expected_store_owners);
			identity_index++) {
		const struct expected_store_owner *expected =
			&expected_store_owners[identity_index];

		identity_store = &stores[f_info[expected->feat].shopnum - 1];
		identity_owner = store_ownerbyidx(identity_store, expected->oidx);
		require(identity_owner != NULL);
		eq(identity_owner->max_cost, expected->max_cost);
		require(streq(identity_owner->name, expected->name));
	}
	general = &stores[f_info[FEAT_STORE_GENERAL].shopnum - 1];
	fishing_store = &stores[f_info[FEAT_STORE_FISHING].shopnum - 1];
	require(fishing_store->feat == FEAT_STORE_FISHING);
	require(fishing_store->owner != NULL);
	require(streq(fishing_store->owner->name,
		"Mara Venn the Outfitter"));
	require(fishing_store->welcome != NULL);
	require(strstr(fishing_store->welcome, "yew longline") != NULL);
	eq(fishing_store->always_num, 2);
	eq(fishing_store->limited_num, 4);
	tool_tval = tval_find_idx("tool");
	piton_kind = lookup_kind(tool_tval, lookup_sval(tool_tval, "Piton"));
	rope_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Rope Length (2 metres)"));
	scuba_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Scuba Gear"));
	reed_rod_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Reed Bank Rod"));
	ash_rod_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Ash Water Rod"));
	yew_rod_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Yew Longline Rod"));
	notnull(piton_kind);
	notnull(rope_kind);
	notnull(scuba_kind);
	notnull(reed_rod_kind);
	notnull(ash_rod_kind);
	notnull(yew_rod_kind);
	require(kf_has(piton_kind->kind_flags, KF_SPELUNK_PITON));
	require(kf_has(rope_kind->kind_flags, KF_SPELUNK_ROPE));
	yew_rig = world_fishing_rig_for_kind(yew_rod_kind);
	notnull(yew_rig);
	eq(yew_rig->maximum_reach, 71);
	eq(yew_rig->maximum_depth, 16);
	require(store_has_kind(fishing_store, piton_kind));
	require(store_has_kind(fishing_store, rope_kind));
	require(store_has_kind(fishing_store, scuba_kind));
	require(store_has_kind(fishing_store, reed_rod_kind));
	require(store_has_kind(fishing_store, ash_rod_kind));
	require(store_has_kind(fishing_store, yew_rod_kind));
	eq(store_kind_quantity(fishing_store, yew_rod_kind), 1);
	eq(yew_rod_kind->cost, 6000);
	eq(store_kind_quantity(fishing_store, piton_kind), 24);
	eq(store_kind_quantity(fishing_store, rope_kind), 200);
	eq(store_kind_quantity(fishing_store, scuba_kind), 2);
	eq(world_objective_count(), 3);
	longline_objective = world_objective_by_id("lost-longline");
	reserve_objective = world_objective_by_id("emergency-reserve");
	underflow_objective = world_objective_by_id("underflow-sounding");
	notnull(longline_objective);
	notnull(reserve_objective);
	notnull(underflow_objective);
	eq(longline_objective->tval, yew_rod_kind->tval);
	eq(longline_objective->sval, yew_rod_kind->sval);
	eq(longline_objective->completion_message, MSG_OBJECTIVE_COMPLETE);
	require(world_objective_progress(&objective_player,
		longline_objective, &objective_progress));
	require(!objective_progress.complete);
	objective_item.kind = yew_rod_kind;
	objective_item.number = 1;
	eq(world_objective_item_completion_message(&objective_player,
		&objective_item), MSG_OBJECTIVE_COMPLETE);
	objective_player.gear = &objective_item;
	require(world_objective_progress(&objective_player,
		longline_objective, &objective_progress));
	require(objective_progress.complete);
	eq(world_objective_item_completion_message(&objective_player,
		&objective_item), MSG_GENERIC);
	require(world_objective_progress(&objective_player,
		underflow_objective, &objective_progress));
	require(!objective_progress.complete);
	require(world_player_unlock_route(&objective_player,
		"core.route.underflow.out"));
	require(world_objective_progress(&objective_player,
		underflow_objective, &objective_progress));
	require(objective_progress.complete);
	world_player_clear_ledgers(&objective_player);
	objective_player.larder.food_points = reserve_objective->target - 1;
	require(world_objective_progress(&objective_player,
		reserve_objective, &objective_progress));
	require(!objective_progress.complete);
	objective_player.larder.food_points = reserve_objective->target;
	require(world_objective_progress(&objective_player,
		reserve_objective, &objective_progress));
	require(objective_progress.complete);
	require(!store_has_kind(general, piton_kind));
	require(!store_has_kind(general, rope_kind));
	require(!store_has_kind(general, scuba_kind));
	require(max_shop_x - min_shop_x < z_info->town_wid);
	require(max_shop_y - min_shop_y < z_info->town_hgt);
	require(road_count >= 2 * (cave->width - 2));
	require(water_count >= 40);
	require(square_isfloor(cave, road_grid));
	require(square_ispassable(cave, road_grid));
	require(!square_isfloor(cave, water_grid));
	require(!square_ispassable(cave, water_grid));
	require(!square_isarrivable(cave, water_grid));
	require(!square_is_monster_walkable(cave, water_grid));
	require(!square_istrappable(cave, water_grid));
	require(!square_isobjectholding(cave, water_grid));
	require(square_isprojectable(cave, water_grid));
	require(square_allowslos(cave, water_grid));
	require(square_isnoflow(cave, water_grid));
	require(square_isnoscent(cave, water_grid));
	require(streq(square_apparent_name(cave, road_grid), "packed road"));
	require(streq(square_apparent_name(cave, water_grid), "deep water"));
	square_memorize(cave, road_grid);
	square_memorize(cave, water_grid);
	require(square_isroad(player->cave, road_grid));
	require(square_iswater(player->cave, water_grid));
	map_info(road_grid, &grid_data);
	require(grid_data.surface == GRID_SURFACE_ROAD);
	map_info(water_grid, &grid_data);
	require(grid_data.surface == GRID_SURFACE_WATER);
	eq(player->chp, player->mhp);
	eq(player->timed[TMD_FOOD], PY_FOOD_FULL - 1);
	require(world_entry_locate(cave, "stair.main", &node_grid) ==
		WORLD_ENTRY_OK);
	/* Entering through a named endpoint activates it automatically.
	 * If a generator placed the player elsewhere, deliberately reaching the
	 * endpoint exercises the same position helper. */
	if (world_entry_contains(cave, "stair.main", player->grid)) {
		require(world_player_has_activated_travel_node(player,
			"core.node.hub.main"));
		dir = choose_direction(cave, player);
		require(dir >= 0);
		off_node_grid = loc_sum(player->grid, ddgrid[dir]);
		square_set_mon(cave, player->grid, 0);
		player_place(cave, player, off_node_grid);
	} else {
		require(!world_player_has_activated_travel_node(player,
			"core.node.hub.main"));
		off_node_grid = player->grid;
		square_set_mon(cave, player->grid, 0);
		player_place(cave, player, node_grid);
		eq(world_player_activate_travel_nodes_here(player, cave), 1);
		square_set_mon(cave, player->grid, 0);
		player_place(cave, player, off_node_grid);
	}
	require(!world_player_activate_travel_node_here(player, cave,
		"core.node.hub.main"));

	/* Preserve one source save with no learned routes for the ordinary-stair
	 * integration regression. */
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, node_grid);
	require(savefile_save("TestPhysical"));
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, off_node_grid);
	require(world_player_unlock_route(player, "core.route.main.down"));
	require(world_player_route_is_unlocked(player, "core.route.main.down"));

	/* Fast travel is stricter than physical traversal: both stable endpoint
	 * nodes must be active and the player must stand on the departure node. */
	require(world_transition_prepare_travel(player, cave,
		"core.route.main.down", &transition) ==
		WORLD_TRANSITION_DESTINATION_NODE_INACTIVE);
	require(world_player_activate_travel_node(player,
		"core.node.main.001.up"));
	require(world_transition_prepare_travel(player, cave,
		"core.route.main.down", &transition) ==
		WORLD_TRANSITION_NOT_AT_SOURCE_NODE);
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, node_grid);
	require(world_player_activate_travel_node_here(player, cave,
		"core.node.hub.main"));
	eq(player->activated_travel_nodes.count, 3);

	/* Runtime transition preparation is not save state.  Until commit, the
	 * source remains authoritative even when a destination reports ready. */
	require(world_transition_coordinator_begin_travel(&coordinator, player,
		cave, "core.route.main.down") == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_destination_ready(&coordinator,
		"core.main.001", "stair.up", 1));
	require(streq(player->world_location.id, "core.hub"));
	dir = choose_direction(cave, player);
	require(dir >= 0);
	off_node_grid = loc_sum(player->grid, ddgrid[dir]);
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, off_node_grid);
	require(!world_transition_coordinator_commit(&coordinator, player));
	require(world_transition_coordinator_is_active(&coordinator));
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, node_grid);

	/* Should be all set up to save properly now */
	require(world_larder_donate(&player->larder,
		world_fishing_kind_by_id("mudbelly"), 2, &larder_report));
	eq(player->larder.food_points, 6);


	eq(savefile_save("Test1"), true);
	expected_random_after_save = Rand_div(UINT32_MAX);
	world_transition_coordinator_abort(&coordinator);

	/* Make sure it saved properly */
	eq(file_exists("Test1"), true);

	ok;
}

static int test_loadgame(void *state) {
	struct loc scan;
	int road_count = 0;
	int water_count = 0;

	reset_before_load();

	/* Try loading the just-saved game */
	eq(savefile_load("Test1", false), true);
	require(Rand_div(UINT32_MAX) == expected_random_after_save);

	eq(player->is_dead, false);
	notnull(cave);
	require(streq(player->world_location.id, "core.hub"));
	require(streq(player->world_location.entry, "default"));
	require(world_player_has_discovered_location(player, "core.hub"));
	eq(player->discovered_locations.count, 1);
	require(world_player_route_is_unlocked(player, "core.route.main.down"));
	eq(player->unlocked_routes.count, 1);
	require(world_player_has_activated_travel_node(player,
		"core.node.hub.main"));
	require(world_player_has_activated_travel_node(player,
		"core.node.main.001.up"));
	eq(player->activated_travel_nodes.count, 3);
	eq(player->larder.food_points, 6);
	eq(player->larder.milestone, WORLD_LARDER_MILESTONE_NONE);
	require(streq(cave->location_id, "core.hub"));
	require(!cave->is_known);
	require(streq(player->cave->location_id, "core.hub"));
	require(player->cave->is_known);
	for (scan.y = 0; scan.y < cave->height; scan.y++) {
		for (scan.x = 0; scan.x < cave->width; scan.x++) {
			if (square_isroad(cave, scan)) road_count++;
			if (square_iswater(cave, scan)) water_count++;
		}
	}
	require(road_count >= 2 * (cave->width - 2));
	require(water_count >= 40);
	eq(player->chp, player->mhp);
	eq(player->timed[TMD_FOOD], PY_FOOD_FULL - 1);

	ok;
}

static int test_stairs1(void *state) {
	struct loc entry_grid;

	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("TestPhysical", false), true);

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();
	require(!world_player_route_is_unlocked(player,
		"core.route.main.down"));
	require(!world_player_route_is_unlocked(player,
		"core.route.main.up"));
	require(world_player_begin_physical_route(player, cave,
		"core.main.001"));
	require(streq(player->upkeep->physical_route.route_id,
		"core.route.main.down"));
	world_player_cancel_physical_route(player);
	require(!player->upkeep->physical_route.route_id[0]);

	cmdq_push(CMD_GO_DOWN);
	run_game_loop();
	eq(player->depth, 1);
	require(streq(player->world_location.id, "core.main.001"));
	require(streq(player->world_location.entry, "default"));
	require(world_player_has_discovered_location(player, "core.hub"));
	require(world_player_has_discovered_location(player, "core.main.001"));
	eq(player->discovered_locations.count, 2);
	require(streq(cave->location_id, "core.main.001"));
	require(!cave->is_known);
	require(streq(player->cave->location_id, "core.main.001"));
	require(player->cave->is_known);
	require(!player->upkeep->physical_route.route_id[0]);
	require(world_entry_locate(cave, "stair.up", &entry_grid) ==
		WORLD_ENTRY_OK);
	require(world_entry_contains(cave, "stair.up", player->grid));
	require(world_player_has_activated_travel_node(player,
		"core.node.main.001.up"));
	require(world_player_route_is_unlocked(player,
		"core.route.main.down"));

	/* Routes are directed.  Descending does not teach the return journey;
	 * physically climbing back from and to matching declared stairs does. */
	require(!world_player_route_is_unlocked(player,
		"core.route.main.up"));
	cmdq_push(CMD_GO_UP);
	run_game_loop();
	eq(player->depth, 0);
	require(streq(player->world_location.id, "core.hub"));
	require(world_player_route_is_unlocked(player,
		"core.route.main.up"));
	require(!player->upkeep->physical_route.route_id[0]);
	require(world_player_has_activated_travel_node(player,
		"core.node.hub.main"));

	ok;
}
static int test_stairs2(void *state) {
	bool at_stairs = true;
	int dir;

	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("Test1", false), true);

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();

	dir = choose_direction(cave, player);
	if (dir >= 0) {
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction", dir);
		run_game_loop();
		if (!square_monster(cave, loc_sum(player->grid,
				ddgrid[reverse_direction(dir)]))) {
			cmdq_push(CMD_WALK);
			cmd_set_arg_direction(cmdq_peek(), "direction",
				reverse_direction(dir));
			run_game_loop();
		} else {
			/*
			 * A monster got in the way.  Skip testing if can go
			 * down the stairs and report the test was successful.
			 */
			at_stairs = false;
		}
	}
	if (at_stairs) {
		cmdq_push(CMD_GO_DOWN);
		run_game_loop();
		eq(player->depth, 1);
	}

	ok;
}

static int test_drop_pickup(void *state) {
	int dir;

	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("Test1", false), true);

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();

	dir = choose_direction(cave, player);
	if (dir >= 0) {
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction", dir);
		run_game_loop();
		if (player->upkeep->inven[0]->number > 1) {
			cmdq_push(CMD_DROP);
			cmd_set_arg_item(cmdq_peek(), "item",
				player->upkeep->inven[0]);
			cmd_set_arg_number(cmdq_peek(), "quantity", 1);
			run_game_loop();
			eq(square_object(cave, player->grid)->number, 1);
			cmdq_push(CMD_AUTOPICKUP);
			run_game_loop();
		}
		null(square_object(cave, player->grid));
	}

	ok;
}

static int test_drop_eat(void *state) {
	int num = 0;
	int dir;

	reset_before_load();

	/* Load the saved game */
	eq(savefile_load("Test1", false), true);
	num = player->upkeep->inven[0]->number;

	/* Perform normal set up after loading. */
	require(character_dungeon);
	on_new_level();

	dir = choose_direction(cave, player);
	if (dir >= 0) {
		cmdq_push(CMD_WALK);
		cmd_set_arg_direction(cmdq_peek(), "direction", dir);
		run_game_loop();
		cmdq_push(CMD_DROP);
		cmd_set_arg_item(cmdq_peek(), "item", player->upkeep->inven[0]);
		cmd_set_arg_number(cmdq_peek(), "quantity",
					   player->upkeep->inven[0]->number);
		run_game_loop();
		eq(square_object(cave, player->grid)->number, num);
		cmdq_push(CMD_EAT);
		cmd_set_arg_item(cmdq_peek(), "item",
					 square_object(cave, player->grid));
		run_game_loop();
		if (num > 1) {
			eq(square_object(cave, player->grid)->number, num - 1);
		} else {
			null(square_object(cave, player->grid));
		}
	}

	ok;
}

static int test_save_identity(void *state)
{
	static const uint8_t expected_header[8] = {
		'S', 'a', 'v', 'e', 'A', 'M', 'M', 'R'
	};
	uint8_t header[8];
	ang_file *stream;

	(void)state;
	stream = file_open("Test1", MODE_READ, FTYPE_SAVE);
	notnull(stream);
	eq(file_read(stream, (char *)header, sizeof(header)), sizeof(header));
	require(file_close(stream));
	require(memcmp(header, expected_header, sizeof(header)) == 0);
	ok;
}

static int test_store_backfill(void *state) {
	struct loc old_grid = loc(0, 0);
	struct loc scan;
	int supplier_count = 0;
	bool found = false;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (scan.y = 0; scan.y < cave->height && !found; scan.y++) {
		for (scan.x = 0; scan.x < cave->width; scan.x++) {
			if (square(cave, scan)->feat != FEAT_STORE_FISHING) continue;
			old_grid = scan;
			found = true;
			break;
		}
	}
	require(found);
	square_set_feat(cave, old_grid, FEAT_FLOOR);
	square_set_feat(player->cave, old_grid, FEAT_FLOOR);
	require(world_reconcile_location_services(cave, player->cave));
	for (scan.y = 0; scan.y < cave->height; scan.y++) {
		for (scan.x = 0; scan.x < cave->width; scan.x++) {
			if (square(cave, scan)->feat != FEAT_STORE_FISHING) continue;
			supplier_count++;
			require(square(player->cave, scan)->feat ==
				FEAT_STORE_FISHING);
			require(!loc_eq(scan, old_grid));
		}
	}
	eq(supplier_count, 1);
	ok;
}

/* A displayed ability rename must still resolve through the terrain helpers.
 * Exercise actual placement and removal, not just the new text lookup. */
static int test_standing_mark(void *state)
{
	struct trap_kind *mark;
	int dir;
	struct loc grid;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	dir = choose_direction(cave, player);
	require(dir > 0);
	grid = loc_sum(player->grid, ddgrid[dir]);
	mark = lookup_trap("standing mark");
	notnull(mark);
	require(!square_iswarded(cave, grid));
	square_add_glyph(cave, grid, GLYPH_WARDING);
	require(square_iswarded(cave, grid));
	square_remove_all_traps(cave, grid);
	require(!square_iswarded(cave, grid));
	ok;
}

const char *suite_name = "game/basic";
struct test tests[] = {
	{ "newgame", test_newgame },
	{ "save identity", test_save_identity },
	{ "loadgame", test_loadgame },
	{ "stairs1", test_stairs1 },
	{ "stairs2", test_stairs2 },
	{ "droppickup", test_drop_pickup },
	{ "dropeat", test_drop_eat },
	{ "store backfill", test_store_backfill },
	{ "standing mark", test_standing_mark },
	{ NULL, NULL }
};
