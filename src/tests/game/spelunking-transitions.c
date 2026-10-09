/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Cross-mode transitions, fishing links, and procedural publication. */

#include "spelunking-integration-fixture.h"
#include "ui-spelunking.h"

static bool generated_node_population_is_valid(
		const struct world_spelunk_system_node *node);

static void capture_resume_ambience(game_event_type type,
		game_event_data *data, void *user)
{
	(void)type;
	if (data->message.type >= MSG_AMBIENT_DAY &&
			data->message.type <= MSG_AMBIENT_DNG5) {
		*(int *)user = data->message.type;
	}
}

static int test_cave_status_snapshot(void *unused)
{
	struct ui_spelunking_status status;
	int air, maximum;
	(void)unused;
	reset_before_load();
	require(savefile_load("TestPhysical", false));
	require(!textui_spelunking_status(player, &status));
	require(!status.active);
	eq(cmdq_push(CMD_WIZ_VISIT_CAVE), 0);
	require(cmdq_pop(CTX_GAME));
	require(textui_spelunking_status(player, &status));
	require(status.active);
	require(strstr(status.location, "Upper Reaches"));
	eq(status.hp, player->chp);
	eq(status.max_hp, player->mhp);
	eq(status.stamina, player_stamina_current(player));
	eq(status.max_stamina, player_stamina_maximum(player));
	require(world_spelunk_player_air_status(player, &air, &maximum));
	eq(status.air, air);
	eq(status.max_air, maximum);
	player->spelunking->peeking = true;
	require(textui_spelunking_status(player, &status));
	require(streq(status.activity, "Peek / Standing"));
	player->spelunking->peeking = false;
	require(textui_spelunking_status(player, &status));
	require(streq(status.activity, "Standing"));
	require(!status.effort[0]);
	ok;
}

static int test_debug_cave_shortcut(void *state)
{
	struct world_larder_state larder;
	struct world_spelunking_exit_report report;
	uint32_t seed;
	int32_t experience;
	int weight;
	(void)state;
	reset_before_load();
	require(savefile_load("TestPhysical", false));
	larder = player->larder;
	experience = player->exp;
	weight = player->upkeep->total_weight;
	require(!(player->noscore & NOSCORE_DEBUG));
	/* A pending ordinary relocation must not be carried across the shortcut. */
	player->word_recall = 5;
	eq(cmdq_push(CMD_WIZ_VISIT_CAVE), 0);
	require(cmdq_pop(CTX_GAME));
	require(streq(player->world_location.id, "core.hub"));
	require(!(player->noscore & NOSCORE_DEBUG));
	player->word_recall = 0;
	eq(cmdq_push(CMD_WIZ_VISIT_CAVE), 0);
	require(cmdq_pop(CTX_GAME));
	require(player->noscore & NOSCORE_DEBUG);
	require(world_spelunk_player_is_active(player));
	require(streq(player->world_location.id, "core.spelunk.east.001"));
	notnull(player->spelunking_system);
	seed = player->spelunking_system->seed;
	eq(player->larder.food_points, larder.food_points);
	eq(player->larder.milestone, larder.milestone);
	eq(player->exp, experience);
	eq(player->upkeep->total_weight, weight);
	require(!world_player_route_is_unlocked(player,
		"core.route.underflow.out"));
	/* The shortcut must leave a real, normally escapable cave, and revisiting
	 * must reuse the existing cave system rather than erase its progress. */
	player->upkeep->energy_use = 0;
	eq(world_spelunking_exit_execute(player, cave, &report),
		WORLD_SPELUNKING_EXIT_OK);
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_WIZ_VISIT_CAVE), 0);
	require(cmdq_pop(CTX_GAME));
	require(world_spelunk_player_is_active(player));
	eq(player->spelunking_system->seed, seed);
	require(savefile_save("TestDebugCave"));
	reset_before_load();
	require(savefile_load("TestDebugCave", false));
	require(player->noscore & NOSCORE_DEBUG);
	require(world_spelunk_player_is_active(player));
	require(file_delete("TestDebugCave"));
	ok;
}

