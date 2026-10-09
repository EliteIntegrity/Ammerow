/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Melee, launcher, and throwing parity in side view. */

#include "spelunking-integration-fixture.h"

static int test_spelunking_combat_slice(void *state)
{
	enum world_spelunk_tile cells[36];
	struct world_spelunk_actor_report actor_reports[WORLD_SPELUNK_ACTOR_MAX];
	struct level *test_level;
	const struct world_spelunk_actor *actor;
	size_t report_count = 0;
	int32_t experience_before;
	int hp_before;
	int actor_phases;
	int actor_energy_gain;
	int i;
	bool command_processed;
	bool actors_processed;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 6, 6, 2, 4, 90));
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	require(world_spelunk_player_is_active(player));
	player->spelunking->actor_roster_initialized = true;
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 3, 4));
	player->spelunking->actors[0].hp = 1;

	/* A movement command into a creature becomes a real melee turn.  The
	 * player stays put, standard hit/damage/experience authorities are used,
	 * and the persistent actor is removed on death. */
	experience_before = player->exp;
	player->upkeep->energy_use = 0;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, 1, 0), 0);
	rand_fix(100);
	command_processed = cmdq_pop(CTX_GAME);
	rand_unfix();
	require(command_processed);
	eq(player->spelunking->state.x, 2);
	eq(player->spelunking->state.y, 4);
	eq(player->upkeep->energy_use, z_info->move_energy);
	eq(player->spelunking->actor_count, 0);
	require(player->exp >= experience_before + 2);

	/* A visible skitter accumulates normal-speed energy rather than acting on
	 * every scheduler tick.  Its first ready action closes the supported gap;
	 * its next ready action uses shared HP/AC. */
	player->upkeep->energy_use = 0;
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 4));
	actor_energy_gain = turn_energy(110);
	require(actor_energy_gain > 0);
	actor_phases = (int)((player->spelunking->state.rules.action_energy +
		actor_energy_gain - 1) / actor_energy_gain);
	for (i = 1; i < actor_phases; i++) {
		require(world_spelunk_process_actors(player, actor_reports,
			N_ELEMENTS(actor_reports), &report_count));
		eq(report_count, 0);
	}
	require(world_spelunk_process_actors(player, actor_reports,
		N_ELEMENTS(actor_reports), &report_count));
	eq(report_count, 1);
	eq(actor_reports[0].event, WORLD_SPELUNK_ACTOR_EVENT_MOVED);
	eq(actor_reports[0].from_x, 4);
	eq(actor_reports[0].to_x, 3);
	actor = world_spelunk_runtime_actor_at(player->spelunking, 3, 4);
	notnull(actor);
	hp_before = player->chp;
	for (i = 1; i < actor_phases; i++) {
		require(world_spelunk_process_actors(player, actor_reports,
			N_ELEMENTS(actor_reports), &report_count));
		eq(report_count, 0);
	}
	rand_fix(100);
	actors_processed = world_spelunk_process_actors(player, actor_reports,
		N_ELEMENTS(actor_reports), &report_count);
	rand_unfix();
	require(actors_processed);
	eq(report_count, 1);
	eq(actor_reports[0].event, WORLD_SPELUNK_ACTOR_EVENT_HIT);
	require(actor_reports[0].damage >= 1 && actor_reports[0].damage <= 2);
	eq(player->chp, hp_before - actor_reports[0].damage);

	/* The shared game loop must translate one ordinary player action into no
	 * more than one ready normal-speed actor action.  Before cadence accounting,
	 * this wait let the adjacent skitter attack on every energy tick. */
	player->energy = z_info->move_energy;
	player->upkeep->energy_use = 0;
	hp_before = player->chp;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	run_game_loop();
	require(!player->is_dead);
	require(player->chp <= hp_before && player->chp >= hp_before - 2);
	ok;
}

