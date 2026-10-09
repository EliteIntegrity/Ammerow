/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Movement, falling, water, grip, jump, and rope contracts. */

#include "spelunking-fixture.h"

static int test_rejected_and_turn_contract(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command move = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 5);
	fixture_rock(&f, 3, 4);
	require(fixture_init(&f, 2, 4, 100));
	eq(world_spelunk_apply_command(&f.state, &move, &report),
		WORLD_SPELUNK_ACTION_REJECTED);
	eq(report.energy_use, 0);
	eq(f.state.x, 2);
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.energy_use, 100);
	ok;
}

static int test_initial_airborne_state_and_edge_grip(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command grip = {
		WORLD_SPELUNK_COMMAND_GRIP, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	require(fixture_init(&f, 0, 3, 100));
	eq(f.state.movement, WORLD_SPELUNK_FALLING);
	eq(world_spelunk_apply_command(&f.state, &grip, &report),
		WORLD_SPELUNK_ACTION_FREE);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIP_CLEARED);
	eq(f.state.grip_target_x, -1);
	eq(f.state.grip_target_y, -1);
	ok;
}

static int test_safe_and_damaging_falls(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	require(fixture_init(&f, 2, 3, 100));
	f.state.movement = WORLD_SPELUNK_FALLING;
	f.state.fall_start_y = 3;
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.y, 5);
	eq(report.damage, 0);
	eq(report.event, WORLD_SPELUNK_EVENT_LANDED);

	fixture_open(&f);
	fixture_ground_row(&f, 8);
	require(fixture_init(&f, 2, 4, 100));
	f.state.movement = WORLD_SPELUNK_FALLING;
	f.state.fall_start_y = 4;
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.y, 7);
	eq(report.damage, 10);
	eq(report.fall_tiles, 3);
	eq(report.event, WORLD_SPELUNK_EVENT_FALL_HURT);
	ok;
}

static int test_severe_fall_reports_shared_damage(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	require(fixture_init(&f, 2, 2, 100));
	f.state.movement = WORLD_SPELUNK_FALLING;
	f.state.fall_start_y = 2;
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.damage, 80);
	eq(report.fall_tiles, 6);
	eq(report.energy_use, 100);
	ok;
}

