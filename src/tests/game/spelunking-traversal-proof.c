/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* game/spelunking-traversal-proof */
/* Replay exact climbing witnesses through the side-view rules core. */

#include "unit-test.h"

#include "world-spelunking-traversal-proof.h"
#include "z-color.h"

#include <string.h>

NOSETUP
NOTEARDOWN

static const struct world_spelunk_perception test_perception = {
	10, 6, 2, 16, 28, 5, 2, 24,
	L'.', COLOUR_SLATE, L'.', COLOUR_L_DARK
};

static void set_air(struct world_spelunk_generated_layout *generated,
		int x, int y)
{
	generated->cells[y * generated->width + x] = WORLD_SPELUNK_AIR;
}

static void build_shaft(struct world_spelunk_generated_layout *generated,
		enum world_spelunk_tile *cells)
{
	int x;
	int y;

	memset(generated, 0, sizeof(*generated));
	generated->width = 7;
	generated->height = 7;
	generated->cells = cells;
	for (y = 0; y < generated->height; y++) {
		for (x = 0; x < generated->width; x++) {
			cells[y * generated->width + x] = WORLD_SPELUNK_ROCK;
		}
	}
	for (x = 1; x <= 3; x++) set_air(generated, x, 2);
	for (x = 1; x <= 3; x++) set_air(generated, x, 3);
	set_air(generated, 3, 4);
	set_air(generated, 3, 5);
	my_strcpy(generated->system_id, "deepwell.test",
		sizeof(generated->system_id));
	my_strcpy(generated->traversal_profile_id, "deepwell.test.proof",
		sizeof(generated->traversal_profile_id));
	generated->initial_stamina = 100;
	generated->perception = test_perception;
	generated->rules.action_energy = 100;
	generated->rules.safe_fall_tiles = 2;
	generated->rules.fall_base_damage = 10;
	generated->rules.jump_stamina_cost = 10;
	generated->rules.grip_up_stamina_cost = 5;
	generated->rules.grip_lateral_stamina_cost = 5;
	generated->rules.grip_down_stamina_cost = 5;
	generated->rules.rest_stamina_gain = 15;
	generated->rules.rope_max_length = 20;
	generated->rules.rope_up_stamina_cost = 1;
	generated->rules.rope_lateral_stamina_cost = 1;
	generated->rules.rope_down_stamina_cost = 1;
	generated->rules.breath_turns = 6;
	generated->rules.drowning_damage = 10;
	generated->rules.swim_turn_stamina_cost = 2;
}

static struct world_spelunk_traversal_profile proof_profile(void)
{
	struct world_spelunk_traversal_profile profile;

	memset(&profile, 0, sizeof(profile));
	profile.id = "deepwell.test.proof";
	profile.version = 1;
	profile.piton_budget = 1;
	profile.rope_segment_budget = 2;
	profile.return_required = true;
	return profile;
}

static void set_step(struct world_spelunk_witness_step *step,
		enum world_spelunk_witness_step_kind kind, int x, int y)
{
	memset(step, 0, sizeof(*step));
	step->kind = kind;
	step->expected_x = x;
	step->expected_y = y;
}

static void set_move(struct world_spelunk_witness_step *step,
		int dx, int dy, int x, int y)
{
	set_step(step, WORLD_SPELUNK_WITNESS_COMMAND, x, y);
	step->command.kind = WORLD_SPELUNK_COMMAND_MOVE;
	step->command.dx = dx;
	step->command.dy = dy;
}

static void build_witness(struct world_spelunk_traversal_witness *witness,
		struct world_spelunk_witness_step *steps)
{
	set_step(&steps[0], WORLD_SPELUNK_WITNESS_COMMAND, 2, 3);
	steps[0].command.kind = WORLD_SPELUNK_COMMAND_GRIP;
	set_move(&steps[1], 1, 0, 3, 3);
	set_step(&steps[2], WORLD_SPELUNK_WITNESS_PLACE_PITON, 3, 3);
	set_step(&steps[3], WORLD_SPELUNK_WITNESS_DEPLOY_ROPE, 3, 3);
	set_move(&steps[4], 0, 1, 3, 4);
	set_move(&steps[5], 0, 1, 3, 5);
	set_step(&steps[6], WORLD_SPELUNK_WITNESS_CHECKPOINT, 3, 5);
	steps[6].checkpoint_mask = 0x01;
	set_move(&steps[7], 0, -1, 3, 4);
	set_move(&steps[8], 0, -1, 3, 3);
	set_move(&steps[9], -1, 0, 2, 3);
	memset(witness, 0, sizeof(*witness));
	witness->start_x = 2;
	witness->start_y = 3;
	witness->steps = steps;
	witness->step_count = 10;
	witness->required_checkpoint_mask = 0x01;
}

static int test_climbing_route_replays_and_returns(void *unused)
{
	struct world_spelunk_generated_layout generated;
	enum world_spelunk_tile cells[49];
	struct world_spelunk_traversal_profile profile = proof_profile();
	struct world_spelunk_witness_step steps[10];
	struct world_spelunk_traversal_witness witness;
	struct world_spelunk_proof_report report;

	(void)unused;
	build_shaft(&generated, cells);
	build_witness(&witness, steps);
	eq(world_spelunk_replay_traversal_witness(&generated, &profile,
		&witness, &report), WORLD_SPELUNK_PROOF_OK);
	eq(report.result, WORLD_SPELUNK_PROOF_OK);
	eq(report.steps_completed, 10);
	eq(report.final_x, 2);
	eq(report.final_y, 3);
	eq(report.pitons_used, 1);
	eq(report.rope_segments_used, 2);
	eq(report.fall_damage, 0);
	eq(report.reached_checkpoint_mask, 0x01U);
	ok;
}

static int test_budget_and_route_drift_fail_closed(void *unused)
{
	struct world_spelunk_generated_layout generated;
	enum world_spelunk_tile cells[49];
	struct world_spelunk_traversal_profile profile = proof_profile();
	struct world_spelunk_witness_step steps[10];
	struct world_spelunk_traversal_witness witness;
	struct world_spelunk_proof_report report;

	(void)unused;
	build_shaft(&generated, cells);
	build_witness(&witness, steps);
	profile.piton_budget = 0;
	eq(world_spelunk_replay_traversal_witness(&generated, &profile,
		&witness, &report), WORLD_SPELUNK_PROOF_EQUIPMENT_BUDGET);
	eq(report.failed_step, 2);
	eq(report.pitons_used, 0);

	profile = proof_profile();
	steps[5].expected_y = 4;
	eq(world_spelunk_replay_traversal_witness(&generated, &profile,
		&witness, &report), WORLD_SPELUNK_PROOF_POSITION_MISMATCH);
	eq(report.failed_step, 5);
	eq(report.final_y, 5);
	ok;
}

const char *suite_name = "game/spelunking-traversal-proof";
struct test tests[] = {
	{ "climbing route replays and returns",
		test_climbing_route_replays_and_returns },
	{ "budget and route drift fail closed",
		test_budget_and_route_drift_fail_closed },
	{ NULL, NULL }
};