static int test_spelunking_launcher_fire(void *state)
{
	enum world_spelunk_tile cells[60];
	struct object_kind *bow_kind;
	struct object_kind *arrow_kind;
	struct object *bow;
	struct object *arrows;
	struct object *recovered;
	struct level *test_level;
	struct chunk *top_down_cave;
	struct loc target;
	int expected_energy;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 60; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 10; i++) cells[5 * 10 + i] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 10, 6, 2, 4, 90));
	world_spelunk_visibility_follow_player(player->spelunking);
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	player->spelunking->actor_roster_initialized = true;

	bow_kind = lookup_kind(TV_BOW, 1);
	notnull(bow_kind);
	bow = make_test_object(bow_kind, 1);
	notnull(bow);
	inven_carry(player, bow, false, false);
	inven_wield(bow, wield_slot(bow));
	require(player->state.ammo_tval > 0);
	arrow_kind = lookup_kind(player->state.ammo_tval, 1);
	notnull(arrow_kind);
	arrows = make_test_object(arrow_kind, 3);
	notnull(arrows);
	inven_carry(player, arrows, false, false);
	arrows = carried_kind_object(player, arrow_kind);
	notnull(arrows);
	require(player->state.num_shots > 0);
	expected_energy = z_info->move_energy * 10 / player->state.num_shots;

	/* Tab's nearest-target convenience selects a visible actor through the
	 * side-view geometry, then queues the ordinary launcher command. */
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 3, 4));
	player->spelunking->actors[0].hp = 1;
	require(world_spelunk_closest_visible_actor(player,
		MIN(6 + 2 * player->state.ammo_mult, z_info->max_range), &target));
	eq(target.x, 3);
	eq(target.y, 4);
	player->upkeep->energy_use = 0;
	do_cmd_fire_at_nearest();
	top_down_cave = cave;
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	eq(player->upkeep->energy_use, expected_energy);
	eq(player->spelunking->actor_count, 0);
	eq(carried_kind_count(player, arrow_kind), 2);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	recovered = world_spelunk_runtime_ground_object_at(player->spelunking,
		3, 4);
	notnull(recovered);
	require(recovered->kind == arrow_kind);

	/* Directional firing uses the same command but the persistent cave owns
	 * collision and recovery: rock stops the shot and protects an actor beyond
	 * it even while the native top-down cave pointer is absent. */
	player->spelunking->cells[4 * 10 + 4] = WORLD_SPELUNK_ROCK;
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 6, 4));
	player->upkeep->energy_use = 0;
	arrows = carried_kind_object(player, arrow_kind);
	notnull(arrows);
	eq(cmdq_push(CMD_FIRE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", arrows);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	eq(player->upkeep->energy_use, expected_energy);
	eq(player->spelunking->actor_count, 1);
	eq(carried_kind_count(player, arrow_kind), 1);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 2);
	recovered = world_spelunk_runtime_ground_object_at(player->spelunking,
		3, 4);
	notnull(recovered);
	require(recovered->kind == arrow_kind);
	ok;
}

static int test_spelunking_throwing(void *state)
{
	enum world_spelunk_tile cells[60];
	struct object_kind *dagger_kind;
	struct object *dagger;
	struct object *recovered;
	struct level *test_level;
	struct chunk *top_down_cave;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 60; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 10; i++) cells[5 * 10 + i] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 10, 6, 2, 4, 90));
	world_spelunk_visibility_follow_player(player->spelunking);
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	player->spelunking->actor_roster_initialized = true;

	dagger_kind = lookup_kind(TV_SWORD, lookup_sval(TV_SWORD, "dagger"));
	notnull(dagger_kind);
	dagger = make_test_object(dagger_kind, 1);
	notnull(dagger);
	inven_carry(player, dagger, false, false);
	inven_wield(dagger, wield_slot(dagger));
	require(object_is_equipped(player->body, dagger));
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 3, 4));
	player->spelunking->actors[0].hp = 1;
	top_down_cave = cave;

	/* Rejection is transactional: attempting to throw an equipped weapon
	 * without stable footing must not silently take it off. */
	player->spelunking->cells[5 * 10 + 2] = WORLD_SPELUNK_AIR;
	player->spelunking->state.movement = WORLD_SPELUNK_FALLING;
	player->spelunking->state.fall_start_y = 4;
	require(world_spelunk_runtime_is_valid(player->spelunking));
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_THROW), 0);
	cmd_set_arg_item(cmdq_peek(), "item", dagger);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	eq(player->upkeep->energy_use, 0);
	require(object_is_equipped(player->body, dagger));
	eq(player->spelunking->actor_count, 1);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 0);
	player->spelunking->cells[5 * 10 + 2] = WORLD_SPELUNK_ROCK;
	player->spelunking->state.movement = WORLD_SPELUNK_STANDING;
	player->spelunking->state.fall_start_y = 4;
	require(world_spelunk_runtime_is_valid(player->spelunking));

	/* Throwing keeps its distinct equipment and timing rules while trajectory,
	 * actor collision and the recovered object belong only to the side view. */
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_THROW), 0);
	cmd_set_arg_item(cmdq_peek(), "item", dagger);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	eq(player->upkeep->energy_use, z_info->move_energy);
	eq(player->spelunking->actor_count, 0);
	require(!object_is_carried(player, dagger));
	require(!object_is_equipped(player->body, dagger));
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	recovered = world_spelunk_runtime_ground_object_at(player->spelunking,
		3, 4);
	require(recovered == dagger);
	require(recovered->kind == dagger_kind);
	ok;
}

const char *suite_name = "game/spelunking-combat";
struct test tests[] = {
	{ "combat slice", test_spelunking_combat_slice },
	{ "launcher fire", test_spelunking_launcher_fire },
	{ "throwing", test_spelunking_throwing },
	{ NULL, NULL }
};