static int test_water_catches_falls_and_allows_swimming(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};
	struct world_spelunk_command down = {
		WORLD_SPELUNK_COMMAND_MOVE, 0, 1
	};
	struct world_spelunk_command left = {
		WORLD_SPELUNK_COMMAND_MOVE, -1, 0
	};
	int x;
	int y;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 8);
	for (y = 6; y <= 7; y++) {
		for (x = 2; x <= 5; x++) fixture_water(&f, x, y);
	}
	require(fixture_init(&f, 3, 2, 100));
	f.state.movement = WORLD_SPELUNK_FALLING;
	f.state.fall_start_y = 2;
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 3);
	eq(f.state.y, 6);
	eq(f.state.movement, WORLD_SPELUNK_SWIMMING);
	eq(report.damage, 0);
	eq(report.event, WORLD_SPELUNK_EVENT_WATER_LANDED);
	eq(f.state.stamina, 100);
	require(world_spelunk_state_is_valid(&f.state));
	eq(world_spelunk_apply_command(&f.state, &down, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.y, 7);
	eq(f.state.movement, WORLD_SPELUNK_SWIMMING);
	eq(report.event, WORLD_SPELUNK_EVENT_SWAM);
	eq(f.state.stamina, 98);
	eq(world_spelunk_apply_command(&f.state, &left, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 2);
	eq(f.state.movement, WORLD_SPELUNK_SWIMMING);
	eq(f.state.stamina, 96);
	eq(world_spelunk_apply_command(&f.state, &left, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 1);
	eq(f.state.y, 7);
	eq(f.state.movement, WORLD_SPELUNK_STANDING);
	eq(report.event, WORLD_SPELUNK_EVENT_MOVED);
	eq(f.state.stamina, 96);
	require(world_spelunk_state_is_valid(&f.state));
	ok;
}

static int test_breath_scuba_and_drowning_rules(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_breath_report report;
	int i;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 5);
	fixture_water(&f, 2, 4);
	fixture_water(&f, 2, 3);
	require(fixture_init(&f, 2, 4, 100));
	eq(f.state.movement, WORLD_SPELUNK_SWIMMING);
	eq(f.state.breath, 6);
	for (i = 0; i < f.rules.breath_turns; i++) {
		require(world_spelunk_apply_breath_turn(&f.state, false, &report));
		eq(report.event, WORLD_SPELUNK_BREATH_HELD);
		eq(report.damage, 0);
	}
	eq(f.state.breath, 0);
	require(world_spelunk_apply_breath_turn(&f.state, false, &report));
	eq(report.event, WORLD_SPELUNK_BREATH_DROWNING);
	eq(report.damage, f.rules.drowning_damage);
	eq(f.state.breath, 0);

	/* Water with clear air immediately overhead is the surface.  It restores
	 * breath while ordinary swimming actions continue to spend stamina. */
	f.state.y = 3;
	f.state.stamina = 50;
	require(!world_spelunk_is_submerged(&f.state));
	require(world_spelunk_apply_breath_turn(&f.state, false, &report));
	eq(report.event, WORLD_SPELUNK_BREATH_RESTORED);
	eq(f.state.breath, f.rules.breath_turns);
	{
		struct world_spelunk_action_report action_report;
		struct world_spelunk_command wait = {
			WORLD_SPELUNK_COMMAND_WAIT, 0, 0
		};

		eq(world_spelunk_apply_command(&f.state, &wait, &action_report),
			WORLD_SPELUNK_ACTION_TURN);
		eq(f.state.stamina, 48);
	}

	/* A charged supply is consumed before held breath. */
	f.state.y = 4;
	f.state.breath = 3;
	require(world_spelunk_apply_breath_turn(&f.state, true, &report));
	eq(report.event, WORLD_SPELUNK_BREATH_SCUBA_USED);
	eq(report.air_supply_used, 1);
	eq(f.state.breath, 3);

	/* Reaching a supported air cell restores the bounded personal reserve. */
	f.state.x = 1;
	f.state.movement = WORLD_SPELUNK_STANDING;
	f.state.fall_start_y = f.state.y;
	require(world_spelunk_state_is_valid(&f.state));
	require(world_spelunk_apply_breath_turn(&f.state, false, &report));
	eq(report.event, WORLD_SPELUNK_BREATH_RESTORED);
	eq(f.state.breath, f.rules.breath_turns);
	ok;
}

static int test_walking_off_ledge_resolves_once(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command move = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_rock(&f, 1, 5);
	fixture_rock(&f, 2, 8);
	require(fixture_init(&f, 1, 4, 100));
	eq(world_spelunk_apply_command(&f.state, &move, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 2);
	eq(f.state.y, 7);
	eq(report.damage, 10);
	eq(report.energy_use, 100);
	ok;
}

static int test_pending_fall_ignores_move_direction(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command move = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	require(fixture_init(&f, 2, 3, 100));
	f.state.movement = WORLD_SPELUNK_FALLING;
	f.state.fall_start_y = 3;
	eq(world_spelunk_apply_command(&f.state, &move, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 2);
	eq(f.state.y, 5);
	ok;
}

static int test_jump_reach_and_release(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command jump = {
		WORLD_SPELUNK_COMMAND_JUMP, 0, 0
	};
	struct world_spelunk_command move = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 5);
	require(fixture_init(&f, 2, 4, 100));
	eq(world_spelunk_apply_command(&f.state, &jump, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.y, 3);
	eq(f.state.stamina, 90);
	eq(f.state.movement, WORLD_SPELUNK_JUMP_APEX);
	eq(world_spelunk_apply_command(&f.state, &move, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 4);
	eq(f.state.y, 3);
	require(f.state.jump_holding);
	eq(report.event, WORLD_SPELUNK_EVENT_JUMP_HELD);
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.y, 4);
	eq(f.state.movement, WORLD_SPELUNK_STANDING);
	ok;
}

static int test_grip_is_free_and_catches_jump(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command grip = {
		WORLD_SPELUNK_COMMAND_GRIP, 0, 0
	};
	struct world_spelunk_command jump = {
		WORLD_SPELUNK_COMMAND_JUMP, 0, 0
	};
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 5);
	fixture_rock(&f, 1, 4);
	require(fixture_init(&f, 2, 4, 100));
	eq(world_spelunk_apply_command(&f.state, &grip, &report),
		WORLD_SPELUNK_ACTION_FREE);
	eq(report.energy_use, 0);
	eq(f.state.grip_target_x, 1);
	eq(f.state.grip_target_y, 4);
	eq(world_spelunk_apply_command(&f.state, &jump, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.movement, WORLD_SPELUNK_CLIMBING);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIPPED);
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.stamina, 85);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIP_DRAINED);
	ok;
}

