/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* game/fishing.c */

#include "unit-test.h"

#include "datafile.h"
#include "fishing-data.h"
#include "test-utils.h"
#include "ui-fishing-input.h"
#include "world-fishing.h"

static world_fishing_kind rain_minnow;
static world_fishing_kind mudbelly;
static world_fishing_kind glassfin;

static void exact_fish(struct world_fishing_runtime *runtime)
{
	struct world_fishing_fish *fish = &runtime->fish[0];

	runtime->fish_count = 1;
	runtime->depth = 2;
	runtime->fish_move_chance_percent = 0;
	runtime->fish_depth_move_chance_percent = 0;
	runtime->moving_hook_evasion_chance_percent = 0;
	memset(fish, 0, sizeof(*fish));
	fish->kind = rain_minnow;
	fish->col = world_fishing_hook_col(runtime);
	fish->row = runtime->depth;
	fish->depth_min = 1;
	fish->depth_max = runtime->depth_rows;
	fish->wind_needed = 2;
	fish->bite_timeout = 2;
}

static int test_start_is_bounded_and_valid(void *unused)
{
	struct world_fishing_runtime runtime;
	const struct world_fishing_rules *rules = world_fishing_rules();

	(void)unused;
	world_fishing_runtime_start(&runtime, 7,
		WORLD_FISHING_HABITAT_RAINWATER, 0,
		WORLD_FISHING_BASELINE_MAX_REACH, WORLD_FISHING_DEPTH_ROWS,
		"test-rig", "Test rig", loc(4, 5));
	require(world_fishing_runtime_is_valid(&runtime));
	notnull(rules);
	require(runtime.fish_count >= rules->minimum_fish);
	require(runtime.fish_count <= WORLD_FISHING_MAX_FISH);
	eq(runtime.columns, 72);
	eq(runtime.depth_rows, 16);
	eq(runtime.maximum_reach, runtime.columns - 1);
	eq(runtime.maximum_depth, runtime.depth_rows);
	eq(runtime.habitat, WORLD_FISHING_HABITAT_RAINWATER);
	require(streq(runtime.rig_name, "Test rig"));
	eq(runtime.fish_move_chance_percent, 65);
	eq(runtime.fish_depth_move_chance_percent, 25);
	eq(runtime.moving_hook_evasion_chance_percent, 55);
	eq(runtime.moving_hook_evasion_radius, 3);
	eq(runtime.patience_turns, 40);
	eq(runtime.attraction_horizontal, 3);
	eq(runtime.attraction_vertical, 1);
	eq(runtime.rod_reach, WORLD_FISHING_INITIAL_REACH);
	eq(world_fishing_hook_col(&runtime), runtime.columns - 2);
	eq(runtime.origin.x, 4);
	eq(runtime.origin.y, 5);
	for (int i = 0; i < runtime.fish_count; i++) {
		require(runtime.fish[i].glyph == '.' ||
			runtime.fish[i].glyph == '=' || runtime.fish[i].glyph == '0');
	}
	ok;
}

static int test_moving_submerged_hook_causes_evasion(void *unused)
{
	struct world_fishing_runtime runtime;
	struct world_fishing_report report;
	int future_col;

	(void)unused;
	world_fishing_runtime_start(&runtime, 29,
		WORLD_FISHING_HABITAT_RAINWATER, 0,
		WORLD_FISHING_BASELINE_MAX_REACH, WORLD_FISHING_DEPTH_ROWS,
		"test-rig", "Test rig", loc(0, 0));
	runtime.depth = 8;
	runtime.fish_move_chance_percent = 0;
	runtime.fish_depth_move_chance_percent = 0;
	runtime.moving_hook_evasion_chance_percent = 100;
	future_col = runtime.columns - 1 - (runtime.rod_reach + 1);
	runtime.fish_count = 1;
	memset(&runtime.fish[0], 0, sizeof(runtime.fish[0]));
	runtime.fish[0].kind = mudbelly;
	runtime.fish[0].glyph = '=';
	runtime.fish[0].col = future_col;
	runtime.fish[0].row = runtime.depth;
	runtime.fish[0].depth_min = 1;
	runtime.fish[0].depth_max = runtime.depth_rows;
	runtime.fish[0].wind_needed = 4;
	runtime.fish[0].bite_timeout = 4;
	require(world_fishing_apply(&runtime, WORLD_FISHING_EXTEND, &report));
	eq(report.event, WORLD_FISHING_EVENT_MOVED);
	eq(runtime.phase, WORLD_FISHING_WAITING);
	eq(runtime.fish[0].col, future_col - 1);
	require(runtime.fish[0].row != runtime.depth);
	ok;
}