static int test_spelunking_transition_lifecycle(void *state)
{
	struct world_transition_coordinator coordinator = { 0 };
	struct world_destination_candidate candidate = { 0 };
	struct world_spelunking_entry_stage entry_stage = { 0 };
	struct world_spelunking_entry_report entry_report;
	struct world_spelunking_exit_report exit_report;
	enum world_destination_result stale_exit_result;
	struct world_retired_chunks retired = { 0 };
	struct world_spelunk_runtime *layout_a = NULL;
	struct world_spelunk_runtime *layout_b = NULL;
	struct world_spelunk_view exit_view;
	struct chunk *source_actual;
	struct chunk *source_known;
	const struct level *source_level;
	struct level *spelunk_level;
	const struct world_entry *source_entry;
	struct loc source_grid;
	uint16_t initial_chunk_count;
	int16_t saved_food;
	size_t cell_count;
	uint8_t exit_view_cells[48 * 30];
	uint32_t generated_seed;
	int entrance_x;
	int entrance_y;
	int away_x;
	int away_y;
	bool away_is_portal;
	int speed_before;
	struct object_kind *yew_kind;
	struct object *authored_object;
	struct object *known_object;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	source_level = level_by_id("core.cave.east.001");
	spelunk_level = level_by_id("core.spelunk.east.001");
	notnull(source_level);
	notnull(spelunk_level);
	require(source_level->mode == WORLD_MODE_TOP_DOWN);
	require(spelunk_level->mode == WORLD_MODE_SPELUNKING);
	source_entry = world_entry_by_id(source_level, "shaft.spelunk");
	notnull(source_entry);
	require(source_entry->kind == WORLD_ENTRY_GRID);

	/* The first authored layout is deterministic and its named arrival is a
	 * valid grounded side-view cell. */
	require(world_spelunk_layout_create(spelunk_level, "shaft.surface",
		z_info->move_energy, &layout_a) == WORLD_SPELUNK_LAYOUT_OK);
	require(world_spelunk_layout_create(spelunk_level, "shaft.surface",
		z_info->move_energy, &layout_b) == WORLD_SPELUNK_LAYOUT_OK);
	notnull(layout_a);
	notnull(layout_b);
	require(world_spelunk_runtime_is_valid(layout_a));
	require(world_spelunk_runtime_is_valid(layout_b));
	yew_kind = lookup_kind(tval_find_idx("tool"),
		lookup_sval(tval_find_idx("tool"), "Yew Longline Rod"));
	notnull(yew_kind);
	eq(world_spelunk_runtime_ground_object_count(layout_a), 1);
	authored_object = world_spelunk_runtime_ground_object_at(layout_a,
		25, 28);
	notnull(authored_object);
	ptreq(authored_object->kind, yew_kind);
	eq(layout_a->layout_revision,
		world_spelunk_layout_definition_by_id(spelunk_level->id)->
			current_revision);
	eq(layout_a->cells[20 * layout_a->state.map.width + 31],
		WORLD_SPELUNK_WATER);
	/* A retained pre-water runtime receives the authored pool once without
	 * replacing the rest of its mutable terrain. */
	for (i = 0; i < layout_b->state.map.width *
			layout_b->state.map.height; i++) {
		if (layout_b->cells[i] == WORLD_SPELUNK_WATER) {
			layout_b->cells[i] = WORLD_SPELUNK_AIR;
		}
	}
	/* Strip the new-game placement to model an old save before upgrading. */
	authored_object = world_spelunk_runtime_ground_object_at(layout_b,
		25, 28);
	notnull(authored_object);
	require(world_spelunk_runtime_take_ground_object(layout_b,
		authored_object));
	known_object = authored_object->known;
	authored_object->known = NULL;
	object_delete(NULL, NULL, &known_object);
	object_delete(NULL, NULL, &authored_object);
	layout_b->layout_revision = 0;
	require(world_spelunk_runtime_is_valid(layout_b));
	require(world_spelunk_layout_upgrade_runtime(layout_b));
	eq(layout_b->cells[20 * layout_b->state.map.width + 31],
		WORLD_SPELUNK_WATER);
	eq(world_spelunk_runtime_ground_object_count(layout_b), 1);
	authored_object = world_spelunk_runtime_ground_object_at(layout_b,
		25, 28);
	notnull(authored_object);
	ptreq(authored_object->kind, yew_kind);
	/* Once revision 2 is recorded, removing the discovery is permanent. */
	require(world_spelunk_runtime_take_ground_object(layout_b,
		authored_object));
	known_object = authored_object->known;
	authored_object->known = NULL;
	object_delete(NULL, NULL, &known_object);
	object_delete(NULL, NULL, &authored_object);
	require(world_spelunk_layout_upgrade_runtime(layout_b));
	eq(world_spelunk_runtime_ground_object_count(layout_b), 0);
	eq(layout_a->state.map.width, 48);
	eq(layout_a->state.map.height, 30);
	eq(layout_a->state.x, 4);
	eq(layout_a->state.y, 4);
	require(world_spelunk_is_grounded(&layout_a->state));
	cell_count = (size_t)layout_a->state.map.width *
		(size_t)layout_a->state.map.height;
	require(memcmp(layout_a->cells, layout_b->cells,
		cell_count * sizeof(*layout_a->cells)) == 0);
	world_spelunk_runtime_free(layout_a);
	world_spelunk_runtime_free(layout_b);
	layout_a = NULL;
	layout_b = NULL;
	require(world_spelunk_layout_create(spelunk_level, "shaft.practice",
		z_info->move_energy, &layout_a) == WORLD_SPELUNK_LAYOUT_OK);
	notnull(layout_a);
	eq(layout_a->state.x, 8);
	eq(layout_a->state.y, 4);
	require(world_spelunk_is_grounded(&layout_a->state));
	world_spelunk_runtime_free(layout_a);
	layout_a = NULL;
	require(world_spelunk_layout_create(spelunk_level, "missing",
		z_info->move_energy, &layout_a) ==
		WORLD_SPELUNK_LAYOUT_INVALID_ENTRY);
	null(layout_a);

	/* Re-label the loaded test fixture as the authored top-down source and put
	 * its sole player marker on the dormant shaft coordinate. */
	source_actual = cave;
	source_known = player->cave;
	source_actual->depth = source_level->danger;
	source_known->depth = source_level->danger;
	require(world_chunk_set_location(source_actual, source_level->id, false));
	require(world_chunk_set_location(source_known, source_level->id, true));
	require(world_player_set_location(player, source_level->id,
		"shaft.spelunk"));
	square_set_mon(source_actual, player->grid, 0);
	square_set_feat(source_actual, source_entry->grid, FEAT_HOLE);
	square_set_feat(source_known, source_entry->grid, FEAT_FLOOR);
	player_place(source_actual, player, source_entry->grid);
	source_actual = cave;
	source_known = player->cave;
	source_grid = player->grid;
	initial_chunk_count = chunk_list_max;

	/* A complete off-world entry can still reject immediately before commit.
	 * Aborting releases both the envelope and staged generated system, while
	 * the source pair, marker, position, clock, and stored-list ownership stay
	 * unchanged. */
	require(world_transition_coordinator_begin_cross_mode(&coordinator, player,
		cave, "core.route.spelunk.east.in") == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_spelunking_entry_prepare(&entry_stage, &candidate,
		&coordinator, player) == WORLD_DESTINATION_OK);
	require(entry_stage.runtime && entry_stage.system &&
		entry_stage.owns_system && !entry_stage.owns_runtime &&
		!entry_stage.committed);
	require(entry_stage.system->graph_version ==
		WORLD_SPELUNK_SYSTEM_GRAPH_VERSION);
	require(streq(entry_stage.system->id, "deepwell"));
	require(entry_stage.system->node_count >= 6 &&
		entry_stage.system->node_count <= 9);
	generated_seed = entry_stage.system->seed;
	entrance_x = entry_stage.arrival_x;
	entrance_y = entry_stage.arrival_y;
	require(candidate.actual && candidate.known);
	eq(candidate.actual->width, 3);
	eq(candidate.actual->height, 3);
	require(!chunk_find(candidate.actual));
	require(!chunk_find(candidate.known));
	saved_food = player->timed[TMD_FOOD];
	player->timed[TMD_FOOD]++;
	require(world_spelunking_entry_publish(&entry_stage, &candidate,
		&coordinator, player) == WORLD_DESTINATION_RESOURCES_STALE);
	player->timed[TMD_FOOD] = saved_food;
	require(cave == source_actual);
	require(player->cave == source_known);
	require(loc_eq(player->grid, source_grid));
	require(square(source_actual, source_grid)->mon == -1);
	eq(chunk_list_max, initial_chunk_count);
	null(player->spelunking);
	require(world_spelunking_entry_abort(&entry_stage, &candidate,
		&coordinator, player));
	require(!world_transition_coordinator_is_active(&coordinator));
	null(entry_stage.runtime);
	null(entry_stage.system);
	require(!entry_stage.owns_runtime);
	require(!entry_stage.owns_system);

	/* The same preparation commits once: the real top-down source is stored,
	 * the tiny envelope becomes the active compatibility pair, and only then
	 * is the generated system transferred to the player. */
	require(world_transition_coordinator_begin_cross_mode(&coordinator, player,
		cave, "core.route.spelunk.east.in") == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_spelunking_entry_prepare(&entry_stage, &candidate,
		&coordinator, player) == WORLD_DESTINATION_OK);
	eq(entry_stage.system->seed, generated_seed);
	eq(entry_stage.arrival_x, entrance_x);
	eq(entry_stage.arrival_y, entrance_y);
	require(world_spelunking_entry_publish(&entry_stage, &candidate,
		&coordinator, player) == WORLD_DESTINATION_OK);
	require(entry_stage.committed && !entry_stage.owns_runtime &&
		!entry_stage.owns_system);
	require(player->spelunking == entry_stage.runtime);
	require(player->spelunking_system == entry_stage.system);
	require(player->spelunking_system->graph_version ==
		WORLD_SPELUNK_SYSTEM_GRAPH_VERSION);
	require(world_spelunk_player_is_active(player));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_SPELUNKING);
	require(streq(player->world_location.id, "core.spelunk.east.001"));
	require(streq(player->world_location.entry, "shaft.surface"));
	require(world_player_route_is_unlocked(player,
		"core.route.spelunk.east.in"));
	require(player->upkeep->autosave);
	eq(cave->width, 3);
	eq(cave->height, 3);
	require(loc_eq(player->grid, loc(1, 1)));
	require(square(cave, player->grid)->mon == -1);
	require(chunk_find_location("core.cave.east.001", false) == source_actual);
	require(chunk_find_location("core.cave.east.001", true) == source_known);
	eq(chunk_list_max, initial_chunk_count + 2);
	world_spelunking_entry_finish(&entry_stage);
	null(entry_stage.runtime);
	null(entry_stage.system);

	/* The generated graph, active side-view payload and compatibility envelope
	 * survive a normal save. */
	player->timed[TMD_SLOW] = 0;
	player->upkeep->update |= PU_BONUS;
	update_stuff(player);
	speed_before = player->state.speed;
	require(speed_before > 0);
	require(savefile_save("TestSpelunkActive"));
	reset_before_load();
	require(savefile_load("TestSpelunkActive", false));
	require(world_spelunk_player_is_active(player));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_SPELUNKING);
	player->upkeep->autosave = false;
	{
		int ambience = -1;
		bool entered;
		player->opts.opt[OPT_use_sound] = true;
		event_add_handler(EVENT_SOUND, capture_resume_ambience, &ambience);
		entered = on_enter_world_location();
		event_remove_handler(EVENT_SOUND, capture_resume_ambience, &ambience);
		require(entered);
		eq(ambience, MSG_AMBIENT_DNG1);
	}
	require(!player->upkeep->autosave);
	eq(player->timed[TMD_SLOW], 0);
	eq(player->state.speed, speed_before);
	eq(cave->width, 3);
	eq(cave->height, 3);
	require(loc_eq(player->grid, loc(1, 1)));
	require(player->spelunking_system->graph_version ==
		WORLD_SPELUNK_SYSTEM_GRAPH_VERSION);
	eq(player->spelunking_system->seed, generated_seed);
	require(player->spelunking->state.x == entrance_x);
	require(player->spelunking->state.y == entrance_y);
	notnull(chunk_find_location("core.cave.east.001", false));
	notnull(chunk_find_location("core.cave.east.001", true));
	away_x = -1;
	away_y = -1;
	for (i = 0; i < player->spelunking->state.map.width *
			(player->spelunking->state.map.height - 1); i++) {
		int x = i % player->spelunking->state.map.width;
		int y = i / player->spelunking->state.map.width;
		struct loc portal_grid;
		size_t portal_index;

		away_is_portal = false;
		for (portal_index = 0; world_spelunking_exit_grid_at(
				player->spelunking_system, player->spelunking,
				portal_index, &portal_grid); portal_index++) {
			if (portal_grid.x == x && portal_grid.y == y) {
				away_is_portal = true;
				break;
			}
		}

		if ((x != entrance_x || y != entrance_y) &&
				!away_is_portal &&
				player->spelunking->cells[i] == WORLD_SPELUNK_AIR &&
				player->spelunking->cells[i +
					player->spelunking->state.map.width] ==
					WORLD_SPELUNK_ROCK) {
			away_x = x;
			away_y = y;
			break;
		}
	}
	require(away_x >= 0 && away_y >= 0);

	/* A shaft ticket also snapshots the authoritative side-view coordinate.
	 * Moving after preparation rejects publication without retiring either
	 * active or stored state. */
	require(world_transition_coordinator_begin_cross_mode(&coordinator, player,
		cave, "core.route.spelunk.east.out") == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_destination_prepare_stored(&candidate, &coordinator) ==
		WORLD_DESTINATION_OK);
	require(world_destination_candidate_resolve_arrival(&candidate,
		&coordinator) == WORLD_DESTINATION_OK);
	player->spelunking->state.x = away_x;
	player->spelunking->state.y = away_y;
	player->spelunking->state.fall_start_y = away_y;
	stale_exit_result = world_spelunking_exit_publish(&candidate, &coordinator,
		player, &retired);
	eq(stale_exit_result, WORLD_DESTINATION_COMMIT_REJECTED);
	player->spelunking->state.x = entrance_x;
	player->spelunking->state.y = entrance_y;
	player->spelunking->state.fall_start_y = entrance_y;
	require(world_destination_candidate_abort(&candidate, &coordinator,
		player));
	null(retired.actual);
	null(retired.known);
	require(world_spelunk_player_is_active(player));

	/* The player-facing action refuses another stable cell without spending a
	 * turn, then returns from the exact generated portal in one transaction. */
	player->spelunking->state.x = away_x;
	player->spelunking->state.y = away_y;
	player->spelunking->state.fall_start_y = away_y;
	require(world_spelunk_view_capture_at_system(player->spelunking_system,
		player->spelunking, entrance_x, entrance_y, 48, 30,
		exit_view_cells, N_ELEMENTS(exit_view_cells), &exit_view));
	eq(world_spelunk_view_cell_at(&exit_view,
		entrance_x - exit_view.source_x, entrance_y - exit_view.source_y),
		WORLD_SPELUNK_VIEW_EXIT);
	require(world_spelunking_exit_execute(player, cave, &exit_report) ==
		WORLD_SPELUNKING_EXIT_NOT_HERE);
	eq(exit_report.result, WORLD_SPELUNKING_EXIT_NOT_HERE);
	eq(player->upkeep->energy_use, 0);
	require(world_spelunk_player_is_active(player));
	eq(cmdq_push_spelunk_exit(), 0);
	require(cmdq_pop(CTX_GAME));
	eq(player->upkeep->energy_use, 0);
	require(world_spelunk_player_is_active(player));
	player->spelunking->state.x = entrance_x;
	player->spelunking->state.y = entrance_y;
	player->spelunking->state.fall_start_y = entrance_y;
	require(world_spelunking_exit_execute(player, cave, &exit_report) ==
		WORLD_SPELUNKING_EXIT_OK);
	eq(exit_report.result, WORLD_SPELUNKING_EXIT_OK);
	eq(exit_report.transition, WORLD_TRANSITION_OK);
	eq(exit_report.resources, WORLD_TRAVEL_RESOURCES_OK);
	eq(exit_report.destination, WORLD_DESTINATION_OK);
	eq(player->upkeep->energy_use, z_info->move_energy);
	require(streq(player->world_location.id, "core.cave.east.001"));
	require(streq(cave->location_id, "core.cave.east.001"));
	require(streq(player->cave->location_id, "core.cave.east.001"));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_TOP_DOWN);
	require(player->spelunking &&
		world_spelunk_runtime_is_valid(player->spelunking));
	require(!world_spelunk_player_is_active(player));
	require(world_player_route_is_unlocked(player,
		"core.route.spelunk.east.out"));
	eq(chunk_list_max, initial_chunk_count);
	player->upkeep->energy_use = 0;

	/* Dormant runtime persistence does not change ordinary top-down startup. */
	/* A development save may still carry the retired shortcut's portal. */
	require(world_spelunk_system_add_portal(player->spelunking_system,
		"deepwell.practice", "shaft.practice",
		player->spelunking_system->nodes[0].id, true));
	require(savefile_save("TestSpelunkReturned"));
	reset_before_load();
	require(savefile_load("TestSpelunkReturned", false));
	null(world_spelunk_system_portal_by_entry(player->spelunking_system,
		"shaft.practice"));
	notnull(world_spelunk_system_portal_by_entry(player->spelunking_system,
		"shaft.surface"));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_TOP_DOWN);
	require(player->spelunking &&
		world_spelunk_runtime_is_valid(player->spelunking));
	require(!world_spelunk_player_is_active(player));

	/* Re-entry preparation borrows the exact dormant runtime; aborting that
	 * attempt cannot delete or regenerate the saved side-view state. */
	require(world_transition_coordinator_begin_cross_mode(&coordinator, player,
		cave, "core.route.spelunk.east.in") == WORLD_TRANSITION_OK);
	require(world_transition_coordinator_stage_clock(&coordinator));
	require(world_transition_coordinator_stage_resources(&coordinator, player) ==
		WORLD_TRAVEL_RESOURCES_OK);
	require(world_spelunking_entry_prepare(&entry_stage, &candidate,
		&coordinator, player) == WORLD_DESTINATION_OK);
	require(entry_stage.runtime == player->spelunking);
	require(entry_stage.system == player->spelunking_system);
	require(!entry_stage.owns_runtime && !entry_stage.owns_system &&
		!entry_stage.committed);
	require(world_spelunking_entry_abort(&entry_stage, &candidate,
		&coordinator, player));
	require(player->spelunking && player->spelunking->state.x == entrance_x &&
		player->spelunking->state.y == entrance_y);

	/* The symmetric player-facing entry action reuses that exact dormant
	 * runtime, commits one turn, and retains the top-down source for return. */
	require(world_entry_cross_mode_route_here(cave, player->grid) ==
		world_route_by_id("core.route.spelunk.east.in"));
	require(world_spelunking_entry_execute(player, cave,
		"core.route.spelunk.east.in", &entry_report) ==
		WORLD_SPELUNKING_ENTRY_OK);
	eq(entry_report.result, WORLD_SPELUNKING_ENTRY_OK);
	eq(entry_report.transition, WORLD_TRANSITION_OK);
	eq(entry_report.resources, WORLD_TRAVEL_RESOURCES_OK);
	eq(entry_report.destination, WORLD_DESTINATION_OK);
	eq(player->upkeep->energy_use, z_info->move_energy);
	require(world_spelunk_player_is_active(player));
	require(player->spelunking->state.x == entrance_x &&
		player->spelunking->state.y == entrance_y);
	player->upkeep->energy_use = 0;
	require(world_spelunking_exit_execute(player, cave, &exit_report) ==
		WORLD_SPELUNKING_EXIT_OK);
	require(!world_spelunk_player_is_active(player));

	/* The ordinary '>' command recognizes the authored hole and takes the
	 * same transactional route, rather than falling through to Angband's
	 * staircase error. */
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_GO_DOWN), 0);
	require(cmdq_pop(CTX_GAME));
	require(world_spelunk_player_is_active(player));
	eq(player->upkeep->energy_use, z_info->move_energy);
	player->upkeep->energy_use = 0;
	require(world_spelunking_exit_execute(player, cave, &exit_report) ==
		WORLD_SPELUNKING_EXIT_OK);
	require(!world_spelunk_player_is_active(player));

	/* The optional Underflow must also work before Meridian 5 has ever been
	 * generated.  Materialize the planned cave, sound the route, cross into a
	 * newly generated destination, and immediately take the reciprocal shaft. */
	reset_before_load();
	require(savefile_load("TestSpelunkActive", false));
	require(world_spelunk_player_is_active(player));
	{
		struct world_spelunk_system *system = player->spelunking_system;
		const struct world_spelunk_system_portal *underflow;
		struct world_fishing_runtime fishing = { 0 };
		struct world_fishing_report catch_report = { 0 };
		world_fishing_kind glassfin_kind =
			world_fishing_kind_by_id("glassfin");
		const struct world_entry *return_entry;
		const char *message = NULL;
		size_t edge_index;

		notnull(system);
		for (edge_index = 0; edge_index < system->edge_count; edge_index++) {
			const struct world_spelunk_system_edge *edge =
				&system->edges[edge_index];
			const struct world_spelunk_system_node *child =
				world_spelunk_system_node_by_id(system, edge->node_b_id);

			notnull(child);
			if (!child->runtime) {
				eq(world_spelunk_materialize_node_candidate(system,
					child->id, z_info->move_energy, edge->endpoint_b_id),
					WORLD_SPELUNK_PUBLICATION_OK);
			}
			require(world_spelunk_system_discover_edge(system, edge->id));
		}
		underflow = world_spelunk_system_portal_by_id(system,
			"deepwell.underflow");
		notnull(underflow);
		require(underflow->materialized && !underflow->enabled);
		require(world_spelunk_system_set_active_node(system,
			underflow->node_id));
		require(player_spelunking_set_system(player, system));
		my_strcpy(fishing.rig_id, "yew-longline-rod",
			sizeof(fishing.rig_id));
		fishing.habitat = WORLD_FISHING_HABITAT_CAVE_POOL;
		catch_report.landed_kind = glassfin_kind;
		catch_report.landed_depth = WORLD_FISHING_DEPTH_SCALE;
		eq(world_fishing_apply_discovery(player, &fishing, &catch_report,
			&message), WORLD_FISHING_DISCOVERY_UNLOCKED);
		notnull(message);
		player->spelunking->state.x = underflow->x;
		player->spelunking->state.y = underflow->y;
		player->spelunking->state.fall_start_y = underflow->y;
		player->spelunking->state.movement = WORLD_SPELUNK_STANDING;
		player->upkeep->energy_use = 0;
		null(chunk_find_location("core.main.005", false));
		eq(world_spelunking_exit_execute(player, cave, &exit_report),
			WORLD_SPELUNKING_EXIT_OK);
		require(streq(exit_report.route_id,
			"core.route.underflow.out"));
		require(streq(player->world_location.id, "core.main.005"));
		require(!world_spelunk_player_is_active(player));
		return_entry = world_entry_by_id(world_player_level(player),
			"shaft.underflow");
		notnull(return_entry);
		require(world_entry_contains(cave, return_entry->id, player->grid));
		eq(square(cave, player->grid)->feat, FEAT_HOLE);

		player->upkeep->energy_use = 0;
		eq(world_spelunking_entry_execute(player, cave,
			"core.route.underflow.in", &entry_report),
			WORLD_SPELUNKING_ENTRY_OK);
		require(world_spelunk_player_is_active(player));
		require(streq(player->spelunking_system->active_node_id,
			underflow->node_id));
		eq(player->spelunking->state.x, underflow->x);
		eq(player->spelunking->state.y, underflow->y);

		/* Recall from a deep generated section follows the cave's unique
		 * authored hub route without pretending its physical portal was used or
		 * discovered. */
		require(world_route_is_retired(world_route_by_id(
			"core.route.spelunk.practice.out")));
		require(!world_player_route_is_unlocked(player,
			"core.route.spelunk.recall"));
		player->word_recall = 1;
		player->energy = z_info->move_energy;
		player->upkeep->energy_use = 0;
		turn = 10;
		eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
		run_game_loop();
		eq(player->word_recall, 0);
		require(streq(player->world_location.id, "core.hub"));
		require(world_turn_context_for_player(player) ==
			WORLD_TURN_CONTEXT_TOP_DOWN);
		require(!world_player_route_is_unlocked(player,
			"core.route.spelunk.recall"));
	}

	/* Restore the shared fixture after the test-only source relabelling. */
	reset_before_load();
	require(savefile_load("Test1", false));
	null(player->spelunking);
	ok;
}

