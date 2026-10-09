/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Persistence, resources, and native-player adapters. */

#include "spelunking-integration-fixture.h"

static int test_spelunking_save_roundtrip(void *state)
{
	enum world_spelunk_tile cells[36];
	uint8_t material_tags[36] = { 0 };
	uint8_t decoration_tags[36] = { 0 };
	struct world_spelunk_map map = { cells, 6, 6, 6 };
	struct world_spelunk_rules rules;
	struct world_spelunk_state spelunk_state;
	struct object_kind *piton_kind;
	struct object_kind *scuba_kind;
	const struct world_spelunk_actor *skitter;
	const struct world_spelunk_system_node *branch;
	const struct world_spelunk_system_node *planned;
	const struct world_spelunk_recipe_definition *branch_recipe;
	const struct world_spelunk_system_edge *known_edge;
	const struct world_spelunk_system_edge *unknown_edge;
	const struct world_spelunk_system_portal *portal;
	struct world_spelunk_inspect geology_inspect;
	struct object *ground;
	struct object *scuba;
	struct world_spelunk_runtime *branch_runtime;
	struct world_spelunk_runtime *geology_decoded = NULL;
	uint8_t *geology_payload;
	size_t geology_payload_size;
	size_t geology_written;
	int i;

	(void)state;
	require(world_turn_context_for_player(NULL) ==
		WORLD_TURN_CONTEXT_INVALID);
	reset_before_load();
	require(savefile_load("Test1", false));
	null(player->spelunking);
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	cells[2 * 6 + 1] = WORLD_SPELUNK_ROCK;
	for (i = 0; i < 36; i++) {
		if (cells[i] == WORLD_SPELUNK_ROCK) material_tags[i] = 1;
	}
	test_spelunk_rules(&rules, 100);
	rules.rest_stamina_gain = 17;
	rules.rope_down_stamina_cost = 2;
	require(world_spelunk_state_init(&spelunk_state, &map, &rules,
		2, 2, 90));
	spelunk_state.stamina = 61;
	spelunk_state.breath = 3;
	spelunk_state.grip_target_x = 1;
	spelunk_state.grip_target_y = 2;
	player->spelunking = world_spelunk_runtime_create(
		"core.spelunk.test.001", &spelunk_state);
	notnull(player->spelunking);
	require(world_spelunk_runtime_set_geology(player->spelunking,
		"deepwell.limestone-shale", 1, material_tags, decoration_tags));
	player->spelunking->actor_roster_initialized = true;
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 4));
	player->spelunking->actors[0].hp = 3;
	player->spelunking->actors[0].energy = 37;
	piton_kind = lookup_kind(tval_find_idx("tool"),
		lookup_sval(tval_find_idx("tool"), "Piton"));
	scuba_kind = lookup_kind(tval_find_idx("tool"),
		lookup_sval(tval_find_idx("tool"), "Scuba Gear"));
	notnull(piton_kind);
	notnull(scuba_kind);
	scuba = make_test_object(scuba_kind, 1);
	notnull(scuba);
	eq(scuba->pval, 30);
	scuba->pval = 17;
	scuba->known->pval = 17;
	inven_carry(player, scuba, true, false);
	ground = make_test_object(piton_kind, 3);
	notnull(ground);
	ground->note = quark_add("side-view-local");
	ground->known->note = ground->note;
	require(world_spelunk_runtime_add_ground_object(player->spelunking,
		ground, 3, 4));
	player->spelunking->pitons[1 * 6 + 2] = 1;
	player->spelunking->ropes[2 * 6 + 2] = 1;
	player->spelunking->ropes[4 * 6 + 4] = 1;
	/* The rock at 1,2 currently occludes 0,2.  Mark it as previously seen so
	 * the complete Angband save/load path must preserve remembered terrain. */
	player->spelunking->explored[2 * 6] = 1;
	require(!world_spelunk_visibility_is_visible(player->spelunking, 0, 2));
	require(world_spelunk_visibility_is_explored(player->spelunking, 0, 2));
	player->spelunking->state.movement = WORLD_SPELUNK_HANGING;
	require(world_spelunk_runtime_is_valid(player->spelunking));
	require(streq(player->spelunking->geology_profile_id,
		"deepwell.limestone-shale"));
	eq(player->spelunking->geology_version, 1);
	eq(player->spelunking->material_tags[2 * 6 + 1], 1);
	eq(player->spelunking->decoration_tags[2 * 6 + 1], 0);
	geology_payload_size = world_spelunk_runtime_encoded_size(
		player->spelunking);
	require(geology_payload_size > 0);
	geology_payload = mem_alloc(geology_payload_size);
	eq(world_spelunk_runtime_encode(player->spelunking, geology_payload,
		geology_payload_size, &geology_written), WORLD_SPELUNK_CODEC_OK);
	eq(geology_written, geology_payload_size);
	eq(world_spelunk_runtime_decode(geology_payload, geology_written,
		&geology_decoded), WORLD_SPELUNK_CODEC_OK);
	notnull(geology_decoded);
	require(streq(geology_decoded->geology_profile_id,
		"deepwell.limestone-shale"));
	world_spelunk_runtime_free(geology_decoded);
	mem_free(geology_payload);
	/* A system can retain materialized and planned optional sections while
	 * the compatibility alias remains on the active root. */
	require(player_spelunking_ensure_system(player));
	player->spelunking_system->graph_version =
		WORLD_SPELUNK_SYSTEM_GRAPH_VERSION;
	spelunk_state.stamina = 44;
	spelunk_state.x = 2;
	spelunk_state.y = 4;
	spelunk_state.fall_start_y = 4;
	spelunk_state.grip_target_x = -1;
	spelunk_state.grip_target_y = -1;
	spelunk_state.movement = WORLD_SPELUNK_STANDING;
	branch_runtime = world_spelunk_runtime_create(
		"core.spelunk.test.001", &spelunk_state);
	notnull(branch_runtime);
	require(world_spelunk_system_add_node(player->spelunking_system,
		"core.spelunk.test.node.002", "deepwell.broken-galleries", 0x1234,
		1, 1, branch_runtime));
	require(world_spelunk_system_add_node(player->spelunking_system,
		"core.spelunk.test.node.003", "deepwell.drowned-fault", 0x5678,
		1, 2, NULL));
	/* Current graph orientation is semantic: A is the shallower parent and B
	 * is its depth-plus-one child. Reject an inversion at the owner boundary. */
	require(!world_spelunk_system_add_edge(player->spelunking_system,
		"core.spelunk.test.edge.reversed", "core.spelunk.test.node.002",
		"core.spelunk.test.endpoint.reversed.a", "core.spelunk.test.001",
		"core.spelunk.test.endpoint.reversed.b", false));
	require(world_spelunk_system_add_edge(player->spelunking_system,
		"core.spelunk.test.edge.001", "core.spelunk.test.001",
		"core.spelunk.test.endpoint.001a", "core.spelunk.test.node.002",
		"core.spelunk.test.endpoint.001b", true));
	require(world_spelunk_system_add_edge(player->spelunking_system,
		"core.spelunk.test.edge.002", "core.spelunk.test.node.002",
		"core.spelunk.test.endpoint.002a", "core.spelunk.test.node.003",
		"core.spelunk.test.endpoint.002b", false));
	require(world_spelunk_system_set_endpoint_grid(player->spelunking_system,
		"core.spelunk.test.endpoint.001a", 2, 3));
	require(world_spelunk_system_set_endpoint_grid(player->spelunking_system,
		"core.spelunk.test.endpoint.001b", 3, 4));
	require(world_spelunk_system_add_portal(player->spelunking_system,
		"core.spelunk.test.surface", "shaft.surface",
		"core.spelunk.test.001", true));
	require(!world_spelunk_system_set_portal_grid(player->spelunking_system,
		"core.spelunk.test.surface", 2, 3));
	require(world_spelunk_system_set_portal_grid(player->spelunking_system,
		"core.spelunk.test.surface", 5, 4));
	require(world_spelunk_system_discover_portal(player->spelunking_system,
		"core.spelunk.test.surface"));
	require(world_spelunk_system_add_portal(player->spelunking_system,
		"core.spelunk.test.sealed", "shaft.sealed",
		"core.spelunk.test.001", false));
	require(world_spelunk_system_set_portal_grid(player->spelunking_system,
		"core.spelunk.test.sealed", 4, 4));
	require(world_spelunk_system_is_valid(player->spelunking_system));
	player_resources_ensure(player);
	player->resources.stamina.current = 61;
	player_air_set(player, 3);
	require(savefile_save("TestSpelunk"));

	reset_before_load();
	require(savefile_load("TestSpelunk", false));
	piton_kind = lookup_kind(tval_find_idx("tool"),
		lookup_sval(tval_find_idx("tool"), "Piton"));
	scuba_kind = lookup_kind(tval_find_idx("tool"),
		lookup_sval(tval_find_idx("tool"), "Scuba Gear"));
	notnull(piton_kind);
	notnull(scuba_kind);
	notnull(player->spelunking);
	notnull(player->spelunking_system);
	require(world_spelunk_system_is_valid(player->spelunking_system));
	require(player->spelunking == world_spelunk_system_active_runtime(
		player->spelunking_system));
	eq(player->spelunking_system->node_count, 3);
	eq(player->spelunking_system->edge_count, 2);
	eq(player->spelunking_system->portal_count, 2);
	require(streq(player->spelunking_system->location_id,
		"core.spelunk.test.001"));
	branch = world_spelunk_system_node_by_id(player->spelunking_system,
		"core.spelunk.test.node.002");
	planned = world_spelunk_system_node_by_id(player->spelunking_system,
		"core.spelunk.test.node.003");
	notnull(branch);
	notnull(branch->runtime);
	eq(branch->runtime->state.stamina, 44);
	require(streq(branch->recipe_id, "deepwell.broken-galleries"));
	branch_recipe = world_spelunk_recipe_definition_by_id(branch->recipe_id);
	notnull(branch_recipe);
	require(world_spelunk_perception_matches(&branch->runtime->perception,
		&branch_recipe->perception));
	eq(branch->seed, 0x1234U);
	eq(branch->depth, 1);
	notnull(planned);
	null(planned->runtime);
	require(!planned->discovered);
	require(streq(planned->recipe_id, "deepwell.drowned-fault"));
	known_edge = world_spelunk_system_edge_by_id(player->spelunking_system,
		"core.spelunk.test.edge.001");
	unknown_edge = world_spelunk_system_edge_by_id(player->spelunking_system,
		"core.spelunk.test.edge.002");
	notnull(known_edge);
	notnull(unknown_edge);
	require(known_edge->discovered);
	require(known_edge->endpoint_a_materialized);
	require(known_edge->endpoint_b_materialized);
	eq(known_edge->endpoint_a_x, 2);
	eq(known_edge->endpoint_a_y, 3);
	eq(known_edge->endpoint_b_x, 3);
	eq(known_edge->endpoint_b_y, 4);
	require(!unknown_edge->discovered);
	require(!unknown_edge->endpoint_a_materialized);
	portal = world_spelunk_system_portal_by_entry(
		player->spelunking_system, "shaft.surface");
	notnull(portal);
	require(portal->materialized);
	require(portal->enabled);
	require(portal->discovered);
	eq(portal->x, 5);
	eq(portal->y, 4);
	portal = world_spelunk_system_portal_by_entry(
		player->spelunking_system, "shaft.sealed");
	notnull(portal);
	require(portal->materialized);
	require(!portal->enabled);
	require(!portal->discovered);
	require(streq(unknown_edge->endpoint_b_id,
		"core.spelunk.test.endpoint.002b"));
	require(world_spelunk_runtime_is_valid(player->spelunking));
	require(streq(player->spelunking->location_id,
		"core.spelunk.test.001"));
	require(streq(player->spelunking->geology_profile_id,
		"deepwell.limestone-shale"));
	eq(player->spelunking->geology_version, 1);
	eq(player->spelunking->material_tags[2 * 6 + 1], 1);
	eq(player->spelunking->decoration_tags[2 * 6 + 1], 0);
	require(world_spelunk_view_inspect(player->spelunking, 1, 2,
		&geology_inspect));
	notnull(geology_inspect.material);
	require(streq(geology_inspect.material->name,
		"Weathered limestone"));
	eq(player->spelunking->state.map.width, 6);
	eq(player->spelunking->state.map.height, 6);
	eq(player->spelunking->state.x, 2);
	eq(player->spelunking->state.y, 2);
	eq(player->spelunking->state.stamina, 61);
	eq(player->spelunking->state.max_stamina,
		player_stamina_maximum(player));
	eq(player->spelunking->state.breath, 3);
	eq(player->spelunking->state.grip_target_x, 1);
	eq(player->spelunking->state.grip_target_y, 2);
	eq(player->spelunking->state.rules.rest_stamina_gain, 17);
	eq(player->spelunking->state.rules.rope_down_stamina_cost, 2);
	eq(player->spelunking->state.movement, WORLD_SPELUNK_HANGING);
	eq(player->spelunking->cells[2 * 6 + 1], WORLD_SPELUNK_ROCK);
	require(world_spelunk_runtime_has_piton(player->spelunking, 2, 1));
	require(world_spelunk_runtime_has_rope(player->spelunking, 2, 2));
	require(world_spelunk_runtime_has_rope(player->spelunking, 4, 4));
	require(player->spelunking->actor_roster_initialized);
	eq(player->spelunking->actor_count, 1);
	skitter = world_spelunk_runtime_actor_at(player->spelunking, 4, 4);
	notnull(skitter);
	require(streq(skitter->id, WORLD_SPELUNK_CHASM_SKITTER_ID));
	eq(skitter->hp, 3);
	eq(skitter->max_hp, 4);
	eq(skitter->energy, 37U);
	require(!world_spelunk_visibility_is_visible(player->spelunking, 0, 2));
	require(world_spelunk_visibility_is_explored(player->spelunking, 0, 2));
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	ground = world_spelunk_runtime_ground_object_at(player->spelunking, 3, 4);
	notnull(ground);
	require(ground->kind == piton_kind);
	eq(ground->number, 3);
	require(ground->known && ground->known->number == 3);
	require(streq(quark_str(ground->note), "side-view-local"));
	scuba = carried_kind_object(player, scuba_kind);
	notnull(scuba);
	eq(scuba->pval, 17);
	eq(scuba->known->pval, 17);
	ok;
}
static int test_spelunking_player_adapter(void *state)
{
	enum world_spelunk_tile cells[36];
	struct keypress direction = { EVT_KBRD, '4', 0 };
	struct world_spelunk_command command = { WORLD_SPELUNK_COMMAND_MOVE,
		-1, 0 };
	struct world_spelunk_action_report report;
	struct world_spelunk_state before;
	struct level *test_level;
	struct chunk *top_down_cave;
	uint32_t total_energy_before;
	uint32_t precise_hp_before;
	uint32_t precise_hp_after;
	uint32_t precise_mana_before;
	uint32_t precise_mana_after;
	int32_t turn_before;
	int16_t bloodlust_before;
	int16_t food_before;
	int16_t fast_before;
	int16_t poisoned_before;
	int16_t cut_before;
	int16_t heal_before;
	int16_t deadfield_sickness_before;
	int16_t msp_before;
	int16_t csp_before;
	uint16_t csp_frac_before;
	int hp_before;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	cells[4 * 6 + 1] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 6, 6, 2, 4, 90));
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	require(test_level->mode == WORLD_MODE_TOP_DOWN);
	require(!world_spelunk_player_is_active(player));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_TOP_DOWN);
	require(!textui_mode_input_owns_key(player, direction));
	require(!textui_mode_input_uses_held_peek(player));
	require(textui_mode_input_mouse_rejection(player) == NULL);
	require(textui_mode_input_allows_command(player, CMD_HOLD, false, false));

	/* A queued mode command is harmless while the same runtime is inactive. */
	before = player->spelunking->state;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	require(cmdq_pop(CTX_GAME));
	eq(player->spelunking->state.x, before.x);
	eq(player->spelunking->state.y, before.y);
	eq(player->upkeep->energy_use, 0);

	/* The registered location mode is authoritative, not the existence of a
	 * saved payload by itself.  No shipping location uses this test mutation. */
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	require(world_spelunk_player_is_active(player));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_SPELUNKING);
	my_strcpy(player->spelunking->location_id, "core.main.001",
		sizeof(player->spelunking->location_id));
	require(!world_spelunk_player_is_active(player));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_INVALID);
	my_strcpy(player->spelunking->location_id, player->world_location.id,
		sizeof(player->spelunking->location_id));
	require(world_spelunk_player_is_active(player));
	my_strcpy(player->world_location.entry, "missing-entry",
		sizeof(player->world_location.entry));
	require(!world_spelunk_player_is_active(player));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_INVALID);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	require(world_spelunk_player_is_active(player));
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_SPELUNKING);
	require(textui_mode_input_owns_key(player, direction));
	require(textui_mode_input_uses_held_peek(player));
	notnull(textui_mode_input_mouse_rejection(player));
	require(textui_mode_input_allows_command(player, CMD_EAT, false, false));
	require(!textui_mode_input_allows_command(player, CMD_HOLD, false, false));
	require(textui_mode_input_allows_command(player, CMD_NULL, true, true));
	require(!textui_mode_input_allows_command(player, CMD_NULL, true, false));
	require(world_spelunk_visibility_begin_peek(player->spelunking));
	textui_mode_input_cancel_transient(player);
	require(!player->spelunking->peeking);
	require(!world_spelunking_active_storage_is_valid(player));
	require(!savefile_save("TestInvalidSpelunkStorage"));
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, 0, 0), 1);

	/* An inherited top-down command, including an existing repeat request,
	 * is rejected before it can prompt or inspect the compatibility chunk. */
	player->upkeep->energy_use = 0;
	eq(cmdq_push_repeat(CMD_HOLD, 7), 0);
	top_down_cave = cave;
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	eq(player->upkeep->energy_use, 0);
	eq(cmd_get_nrepeats(), 0);
	eq(cmdq_push(CMD_REPEAT), 1);

	/* Even an ordinary energy command cannot invoke top-down bloodlust while
	 * the side-view context is active.  Forced sleep is the one audited
	 * inherited wait adapter, so paralysis cannot create a free-turn loop. */
	bloodlust_before = player->timed[TMD_BLOODLUST];
	player->timed[TMD_BLOODLUST] = 200;
	cave = NULL;
	eq(cmdq_push(CMD_SLEEP), 0);
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	player->timed[TMD_BLOODLUST] = bloodlust_before;
	eq(player->upkeep->energy_use, z_info->move_energy);
	player->upkeep->energy_use = 0;

	/* The periodic side-view clock advances player-owned physiology without
	 * reading the top-down chunk: ordinary statuses decay, poison hurts,
	 * digestion proceeds, and mana regenerates. */
	while (player->spelunking->actor_count) {
		require(world_spelunk_runtime_remove_actor(player->spelunking,
			&player->spelunking->actors[
				player->spelunking->actor_count - 1]));
	}
	player->spelunking->actor_roster_initialized = true;
	player->energy = z_info->move_energy;
	player->chp = player->mhp;
	player->chp_frac = 0;
	food_before = player->timed[TMD_FOOD];
	fast_before = player->timed[TMD_FAST];
	poisoned_before = player->timed[TMD_POISONED];
	cut_before = player->timed[TMD_CUT];
	heal_before = player->timed[TMD_HEAL];
	deadfield_sickness_before = player->timed[TMD_DEADFIELD_SICKNESS];
	msp_before = player->msp;
	csp_before = player->csp;
	csp_frac_before = player->csp_frac;
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL - 1);
	player->timed[TMD_FAST] = 50;
	player->timed[TMD_POISONED] = 50;
	player->timed[TMD_CUT] = 0;
	player->timed[TMD_BLOODLUST] = 0;
	player->timed[TMD_HEAL] = 0;
	player->timed[TMD_DEADFIELD_SICKNESS] = 0;
	player->msp = 20;
	player->csp = 10;
	player->csp_frac = 0;
	hp_before = player->chp;
	precise_mana_before = ((uint32_t)player->csp << 16) |
		player->csp_frac;
	turn = ((turn + 99) / 100) * 100;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	cave = NULL;
	run_game_loop();
	cave = top_down_cave;
	precise_mana_after = ((uint32_t)player->csp << 16) |
		player->csp_frac;
	require(player->timed[TMD_FAST] < 50);
	require(player->timed[TMD_POISONED] < 50);
	require(player->timed[TMD_FOOD] < PY_FOOD_FULL - 1);
	require(precise_mana_after > precise_mana_before);
	require(player->chp < hp_before);
	player->timed[TMD_FOOD] = food_before;
	player->timed[TMD_FAST] = fast_before;
	player->timed[TMD_POISONED] = poisoned_before;
	player->timed[TMD_CUT] = cut_before;
	player->timed[TMD_BLOODLUST] = bloodlust_before;
	player->timed[TMD_HEAL] = heal_before;
	player->timed[TMD_DEADFIELD_SICKNESS] = deadfield_sickness_before;
	player->msp = msp_before;
	player->csp = csp_before;
	player->csp_frac = csp_frac_before;

	/* A complete side-view action advances shared energy and global turns
	 * without reading the top-down chunk.  At the standard periodic boundary,
	 * natural HP recovery uses the same shared physiology processor. */
	player->energy = z_info->move_energy;
	total_energy_before = player->total_energy;
	player->chp = MAX(1, player->mhp - 10);
	player->chp_frac = 0;
	precise_hp_before = ((uint32_t)player->chp << 16) | player->chp_frac;
	turn = ((turn + 9) / 10) * 10;
	turn_before = turn;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	cave = NULL;
	run_game_loop();
	cave = top_down_cave;
	eq(player->total_energy, total_energy_before + 100);
	require(turn > turn_before);
	precise_hp_after = ((uint32_t)player->chp << 16) | player->chp_frac;
	require(precise_hp_after > precise_hp_before);
	eq(player->upkeep->energy_use, 0);
	require(world_turn_context_for_player(player) ==
		WORLD_TURN_CONTEXT_SPELUNKING);

	/* Dispatch rechecks a command if the active mode changes after queueing. */
	before = player->spelunking->state;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	test_level->mode = WORLD_MODE_TOP_DOWN;
	require(cmdq_pop(CTX_GAME));
	eq(player->spelunking->state.x, before.x);
	eq(player->spelunking->state.y, before.y);
	eq(player->upkeep->energy_use, 0);
	test_level->mode = WORLD_MODE_SPELUNKING;

	/* A malformed raw queue entry cannot bypass the semantic helper. */
	before = player->spelunking->state;
	eq(cmdq_push(CMD_SPELUNK_ACTION), 0);
	cmd_set_arg_choice(cmdq_peek(), "kind", WORLD_SPELUNK_COMMAND_WAIT);
	require(cmdq_pop(CTX_GAME));
	eq(player->spelunking->state.x, before.x);
	eq(player->spelunking->state.y, before.y);
	eq(player->upkeep->energy_use, 0);

	/* Persistable rules may still exceed Angband's signed energy field. */
	before = player->spelunking->state;
	player->spelunking->state.rules.action_energy =
		(unsigned int)INT_MAX + 1U;
	command.kind = WORLD_SPELUNK_COMMAND_WAIT;
	command.dx = 0;
	eq(world_spelunk_player_apply(player, &command, &report),
		WORLD_SPELUNK_PLAYER_INVALID);
	eq(player->spelunking->state.x, before.x);
	eq(player->spelunking->state.y, before.y);
	eq(player->upkeep->energy_use, 0);
	player->spelunking->state.rules.action_energy = 100;

	/* Walking into rock is rejected without moving or spending energy. */
	command.kind = WORLD_SPELUNK_COMMAND_MOVE;
	command.dx = -1;
	before = player->spelunking->state;
	eq(world_spelunk_player_apply(player, &command, &report),
		WORLD_SPELUNK_PLAYER_REJECTED);
	eq(player->spelunking->state.x, before.x);
	eq(player->spelunking->state.y, before.y);
	eq(player->upkeep->energy_use, 0);
	eq(report.energy_use, 0);

	/* Selecting a grip changes only mode state and remains a free action. */
	command.kind = WORLD_SPELUNK_COMMAND_GRIP;
	command.dx = 0;
	command.dy = 0;
	eq(world_spelunk_player_apply(player, &command, &report),
		WORLD_SPELUNK_PLAYER_FREE);
	eq(report.event, WORLD_SPELUNK_EVENT_GRIP_SELECTED);
	eq(player->spelunking->state.grip_target_x, 1);
	eq(player->spelunking->state.grip_target_y, 4);
	eq(player->upkeep->energy_use, 0);

	/* The shared command queue commits one accepted mode action. */
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, 1, 0), 0);
	require(cmdq_pop(CTX_GAME));
	eq(player->spelunking->state.x, 3);
	eq(player->upkeep->energy_use, 100);

	/* A second semantic action cannot be smuggled into an existing turn. */
	before = player->spelunking->state;
	command.kind = WORLD_SPELUNK_COMMAND_WAIT;
	command.dx = 0;
	eq(world_spelunk_player_apply(player, &command, &report),
		WORLD_SPELUNK_PLAYER_INVALID);
	eq(player->spelunking->state.x, before.x);
	eq(player->spelunking->state.y, before.y);
	eq(player->upkeep->energy_use, 100);
	player->upkeep->energy_use = 0;

	/* Falling mutates side-view position but damages the shared player. */
	cells[4 * 6 + 1] = WORLD_SPELUNK_AIR;
	require(install_spelunking_runtime(cells, 6, 6, 2, 1, 90));
	hp_before = player->chp;
	eq(world_spelunk_player_apply(player, &command, &report),
		WORLD_SPELUNK_PLAYER_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_FALL_HURT);
	eq(report.damage, 10);
	eq(player->chp, hp_before - 10);
	eq(player->spelunking->state.y, 4);
	eq(player->upkeep->energy_use, 100);

	/* The same path reaches Angband's normal death authority. */
	player->upkeep->energy_use = 0;
	require(install_spelunking_runtime(cells, 6, 6, 2, 1, 90));
	player->chp = 5;
	eq(world_spelunk_player_apply(player, &command, &report),
		WORLD_SPELUNK_PLAYER_DEAD);
	require(player->is_dead);
	require(streq(player->died_from, "a fall"));
	eq(player->spelunking->state.x, 2);
	eq(player->spelunking->state.y, 4);
	eq(report.fall_tiles, 3);
	eq(player->upkeep->energy_use, 100);

	/* Leave the shared fixture alive and clean for the following test. */
	reset_before_load();
	require(savefile_load("Test1", false));
	require(!player->is_dead);
	null(player->spelunking);
	ok;
}