static int test_only_stationary_exact_hook_bites(void *unused)
{
	struct world_fishing_runtime runtime;
	struct world_fishing_report report;
	int future_col;

	(void)unused;
	world_fishing_runtime_start(&runtime, 11,
		WORLD_FISHING_HABITAT_RAINWATER, 0, 15,
		WORLD_FISHING_DEPTH_ROWS, "test-rig", "Test rig", loc(0, 0));
	runtime.depth = 2;
	runtime.fish_move_chance_percent = 0;
	runtime.fish_depth_move_chance_percent = 0;
	runtime.moving_hook_evasion_chance_percent = 0;
	future_col = runtime.columns - 1 - (runtime.rod_reach + 1);
	runtime.fish_count = 1;
	runtime.fish[0].col = future_col;
	runtime.fish[0].row = runtime.depth;
	runtime.fish[0].depth_min = 1;
	runtime.fish[0].depth_max = runtime.depth_rows;
	runtime.fish[0].wind_needed = 2;
	runtime.fish[0].bite_timeout = 2;
	runtime.fish[0].run_chance = 0;
	require(world_fishing_apply(&runtime, WORLD_FISHING_EXTEND, &report));
	eq(report.event, WORLD_FISHING_EVENT_MOVED);
	eq(runtime.phase, WORLD_FISHING_WAITING);
	runtime.fish[0].col = world_fishing_hook_col(&runtime);
	runtime.fish[0].row = runtime.depth;
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	eq(report.event, WORLD_FISHING_EVENT_BITE);
	eq(runtime.phase, WORLD_FISHING_BITE);
	require(report.consumes_turn);
	ok;
}

static int test_strike_and_reel_land_fish(void *unused)
{
	struct world_fishing_runtime runtime;
	struct world_fishing_report report;

	(void)unused;
	world_fishing_runtime_start(&runtime, 19,
		WORLD_FISHING_HABITAT_RAINWATER, 0, 15,
		WORLD_FISHING_DEPTH_ROWS, "test-rig", "Test rig", loc(0, 0));
	exact_fish(&runtime);
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	require(world_fishing_apply(&runtime, WORLD_FISHING_STRIKE, &report));
	eq(runtime.phase, WORLD_FISHING_WINDING);
	require(world_fishing_apply(&runtime, WORLD_FISHING_REEL, &report));
	require(world_fishing_apply(&runtime, WORLD_FISHING_REEL, &report));
	eq(report.event, WORLD_FISHING_EVENT_LANDED);
	eq(report.landed_kind, rain_minnow);
	eq(report.landed_depth, 2);
	eq(report.landed_depth, 2);
	eq(runtime.phase, WORLD_FISHING_WAITING);
	eq(runtime.fish_count, 0);
	ok;
}

static int test_bite_timeout_and_cancel_are_safe(void *unused)
{
	struct world_fishing_runtime runtime;
	struct world_fishing_report report;

	(void)unused;
	world_fishing_runtime_start(&runtime, 23,
		WORLD_FISHING_HABITAT_RAINWATER, 0, 15,
		WORLD_FISHING_DEPTH_ROWS, "test-rig", "Test rig", loc(0, 0));
	exact_fish(&runtime);
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	eq(report.event, WORLD_FISHING_EVENT_WAITED);
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	eq(report.event, WORLD_FISHING_EVENT_ESCAPED);
	eq(runtime.phase, WORLD_FISHING_WAITING);
	require(world_fishing_apply(&runtime, WORLD_FISHING_CANCEL, &report));
	eq(report.event, WORLD_FISHING_EVENT_CANCELLED);
	require(!runtime.active);
	require(!report.consumes_turn);
	ok;
}