static int test_generated_player_passages(void *state)
{
	struct world_spelunk_system *system;
	const struct world_spelunk_system_edge *edge = NULL;
	struct world_spelunk_system_node *child;
	const struct world_spelunk_system_portal *portal;
	struct world_spelunk_player_passage_report report;
	struct world_spelunk_inspect inspect;
	struct randomizer_state gameplay_before;
	struct randomizer_state gameplay_after;
	char edge_id[WORLD_ID_LEN];
	char root_id[WORLD_ID_LEN];
	char child_id[WORLD_ID_LEN];
	char child_recipe_id[WORLD_ID_LEN];
	int saved_stamina;
	size_t i;

	(void)state;
	reset_before_load();
	require(savefile_load("TestSpelunkActive", false));
	require(world_spelunk_player_is_active(player));
	system = player->spelunking_system;
	notnull(system);
	require(system->graph_version == WORLD_SPELUNK_SYSTEM_GRAPH_VERSION);
	my_strcpy(root_id, system->active_node_id, sizeof(root_id));
	for (i = 0; i < system->edge_count; i++) {
		if (streq(system->edges[i].node_a_id, root_id)) {
			edge = &system->edges[i];
			break;
		}
	}
	notnull(edge);
	require(edge->endpoint_a_materialized);
	require(!edge->endpoint_b_materialized);
	my_strcpy(edge_id, edge->id, sizeof(edge_id));
	my_strcpy(child_id, edge->node_b_id, sizeof(child_id));
	child = world_spelunk_system_node_by_id_mutable(system, child_id);
	notnull(child);
	null(child->runtime);
	my_strcpy(child_recipe_id, child->recipe_id, sizeof(child_recipe_id));

	/* Passage direction is semantic view data. Look resolves the underlying
	 * passage once the stable geometry has been explored; player priority owns
	 * that same endpoint when the player stands on it. */
	world_spelunk_visibility_reveal_layout(player->spelunking);
	require(world_spelunk_view_inspect_system(system, player->spelunking,
		edge->endpoint_a_x, edge->endpoint_a_y, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_PASSAGE_DOWN);
	player->spelunking->state.x = edge->endpoint_a_x;
	player->spelunking->state.y = edge->endpoint_a_y;
	player->spelunking->state.fall_start_y = edge->endpoint_a_y;
	player->spelunking->state.grip_target_x = -1;
	player->spelunking->state.grip_target_y = -1;
	player->spelunking->state.movement = WORLD_SPELUNK_STANDING;
	player->spelunking->state.jump_holding = false;
	world_spelunk_visibility_follow_player(player->spelunking);
	require(world_spelunk_view_inspect_system(system, player->spelunking,
		edge->endpoint_a_x, edge->endpoint_a_y, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_PLAYER);

	/* A failed first-visit recipe leaves the graph, alias, energy and gameplay
	 * RNG unchanged. This is the player-level rollback contract. */
	my_strcpy(child->recipe_id, "missing.recipe", sizeof(child->recipe_id));
	Rand_state_export(&gameplay_before);
	eq(world_spelunk_player_use_passage(player,
		WORLD_SPELUNK_PASSAGE_DOWN, &report),
		WORLD_SPELUNK_PLAYER_PASSAGE_GENERATION_FAILED);
	eq(report.publication, WORLD_SPELUNK_PUBLICATION_PLAN_FAILED);
	require(!report.first_visit);
	Rand_state_export(&gameplay_after);
	require(memcmp(&gameplay_before, &gameplay_after,
		sizeof(gameplay_before)) == 0);
	null(child->runtime);
	require(!edge->endpoint_b_materialized && !edge->discovered);
	require(streq(system->active_node_id, root_id));
	require(player->spelunking == world_spelunk_system_active_runtime(system));
	eq(player->upkeep->energy_use, 0);
	my_strcpy(child->recipe_id, child_recipe_id, sizeof(child->recipe_id));
	require(world_spelunk_system_is_valid(system));

	/* The real player action materializes the child, traverses to its safe
	 * reciprocal endpoint and spends exactly one ordinary action. */
	player->upkeep->autosave = false;
	Rand_state_export(&gameplay_before);
	eq(world_spelunk_player_use_passage(player,
		WORLD_SPELUNK_PASSAGE_DOWN, &report),
		WORLD_SPELUNK_PLAYER_PASSAGE_OK);
	require(report.first_visit);
	Rand_state_export(&gameplay_after);
	require(memcmp(&gameplay_before, &gameplay_after,
		sizeof(gameplay_before)) == 0);
	child = world_spelunk_system_node_by_id_mutable(system, child_id);
	notnull(child);
	notnull(child->runtime);
	require(streq(system->active_node_id, child_id));
	require(player->spelunking == child->runtime);
	require(edge->endpoint_b_materialized && edge->discovered);
	eq(player->spelunking->state.x, edge->endpoint_b_x);
	eq(player->spelunking->state.y, edge->endpoint_b_y);
	eq(player->upkeep->energy_use, z_info->move_energy);
	require(player->upkeep->autosave);
	require(generated_node_population_is_valid(child));

	/* Saving away from the root retains both the active alias and mutable
	 * section state. The reciprocal command returns to the exact old root. */
	player->upkeep->energy_use = 0;
	require(player->spelunking->state.stamina > 0);
	require(player_stamina_spend(player, 1));
	saved_stamina = player_stamina_current(player);
	require(savefile_save("TestSpelunkChild"));
	reset_before_load();
	require(savefile_load("TestSpelunkChild", false));
	require(world_spelunk_player_is_active(player));
	system = player->spelunking_system;
	require(streq(system->active_node_id, child_id));
	child = world_spelunk_system_node_by_id_mutable(system, child_id);
	notnull(child);
	require(player->spelunking == child->runtime);
	eq(player->spelunking->state.stamina, saved_stamina);
	edge = world_spelunk_system_edge_by_id(system, edge_id);
	notnull(edge);
	player->upkeep->energy_use = 0;
	eq(cmdq_push_spelunk_passage(WORLD_SPELUNK_PASSAGE_UP), 0);
	require(cmdq_pop(CTX_GAME));
	require(streq(system->active_node_id, root_id));
	require(player->spelunking == world_spelunk_system_active_runtime(system));
	eq(player->spelunking->state.x, edge->endpoint_a_x);
	eq(player->spelunking->state.y, edge->endpoint_a_y);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* A revisited child is reused rather than regenerated. */
	player->upkeep->energy_use = 0;
	eq(cmdq_push_spelunk_passage(WORLD_SPELUNK_PASSAGE_DOWN), 0);
	require(cmdq_pop(CTX_GAME));
	require(streq(system->active_node_id, child_id));
	require(player->spelunking == child->runtime);
	eq(player->spelunking->state.stamina, saved_stamina);
	player->upkeep->energy_use = 0;
	eq(world_spelunk_player_use_passage(player,
		WORLD_SPELUNK_PASSAGE_UP, &report),
		WORLD_SPELUNK_PLAYER_PASSAGE_OK);
	require(!report.first_visit);
	require(streq(system->active_node_id, root_id));

	/* At the root, the same '<' command falls through to the exact external
	 * portal instead of trapping the player in a cave-only command loop. */
	portal = world_spelunk_system_portal_by_entry(system,
		player->world_location.entry);
	notnull(portal);
	require(streq(portal->node_id, root_id) && portal->materialized);
	player->spelunking->state.x = portal->x;
	player->spelunking->state.y = portal->y;
	player->spelunking->state.fall_start_y = portal->y;
	player->spelunking->state.grip_target_x = -1;
	player->spelunking->state.grip_target_y = -1;
	player->spelunking->state.movement = WORLD_SPELUNK_STANDING;
	player->spelunking->state.jump_holding = false;
	world_spelunk_visibility_follow_player(player->spelunking);
	player->upkeep->energy_use = 0;
	eq(cmdq_push_spelunk_passage(WORLD_SPELUNK_PASSAGE_UP), 0);
	require(cmdq_pop(CTX_GAME));
	require(!world_spelunk_player_is_active(player));
	require(streq(player->world_location.id, "core.cave.east.001"));
	eq(player->upkeep->energy_use, z_info->move_energy);
	reset_before_load();
	require(savefile_load("Test1", false));
	ok;
}

static int test_spelunking_fishing_site(void *state)
{
	enum world_spelunk_tile cells[36];
	struct world_fishing_site site;
	struct world_fishing_fish *fish;
	struct object_kind *yew_kind;
	struct object_kind *mudbelly_kind;
	struct object_kind *glassfin_kind;
	struct object *obj;
	struct object *dropped;
	struct level *test_level;
	world_fishing_kind glassfin;
	int food_tval;
	int tool_tval;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	cells[4 * 6 + 3] = WORLD_SPELUNK_WATER;
	require(install_spelunking_runtime(cells, 6, 6, 2, 4, 90));
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	test_level->fishing_habitat = WORLD_FISHING_HABITAT_CAVE_POOL;
	require(world_spelunk_player_is_active(player));
	require(world_player_mode_has_capability(player,
		WORLD_MODE_CAP_FISHING));

	/* The site dispatcher reads only the side-view runtime.  A null ordinary
	 * cave proves the compatibility envelope cannot provide water or position. */
	require(world_fishing_site_query(player, NULL, &site) ==
		WORLD_FISHING_SITE_OK);
	require(loc_eq(site.origin, loc(2, 4)));
	eq(site.habitat, WORLD_FISHING_HABITAT_CAVE_POOL);
	eq(site.action_energy, 100);
	require(world_fishing_site_matches(player, NULL, &site));
	player->spelunking->state.x = 3;
	player->spelunking->state.movement = WORLD_SPELUNK_SWIMMING;
	require(world_fishing_site_query(player, NULL, &site) ==
		WORLD_FISHING_SITE_UNSTABLE);
	player->spelunking->state.x = 2;
	player->spelunking->state.movement = WORLD_SPELUNK_STANDING;

	/* A full pack uses the same mode-owned local object path rather than
	 * dropping the catch into the inert Angband chunk. */
	food_tval = tval_find_idx("food");
	mudbelly_kind = lookup_kind(food_tval,
		lookup_sval(food_tval, "Mudbelly"));
	notnull(mudbelly_kind);
	obj = make_test_object(mudbelly_kind, 1);
	notnull(obj);
	require(world_fishing_site_drop_catch(player, NULL, &obj));
	null(obj);
	dropped = world_spelunk_runtime_ground_object_at(player->spelunking, 2, 4);
	notnull(dropped);
	ptreq(dropped->kind, mudbelly_kind);

	/* Starting and completing a deterministic deep catch uses normal command,
	 * inventory, scheduler-energy, habitat, and data-authored rig authorities. */
	tool_tval = tval_find_idx("tool");
	yew_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Yew Longline Rod"));
	glassfin_kind = lookup_kind(food_tval,
		lookup_sval(food_tval, "Glassfin"));
	glassfin = world_fishing_kind_by_id("glassfin");
	notnull(yew_kind);
	notnull(glassfin_kind);
	require(glassfin != WORLD_FISHING_KIND_NONE);
	obj = make_test_object(yew_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_FISHING_START), 0);
	require(cmdq_pop(CTX_GAME));
	notnull(player->fishing);
	require(loc_eq(player->fishing->origin, loc(2, 4)));
	eq(player->fishing->habitat, WORLD_FISHING_HABITAT_CAVE_POOL);
	eq(player->fishing->maximum_depth, 16);
	player->fishing->fish_count = 1;
	player->fishing->depth = 9;
	player->fishing->fish_move_chance_percent = 0;
	player->fishing->fish_depth_move_chance_percent = 0;
	player->fishing->moving_hook_evasion_chance_percent = 0;
	memset(&player->fishing->fish[0], 0,
		sizeof(player->fishing->fish[0]));
	fish = &player->fishing->fish[0];
	fish->kind = glassfin;
	fish->col = world_fishing_hook_col(player->fishing);
	fish->row = 9;
	fish->depth_min = 9;
	fish->depth_max = 16;
	fish->wind_needed = 1;
	fish->bite_timeout = 3;
	require(world_fishing_runtime_is_valid(player->fishing));
	player->upkeep->energy_use = 0;
	eq(cmdq_push_fishing_action(WORLD_FISHING_WAIT), 0);
	require(cmdq_pop(CTX_GAME));
	eq(player->fishing->phase, WORLD_FISHING_BITE);
	eq(player->upkeep->energy_use, 100);
	player->upkeep->energy_use = 0;
	eq(cmdq_push_fishing_action(WORLD_FISHING_STRIKE), 0);
	require(cmdq_pop(CTX_GAME));
	eq(player->fishing->phase, WORLD_FISHING_WINDING);
	player->upkeep->energy_use = 0;
	eq(cmdq_push_fishing_action(WORLD_FISHING_REEL), 0);
	require(cmdq_pop(CTX_GAME));
	eq(carried_kind_count(player, glassfin_kind), 1);
	player->upkeep->energy_use = 0;
	eq(cmdq_push_fishing_action(WORLD_FISHING_CANCEL), 0);
	require(cmdq_pop(CTX_GAME));
	null(player->fishing);
	ok;
}
static bool test_position_at_passage(struct world_spelunk_runtime *runtime,
		int x, int y)
{
	if (!runtime) return false;
	runtime->state.x = x;
	runtime->state.y = y;
	runtime->state.fall_start_y = y;
	runtime->state.grip_target_x = -1;
	runtime->state.grip_target_y = -1;
	runtime->state.movement = WORLD_SPELUNK_STANDING;
	runtime->state.jump_holding = false;
	return world_spelunk_runtime_is_valid(runtime);
}

static bool test_traverse_spelunk_subtree(struct world_spelunk_system *system,
		const char *node_id, size_t *traversal_count)
{
	size_t i;

	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &system->edges[i];
		struct world_spelunk_system_node *source;
		struct world_spelunk_system_node *destination;

		if (!streq(edge->node_a_id, node_id)) continue;
		source = world_spelunk_system_node_by_id_mutable(system,
			edge->node_a_id);
		destination = world_spelunk_system_node_by_id_mutable(system,
			edge->node_b_id);
		if (!source || !destination ||
				!test_position_at_passage(source->runtime,
					edge->endpoint_a_x, edge->endpoint_a_y) ||
				world_spelunk_system_traverse_passage(system,
					edge->endpoint_a_id) != WORLD_SPELUNK_PASSAGE_OK ||
				!streq(system->active_node_id, destination->id) ||
				!test_traverse_spelunk_subtree(system, destination->id,
					traversal_count) ||
				!test_position_at_passage(destination->runtime,
					edge->endpoint_b_x, edge->endpoint_b_y) ||
				world_spelunk_system_traverse_passage(system,
					edge->endpoint_b_id) != WORLD_SPELUNK_PASSAGE_OK ||
				!streq(system->active_node_id, source->id)) {
			return false;
		}
		*traversal_count += 2;
	}
	return true;
}