static int test_spelunking_inventory_clock(void *state)
{
	enum world_spelunk_tile cells[36];
	struct object_kind *rod_kind;
	struct object *carried_rod;
	struct object *ground_rod;
	struct object *light;
	struct curse_data poison_before;
	struct curse_data summon_before;
	struct level *test_level;
	struct chunk *top_down_cave;
	bool allocated_curses = false;
	int poison_curse;
	int summon_curse;
	int light_fuel_before;
	int i;

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

	rod_kind = lookup_kind(tval_find_idx("rod"),
		lookup_sval(tval_find_idx("rod"), "Light"));
	notnull(rod_kind);
	carried_rod = make_test_object(rod_kind, 1);
	notnull(carried_rod);
	carried_rod->timeout = 5;
	carried_rod->known->timeout = 5;
	inven_carry(player, carried_rod, true, false);
	carried_rod = carried_kind_object(player, rod_kind);
	notnull(carried_rod);
	ground_rod = make_test_object(rod_kind, 1);
	notnull(ground_rod);
	ground_rod->timeout = 5;
	ground_rod->known->timeout = 5;
	require(world_spelunk_runtime_add_ground_object(player->spelunking,
		ground_rod, 3, 4));

	/* Fuel is deliberately paused: side-view visibility currently has no
	 * equipped-light input, so consuming it would provide no benefit. */
	light = equipped_item_by_slot_name(player, "light");
	notnull(light);
	light_fuel_before = light->timeout;
	require(light_fuel_before > 0);

	/* Player-local curse effects trigger from equipped-object origins.  A
	 * native-map effect counts down to one and waits there for a native map. */
	poison_curse = lookup_curse("poison");
	summon_curse = lookup_curse("dragon summon");
	require(poison_curse > 0);
	require(summon_curse > 0);
	require(world_spelunk_effect_chain_is_player_local(
		curses[poison_curse].obj->effect));
	require(!world_spelunk_effect_chain_is_player_local(
		curses[summon_curse].obj->effect));
	if (!light->curses) {
		light->curses = mem_zalloc(z_info->curse_max *
			sizeof(*light->curses));
		allocated_curses = true;
	}
	poison_before = light->curses[poison_curse];
	summon_before = light->curses[summon_curse];
	light->curses[poison_curse].power = 1;
	light->curses[poison_curse].timeout = 1;
	light->curses[summon_curse].power = 1;
	light->curses[summon_curse].timeout = 1;
	player->timed[TMD_POISONED] = 0;

	player->energy = z_info->move_energy;
	turn = ((turn + 9) / 10) * 10;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	top_down_cave = cave;
	cave = NULL;
	run_game_loop();
	cave = top_down_cave;

	eq(carried_rod->timeout, 4);
	eq(ground_rod->timeout, 4);
	require(!(player->upkeep->notice & PN_COMBINE));
	eq(light->timeout, light_fuel_before);
	require(player->timed[TMD_POISONED] > 0);
	eq(light->curses[summon_curse].timeout, 1);

	light->curses[poison_curse] = poison_before;
	light->curses[summon_curse] = summon_before;
	if (allocated_curses) {
		mem_free(light->curses);
		light->curses = NULL;
	}
	ok;
}