static int test_patience_attracts_visible_fish(void *unused)
{
	struct world_fishing_runtime runtime;
	struct world_fishing_report report;
	int hook_col;
	int i;

	(void)unused;
	world_fishing_runtime_start(&runtime, 43,
		WORLD_FISHING_HABITAT_RAINWATER, 0, 15,
		WORLD_FISHING_DEPTH_ROWS, "test-rig", "Test rig", loc(0, 0));
	runtime.depth = 2;
	runtime.patience_turns = 3;
	runtime.attraction_horizontal = 2;
	runtime.attraction_vertical = 1;
	runtime.fish_count = 1;
	runtime.fish_move_chance_percent = 0;
	runtime.fish_depth_move_chance_percent = 0;
	memset(&runtime.fish[0], 0, sizeof(runtime.fish[0]));
	runtime.fish[0].kind = rain_minnow;
	hook_col = world_fishing_hook_col(&runtime);
	runtime.fish[0].col = hook_col - 5;
	runtime.fish[0].row = 1;
	runtime.fish[0].depth_min = 1;
	runtime.fish[0].depth_max = 8;
	runtime.fish[0].wind_needed = 2;
	runtime.fish[0].bite_timeout = 3;
	for (i = 0; i < 3; i++) {
		require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	}
	eq(report.event, WORLD_FISHING_EVENT_APPROACH);
	eq(runtime.phase, WORLD_FISHING_WAITING);
	eq(runtime.fish[0].col, hook_col - 3);
	eq(runtime.fish[0].row, runtime.depth);
	eq(runtime.wait_counter, runtime.patience_turns);
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	eq(report.event, WORLD_FISHING_EVENT_APPROACH);
	eq(runtime.fish[0].col, hook_col - 1);
	require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
	eq(report.event, WORLD_FISHING_EVENT_BITE);
	eq(runtime.phase, WORLD_FISHING_BITE);
	eq(runtime.fish[0].col, hook_col);
	ok;
}

static int test_patience_without_compatible_fish_remains_valid(void *unused)
{
	struct world_fishing_runtime runtime;
	struct world_fishing_report report;
	int i;

	(void)unused;
	world_fishing_runtime_start(&runtime, 47,
		WORLD_FISHING_HABITAT_RAINWATER, 0, 15,
		WORLD_FISHING_DEPTH_ROWS, "test-rig", "Test rig", loc(0, 0));
	runtime.depth = 16;
	runtime.patience_turns = 2;
	runtime.fish_count = 1;
	runtime.fish_move_chance_percent = 0;
	runtime.fish_depth_move_chance_percent = 0;
	memset(&runtime.fish[0], 0, sizeof(runtime.fish[0]));
	runtime.fish[0].kind = rain_minnow;
	runtime.fish[0].col = 0;
	runtime.fish[0].row = 1;
	runtime.fish[0].depth_min = 1;
	runtime.fish[0].depth_max = 8;
	runtime.fish[0].wind_needed = 2;
	runtime.fish[0].bite_timeout = 3;
	for (i = 0; i < 5; i++) {
		require(world_fishing_apply(&runtime, WORLD_FISHING_WAIT, &report));
		eq(report.event, WORLD_FISHING_EVENT_WAITED);
		require(world_fishing_runtime_is_valid(&runtime));
	}
	eq(runtime.wait_counter, runtime.patience_turns);
	ok;
}