static bool generated_runtime_payloads_equal(
		const struct world_spelunk_runtime *first,
		const struct world_spelunk_runtime *second)
{
	const struct object *first_obj;
	const struct object *second_obj;
	size_t first_size = world_spelunk_runtime_encoded_size(first);
	size_t second_size = world_spelunk_runtime_encoded_size(second);
	size_t first_written = 0;
	size_t second_written = 0;
	uint8_t *first_bytes;
	uint8_t *second_bytes;
	bool equal;

	if (!first_size || first_size != second_size) return false;
	first_bytes = mem_alloc(first_size);
	second_bytes = mem_alloc(second_size);
	equal = world_spelunk_runtime_encode(first, first_bytes, first_size,
		&first_written) == WORLD_SPELUNK_CODEC_OK &&
		world_spelunk_runtime_encode(second, second_bytes, second_size,
			&second_written) == WORLD_SPELUNK_CODEC_OK &&
		first_written == second_written &&
		memcmp(first_bytes, second_bytes, first_written) == 0;
	mem_free(second_bytes);
	mem_free(first_bytes);
	if (!equal) return false;
	first_obj = first->ground_objects;
	second_obj = second->ground_objects;
	while (first_obj && second_obj) {
		if (first_obj->kind != second_obj->kind ||
				!loc_eq(first_obj->grid, second_obj->grid)) {
			return false;
		}
		first_obj = first_obj->next;
		second_obj = second_obj->next;
	}
	return !first_obj && !second_obj;
}