static int test_piton_jump_and_grip_during_both_jump_stages(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_action_report report;
	struct world_spelunk_command jump = {
		WORLD_SPELUNK_COMMAND_JUMP, 0, 0
	};
	struct world_spelunk_command right = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};
	struct world_spelunk_command grip = {
		WORLD_SPELUNK_COMMAND_GRIP, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 5);
	fixture_rock(&f, 1, 4);
	fixture_rock(&f, 5, 3);
	require(fixture_init(&f, 2, 4, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	eq(world_spelunk_runtime_place_piton(runtime),
		WORLD_SPELUNK_INFRASTRUCTURE_OK);
	require(world_spelunk_runtime_has_piton(runtime, 2, 4));

	/* A piton is secure support, not a state that suppresses jumping.  A grip
	 * selected after the initial rise catches the player immediately. */
	eq(world_spelunk_apply_command(&runtime->state, &jump, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(runtime->state.y, 3);
	eq(runtime->state.movement, WORLD_SPELUNK_JUMP_APEX);
	eq(world_spelunk_apply_command(&runtime->state, &grip, &report),
		WORLD_SPELUNK_ACTION_FREE);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIPPED);
	eq(runtime->state.movement, WORLD_SPELUNK_CLIMBING);

	/* Repeat without the nearby rise hold.  The horizontal jump reaches its
	 * one-turn pause, where selecting a new hold must still arrest the drop. */
	runtime->state.x = 2;
	runtime->state.y = 4;
	runtime->state.stamina = runtime->state.max_stamina;
	runtime->state.fall_start_y = 4;
	runtime->state.grip_target_x = -1;
	runtime->state.grip_target_y = -1;
	runtime->state.movement = WORLD_SPELUNK_STANDING;
	runtime->state.jump_holding = false;
	require(world_spelunk_runtime_is_valid(runtime));
	eq(world_spelunk_apply_command(&runtime->state, &jump, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(world_spelunk_apply_command(&runtime->state, &right, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(runtime->state.x, 4);
	require(runtime->state.jump_holding);
	eq(world_spelunk_apply_command(&runtime->state, &grip, &report),
		WORLD_SPELUNK_ACTION_FREE);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIPPED);
	eq(runtime->state.grip_target_x, 5);
	eq(runtime->state.grip_target_y, 3);
	eq(runtime->state.movement, WORLD_SPELUNK_CLIMBING);
	require(!runtime->state.jump_holding);
	require(world_spelunk_runtime_is_valid(runtime));

	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_piton_support_no_rest_landing_and_grip_cleanup(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_action_report report;
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};
	struct world_spelunk_command jump = {
		WORLD_SPELUNK_COMMAND_JUMP, 0, 0
	};
	struct world_spelunk_command right = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 5);
	fixture_rock(&f, 1, 4);
	require(fixture_init(&f, 2, 4, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	eq(world_spelunk_runtime_place_piton(runtime),
		WORLD_SPELUNK_INFRASTRUCTURE_OK);
	/* Remove the ordinary floor under the anchor: every assertion below now
	 * depends on the piton being a genuine support surface. */
	runtime->cells[5 * TEST_WIDTH + 2] = WORLD_SPELUNK_AIR;
	runtime->state.stamina = 40;
	require(world_spelunk_is_grounded(&runtime->state));
	require(world_spelunk_runtime_is_valid(runtime));
	eq(world_spelunk_apply_command(&runtime->state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_RESTED);
	/* An isolated piton is secure enough to stand on, land on and jump from,
	 * but does not regenerate stamina like a proper ledge. */
	eq(runtime->state.stamina, 40);

	/* Jumping from the anchor is legal, and gravity lands on that same piton
	 * instead of scanning through it to the cave floor. */
	eq(world_spelunk_apply_command(&runtime->state, &jump, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(runtime->state.y, 3);
	eq(world_spelunk_apply_command(&runtime->state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_LANDED);
	eq(report.damage, 0);
	eq(runtime->state.y, 4);
	eq(runtime->state.movement, WORLD_SPELUNK_STANDING);

	/* Once normal movement carries the player beyond a selected hold, both the
	 * authoritative target and its view marker are retired immediately. */
	runtime->state.grip_target_x = 1;
	runtime->state.grip_target_y = 4;
	require(world_spelunk_has_active_grip(&runtime->state));
	eq(world_spelunk_apply_command(&runtime->state, &right, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(runtime->state.x, 3);
	eq(runtime->state.grip_target_x, -1);
	eq(runtime->state.grip_target_y, -1);
	require(!world_spelunk_has_active_grip(&runtime->state));
	require(world_spelunk_runtime_is_valid(runtime));

	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_exhausted_grip_falls(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 8);
	fixture_rock(&f, 1, 3);
	require(fixture_init(&f, 2, 3, 5));
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 3;
	f.state.movement = WORLD_SPELUNK_CLIMBING;
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.stamina, 0);
	eq(f.state.y, 7);
	eq(report.damage, 20);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIP_EXHAUSTED);
	ok;
}

static int test_directional_grip_costs(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command up = {
		WORLD_SPELUNK_COMMAND_MOVE, 0, -1
	};
	struct world_spelunk_command down = {
		WORLD_SPELUNK_COMMAND_MOVE, 0, 1
	};
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	fixture_rock(&f, 1, 4);
	f.rules.grip_up_stamina_cost = 30;
	f.rules.grip_lateral_stamina_cost = 23;
	f.rules.grip_down_stamina_cost = 16;
	require(fixture_init(&f, 2, 4, 100));
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 4;
	f.state.movement = WORLD_SPELUNK_CLIMBING;
	eq(world_spelunk_apply_command(&f.state, &up, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.stamina, 70);

	require(fixture_init(&f, 2, 4, 100));
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 4;
	f.state.movement = WORLD_SPELUNK_CLIMBING;
	eq(world_spelunk_apply_command(&f.state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.stamina, 77);

	require(fixture_init(&f, 2, 4, 100));
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 4;
	f.state.movement = WORLD_SPELUNK_CLIMBING;
	eq(world_spelunk_apply_command(&f.state, &down, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.stamina, 84);
	ok;
}

static int test_grip_allows_diagonal_move_and_deliberate_release(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command diagonal = {
		WORLD_SPELUNK_COMMAND_MOVE, -1, 1
	};
	struct world_spelunk_command away = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};
	struct world_spelunk_command grip = {
		WORLD_SPELUNK_COMMAND_GRIP, 0, 0
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	fixture_rock(&f, 1, 3);
	require(fixture_init(&f, 2, 3, 100));
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 3;
	f.state.movement = WORLD_SPELUNK_CLIMBING;
	require(world_spelunk_state_is_valid(&f.state));
	eq(world_spelunk_apply_command(&f.state, &diagonal, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 1);
	eq(f.state.y, 4);
	eq(f.state.movement, WORLD_SPELUNK_CLIMBING);
	eq(f.state.stamina, 95);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIP_DRAINED);

	/* A grip is protection, not a movement lock.  Deliberately moving beyond
	 * its adjacent reach commits the move and resolves the resulting fall. */
	eq(world_spelunk_apply_command(&f.state, &away, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 2);
	eq(f.state.y, 4);
	eq(f.state.movement, WORLD_SPELUNK_CLIMBING);
	/* The first step is still a valid diagonal grip; the second leaves its
	 * one-cell reach and therefore becomes the deliberate release. */
	eq(world_spelunk_apply_command(&f.state, &away, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 3);
	eq(f.state.y, 8);
	eq(f.state.movement, WORLD_SPELUNK_STANDING);
	eq(report.event, WORLD_SPELUNK_EVENT_FALL_HURT);
	eq(report.damage, 20);

	/* A diagonally adjacent face is a first-class grip candidate. */
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	fixture_rock(&f, 1, 2);
	require(fixture_init(&f, 2, 3, 100));
	eq(world_spelunk_apply_command(&f.state, &grip, &report),
		WORLD_SPELUNK_ACTION_FREE);
	eq(f.state.grip_target_x, 1);
	eq(f.state.grip_target_y, 2);
	eq(f.state.movement, WORLD_SPELUNK_CLIMBING);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIPPED);
	ok;
}

static int test_supported_corner_allows_diagonal_squeeze(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_action_report report;
	struct world_spelunk_command up_right = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, -1
	};

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	fixture_rock(&f, 3, 5);
	fixture_rock(&f, 2, 4);
	require(fixture_init(&f, 2, 5, 100));
	/* The open target is supported by the side rock.  The two blocked
	 * orthogonal cells form the classic diagonal corner squeeze. */
	eq(world_spelunk_apply_command(&f.state, &up_right, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 3);
	eq(f.state.y, 4);
	eq(f.state.movement, WORLD_SPELUNK_STANDING);
	eq(report.event, WORLD_SPELUNK_EVENT_MOVED);

	/* An open diagonal without those bracing corners still needs a grip. */
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	require(fixture_init(&f, 2, 5, 100));
	eq(world_spelunk_apply_command(&f.state, &up_right, &report),
		WORLD_SPELUNK_ACTION_REJECTED);
	ok;
}

static int test_rope_traversal_rest_and_exhaustion(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_action_report report;
	struct world_spelunk_command down = {
		WORLD_SPELUNK_COMMAND_MOVE, 0, 1
	};
	struct world_spelunk_command up = {
		WORLD_SPELUNK_COMMAND_MOVE, 0, -1
	};
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};
	struct world_spelunk_command right = {
		WORLD_SPELUNK_COMMAND_MOVE, 1, 0
	};
	struct world_spelunk_command grip = {
		WORLD_SPELUNK_COMMAND_GRIP, 0, 0
	};
	int segments = 0;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 8);
	fixture_rock(&f, 1, 2);
	f.rules.rope_max_length = 5;
	f.rules.rope_up_stamina_cost = 5;
	f.rules.rope_lateral_stamina_cost = 3;
	f.rules.rope_down_stamina_cost = 1;
	require(fixture_init(&f, 2, 2, 10));
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 2;
	f.state.movement = WORLD_SPELUNK_CLIMBING;
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	eq(world_spelunk_runtime_place_piton(runtime),
		WORLD_SPELUNK_INFRASTRUCTURE_OK);
	eq(world_spelunk_runtime_deploy_rope(runtime, 5, &segments),
		WORLD_SPELUNK_INFRASTRUCTURE_OK);
	eq(segments, 5);

	/* A fall that reaches a usable rope attaches at the first segment rather
	 * than continuing through the complete shaft. */
	runtime->state.y = 3;
	runtime->state.fall_start_y = 2;
	runtime->state.movement = WORLD_SPELUNK_FALLING;
	require(world_spelunk_runtime_is_valid(runtime));
	eq(world_spelunk_apply_command(&runtime->state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_ROPE_ATTACHED);
	eq(runtime->state.y, 3);
	eq(runtime->state.movement, WORLD_SPELUNK_HANGING);
	runtime->state.y = 2;
	runtime->state.fall_start_y = 2;
	runtime->state.movement = WORLD_SPELUNK_STANDING;
	require(world_spelunk_runtime_is_valid(runtime));

	/* First contact is free; subsequent hanging turns use the cheaper rope
	 * cost rather than the bare-rock grip cost. */
	eq(world_spelunk_apply_command(&runtime->state, &down, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_ROPE_ATTACHED);
	eq(runtime->state.stamina, 10);
	eq(runtime->state.movement, WORLD_SPELUNK_HANGING);
	eq(world_spelunk_apply_command(&runtime->state, &down, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_ROPE_DRAINED);
	eq(runtime->state.stamina, 9);
	eq(world_spelunk_apply_command(&runtime->state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_ROPE_DRAINED);
	eq(runtime->state.stamina, 6);
	eq(world_spelunk_apply_command(&runtime->state, &up, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(runtime->state.stamina, 1);
	eq(world_spelunk_apply_command(&runtime->state, &up, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(runtime->state.movement, WORLD_SPELUNK_STANDING);
	eq(runtime->state.y, 2);
	eq(world_spelunk_apply_command(&runtime->state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_RESTED);
	eq(runtime->state.stamina, 1);

	/* Exhaustion while hanging releases the rope and resolves the complete fall
	 * during the same shared turn. */
	runtime->state.stamina = 1;
	eq(world_spelunk_apply_command(&runtime->state, &down, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_ROPE_ATTACHED);
	eq(world_spelunk_apply_command(&runtime->state, &wait, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_ROPE_EXHAUSTED);
	eq(runtime->state.stamina, 0);
	eq(runtime->state.y, 7);
	eq(runtime->state.movement, WORLD_SPELUNK_STANDING);
	eq(report.damage, 20);
	require(world_spelunk_runtime_is_valid(runtime));

	/* Leaving the rope without another hold is a committed fall, while an
	 * adjacent piton can be deliberately selected as a normal grip target. */
	runtime->state.x = 2;
	runtime->state.y = 3;
	runtime->state.stamina = 10;
	runtime->state.fall_start_y = 3;
	runtime->state.movement = WORLD_SPELUNK_HANGING;
	require(world_spelunk_runtime_is_valid(runtime));
	eq(world_spelunk_apply_command(&runtime->state, &right, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_FALL_HURT);
	eq(report.damage, 20);
	eq(runtime->state.x, 3);
	eq(runtime->state.y, 7);
	runtime->state.x = 3;
	runtime->state.y = 2;
	runtime->state.fall_start_y = 2;
	runtime->state.grip_target_x = -1;
	runtime->state.grip_target_y = -1;
	runtime->state.movement = WORLD_SPELUNK_FALLING;
	require(world_spelunk_runtime_is_valid(runtime));
	eq(world_spelunk_apply_command(&runtime->state, &grip, &report),
		WORLD_SPELUNK_ACTION_FREE);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIPPED);
	eq(runtime->state.grip_target_x, 2);
	eq(runtime->state.grip_target_y, 2);
	eq(runtime->state.movement, WORLD_SPELUNK_CLIMBING);
	require(world_spelunk_runtime_is_valid(runtime));

	world_spelunk_runtime_free(runtime);
	ok;
}


static int test_rope_anchor_and_ledge_transfers(void *unused)
{
	int side;
	(void)unused;

	/* Exercise both shaft edges: a rope reaches its anchor directly, but
	 * leaving the anchor for the higher ledge requires a diagonal step. */
	for (side = -1; side <= 1; side += 2) {
		struct spelunk_fixture f;
		struct world_spelunk_runtime *runtime;
		struct world_spelunk_action_report report;
		struct world_spelunk_command up = { WORLD_SPELUNK_COMMAND_MOVE, 0, -1 };
		struct world_spelunk_command up_side = { WORLD_SPELUNK_COMMAND_MOVE, side, -1 };
		struct world_spelunk_command down_back = { WORLD_SPELUNK_COMMAND_MOVE, -side, 1 };
		struct world_spelunk_command down = { WORLD_SPELUNK_COMMAND_MOVE, 0, 1 };
		struct world_spelunk_command wait = { WORLD_SPELUNK_COMMAND_WAIT, 0, 0 };
		int segments;

		fixture_open(&f);
		fixture_ground_row(&f, 8);
		fixture_rock(&f, 3 + side, 3);
		require(fixture_init(&f, 3, 3, 100));
		f.state.grip_target_x = 3 + side;
		f.state.grip_target_y = 3;
		f.state.movement = WORLD_SPELUNK_CLIMBING;
		runtime = world_spelunk_runtime_create("core.spelunk.test.001", &f.state);
		notnull(runtime);
		eq(world_spelunk_runtime_place_piton(runtime), WORLD_SPELUNK_INFRASTRUCTURE_OK);
		eq(world_spelunk_runtime_deploy_rope(runtime, 3, &segments), WORLD_SPELUNK_INFRASTRUCTURE_OK);
		eq(world_spelunk_apply_command(&runtime->state, &down, &report), WORLD_SPELUNK_ACTION_TURN);
		eq(runtime->state.movement, WORLD_SPELUNK_HANGING);
		eq(world_spelunk_apply_command(&runtime->state, &up, &report), WORLD_SPELUNK_ACTION_TURN);
		eq(runtime->state.y, 3);
		eq(runtime->state.movement, WORLD_SPELUNK_STANDING);
		eq(world_spelunk_apply_command(&runtime->state, &up_side, &report), WORLD_SPELUNK_ACTION_TURN);
		eq(runtime->state.x, 3 + side);
		eq(runtime->state.y, 2);
		eq(runtime->state.movement, WORLD_SPELUNK_STANDING);
		eq(report.damage, 0);
		eq(world_spelunk_apply_command(&runtime->state, &down_back, &report), WORLD_SPELUNK_ACTION_TURN);
		eq(runtime->state.x, 3);
		eq(runtime->state.y, 3);
		eq(runtime->state.movement, WORLD_SPELUNK_STANDING);
		runtime->state.stamina = 40;
		eq(world_spelunk_apply_command(&runtime->state, &wait, &report), WORLD_SPELUNK_ACTION_TURN);
		eq(runtime->state.stamina, 40); /* No suspended-anchor regeneration. */
		world_spelunk_runtime_free(runtime);
	}
	ok;
}

static int test_rope_diagonal_exit_and_unsupported_descent(void *unused)
{
	struct spelunk_fixture f;
	uint8_t pitons[TEST_WIDTH * TEST_HEIGHT] = { 0 };
	uint8_t ropes[TEST_WIDTH * TEST_HEIGHT] = { 0 };
	struct world_spelunk_action_report report;
	struct world_spelunk_command up_right = { WORLD_SPELUNK_COMMAND_MOVE, 1, -1 };
	struct world_spelunk_command down_left = { WORLD_SPELUNK_COMMAND_MOVE, -1, 1 };

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 8);
	fixture_rock(&f, 4, 4);
	f.map.pitons = pitons;
	f.map.ropes = ropes;
	f.map.infrastructure_stride = TEST_WIDTH;
	ropes[4 * TEST_WIDTH + 3] = 1;
	require(fixture_init(&f, 3, 4, 100));
	f.state.movement = WORLD_SPELUNK_HANGING;
	eq(world_spelunk_apply_command(&f.state, &up_right, &report), WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 4);
	eq(f.state.y, 3);
	eq(f.state.movement, WORLD_SPELUNK_STANDING);
	eq(world_spelunk_apply_command(&f.state, &down_left, &report), WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 3);
	eq(f.state.y, 4);
	eq(f.state.movement, WORLD_SPELUNK_HANGING);
	/* The same upward input without a destination ledge must not fly. */
	f.cells[4 * TEST_WIDTH + 4] = WORLD_SPELUNK_AIR;
	eq(world_spelunk_apply_command(&f.state, &up_right, &report), WORLD_SPELUNK_ACTION_REJECTED);
	eq(report.energy_use, 0);
	eq(f.state.x, 3);
	eq(f.state.y, 4);
	/* A deliberate diagonal step off a ledge without equipment commits a
	 * fall, including the initial descending tile in the damage distance. */
	fixture_open(&f);
	fixture_ground_row(&f, 8);
	fixture_rock(&f, 4, 4);
	require(fixture_init(&f, 4, 3, 100));
	eq(world_spelunk_apply_command(&f.state, &down_left, &report), WORLD_SPELUNK_ACTION_TURN);
	eq(f.state.x, 3);
	eq(f.state.y, 7);
	eq(f.state.movement, WORLD_SPELUNK_STANDING);
	eq(report.fall_tiles, 4);
	require(report.damage > 0);
	ok;
}

const char *suite_name = "game/spelunking-movement";
struct test tests[] = {
	{ "rejected and turn contract", test_rejected_and_turn_contract },
	{ "initial airborne state and edge grip",
		test_initial_airborne_state_and_edge_grip },
	{ "safe and damaging falls", test_safe_and_damaging_falls },
	{ "severe fall reports shared damage",
		test_severe_fall_reports_shared_damage },
	{ "water catches falls and allows swimming",
		test_water_catches_falls_and_allows_swimming },
	{ "breath scuba and drowning rules",
		test_breath_scuba_and_drowning_rules },
	{ "walking off ledge resolves once",
		test_walking_off_ledge_resolves_once },
	{ "pending fall ignores move direction",
		test_pending_fall_ignores_move_direction },
	{ "jump reach and release", test_jump_reach_and_release },
	{ "grip is free and catches jump", test_grip_is_free_and_catches_jump },
	{ "piton jump and grip during both jump stages",
		test_piton_jump_and_grip_during_both_jump_stages },
	{ "piton support no rest landing and grip cleanup",
		test_piton_support_no_rest_landing_and_grip_cleanup },
	{ "exhausted grip falls", test_exhausted_grip_falls },
	{ "directional grip costs", test_directional_grip_costs },
	{ "grip allows diagonal move and deliberate release",
		test_grip_allows_diagonal_move_and_deliberate_release },
	{ "supported corner allows diagonal squeeze",
		test_supported_corner_allows_diagonal_squeeze },
	{ "rope anchor and ledge transfers", test_rope_anchor_and_ledge_transfers },
	{ "rope diagonal exit and unsupported descent",
		test_rope_diagonal_exit_and_unsupported_descent },
	{ "rope traversal rest and exhaustion",
		test_rope_traversal_rest_and_exhaustion },
	{ NULL, NULL }
};