static int test_spelunking_global_clock(void *state)
{
	enum world_spelunk_tile cells[36];
	struct level *test_level;
	struct chunk *top_down_cave;
	enum world_location_mode saved_mode;
	bool saved_store_services;
	uint16_t saved_daycount;
	int32_t saved_turn;
	int stock_num_before;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 6, 6, 2, 4, 90));
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	require(test_level->has_store_services);
	require(z_info->store_turns > 0);
	require(z_info->store_max > 0);
	saved_mode = test_level->mode;
	saved_store_services = test_level->has_store_services;
	saved_daycount = daycount;
	saved_turn = turn;
	stock_num_before = stores[0].stock_num;

	/* Model the same persistent player and runtime while away from every
	 * authored shop.  Periodic work must remain valid with no native cave. */
	test_level->mode = WORLD_MODE_SPELUNKING;
	test_level->has_store_services = false;
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	player->spelunking->actor_roster_initialized = true;
	daycount = 0;
	turn = 10L * z_info->store_turns;
	player->energy = z_info->move_energy;
	player->upkeep->energy_use = 0;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	top_down_cave = cave;
	cave = NULL;
	run_game_loop();
	cave = top_down_cave;
	eq(daycount, 1);
	eq(stores[0].stock_num, stock_num_before);

	/* The save field is deliberately bounded: an extreme expedition keeps
	 * the maximum debt rather than wrapping it to zero. */
	daycount = UINT16_MAX;
	turn = 20L * z_info->store_turns;
	player->energy = z_info->move_energy;
	player->upkeep->energy_use = 0;
	eq(cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_WAIT, 0, 0), 0);
	cave = NULL;
	run_game_loop();
	cave = top_down_cave;
	eq(daycount, UINT16_MAX);
	eq(stores[0].stock_num, stock_num_before);

	/* The first player-input boundary after arrival, rather than a particular
	 * route or legacy depth transition, consumes the debt once. */
	test_level->mode = saved_mode;
	test_level->has_store_services = true;
	daycount = 1;
	player->energy = z_info->move_energy;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_HOLD), 0);
	run_game_loop();
	eq(daycount, 0);
	player->energy = z_info->move_energy;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_HOLD), 0);
	run_game_loop();
	eq(daycount, 0);

	test_level->has_store_services = saved_store_services;
	turn = saved_turn;
	daycount = saved_daycount;
	ok;
}