static bool generated_node_population_is_valid(
		const struct world_spelunk_system_node *node)
{
	const struct world_spelunk_recipe_definition *recipe;
	const struct world_spelunk_population_profile *population;
	unsigned int actor_minimum = 0;
	unsigned int actor_maximum = 0;
	unsigned int object_minimum = 0;
	unsigned int object_maximum = 0;
	const struct object *obj;
	int i;

	if (!node || !node->runtime || !node->discovered) return false;
	recipe = world_spelunk_recipe_definition_by_id(node->recipe_id);
	population = recipe ? world_spelunk_population_profile_by_id(
		recipe->population_profile_id) : NULL;
	if (!recipe || !population ||
			!node->runtime->actor_roster_initialized) {
		return false;
	}
	for (i = 0; i < population->actor_entry_count; i++) {
		actor_minimum += population->actors[i].minimum_count;
		actor_maximum += population->actors[i].maximum_count;
	}
	for (i = 0; i < population->object_entry_count; i++) {
		object_minimum += population->objects[i].minimum_count;
		object_maximum += population->objects[i].maximum_count;
	}
	for (i = 0; i < recipe->landmark_count; i++) {
		if (recipe->landmarks[i].kind == WORLD_SPELUNK_LANDMARK_OBJECT) {
			object_minimum++;
			object_maximum++;
		}
	}
	if (node->runtime->actor_count < actor_minimum ||
			node->runtime->actor_count > actor_maximum ||
			world_spelunk_runtime_ground_object_count(node->runtime) <
				object_minimum ||
			world_spelunk_runtime_ground_object_count(node->runtime) >
				object_maximum) {
		return false;
	}
	for (i = 0; i < (int)node->runtime->actor_count; i++) {
		const struct world_spelunk_actor *actor = &node->runtime->actors[i];

		if (actor->y + 1 >= node->runtime->state.map.height ||
				node->runtime->cells[(size_t)(actor->y + 1) *
				node->runtime->state.map.width + actor->x] !=
				WORLD_SPELUNK_ROCK) {
			return false;
		}
	}
	for (obj = node->runtime->ground_objects; obj; obj = obj->next) {
		if (obj->grid.y + 1 >= node->runtime->state.map.height ||
				node->runtime->cells[(size_t)(obj->grid.y + 1) *
				node->runtime->state.map.width + obj->grid.x] !=
				WORLD_SPELUNK_ROCK ||
			world_spelunk_runtime_actor_at(node->runtime,
				obj->grid.x, obj->grid.y)) {
			return false;
		}
	}
	return true;
}

