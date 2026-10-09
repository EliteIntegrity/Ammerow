/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Serialization, actor, survey, and inspection contracts. */

#include "spelunking-fixture.h"

static int test_cave_survey_respects_discovery(void *state)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_system *system;
	struct ui_spelunk_survey_model survey;

	(void)state;
	fixture_open(&f);
	fixture_ground_row(&f, TEST_HEIGHT - 1);
	require(fixture_init(&f, 2, TEST_HEIGHT - 2, 100));
	runtime = world_spelunk_runtime_create("test.survey.root", &f.state);
	notnull(runtime);
	system = world_spelunk_system_create("test.survey", 0x12345678u,
		WORLD_SPELUNK_SYSTEM_GRAPH_VERSION);
	if (!system) world_spelunk_runtime_free(runtime);
	notnull(system);
	require(world_spelunk_system_add_node(system, "test.survey.node.001",
		"", 1u, 1u, 0u, runtime));
	require(world_spelunk_system_add_node(system, "test.survey.node.002",
		"", 2u, 1u, 1u, NULL));
	require(world_spelunk_system_add_node(system, "test.survey.node.003",
		"", 3u, 1u, 2u, NULL));
	require(world_spelunk_system_add_edge(system, "test.survey.edge.001",
		"test.survey.node.001", "test.survey.end.001a",
		"test.survey.node.002", "test.survey.end.001b", false));
	require(world_spelunk_system_add_edge(system, "test.survey.edge.002",
		"test.survey.node.002", "test.survey.end.002a",
		"test.survey.node.003", "test.survey.end.002b", false));
	require(world_spelunk_system_set_active_node(system,
		"test.survey.node.001"));
	require(world_spelunk_system_is_valid(system));
	require(ui_spelunk_survey_build(system, &survey));
	eq(survey.node_count, 1);
	eq(survey.edge_count, 0);
	eq(survey.current_node, 0);
	require(survey.nodes[0].current);
	require(world_spelunk_system_discover_edge(system,
		"test.survey.edge.001"));
	require(world_spelunk_system_is_valid(system));
	require(ui_spelunk_survey_build(system, &survey));
	eq(survey.node_count, 2);
	eq(survey.edge_count, 1);
	require(!survey.edges[0].ready);
	require(strstr(survey.rows[1].detail, "charted") != NULL);
	require(strstr(survey.rows[0].detail, "visited") != NULL);
	world_spelunk_system_free(system);
	ok;
}

/** Remove the version-ten layout revision to emulate a version-nine save. */
static size_t payload_without_layout_revision(uint8_t *target,
		size_t capacity, const uint8_t *source, size_t length)
{
	size_t offset;

	if (!target || !source || length < 9u ||
			source[4] < WORLD_SPELUNK_PAYLOAD_VERSION_LAYOUT_REVISION) {
		return 0;
	}
	/* These legacy helpers operate on fixtures without generated geology.
	 * Versions twelve and thirteen append balance fields after version
	 * eleven's explicit false geology byte. */
	if (source[4] >=
			WORLD_SPELUNK_PAYLOAD_VERSION_DIRECTIONAL_STAMINA) {
		if (length < 8u) return 0;
		length -= 8u;
	}
	if (source[4] >= WORLD_SPELUNK_PAYLOAD_VERSION_SWIMMING) {
		if (length < 2u) return 0;
		length -= 2u;
	}
	if (source[4] >= WORLD_SPELUNK_PAYLOAD_VERSION_GEOLOGY) {
		if (length < 1u || source[length - 1u] != 0) return 0;
		length--;
	}
	offset = 7u + source[6];
	if (length < offset + 2u || capacity < length - 2u) return 0;
	memcpy(target, source, offset);
	target[4] = WORLD_SPELUNK_PAYLOAD_VERSION_STABLE_ACTOR_IDS;
	target[5] = 0;
	memcpy(target + offset, source + offset + 2u,
		length - offset - 2u);
	return length - 2u;
}