static int test_spelunking_breath_adapter(void *state)
{
	enum world_spelunk_tile cells[36];
	struct world_spelunk_environment_report report;
	struct object_kind *scuba_kind;
	struct object *scuba;
	struct level *test_level;
	int air;
	int max_air;
	int hp_before;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	cells[4 * 6 + 2] = WORLD_SPELUNK_WATER;
	cells[3 * 6 + 2] = WORLD_SPELUNK_WATER;
	require(install_spelunking_runtime(cells, 6, 6, 2, 4, 90));
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	require(world_spelunk_player_is_active(player));
	eq(player->spelunking->state.movement, WORLD_SPELUNK_SWIMMING);

	scuba_kind = lookup_kind(tval_find_idx("tool"),
		lookup_sval(tval_find_idx("tool"), "Scuba Gear"));
	notnull(scuba_kind);
	require(kf_has(scuba_kind->kind_flags, KF_AIR_SUPPLY));
	eq(scuba_kind->cost, 25);
	scuba = make_test_object(scuba_kind, 1);
	notnull(scuba);
	eq(scuba->pval, 30);
	inven_carry(player, scuba, true, false);
	scuba = carried_kind_object(player, scuba_kind);
	notnull(scuba);
	require(world_spelunk_player_air_status(player, &air, &max_air));
	eq(air, 36);
	eq(max_air, 36);

	/* A completed underwater turn spends the ordinary object's charge before
	 * the player's bounded held-breath reserve. */
	player->upkeep->energy_use = 100;
	eq(world_spelunk_player_process_environment_turn(player, &report),
		WORLD_SPELUNK_ENVIRONMENT_SAFE);
	eq(report.event, WORLD_SPELUNK_BREATH_SCUBA_USED);
	eq(scuba->pval, 29);
	eq(scuba->known->pval, 29);
	eq(player->spelunking->state.breath, 6);
	require(world_spelunk_player_air_status(player, &air, &max_air));
	eq(air, 35);
	eq(max_air, 36);

	/* Depletion falls through to held breath, then to shared HP authority. */
	scuba->pval = 1;
	scuba->known->pval = 1;
	player_air_set(player, 2);
	eq(world_spelunk_player_process_environment_turn(player, &report),
		WORLD_SPELUNK_ENVIRONMENT_SAFE);
	eq(scuba->pval, 0);
	eq(player->spelunking->state.breath, 2);
	eq(world_spelunk_player_process_environment_turn(player, &report),
		WORLD_SPELUNK_ENVIRONMENT_SAFE);
	eq(report.event, WORLD_SPELUNK_BREATH_HELD);
	eq(player->spelunking->state.breath, 1);
	player_air_set(player, 0);
	player->chp = MAX(player->chp, 20);
	hp_before = player->chp;
	eq(world_spelunk_player_process_environment_turn(player, &report),
		WORLD_SPELUNK_ENVIRONMENT_DAMAGED);
	eq(report.event, WORLD_SPELUNK_BREATH_DROWNING);
	eq(report.damage, 10);
	eq(player->chp, hp_before - 10);

	/* One completed turn in open air restores lungs and the same persistent
	 * gear object; no disposable tank inventory or second player is created. */
	player->spelunking->state.x = 1;
	player->spelunking->state.movement = WORLD_SPELUNK_STANDING;
	player->spelunking->state.fall_start_y = 4;
	require(world_spelunk_runtime_is_valid(player->spelunking));
	eq(world_spelunk_player_process_environment_turn(player, &report),
		WORLD_SPELUNK_ENVIRONMENT_SAFE);
	eq(report.event, WORLD_SPELUNK_BREATH_RESTORED);
	eq(scuba->pval, 30);
	eq(scuba->known->pval, 30);
	eq(player->spelunking->state.breath, 6);

	/* Drowning death remains Angband permadeath with an explicit cause. */
	player->spelunking->state.x = 2;
	player->spelunking->state.movement = WORLD_SPELUNK_SWIMMING;
	player_air_set(player, 0);
	scuba->pval = 0;
	scuba->known->pval = 0;
	player->chp = 5;
	require(world_spelunk_runtime_is_valid(player->spelunking));
	eq(world_spelunk_player_process_environment_turn(player, &report),
		WORLD_SPELUNK_ENVIRONMENT_DEAD);
	require(player->is_dead);
	require(streq(player->died_from, "drowning"));

	reset_before_load();
	require(savefile_load("Test1", false));
	require(!player->is_dead);
	null(player->spelunking);
	ok;
}