static int test_procedural_candidate_publication(void *state)
{
	const struct world_spelunk_system_profile *profile;
	const struct world_spelunk_recipe_definition *root_recipe;
	const struct world_spelunk_system_node *root;
	const struct world_spelunk_system_portal *arrival;
	const struct world_spelunk_system_portal *practice;
	const struct world_spelunk_system_portal *underflow;
	struct world_spelunk_system *candidate = NULL;
	struct world_spelunk_system *duplicate = NULL;
	struct world_spelunk_runtime *original_runtime;
	struct world_spelunk_system *original_system;
	struct randomizer_state gameplay_before;
	struct randomizer_state gameplay_after;
	struct object_kind *longline_kind;
	struct object *longline;
	size_t i;
	uint32_t seed;
	size_t traversal_count = 0;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	profile = world_spelunk_system_profile_by_id("deepwell");
	notnull(profile);
	root_recipe = world_spelunk_recipe_definition_by_id(
		profile->root_recipe_id);
	notnull(root_recipe);
	original_runtime = player->spelunking;
	original_system = player->spelunking_system;
	Rand_state_export(&gameplay_before);
	eq(world_spelunk_publish_system_candidate(profile, 0x4d45524dU, 100,
		"shaft.surface", &candidate), WORLD_SPELUNK_PUBLICATION_OK);
	Rand_state_export(&gameplay_after);
	require(memcmp(&gameplay_before, &gameplay_after,
		sizeof(gameplay_before)) == 0);
	notnull(candidate);
	require(player->spelunking == original_runtime);
	require(player->spelunking_system == original_system);
	require(world_spelunk_system_is_valid(candidate));
	require(streq(candidate->id, profile->id));
	require(streq(candidate->location_id, profile->location_id));
	require(candidate->node_count >= profile->min_sections &&
		candidate->node_count <= profile->max_sections);
	eq(candidate->edge_count, candidate->node_count - 1);
	eq(candidate->portal_count, profile->portal_count);
	root = &candidate->nodes[0];
	notnull(root->runtime);
	require(root->discovered);
	require(streq(candidate->active_node_id, root->id));
	require(streq(root->runtime->geology_profile_id,
		root_recipe->material_profile_id));
	require(world_spelunk_perception_matches(&root->runtime->perception,
		&root_recipe->perception));
	arrival = world_spelunk_system_portal_by_entry(candidate,
		"shaft.surface");
	practice = world_spelunk_system_portal_by_entry(candidate,
		"shaft.practice");
	underflow = world_spelunk_system_portal_by_entry(candidate,
		"shaft.underflow");
	notnull(arrival);
	null(practice);
	notnull(underflow);
	require(arrival->enabled);
	require(!underflow->enabled && !underflow->materialized);
	require(arrival->materialized && arrival->discovered);
	{
		struct world_spelunk_generated_layout reconstructed;
		struct world_spelunk_system_node *mutable_root =
			world_spelunk_system_node_by_id_mutable(candidate, root->id);
		int old_rest_gain;

		Rand_state_export(&gameplay_before);
		eq(world_spelunk_reconstruct_node_candidate(candidate, root->id, 100,
			&reconstructed), WORLD_SPELUNK_RECONSTRUCTION_OK);
		eq(world_spelunk_generated_route_maximum_grip_span(&reconstructed),
			3);
		Rand_state_export(&gameplay_after);
		require(memcmp(&gameplay_before, &gameplay_after,
			sizeof(gameplay_before)) == 0);
		require(reconstructed.witness_steps != NULL &&
			reconstructed.witness_step_count > 0);
		eq(arrival->y, reconstructed.route_stations[0].y);
		world_spelunk_generated_layout_dispose(&reconstructed);

		/* Reconstruction must audit immutable runtime rules rather than
		 * silently returning a witness for drifted saved state. */
		notnull(mutable_root);
		old_rest_gain = mutable_root->runtime->state.rules.rest_stamina_gain;
		mutable_root->runtime->state.rules.rest_stamina_gain++;
		eq(world_spelunk_reconstruct_node_candidate(candidate, root->id, 100,
			&reconstructed), WORLD_SPELUNK_RECONSTRUCTION_RUNTIME_MISMATCH);
		null(reconstructed.cells);
		null(reconstructed.witness_steps);
		mutable_root->runtime->state.rules.rest_stamina_gain = old_rest_gain;
		require(world_spelunk_system_is_valid(candidate));
	}
	eq(root->runtime->state.x, arrival->x);
	eq(root->runtime->state.y, arrival->y);
	eq(root->runtime->cells[(size_t)(arrival->y + 1) *
		root->runtime->state.map.width + arrival->x], WORLD_SPELUNK_ROCK);
	longline_kind = lookup_kind(tval_find_idx("tool"),
		lookup_sval(tval_find_idx("tool"), "Yew Longline Rod"));
	notnull(longline_kind);
	longline = root->runtime->ground_objects;
	while (longline && longline->kind != longline_kind) {
		longline = longline->next;
	}
	notnull(longline);
	require(world_spelunk_runtime_ground_object_count(root->runtime) >= 2 &&
		world_spelunk_runtime_ground_object_count(root->runtime) <= 4);
	require(root->runtime->actor_roster_initialized);
	require(root->runtime->actor_count >= 1 &&
		root->runtime->actor_count <= 2);
	{
		const struct world_spelunk_system_node *duplicate_root;

		eq(world_spelunk_publish_system_candidate(profile, 0x4d45524dU, 100,
			"shaft.surface", &duplicate), WORLD_SPELUNK_PUBLICATION_OK);
		notnull(duplicate);
		duplicate_root = &duplicate->nodes[0];
		notnull(duplicate_root->runtime);
		require(generated_runtime_payloads_equal(root->runtime,
			duplicate_root->runtime));
		world_spelunk_system_free(duplicate);
		duplicate = NULL;
	}

	for (i = 0; i < candidate->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &candidate->edges[i];

		if (streq(edge->node_a_id, root->id)) {
			int depth;

			require(edge->endpoint_a_materialized);
			require(!edge->endpoint_b_materialized);
			depth = edge->endpoint_a_y * 100 /
				(root->runtime->state.map.height - 1);
			require(depth >= root_recipe->passages[
				WORLD_SPELUNK_PASSAGE_DOWN].min_depth_percent);
			require(depth <= root_recipe->passages[
				WORLD_SPELUNK_PASSAGE_DOWN].max_depth_percent);
			eq(root->runtime->cells[(size_t)(edge->endpoint_a_y + 1) *
				root->runtime->state.map.width + edge->endpoint_a_x],
				WORLD_SPELUNK_ROCK);
			null(world_spelunk_runtime_actor_at(root->runtime,
				edge->endpoint_a_x, edge->endpoint_a_y));
			null(world_spelunk_runtime_ground_object_at(root->runtime,
				edge->endpoint_a_x, edge->endpoint_a_y));
		} else {
			require(!edge->endpoint_a_materialized);
			require(!edge->endpoint_b_materialized);
		}
	}
	for (i = 0; i < candidate->portal_count; i++) {
		size_t j;
		const struct world_spelunk_system_portal *portal =
			&candidate->portals[i];

		null(world_spelunk_runtime_actor_at(root->runtime,
			portal->x, portal->y));
		null(world_spelunk_runtime_ground_object_at(root->runtime,
			portal->x, portal->y));

		for (j = i + 1; j < candidate->portal_count; j++) {
			require(candidate->portals[i].x != candidate->portals[j].x ||
				candidate->portals[i].y != candidate->portals[j].y);
		}
	}
	/* A bad arrival identity must not leave even the first child partially
	 * materialized.  Then visit every planned child in graph order. */
	{
		const struct world_spelunk_system_edge *first = &candidate->edges[0];
		const struct world_spelunk_system_node *child =
			world_spelunk_system_node_by_id(candidate, first->node_b_id);

		notnull(child);
		null(child->runtime);
		eq(world_spelunk_materialize_node_candidate(candidate,
			first->node_b_id, 100, "missing.endpoint"),
			WORLD_SPELUNK_PUBLICATION_ENDPOINT_FAILED);
		null(child->runtime);
		require(!first->endpoint_b_materialized);
		require(test_position_at_passage(candidate->nodes[0].runtime,
			first->endpoint_a_x, first->endpoint_a_y));
		eq(world_spelunk_system_traverse_passage(candidate,
			first->endpoint_a_id),
			WORLD_SPELUNK_PASSAGE_DESTINATION_UNAVAILABLE);
	}
	Rand_state_export(&gameplay_before);
	for (i = 0; i < candidate->edge_count; i++) {
		const struct world_spelunk_system_edge *edge = &candidate->edges[i];
		const struct world_spelunk_system_node *child =
			world_spelunk_system_node_by_id(candidate, edge->node_b_id);
		const struct world_spelunk_recipe_definition *recipe;

		notnull(child);
		null(child->runtime);
		recipe = world_spelunk_recipe_definition_by_id(child->recipe_id);
		notnull(recipe);
		eq(world_spelunk_materialize_node_candidate(candidate, child->id, 100,
			edge->endpoint_b_id), WORLD_SPELUNK_PUBLICATION_OK);
		notnull(child->runtime);
		require(edge->endpoint_b_materialized);
		eq(child->runtime->state.x, edge->endpoint_b_x);
		eq(child->runtime->state.y, edge->endpoint_b_y);
		require(streq(child->runtime->geology_profile_id,
			recipe->material_profile_id));
		require(world_spelunk_system_discover_edge(candidate, edge->id));
		require(world_spelunk_system_is_valid(candidate));
	}
	Rand_state_export(&gameplay_after);
	require(memcmp(&gameplay_before, &gameplay_after,
		sizeof(gameplay_before)) == 0);
	for (i = 0; i < candidate->node_count; i++) {
		require(generated_node_population_is_valid(&candidate->nodes[i]));
	}
	for (i = 0; i < candidate->edge_count; i++) {
		require(candidate->edges[i].endpoint_a_materialized);
		require(candidate->edges[i].endpoint_b_materialized);
		require(candidate->edges[i].discovered);
	}
	require(underflow->materialized);
	require(!underflow->enabled && !underflow->discovered);
	{
		struct player discovery_player = { 0 };
		struct world_fishing_runtime fishing = { 0 };
		struct world_fishing_report catch_report = { 0 };
		const char *discovery_message = NULL;
		world_fishing_kind glassfin_kind =
			world_fishing_kind_by_id("glassfin");

		require(glassfin_kind != WORLD_FISHING_KIND_NONE);
		my_strcpy(discovery_player.world_location.id,
			"core.spelunk.east.001",
			sizeof(discovery_player.world_location.id));
		discovery_player.spelunking_system = candidate;
		my_strcpy(fishing.rig_id, "yew-longline-rod",
			sizeof(fishing.rig_id));
		fishing.habitat = WORLD_FISHING_HABITAT_CAVE_POOL;
		catch_report.landed_kind = glassfin_kind;
		catch_report.landed_depth = 15;
		require(world_spelunk_system_set_active_node(candidate,
			underflow->node_id));
		eq(world_fishing_apply_discovery(&discovery_player, &fishing,
			&catch_report, &discovery_message), WORLD_FISHING_DISCOVERY_NONE);
		catch_report.landed_depth = 16;
		eq(world_fishing_apply_discovery(&discovery_player, &fishing,
			&catch_report, &discovery_message),
			WORLD_FISHING_DISCOVERY_UNLOCKED);
		notnull(discovery_message);
		require(underflow->enabled);
		require(world_player_route_is_unlocked(&discovery_player,
			"core.route.underflow.out"));
		require(world_player_route_is_unlocked(&discovery_player,
			"core.route.underflow.in"));
		eq(world_fishing_apply_discovery(&discovery_player, &fishing,
			&catch_report, &discovery_message),
			WORLD_FISHING_DISCOVERY_ALREADY_RECORDED);
		world_player_clear_ledgers(&discovery_player);
	}
	require(world_spelunk_system_set_active_node(candidate,
		candidate->nodes[0].id));
	Rand_state_export(&gameplay_before);
	require(test_traverse_spelunk_subtree(candidate,
		candidate->nodes[0].id, &traversal_count));
	Rand_state_export(&gameplay_after);
	require(memcmp(&gameplay_before, &gameplay_after,
		sizeof(gameplay_before)) == 0);
	eq(traversal_count, candidate->edge_count * 2);
	require(streq(candidate->active_node_id, candidate->nodes[0].id));
	require(world_spelunk_system_is_valid(candidate));
	require(world_spelunk_system_set_active_node(candidate,
		candidate->nodes[candidate->node_count - 1].id));
	require(world_spelunk_system_is_valid(candidate));
	world_spelunk_system_free(candidate);
	candidate = NULL;

	/* A modest production-data seed sweep protects endpoint availability and
	 * transaction ownership without publishing any candidate to the player. */
	for (seed = 0; seed < 32; seed++) {
		const char *entry = "shaft.surface";

		eq(world_spelunk_publish_system_candidate(profile, seed, 100, entry,
			&candidate), WORLD_SPELUNK_PUBLICATION_OK);
		notnull(candidate);
		require(world_spelunk_system_is_valid(candidate));
		require(player->spelunking == original_runtime);
		require(player->spelunking_system == original_system);
		/* A retired saved portal disappears without moving its chamber or
		 * active player; the legitimate entrance remains. */
		require(world_spelunk_system_add_portal(candidate, "deepwell.practice",
			"shaft.practice", candidate->nodes[0].id, true));
		require(world_spelunk_system_reconcile_portals(candidate));
		null(world_spelunk_system_portal_by_entry(candidate, "shaft.practice"));
		notnull(world_spelunk_system_portal_by_entry(candidate, "shaft.surface"));
		world_spelunk_system_free(candidate);
		candidate = NULL;
	}
	eq(world_spelunk_publish_system_candidate(profile, 1, 100,
		"missing.entry", &candidate),
		WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT);
	null(candidate);
	ok;
}

const char *suite_name = "game/spelunking-transitions";
struct test tests[] = {
	{ "cave status snapshot", test_cave_status_snapshot },
	{ "debug cave shortcut", test_debug_cave_shortcut },
	{ "transition lifecycle", test_spelunking_transition_lifecycle },
	{ "generated player passages", test_generated_player_passages },
	{ "fishing site", test_spelunking_fishing_site },
	{ "procedural candidate publication", test_procedural_candidate_publication },
	{ NULL, NULL }
};