/** Remove the actor tail from an actor-free current payload. */
static size_t payload_without_actors(uint8_t *target, size_t capacity,
		const uint8_t *source, size_t length)
{
	uint8_t stable_payload[1024];

	if (source && source[4] >=
			WORLD_SPELUNK_PAYLOAD_VERSION_LAYOUT_REVISION) {
		length = payload_without_layout_revision(stable_payload,
			sizeof(stable_payload), source, length);
		if (!length) return 0;
		source = stable_payload;
	}
	if (!target || !source || length < 2u || capacity < length - 2u ||
			source[4] < WORLD_SPELUNK_PAYLOAD_VERSION_ACTORS ||
			source[length - 2u] != 0 || source[length - 1u] != 0) {
		return 0;
	}
	memcpy(target, source, length - 2u);
	target[4] = WORLD_SPELUNK_PAYLOAD_VERSION_BREATH;
	target[5] = 0;
	return length - 2u;
}

/** Remove the version-six breath fields to emulate an exploration-era save. */
static size_t payload_without_breath(uint8_t *target, size_t capacity,
		const uint8_t *source, size_t length)
{
	uint8_t breath_payload[1024];
	size_t breath_rules_offset;
	size_t breath_state_offset;

	if (!target || !source || length < 7u) return 0;
	if (source[4] >= WORLD_SPELUNK_PAYLOAD_VERSION_ACTORS) {
		length = payload_without_actors(breath_payload,
			sizeof(breath_payload), source, length);
		if (!length) return 0;
		source = breath_payload;
	}
	breath_rules_offset = 29u + source[6];
	breath_state_offset = breath_rules_offset + 12u;
	if (length < breath_state_offset + 2u || capacity < length - 6u) {
		return 0;
	}
	memcpy(target, source, breath_rules_offset);
	target[4] = WORLD_SPELUNK_PAYLOAD_VERSION_EXPLORATION;
	target[5] = 0;
	memcpy(target + breath_rules_offset, source + breath_rules_offset + 4u,
		breath_state_offset - breath_rules_offset - 4u);
	memcpy(target + breath_state_offset - 4u,
		source + breath_state_offset + 2u,
		length - breath_state_offset - 2u);
	return length - 6u;
}

static void set_u16(uint8_t *target, uint16_t value)
{
	target[0] = (uint8_t)(value & 0xff);
	target[1] = (uint8_t)(value >> 8);
}