static int test_phase_aware_input(void *unused)
{
	struct keypress key = { EVT_KBRD, ARROW_LEFT, 0 };
	enum world_fishing_action action;

	(void)unused;
	eq(textui_fishing_translate_key(key, WORLD_FISHING_WAITING, &action),
		TEXTUI_FISHING_ACTION);
	eq(action, WORLD_FISHING_EXTEND);
	key.code = KC_ENTER;
	eq(textui_fishing_translate_key(key, WORLD_FISHING_BITE, &action),
		TEXTUI_FISHING_ACTION);
	eq(action, WORLD_FISHING_STRIKE);
	key.code = ARROW_UP;
	eq(textui_fishing_translate_key(key, WORLD_FISHING_WINDING, &action),
		TEXTUI_FISHING_ACTION);
	eq(action, WORLD_FISHING_REEL);
	key.code = ESCAPE;
	eq(textui_fishing_translate_key(key, WORLD_FISHING_WAITING, &action),
		TEXTUI_FISHING_ACTION);
	eq(action, WORLD_FISHING_CANCEL);
	ok;
}

static int test_habitat_and_rig_capabilities(void *unused)
{
	struct world_fishing_runtime runtime;
	struct world_fishing_report report;
	int i;

	(void)unused;
	require(world_fishing_habitat_weight(
		WORLD_FISHING_HABITAT_RAINWATER, 0,
		rain_minnow) >
		world_fishing_habitat_weight(WORLD_FISHING_HABITAT_RAINWATER,
			0, glassfin));
	require(world_fishing_habitat_weight(
		WORLD_FISHING_HABITAT_CAVE_POOL, 0,
		glassfin) >
		world_fishing_habitat_weight(WORLD_FISHING_HABITAT_CAVE_POOL,
			0, rain_minnow));
	eq(world_fishing_habitat_weight(WORLD_FISHING_HABITAT_OPEN_LAKE, 3,
		glassfin), 29);
	eq(world_fishing_habitat_weight(WORLD_FISHING_HABITAT_OPEN_LAKE,
		INT_MAX, glassfin), 20 + INT16_MAX * 3);
	world_fishing_runtime_start(&runtime, 31,
		WORLD_FISHING_HABITAT_OPEN_LAKE, 3, 4, 3,
		"short-test-rig", "Short test rig", loc(0, 0));
	require(world_fishing_runtime_is_valid(&runtime));
	for (i = 0; i < 3; i++) {
		require(world_fishing_apply(&runtime, WORLD_FISHING_LOWER,
			&report));
	}
	eq(runtime.depth, 3);
	require(!world_fishing_apply(&runtime, WORLD_FISHING_LOWER, &report));
	eq(report.event, WORLD_FISHING_EVENT_BLOCKED);
	for (i = 0; i < 3; i++) {
		require(world_fishing_apply(&runtime, WORLD_FISHING_EXTEND,
			&report));
	}
	eq(runtime.rod_reach, 4);
	require(!world_fishing_apply(&runtime, WORLD_FISHING_EXTEND, &report));
	ok;
}

int setup_tests(void **data)
{
	(void)data;
	set_file_paths();
	if (run_parser(&fishing_parser)) return 1;
	rain_minnow = world_fishing_kind_by_id("rain-minnow");
	mudbelly = world_fishing_kind_by_id("mudbelly");
	glassfin = world_fishing_kind_by_id("glassfin");
	return rain_minnow == WORLD_FISHING_KIND_NONE ||
		mudbelly == WORLD_FISHING_KIND_NONE ||
		glassfin == WORLD_FISHING_KIND_NONE;
}

int teardown_tests(void *data)
{
	(void)data;
	cleanup_parser(&fishing_parser);
	return 0;
}

const char *suite_name = "game/fishing";
struct test tests[] = {
	{ "start is bounded and valid", test_start_is_bounded_and_valid },
	{ "moving submerged hook causes evasion",
		test_moving_submerged_hook_causes_evasion },
	{ "only stationary exact hook bites", test_only_stationary_exact_hook_bites },
	{ "strike and reel land fish", test_strike_and_reel_land_fish },
	{ "bite timeout and cancel are safe", test_bite_timeout_and_cancel_are_safe },
	{ "patience attracts visible fish", test_patience_attracts_visible_fish },
	{ "patience without compatible fish remains valid",
		test_patience_without_compatible_fish_remains_valid },
	{ "phase-aware input", test_phase_aware_input },
	{ "habitat and rig capabilities", test_habitat_and_rig_capabilities },
	{ NULL, NULL }
};