static int test_spelunking_local_object_adapter(void *state)
{
	enum world_spelunk_tile cells[36];
	struct world_spelunk_local_report report;
	struct object_kind *piton_kind;
	struct object_kind *rope_kind;
	struct object_kind *yew_kind;
	struct object *pitons;
	struct object *rope;
	struct object *longline;
	char tool_name[80];
	struct object *envelope_object;
	struct level *test_level;
	struct world_spelunk_action_report action_report;
	struct world_spelunk_command down = {
		WORLD_SPELUNK_COMMAND_MOVE, 0, 1
	};
	struct world_spelunk_command wait = {
		WORLD_SPELUNK_COMMAND_WAIT, 0, 0
	};
	int tool_tval;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	cells[2 * 6 + 1] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 6, 6, 2, 2, 90));
	player->spelunking->state.grip_target_x = 1;
	player->spelunking->state.grip_target_y = 2;
	player->spelunking->state.movement = WORLD_SPELUNK_CLIMBING;
	require(world_spelunk_runtime_is_valid(player->spelunking));
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	require(world_spelunk_player_is_active(player));

	tool_tval = tval_find_idx("tool");
	piton_kind = lookup_kind(tool_tval, lookup_sval(tool_tval, "Piton"));
	rope_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Rope Length (2 metres)"));
	yew_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Yew Longline Rod"));
	notnull(piton_kind);
	notnull(rope_kind);
	notnull(yew_kind);
	pitons = make_test_object(piton_kind, 2);
	rope = make_test_object(rope_kind, 3);
	notnull(pitons);
	notnull(rope);
	eq(piton_kind->cost, 1);
	eq(rope_kind->cost, 2);
	require(kf_has(piton_kind->kind_flags, KF_SPELUNK_PITON));
	require(kf_has(rope_kind->kind_flags, KF_SPELUNK_ROPE));
	object_desc(tool_name, sizeof(tool_name), pitons,
		ODESC_PREFIX | ODESC_FULL | ODESC_STORE, player);
	require(strstr(tool_name, "Piton"));
	object_desc(tool_name, sizeof(tool_name), rope,
		ODESC_PREFIX | ODESC_FULL | ODESC_STORE, player);
	require(strstr(tool_name, "Rope Length"));
	require(strstr(tool_name, "2 metres"));
	inven_carry(player, pitons, true, false);
	inven_carry(player, rope, true, false);
	eq(carried_kind_count(player, piton_kind), 2);
	eq(carried_kind_count(player, rope_kind), 3);

	/* Placement mutates the local overlays only after the matching shared
	 * inventory item is found, and each completed operation costs one turn. */
	eq(world_spelunk_player_apply_local(player, WORLD_SPELUNK_LOCAL_PITON,
		&report), WORLD_SPELUNK_LOCAL_TURN);
	eq(report.affected, 1);
	eq(report.energy_use, 100);
	require(world_spelunk_runtime_has_piton(player->spelunking, 2, 2));
	eq(carried_kind_count(player, piton_kind), 1);
	player->upkeep->energy_use = 0;
	eq(world_spelunk_player_apply_local(player, WORLD_SPELUNK_LOCAL_ROPE,
		&report), WORLD_SPELUNK_LOCAL_TURN);
	eq(report.affected, 2);
	require(world_spelunk_runtime_has_rope(player->spelunking, 2, 3));
	require(world_spelunk_runtime_has_rope(player->spelunking, 2, 4));
	eq(carried_kind_count(player, rope_kind), 1);
	player->upkeep->energy_use = 0;
	eq(world_spelunk_player_apply(player, &down, &action_report),
		WORLD_SPELUNK_PLAYER_TURN);
	eq(action_report.event, WORLD_SPELUNK_EVENT_ROPE_ATTACHED);
	eq(player->spelunking->state.movement, WORLD_SPELUNK_HANGING);
	eq(player->spelunking->state.stamina, 90);
	player->upkeep->energy_use = 0;
	eq(world_spelunk_player_apply(player, &wait, &action_report),
		WORLD_SPELUNK_PLAYER_TURN);
	eq(action_report.event, WORLD_SPELUNK_EVENT_ROPE_DRAINED);
	eq(player->spelunking->state.stamina, 89);

	/* Drop and pickup transfer the exact object pair between shared gear and
	 * the local owner.  The active Angband chunk is never used as a floor. */
	player->upkeep->energy_use = 0;
	pitons = NULL;
	for (pitons = player->gear; pitons; pitons = pitons->next) {
		if (pitons->kind == piton_kind) break;
	}
	notnull(pitons);
	envelope_object = square_object(cave, player->grid);
	eq(world_spelunk_player_drop(player, pitons, 1, &report),
		WORLD_SPELUNK_LOCAL_TURN);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	require(world_spelunk_runtime_ground_object_at(player->spelunking, 2, 3));
	require(square_object(cave, player->grid) == envelope_object);
	eq(carried_kind_count(player, piton_kind), 0);
	player->upkeep->energy_use = 0;
	eq(world_spelunk_player_apply_local(player, WORLD_SPELUNK_LOCAL_PICKUP,
		&report), WORLD_SPELUNK_LOCAL_TURN);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 0);
	eq(carried_kind_count(player, piton_kind), 1);
	require(square_object(cave, player->grid) == envelope_object);

	/* Acquiring the data-authored objective item reports its semantic sound
	 * exactly once; the controller decides how that message type is voiced. */
	longline = make_test_object(yew_kind, 1);
	notnull(longline);
	require(world_spelunk_runtime_add_ground_object(player->spelunking,
		longline, 2, 3));
	eq(carried_kind_count(player, yew_kind), 0);
	player->upkeep->energy_use = 0;
	eq(world_spelunk_player_apply_local(player, WORLD_SPELUNK_LOCAL_PICKUP,
		&report), WORLD_SPELUNK_LOCAL_TURN);
	eq(report.completion_message, MSG_OBJECTIVE_COMPLETE);
	eq(carried_kind_count(player, yew_kind), 1);

	reset_before_load();
	require(savefile_load("Test1", false));
	null(player->spelunking);
	ok;
}

