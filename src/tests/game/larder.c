/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* game/larder.c */

#include "unit-test.h"

#include "datafile.h"
#include "fishing-data.h"
#include "test-utils.h"
#include "world-larder.h"
#include "world-larder-data.h"

static world_fishing_kind rain_minnow;
static world_fishing_kind mudbelly;
static world_fishing_kind glassfin;

static int test_values_and_first_milestone(void *unused)
{
	struct world_larder_state state;
	struct world_larder_report report;
	const struct world_larder_milestone_definition *milestone =
		world_larder_milestone_by_id("emergency-reserve");

	(void)unused;
	notnull(milestone);
	world_larder_reset(&state);
	require(world_larder_is_valid(&state));
	eq(world_larder_fish_value(rain_minnow), 1);
	eq(world_larder_fish_value(mudbelly), 3);
	eq(world_larder_fish_value(glassfin), 6);
	require(world_larder_donate(&state, rain_minnow, 2,
		&report));
	eq(state.food_points, 2);
	eq(report.contributed_points, 2);
	eq(report.experience, 2);
	require(!report.milestone_reached);
	require(world_larder_donate(&state, glassfin, 3,
		&report));
	eq(state.food_points, milestone->target);
	eq(state.milestone, milestone->save_index);
	ptreq(report.milestone_reached, milestone);
	eq(report.experience, 6); /* Per fish, not the 18 meal portions. */
	require(world_larder_is_valid(&state));
	require(world_larder_donate(&state, mudbelly, 2,
		&report));
	eq(state.food_points, milestone->target + 6);
	null(report.milestone_reached);
	eq(report.experience, 2); /* Repeatable after the ration unlock. */
	ok;
}

static int test_invalid_and_saturating_inputs(void *unused)
{
	struct world_larder_state state;
	struct world_larder_report report;
	const struct world_larder_milestone_definition *milestone =
		world_larder_milestone_by_id("emergency-reserve");

	(void)unused;
	notnull(milestone);
	world_larder_reset(&state);
	require(!world_larder_donate(&state, WORLD_FISHING_KIND_NONE, 1,
		&report));
	require(!world_larder_donate(&state, rain_minnow, 0,
		&report));
	eq(report.experience, 0);
	state.food_points = milestone->target;
	require(!world_larder_is_valid(&state));
	state.milestone = milestone->save_index;
	state.food_points = UINT32_MAX - 2;
	require(world_larder_donate(&state, glassfin, INT_MAX,
		&report));
	eq(state.food_points, UINT32_MAX);
	eq(report.contributed_points, 2);
	eq(report.experience, INT32_MAX);
	require(world_larder_is_valid(&state));
	ok;
}

static int test_splitting_donations_preserves_reward(void *unused)
{
	struct world_larder_state batch, split;
	struct world_larder_report report;
	int total = 0;
	int i;

	(void)unused;
	world_larder_reset(&batch);
	world_larder_reset(&split);
	for (i = 0; i < 5; i++) {
		require(world_larder_donate(&split, glassfin, 1, &report));
		total += report.experience;
	}
	require(world_larder_donate(&batch, glassfin, 5, &report));
	eq(report.experience, 10);
	eq(report.experience, total);
	eq(batch.food_points, split.food_points);
	eq(batch.milestone, split.milestone);
	ok;
}

int setup_tests(void **data)
{
	(void)data;
	set_file_paths();
	if (run_parser(&fishing_parser)) return 1;
	if (run_parser(&larder_parser)) return 1;
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
	cleanup_parser(&larder_parser);
	cleanup_parser(&fishing_parser);
	return 0;
}

const char *suite_name = "game/larder";

struct test tests[] = {
	{ "values and first milestone", test_values_and_first_milestone },
	{ "invalid and saturating inputs", test_invalid_and_saturating_inputs },
	{ "splitting donations preserves reward",
		test_splitting_donations_preserves_reward },
	{ NULL, NULL }
};