static int test_runtime_actor_persistence_and_view(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_runtime *decoded = NULL;
	struct world_spelunk_runtime *legacy_decoded = NULL;
	struct world_spelunk_view view;
	const struct world_spelunk_actor *actor;
	struct world_spelunk_actor *mutable_actor;
	uint8_t cells[TEST_WIDTH * TEST_HEIGHT];
	uint8_t payload[512];
	uint8_t stable_payload[512];
	uint8_t legacy_payload[512];
	int actor_attr;
	wchar_t actor_glyph;
	size_t written;
	size_t stable_written;
	size_t legacy_written;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	require(fixture_init(&f, 2, 5, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	runtime->actor_roster_initialized = true;
	require(world_spelunk_runtime_add_actor(runtime,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 5));
	mutable_actor = world_spelunk_runtime_actor_at_mutable(runtime, 4, 5);
	notnull(mutable_actor);
	mutable_actor->energy = 37;
	require(world_spelunk_runtime_is_valid(runtime));
	require(world_spelunk_view_capture(runtime, TEST_WIDTH, TEST_HEIGHT,
		cells, N_ELEMENTS(cells), &view));
	eq(world_spelunk_view_cell_at(&view, 4, 5),
		WORLD_SPELUNK_VIEW_ACTOR);
	require(world_spelunk_view_actor_appearance_at(&view, 4, 5,
		&actor_attr, &actor_glyph));
	eq(actor_attr, COLOUR_L_GREEN);
	eq(actor_glyph, L'k');
	require(!world_spelunk_view_actor_appearance_at(&view, 3, 5,
		&actor_attr, &actor_glyph));
	eq(world_spelunk_runtime_encode(runtime, payload, sizeof(payload),
		&written), WORLD_SPELUNK_CODEC_OK);
	eq(world_spelunk_runtime_decode(payload, written, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	require(decoded->actor_roster_initialized);
	eq(decoded->actor_count, 1);
	actor = world_spelunk_runtime_actor_at(decoded, 4, 5);
	notnull(actor);
	require(streq(actor->id, WORLD_SPELUNK_CHASM_SKITTER_ID));
	eq(actor->hp, 4);
	eq(actor->max_hp, 4);
	eq(actor->energy, 37U);
	stable_written = payload_without_layout_revision(stable_payload,
		sizeof(stable_payload), payload, written);
	require(stable_written > 0);

	/* Versions seven and eight used a numeric actor kind.  Their records remain
	 * loadable and are migrated to the stable data ID. */
	{
		size_t stable_record = 11u + strlen(WORLD_SPELUNK_CHASM_SKITTER_ID);
		size_t tail = stable_written - 2u - stable_record;
		size_t record = tail + 2u;

		require(stable_written > 2u + stable_record);
		memcpy(legacy_payload, stable_payload, tail);
		legacy_payload[tail] = 1;
		legacy_payload[tail + 1] = 1;
		legacy_payload[record] = 1;
		set_u16(&legacy_payload[record + 1], 4);
		set_u16(&legacy_payload[record + 3], 5);
		set_u16(&legacy_payload[record + 5], 4);
		set_u16(&legacy_payload[record + 7], 4);
		legacy_written = record + 9u;
	}
	legacy_payload[4] = WORLD_SPELUNK_PAYLOAD_VERSION_ACTORS;
	legacy_payload[5] = 0;
	eq(world_spelunk_runtime_decode(legacy_payload, legacy_written,
		&legacy_decoded), WORLD_SPELUNK_CODEC_OK);
	notnull(legacy_decoded);
	actor = world_spelunk_runtime_actor_at(legacy_decoded, 4, 5);
	notnull(actor);
	eq(actor->energy, 0U);
	world_spelunk_runtime_free(legacy_decoded);
	legacy_decoded = NULL;

	/* An emptied initialized roster remains empty after persistence; defeated
	 * actors must not return when the location is revisited. */
	mutable_actor = world_spelunk_runtime_actor_at_mutable(decoded, 4, 5);
	notnull(mutable_actor);
	require(world_spelunk_runtime_remove_actor(decoded, mutable_actor));
	eq(decoded->actor_count, 0);
	eq(world_spelunk_runtime_encode(decoded, payload, sizeof(payload),
		&written), WORLD_SPELUNK_CODEC_OK);
	world_spelunk_runtime_free(runtime);
	runtime = NULL;
	world_spelunk_runtime_free(decoded);
	decoded = NULL;
	eq(world_spelunk_runtime_decode(payload, written, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	require(decoded->actor_roster_initialized);
	eq(decoded->actor_count, 0);
	world_spelunk_runtime_free(decoded);
	ok;
}

static int test_look_inspection_respects_knowledge(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_inspect inspect;
	struct world_spelunk_view view;
	uint8_t cells[9];
	size_t actor_index;
	size_t rock_index;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	require(fixture_init(&f, 1, 8, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	runtime->actor_roster_initialized = true;
	require(world_spelunk_runtime_add_actor(runtime,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 6, 1));
	actor_index = (size_t)1 * runtime->state.map.stride + 6;
	runtime->explored[actor_index] = 0;
	runtime->visible[actor_index] = 0;
	require(world_spelunk_view_inspect(runtime, 6, 1, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_UNKNOWN);
	null(inspect.actor);
	null(inspect.actor_definition);

	/* Detection may disclose the occupant without pretending the cell is in
	 * direct sight. */
	require(world_spelunk_visibility_detect_cell(runtime, 6, 1));
	require(world_spelunk_view_inspect(runtime, 6, 1, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_ACTOR);
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_DETECTED);
	notnull(inspect.actor);
	notnull(inspect.actor_definition);
	runtime->visible[actor_index] = 1;
	require(world_spelunk_view_inspect(runtime, 6, 1, &inspect));
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_VISIBLE);

	rock_index = (size_t)9 * runtime->state.map.stride + 7;
	runtime->explored[rock_index] = 1;
	runtime->visible[rock_index] = 0;
	require(world_spelunk_visibility_is_surface_visible(runtime, 7, 9));
	require(world_spelunk_view_inspect(runtime, 7, 9, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_ROCK);
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_VISIBLE);
	/* Surface sight does not persist merely because the face is remembered. */
	runtime->visible[(size_t)8 * runtime->state.map.stride + 7] = 0;
	require(!world_spelunk_visibility_is_surface_visible(runtime, 7, 9));
	require(world_spelunk_view_inspect(runtime, 7, 9, &inspect));
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_REMEMBERED);
	require(world_spelunk_view_inspect(runtime, 1, 8, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_PLAYER);
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_VISIBLE);

	/* An explicit Look focus pans presentation without changing player state. */
	require(world_spelunk_view_capture_at(runtime, 6, 1, 3, 3, cells,
		N_ELEMENTS(cells), &view));
	eq(view.source_x, 5);
	eq(view.source_y, 0);
	eq(runtime->state.x, 1);
	eq(runtime->state.y, 8);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_look_identifies_visible_legacy_formation(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_inspect inspect;
	struct world_spelunk_view view;
	uint8_t cells[TEST_WIDTH * TEST_HEIGHT];

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 2);
	fixture_ground_row(&f, 4);
	require(fixture_init(&f, 3, 3, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	require(world_spelunk_view_capture(runtime, TEST_WIDTH, TEST_HEIGHT,
		cells, N_ELEMENTS(cells), &view));
	/* The stable appearance hash gives this ceiling cell a stalactite. */
	eq(world_spelunk_view_cell_at(&view, 6, 3),
		WORLD_SPELUNK_VIEW_STALACTITE);
	require(world_spelunk_view_inspect(runtime, 6, 3, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_STALACTITE);
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_VISIBLE);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_runtime_payload_roundtrip(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_runtime *decoded = NULL;
	uint8_t payload[512];
	size_t size;
	size_t written;
	int i;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	fixture_rock(&f, 1, 5);
	fixture_water(&f, 6, 4);
	f.rules.fall_base_damage = 13;
	f.rules.grip_up_stamina_cost = 11;
	f.rules.grip_lateral_stamina_cost = 7;
	f.rules.grip_down_stamina_cost = 4;
	f.rules.rope_up_stamina_cost = 5;
	f.rules.rope_lateral_stamina_cost = 3;
	f.rules.rope_down_stamina_cost = 2;
	f.rules.breath_turns = 9;
	f.rules.drowning_damage = 17;
	f.rules.swim_turn_stamina_cost = 3;
	require(fixture_init(&f, 2, 5, 100));
	f.state.stamina = 63;
	f.state.breath = 4;
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 5;
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	runtime->layout_revision = 3;
	runtime->explored[7 * TEST_WIDTH + 7] = 1;
	require(!world_spelunk_visibility_is_visible(runtime, 7, 7));
	require(world_spelunk_runtime_is_valid(runtime));
	f.cells[0] = WORLD_SPELUNK_ROCK;
	eq(runtime->cells[0], WORLD_SPELUNK_AIR);
	size = world_spelunk_runtime_encoded_size(runtime);
	require(size > 0 && size <= sizeof(payload));
	eq(world_spelunk_runtime_encode(runtime, payload, sizeof(payload),
		&written), WORLD_SPELUNK_CODEC_OK);
	eq(written, size);
	eq(world_spelunk_runtime_decode(payload, written, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	require(world_spelunk_runtime_is_valid(decoded));
	require(streq(decoded->location_id, runtime->location_id));
	eq(decoded->layout_revision, 3);
	eq(decoded->state.map.width, TEST_WIDTH);
	eq(decoded->state.map.height, TEST_HEIGHT);
	eq(decoded->state.x, 2);
	eq(decoded->state.y, 5);
	eq(decoded->state.stamina, 63);
	eq(decoded->state.max_stamina, 100);
	eq(decoded->state.breath, 4);
	eq(decoded->state.grip_target_x, 1);
	eq(decoded->state.grip_target_y, 5);
	eq(decoded->state.rules.fall_base_damage, 13);
	eq(decoded->state.rules.grip_up_stamina_cost, 11);
	eq(decoded->state.rules.grip_lateral_stamina_cost, 7);
	eq(decoded->state.rules.grip_down_stamina_cost, 4);
	eq(decoded->state.rules.rope_up_stamina_cost, 5);
	eq(decoded->state.rules.rope_lateral_stamina_cost, 3);
	eq(decoded->state.rules.rope_down_stamina_cost, 2);
	eq(decoded->state.rules.breath_turns, 9);
	eq(decoded->state.rules.drowning_damage, 17);
	eq(decoded->state.rules.swim_turn_stamina_cost, 3);
	for (i = 0; i < TEST_WIDTH * TEST_HEIGHT; i++) {
		eq(decoded->cells[i], runtime->cells[i]);
	}
	eq(decoded->cells[4 * TEST_WIDTH + 6], WORLD_SPELUNK_WATER);
	require(world_spelunk_visibility_is_explored(decoded, 7, 7));
	require(!world_spelunk_visibility_is_visible(decoded, 7, 7));
	world_spelunk_runtime_free(decoded);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_runtime_legacy_payload_decode(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_runtime *decoded = NULL;
	uint8_t payload[512];
	uint8_t exploration[512];
	uint8_t legacy[512];
	uint8_t infrastructure[512];
	uint8_t rope[512];
	uint8_t water[512];
	size_t written;
	size_t exploration_size;
	size_t water_size;
	size_t legacy_size;
	size_t infrastructure_size;
	size_t rope_rule_offset;
	size_t rope_cost_offset;
	int i;
	size_t explored_runs = 0;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	require(fixture_init(&f, 2, 5, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	eq(world_spelunk_runtime_encode(runtime, payload, sizeof(payload),
		&written), WORLD_SPELUNK_CODEC_OK);
	exploration_size = payload_without_breath(exploration,
		sizeof(exploration), payload, written);
	require(exploration_size > 0);
	eq(world_spelunk_runtime_decode(exploration, exploration_size, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	eq(decoded->state.rules.breath_turns, 6);
	eq(decoded->state.rules.drowning_damage, 10);
	eq(decoded->state.breath, 6);
	world_spelunk_runtime_free(decoded);
	decoded = NULL;
	for (i = 0; i < TEST_WIDTH * TEST_HEIGHT; i++) {
		if (i == 0 || runtime->explored[i] != runtime->explored[i - 1]) {
			explored_runs++;
		}
	}
	water_size = exploration_size - 2u - 3u * explored_runs;
	memcpy(water, exploration, water_size);
	water[4] = WORLD_SPELUNK_PAYLOAD_VERSION_WATER;
	water[5] = 0;
	eq(world_spelunk_runtime_decode(water, water_size, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	require(world_spelunk_runtime_is_valid(decoded));
	for (i = 0; i < TEST_WIDTH * TEST_HEIGHT; i++) {
		require(world_spelunk_visibility_is_explored(decoded,
			i % TEST_WIDTH, i / TEST_WIDTH));
	}
	world_spelunk_runtime_free(decoded);
	decoded = NULL;

	/* Version 3 has the version-four layout but predates semantic water. */
	memcpy(rope, water, water_size);
	rope[4] = WORLD_SPELUNK_PAYLOAD_VERSION_ROPE;
	rope[5] = 0;
	eq(world_spelunk_runtime_decode(rope, water_size, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	require(world_spelunk_runtime_is_valid(decoded));
	world_spelunk_runtime_free(decoded);
	decoded = NULL;

	/* Version 2 has infrastructure but no separate rope stamina rule. */
	rope_rule_offset = 25u + payload[6];
	rope_cost_offset = rope_rule_offset + 2u;
	require(water_size > rope_cost_offset + 12u);
	memcpy(infrastructure, water, rope_cost_offset);
	infrastructure[4] = WORLD_SPELUNK_PAYLOAD_VERSION_INFRASTRUCTURE;
	infrastructure[5] = 0;
	memcpy(infrastructure + rope_cost_offset, water + rope_cost_offset + 2u,
		water_size - rope_cost_offset - 2u);
	infrastructure_size = water_size - 2u;
	eq(world_spelunk_runtime_decode(infrastructure, infrastructure_size,
		&decoded), WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	require(world_spelunk_runtime_is_valid(decoded));
	eq(decoded->state.rules.rope_max_length, 20);
	eq(decoded->state.rules.rope_down_stamina_cost, 1);
	world_spelunk_runtime_free(decoded);
	decoded = NULL;

	/* Version 1 has neither rope rule nor piton/rope overlay runs. */
	memcpy(legacy, water, rope_rule_offset);
	legacy[4] = WORLD_SPELUNK_PAYLOAD_VERSION_LEGACY;
	legacy[5] = 0;
	memcpy(legacy + rope_rule_offset, water + rope_rule_offset + 4u,
		water_size - rope_rule_offset - 14u);
	legacy_size = water_size - 14u;
	eq(world_spelunk_runtime_decode(legacy, legacy_size, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	require(world_spelunk_runtime_is_valid(decoded));
	eq(decoded->state.rules.rope_max_length, 20);
	eq(decoded->state.rules.rope_down_stamina_cost, 1);
	for (i = 0; i < TEST_WIDTH * TEST_HEIGHT; i++) {
		eq(decoded->cells[i], runtime->cells[i]);
		eq(decoded->pitons[i], 0);
		eq(decoded->ropes[i], 0);
	}

	world_spelunk_runtime_free(decoded);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_runtime_payload_rejects_corruption(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_runtime *decoded = NULL;
	uint8_t payload[512];
	uint8_t saved;
	size_t written;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 6);
	require(fixture_init(&f, 2, 5, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	eq(world_spelunk_runtime_encode(runtime, payload, sizeof(payload),
		&written), WORLD_SPELUNK_CODEC_OK);

	saved = payload[0];
	payload[0] = 'X';
	eq(world_spelunk_runtime_decode(payload, written, &decoded),
		WORLD_SPELUNK_CODEC_BAD_MAGIC);
	null(decoded);
	payload[0] = saved;
	saved = payload[4];
	payload[4] = WORLD_SPELUNK_PAYLOAD_VERSION + 1;
	eq(world_spelunk_runtime_decode(payload, written, &decoded),
		WORLD_SPELUNK_CODEC_UNSUPPORTED_VERSION);
	null(decoded);
	payload[4] = saved;
	eq(world_spelunk_runtime_decode(payload, written - 1, &decoded),
		WORLD_SPELUNK_CODEC_TRUNCATED);
	null(decoded);
	saved = payload[written - 11];
	payload[written - 11] = 99;
	eq(world_spelunk_runtime_decode(payload, written, &decoded),
		WORLD_SPELUNK_CODEC_CORRUPT);
	null(decoded);
	payload[written - 11] = saved;

	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_infrastructure_rules_and_persistence(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_runtime *decoded = NULL;
	struct world_spelunk_action_report report;
	struct world_spelunk_command down = {
		WORLD_SPELUNK_COMMAND_MOVE, 0, 1
	};
	uint8_t payload[1024];
	uint8_t exploration[1024];
	uint8_t previous[1024];
	size_t written;
	size_t exploration_size;
	size_t previous_size;
	size_t water_size;
	size_t rope_cost_offset;
	size_t explored_runs = 0;
	int segments = 0;
	int i;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	fixture_rock(&f, 1, 2);
	fixture_rock(&f, 1, 3);
	f.rules.rope_max_length = 3;
	require(fixture_init(&f, 2, 2, 100));
	f.state.grip_target_x = 1;
	f.state.grip_target_y = 2;
	f.state.movement = WORLD_SPELUNK_CLIMBING;
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	eq(world_spelunk_runtime_place_piton(runtime),
		WORLD_SPELUNK_INFRASTRUCTURE_OK);
	require(world_spelunk_runtime_has_piton(runtime, 2, 2));
	eq(world_spelunk_runtime_place_piton(runtime),
		WORLD_SPELUNK_INFRASTRUCTURE_ALREADY_PRESENT);
	eq(world_spelunk_runtime_deploy_rope(runtime, 3, &segments),
		WORLD_SPELUNK_INFRASTRUCTURE_OK);
	eq(segments, 3);
	require(world_spelunk_runtime_has_rope(runtime, 2, 3));
	require(world_spelunk_runtime_has_rope(runtime, 2, 5));
	require(!world_spelunk_runtime_has_rope(runtime, 2, 6));
	eq(world_spelunk_apply_command(&runtime->state, &down, &report),
		WORLD_SPELUNK_ACTION_TURN);
	eq(report.event, WORLD_SPELUNK_EVENT_ROPE_ATTACHED);
	eq(runtime->state.y, 3);
	eq(runtime->state.movement, WORLD_SPELUNK_HANGING);
	runtime->state.grip_target_x = 1;
	runtime->state.grip_target_y = 3;
	eq(world_spelunk_runtime_place_piton(runtime),
		WORLD_SPELUNK_INFRASTRUCTURE_ALREADY_PRESENT);
	require(!world_spelunk_runtime_has_piton(runtime, 2, 3));
	require(world_spelunk_runtime_is_valid(runtime));
	eq(world_spelunk_runtime_encode(runtime, payload, sizeof(payload),
		&written), WORLD_SPELUNK_CODEC_OK);
	eq(world_spelunk_runtime_decode(payload, written, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	eq(decoded->state.rules.rope_max_length, 3);
	eq(decoded->state.movement, WORLD_SPELUNK_HANGING);
	require(world_spelunk_runtime_has_piton(decoded, 2, 2));
	require(world_spelunk_runtime_has_rope(decoded, 2, 5));
	world_spelunk_runtime_free(decoded);
	decoded = NULL;

	/* Version 2 represented a player on a rope as climbing.  Removing the new
	 * visibility runs and rope-cost field emulates that save and exercises its
	 * state migration. */
	for (i = 0; i < TEST_WIDTH * TEST_HEIGHT; i++) {
		if (i == 0 || runtime->explored[i] != runtime->explored[i - 1]) {
			explored_runs++;
		}
	}
	exploration_size = payload_without_breath(exploration,
		sizeof(exploration), payload, written);
	require(exploration_size > 0);
	water_size = exploration_size - 2u - 3u * explored_runs;
	rope_cost_offset = 27u + exploration[6];
	memcpy(previous, exploration, rope_cost_offset);
	previous[4] = WORLD_SPELUNK_PAYLOAD_VERSION_INFRASTRUCTURE;
	previous[5] = 0;
	memcpy(previous + rope_cost_offset, exploration + rope_cost_offset + 2u,
		water_size - rope_cost_offset - 2u);
	previous_size = water_size - 2u;
	previous[41u + exploration[6]] = WORLD_SPELUNK_CLIMBING;
	eq(world_spelunk_runtime_decode(previous, previous_size, &decoded),
		WORLD_SPELUNK_CODEC_OK);
	notnull(decoded);
	require(world_spelunk_runtime_is_valid(decoded));
	eq(decoded->state.movement, WORLD_SPELUNK_HANGING);
	eq(decoded->state.rules.rope_down_stamina_cost, 1);
	world_spelunk_runtime_free(decoded);
	world_spelunk_runtime_free(runtime);
	ok;
}


const char *suite_name = "game/spelunking-codec";
struct test tests[] = {
	{ "cave survey respects discovery", test_cave_survey_respects_discovery },
	{ "runtime actor persistence and view",
		test_runtime_actor_persistence_and_view },
	{ "Look inspection respects knowledge",
		test_look_inspection_respects_knowledge },
	{ "Look identifies visible legacy formation",
		test_look_identifies_visible_legacy_formation },
	{ "runtime payload roundtrip", test_runtime_payload_roundtrip },
	{ "runtime legacy payload decode", test_runtime_legacy_payload_decode },
	{ "runtime payload rejects corruption",
		test_runtime_payload_rejects_corruption },
	{ "infrastructure rules and persistence",
		test_infrastructure_rules_and_persistence },
	{ NULL, NULL }
};
