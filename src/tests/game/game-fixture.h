/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Shared initialized-game fixture for the integration suites in this folder. */

#ifndef TESTS_GAME_GAME_FIXTURE_H
#define TESTS_GAME_GAME_FIXTURE_H

static void game_test_event_message(game_event_type type,
		game_event_data *data, void *user)
{
	(void)type;
	(void)user;
	printf("Message: %s\n", data->message.msg);
}

static void game_test_println(const char *str)
{
	printf("%s\n", str);
}

static void reset_before_load(void)
{
	play_again = true;
	wipe_mon_list(cave, player);
	cleanup_angband();
	chunk_list_max = 0;
	init_angband();
	play_again = false;
}

#ifndef GAME_TEST_SKIP_BASELINE
static bool game_test_create_baseline_saves(void)
{
	struct world_larder_report larder_report;
	struct loc node_grid;

	if (!player_make_simple(NULL, NULL, "Tester")) return false;
	prepare_next_level(player);
	on_new_level();
	if (!cave || world_entry_locate(cave, "stair.main", &node_grid) !=
			WORLD_ENTRY_OK) {
		return false;
	}
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, node_grid);
	if (!savefile_save("TestPhysical")) return false;
	if (!world_player_unlock_route(player, "core.route.main.down")) {
		return false;
	}
	if (!world_player_activate_travel_node(player,
			"core.node.main.001.up")) {
		return false;
	}
	if (!world_player_activate_travel_node_here(player, cave,
			"core.node.hub.main")) {
		return false;
	}
	if (!world_larder_donate(&player->larder,
			world_fishing_kind_by_id("mudbelly"), 2,
			&larder_report)) {
		return false;
	}
	return savefile_save("Test1");
}
#endif

int setup_tests(void **state)
{
	(void)state;
	file_delete("Test1");
	file_delete("TestPhysical");
	plog_aux = game_test_println;
	event_add_handler(EVENT_MESSAGE, game_test_event_message, NULL);
	event_add_handler(EVENT_INITSTATUS, game_test_event_message, NULL);
	set_file_paths();
	init_angband();
#ifdef UNIX
	create_needed_dirs();
#endif
#ifndef GAME_TEST_SKIP_BASELINE
	if (!game_test_create_baseline_saves()) return 1;
#endif
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	file_delete("Test1");
	file_delete("TestPhysical");
	file_delete("TestChunks");
	file_delete("TestPublished");
	file_delete("TestOwned");
	file_delete("TestEdge");
	file_delete("TestCave");
	file_delete("TestCaveEntry");
	file_delete("TestWorldCircuit");
	file_delete("TestScrollTravel");
	file_delete("TestLandmark");
	file_delete("TestSpelunk");
	file_delete("TestSpelunkActive");
	file_delete("TestSpelunkChild");
	file_delete("TestSpelunkReturned");
	file_delete("TestInvalidSpelunkStorage");
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

#endif /* TESTS_GAME_GAME_FIXTURE_H */
