/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/headless-scenario.c */
/* Exercise the strict, bounded development-scenario data contract. */

#include "unit-test.h"

#include "headless/scenario.h"
#include "init.h"
#include "test-utils.h"

static char test_path[1024];

static bool write_scenario(const char *text)
{
	ang_file *file = file_open(test_path, MODE_WRITE, FTYPE_TEXT);
	bool wrote;

	if (!file) return false;
	wrote = file_putf(file, "%s", text);
	return file_close(file) && wrote;
}

int setup_tests(void **data)
{
	(void)data;
	set_file_paths();
	path_build(test_path, sizeof(test_path), ANGBAND_DIR_USER,
		"headless-scenario-unittest.scn");
	if (file_exists(test_path)) file_delete(test_path);
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	if (file_exists(test_path)) file_delete(test_path);
	return 0;
}

static int test_defaults(void *state)
{
	struct headless_scenario scenario;
	char error[256];
	(void)state;

	require(write_scenario("name:minimal\n"));
	require(headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(streq(scenario.name, "minimal"));
	eq(scenario.seed, 1U);
	require(streq(scenario.race, "Nearlander"));
	require(streq(scenario.player_class, "Warrior"));
	require(streq(scenario.location, "core.hub"));
	require(streq(scenario.entry, "default"));
	require(streq(scenario.map_style, "natural"));
	require(streq(scenario.map_presentation, "ascii"));
	eq(scenario.bot, HEADLESS_BOT_WAIT);
	eq(scenario.steps, 20U);
	eq(scenario.capture, HEADLESS_CAPTURE_FINAL);
	eq(scenario.capture_fps, 30U);
	eq(scenario.capture_preroll_ms, 500U);
	eq(scenario.capture_action_ms, 250U);
	eq(scenario.capture_postroll_ms, 500U);
	eq(scenario.expected_result, HEADLESS_EXPECT_COMPLETE);
	ok;
}

static int test_scripted_contract(void *state)
{
	struct headless_scenario scenario;
	char error[256];
	(void)state;

	require(write_scenario(
		"name:scripted\n"
		"seed:4294967295\n"
		"location:core.hub\n"
		"entry:shaft.practice\n"
		"enter-route:core.route.spelunk.practice.in\n"
		"bot:scripted\n"
		"capture:both\n"
		"expect:dead\n"
		"map-style:natural\n"
		"map-presentation:tiles\n"
		"map-zoom:200\n"
		"screen-width:1280\n"
		"screen-height:720\n"
		"start-x:44\n"
		"start-y:21\n"
		"encounter:cave spider\n"
		"inventory:tool:Yew Longline Rod\n"
		"inventory:24:tool:Rope Length (2 metres)\n"
		"equipment:light:Lantern\n"
		"action:hold\n"
		"action:fish:start\n"));
	require(headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	eq(scenario.seed, UINT32_MAX);
	eq(scenario.bot, HEADLESS_BOT_SCRIPTED);
	eq(scenario.steps, 2U);
	eq(scenario.action_count, 2U);
	require(streq(scenario.actions[1], "fish:start"));
	eq(scenario.capture, HEADLESS_CAPTURE_BOTH);
	eq(scenario.expected_result, HEADLESS_EXPECT_DEAD);
	require(streq(scenario.map_style, "natural"));
	require(streq(scenario.map_presentation, "tiles"));
	eq(scenario.map_zoom_percent, 200);
	eq(scenario.screen_width, 1280);
	eq(scenario.screen_height, 720);
	require(scenario.has_start_position);
	eq(scenario.start_x, 44);
	eq(scenario.start_y, 21);
	require(streq(scenario.encounter, "cave spider"));
	eq(scenario.inventory_count, 2);
	require(streq(scenario.inventory[0], "tool:Yew Longline Rod"));
	eq(scenario.inventory_quantity[0], 1);
	require(streq(scenario.inventory[1],
		"tool:Rope Length (2 metres)"));
	eq(scenario.inventory_quantity[1], 24);
	eq(scenario.equipment_count, 1);
	require(streq(scenario.equipment[0], "light:Lantern"));
	ok;
}

static int test_rejects_unknown_and_duplicate_fields(void *state)
{
	struct headless_scenario scenario;
	char error[256];
	(void)state;

	require(write_scenario("name:bad\nmystery:value\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(strstr(error, "unknown field") != NULL);
	require(write_scenario("name:first\nname:second\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(strstr(error, "duplicate field") != NULL);
	ok;
}

static int test_rejects_incomplete_bounded_records(void *state)
{
	struct headless_scenario scenario;
	char error[256];
	(void)state;

	require(write_scenario(
		"name:size\nscreen-width:1280\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario(
		"name:steps\nbot:scripted\nsteps:2\naction:hold\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario(
		"name:overflow\nsteps:100001\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario("name:position\nstart-x:4\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario(
		"name:position\nstart-anchor:fishing-stance\nstart-x:4\n"
		"start-y:5\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario(
		"name:position\nstart-anchor:first-rope-anchor\n"));
	require(headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(streq(scenario.start_anchor, "first-rope-anchor"));
	require(write_scenario(
		"name:position\nstart-anchor:downward-peek-right\n"));
	require(headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(streq(scenario.start_anchor, "downward-peek-right"));
	require(write_scenario(
		"name:position\nstart-anchor:invented-place\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario("name:item\ninventory:no-separator\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario(
		"name:quantity\ninventory:0:tool:Piton\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario(
		"name:equipment\nequipment:Lantern\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	/* Textured was a briefly exposed third style.  Rejecting its old name
	 * guards the public scenario format as well as the two-style UI. */
	require(write_scenario("name:style\nmap-style:textured\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(write_scenario(
		"name:presentation\nmap-presentation:painted\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	ok;
}

static int test_accepts_cave_larder_loop_bot(void *state)
{
	struct headless_scenario scenario;
	char error[256];
	(void)state;

	require(write_scenario(
		"name:loop\n"
		"bot:cave-larder-loop\n"
		"steps:5000\n"));
	require(headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	eq(scenario.bot, HEADLESS_BOT_CAVE_LARDER_LOOP);
	eq(scenario.steps, 5000U);
	eq(scenario.action_count, 0U);
	require(write_scenario(
		"name:fall\n"
		"bot:cave-fatal-fall\n"
		"steps:100\n"
		"expect:dead\n"));
	require(headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	eq(scenario.bot, HEADLESS_BOT_CAVE_FATAL_FALL);
	eq(scenario.expected_result, HEADLESS_EXPECT_DEAD);
	ok;
}

static int test_capture_sequence_contract(void *state)
{
	struct headless_scenario scenario;
	char error[256];
	(void)state;

	require(write_scenario(
		"name:sequence\n"
		"bot:scripted\n"
		"capture:sequence\n"
		"capture-fps:60\n"
		"capture-preroll-ms:1000\n"
		"capture-action-ms:400\n"
		"capture-postroll-ms:2000\n"
		"action:hold\n"
		"action:walk:east\n"));
	require(headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	eq(scenario.capture, HEADLESS_CAPTURE_SEQUENCE);
	eq(scenario.capture_fps, 60U);
	eq(scenario.capture_preroll_ms, 1000U);
	eq(scenario.capture_action_ms, 400U);
	eq(scenario.capture_postroll_ms, 2000U);
	require(write_scenario(
		"name:bad-timing\n"
		"capture:final\n"
		"capture-fps:30\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(strstr(error, "require capture:sequence") != NULL);
	require(write_scenario(
		"name:too-long\n"
		"capture:sequence\n"
		"steps:100000\n"
		"capture-action-ms:1000\n"));
	require(!headless_scenario_load(test_path, &scenario, error,
		sizeof(error)));
	require(strstr(error, "ten-minute limit") != NULL);
	ok;
}

static int test_hybrid_presentations(void *state)
{
	static const char *modes[] = { "hybrid", "hybrid-32", "hybrid-64" };
	struct headless_scenario scenario;
	char text[256], error[256];
	size_t i;
	(void)state;
	for (i = 0; i < N_ELEMENTS(modes); ++i) {
		strnfmt(text, sizeof(text), "name:hybrid\nmap-presentation:%s\n", modes[i]);
		require(write_scenario(text));
		require(headless_scenario_load(test_path, &scenario, error, sizeof(error)));
		require(streq(scenario.map_presentation, modes[i]));
	}
	require(write_scenario("name:invalid\nmap-presentation:hybrid-16\n"));
	require(!headless_scenario_load(test_path, &scenario, error, sizeof(error)));
	ok;
}

const char *suite_name = "sdl3/headless-scenario";
struct test tests[] = {
	{ "defaults", test_defaults },
	{ "hybrid presentations", test_hybrid_presentations },
	{ "scripted contract", test_scripted_contract },
	{ "unknown and duplicate fields",
		test_rejects_unknown_and_duplicate_fields },
	{ "incomplete bounded records",
		test_rejects_incomplete_bounded_records },
	{ "cave larder loop bot", test_accepts_cave_larder_loop_bot },
	{ "capture sequence contract", test_capture_sequence_contract },
	{ NULL, NULL },
};