static int test_spelunking_shared_inventory_boundary(void *state)
{
	enum world_spelunk_tile cells[36];
	struct object_kind *piton_kind;
	struct object_kind *rope_kind;
	struct object *envelope_object;
	struct object *local_object;
	struct level *test_level;
	struct chunk *top_down_cave;
	struct loc envelope_grid = loc(0, 0);
	quark_t envelope_note;
	bool note = false;
	bool found_envelope_grid = false;
	uint16_t saved_pack_size;
	int used_slots;
	int tool_tval;
	int i;

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

	tool_tval = tval_find_idx("tool");
	piton_kind = lookup_kind(tool_tval, lookup_sval(tool_tval, "Piton"));
	rope_kind = lookup_kind(tool_tval,
		lookup_sval(tool_tval, "Rope Length (2 metres)"));
	notnull(piton_kind);
	notnull(rope_kind);

	/* Even a pre-supplied command argument cannot turn an object in the inert
	 * compatibility envelope into a side-view floor item. */
	envelope_object = make_test_object(piton_kind, 1);
	notnull(envelope_object);
	envelope_note = quark_add("compatibility-envelope");
	envelope_object->note = envelope_note;
	for (i = 0; i < cave->height && !found_envelope_grid; i++) {
		int x;

		for (x = 0; x < cave->width; x++) {
			struct loc candidate = loc(x, i);

			if (square_isobjectholding(cave, candidate) &&
					!square_object(cave, candidate)) {
				envelope_grid = candidate;
				found_envelope_grid = true;
				break;
			}
		}
	}
	require(found_envelope_grid);
	require(floor_carry(cave, envelope_grid, envelope_object, &note));
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_UNINSCRIBE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", envelope_object);
	require(cmdq_pop(CTX_GAME));
	eq(envelope_object->note, envelope_note);
	eq(player->upkeep->energy_use, 0);

	/* Detached byproducts and overflows use the active local owner with no
	 * native cave pointer. */
	local_object = make_test_object(rope_kind, 1);
	notnull(local_object);
	top_down_cave = cave;
	cave = NULL;
	require(world_player_location_accepts_object(player, NULL));
	require(world_player_place_detached_object(player, NULL, &local_object));
	null(local_object);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	notnull(world_spelunk_runtime_ground_object_at(player->spelunking, 2, 4));

	/* Shared gear recalculation may finish player-owned work, but native map
	 * updates and redraws remain pending until a top-down location owns them. */
	player->upkeep->update = PU_HP | PU_UPDATE_VIEW | PU_DISTANCE | PU_MONSTERS;
	update_stuff(player);
	require(!(player->upkeep->update & PU_HP));
	require(player->upkeep->update & PU_UPDATE_VIEW);
	require(player->upkeep->update & PU_DISTANCE);
	require(player->upkeep->update & PU_MONSTERS);
	player->upkeep->redraw = PR_HP | PR_INVEN | PR_MAP | PR_OBJECT;
	redraw_stuff(player);
	require(!(player->upkeep->redraw & PR_HP));
	require(!(player->upkeep->redraw & PR_INVEN));
	require(player->upkeep->redraw & PR_MAP);
	require(player->upkeep->redraw & PR_OBJECT);

	/* A real pack overflow follows that same local transfer path. */
	used_slots = pack_slots_used(player);
	require(used_slots >= 2);
	saved_pack_size = z_info->pack_size;
	z_info->pack_size = (uint16_t)(used_slots - 1);
	require(pack_is_overfull());
	pack_overflow(NULL);
	require(!pack_is_overfull());
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 2);
	z_info->pack_size = saved_pack_size;
	calc_inventory(player);
	cave = top_down_cave;
	require(square_object(cave, envelope_grid) == envelope_object);

	reset_before_load();
	require(savefile_load("Test1", false));
	null(player->spelunking);
	ok;
}

const char *suite_name = "game/spelunking-persistence";
struct test tests[] = {
	{ "save roundtrip", test_spelunking_save_roundtrip },
	{ "player adapter", test_spelunking_player_adapter },
	{ "inventory clock", test_spelunking_inventory_clock },
	{ "global clock", test_spelunking_global_clock },
	{ "breath adapter", test_spelunking_breath_adapter },
	{ "local object adapter", test_spelunking_local_object_adapter },
	{ "shared inventory boundary", test_spelunking_shared_inventory_boundary },
	{ NULL, NULL }
};
