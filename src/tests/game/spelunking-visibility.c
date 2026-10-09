/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Visibility, camera, input translation, and command-policy contracts. */

#include "spelunking-fixture.h"

static int test_view_snapshot_centers_and_marks_state(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_view view;
	uint8_t cells[25];

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	fixture_rock(&f, 3, 8);
	fixture_rock(&f, 5, 8);
	fixture_water(&f, 2, 7);
	require(fixture_init(&f, 4, 8, 73));
	f.state.grip_target_x = 3;
	f.state.grip_target_y = 8;
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	runtime->actor_roster_initialized = true;
	require(world_spelunk_runtime_add_actor(runtime,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 6, 8));
	require(world_spelunk_view_capture(runtime, 5, 5, cells,
		N_ELEMENTS(cells), &view));
	eq(view.cols, 5);
	eq(view.rows, 5);
	eq(view.source_x, 2);
	eq(view.source_y, 5);
	eq(view.player_col, 2);
	eq(view.player_row, 3);
	eq(view.stamina, 73);
	eq(view.max_stamina, 73);
	require(streq(view.location_id, "core.spelunk.test.001"));
	eq(world_spelunk_view_cell_at(&view, 2, 3),
		WORLD_SPELUNK_VIEW_PLAYER);
	eq(world_spelunk_view_cell_at(&view, 1, 3),
		WORLD_SPELUNK_VIEW_GRIP);
	eq(world_spelunk_view_cell_at(&view, 0, 4),
		WORLD_SPELUNK_VIEW_UNKNOWN);
	eq(world_spelunk_view_cell_at(&view, 0, 2),
		WORLD_SPELUNK_VIEW_UNKNOWN);
	eq(world_spelunk_view_cell_at(&view, 0, 0),
		WORLD_SPELUNK_VIEW_AIR);
	eq(world_spelunk_view_cell_at(&view, 4, 3),
		WORLD_SPELUNK_VIEW_UNKNOWN);
	eq(view.actor_appearance_count, 0U);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_visibility_occlusion_peek_and_exploration(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	fixture_rock(&f, 2, 3);
	require(fixture_init(&f, 1, 3, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	require(world_spelunk_visibility_is_visible(runtime, 2, 3));
	/* Exact corner crossings are blocked symmetrically.  The open cell beyond
	 * the wall is neither visible nor prematurely remembered. */
	require(!world_spelunk_visibility_line_is_clear(runtime, 1, 3, 3, 2));
	require(!world_spelunk_visibility_line_is_clear(runtime, 3, 2, 1, 3));
	require(!world_spelunk_visibility_is_visible(runtime, 3, 2));
	require(!world_spelunk_visibility_is_explored(runtime, 3, 2));

	/* Leaning into the open cell creates a real, temporary line of sight. */
	require(world_spelunk_visibility_begin_peek(runtime));
	require(world_spelunk_visibility_move_peek(runtime, 1, -1));
	require(runtime->peeking);
	eq(runtime->state.x, 1);
	eq(runtime->state.y, 3);
	eq(runtime->peek_x, 2);
	eq(runtime->peek_y, 2);
	require(world_spelunk_visibility_is_visible(runtime, 3, 2));
	require(world_spelunk_visibility_is_explored(runtime, 3, 2));
	world_spelunk_visibility_end_peek(runtime);
	require(!runtime->peeking);
	require(!world_spelunk_visibility_is_visible(runtime, 3, 2));
	require(world_spelunk_visibility_is_explored(runtime, 3, 2));
	require(world_spelunk_runtime_is_valid(runtime));
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_directional_sight_and_downward_peek(void *unused)
{
	enum { WIDTH = 9, HEIGHT = 32 };
	enum world_spelunk_tile cells[WIDTH * HEIGHT];
	struct world_spelunk_map map = { cells, WIDTH, HEIGHT, WIDTH };
	struct world_spelunk_rules rules;
	struct world_spelunk_state state;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_view view;
	uint8_t snapshot[WIDTH * HEIGHT];
	int attr;
	wchar_t glyph;
	int x;
	int y;

	(void)unused;
	fixture_rules(&rules, 100);
	for (y = 0; y < HEIGHT; y++) {
		for (x = 0; x < WIDTH; x++) cells[y * WIDTH + x] = WORLD_SPELUNK_ROCK;
	}
	/* An upper ledge and a separate lower chamber prove that sight cannot
	 * cross an uninterrupted floor. */
	for (y = 1; y <= 2; y++) {
		for (x = 1; x <= 5; x++) cells[y * WIDTH + x] = WORLD_SPELUNK_AIR;
	}
	for (y = 5; y <= 8; y++) {
		for (x = 1; x <= 5; x++) cells[y * WIDTH + x] = WORLD_SPELUNK_AIR;
	}
	require(world_spelunk_state_init(&state, &map, &rules, 3, 2, 100));
	runtime = world_spelunk_runtime_create("synthetic.sealed.context", &state);
	notnull(runtime);
	require(!world_spelunk_visibility_is_explored(runtime, 3, 5));
	world_spelunk_runtime_free(runtime);

	/* Rebuild as a narrow open shaft.  Standing at the lip sees the first two
	 * rows.  Leaning down-right turns the shaft into long, genuine sight. */
	for (y = 0; y < HEIGHT; y++) {
		for (x = 0; x < WIDTH; x++) cells[y * WIDTH + x] = WORLD_SPELUNK_ROCK;
	}
	for (x = 1; x <= 3; x++) cells[2 * WIDTH + x] = WORLD_SPELUNK_AIR;
	for (y = 3; y < HEIGHT - 1; y++) cells[y * WIDTH + 3] = WORLD_SPELUNK_AIR;
	require(world_spelunk_state_init(&state, &map, &rules, 2, 2, 100));
	runtime = world_spelunk_runtime_create("synthetic.deep.context", &state);
	notnull(runtime);
	require(world_spelunk_view_capture(runtime, WIDTH, HEIGHT, snapshot,
		N_ELEMENTS(snapshot), &view));
	eq(world_spelunk_view_cell_at(&view, 3, 3),
		WORLD_SPELUNK_VIEW_AIR);
	spelunking_view_cell_appearance(&view, 3, 3, &attr, &glyph);
	eq(glyph, L'.');
	eq(attr, COLOUR_SLATE);
	require(world_spelunk_visibility_is_visible(runtime, 3, 4));
	require(world_spelunk_visibility_is_explored(runtime, 3, 4));
	require(!world_spelunk_visibility_is_explored(runtime, 3, 5));
	require(!world_spelunk_visibility_is_explored(runtime, 3, 14));
	require(!world_spelunk_visibility_is_explored(runtime, 3, 22));
	require(world_spelunk_visibility_begin_peek(runtime));
	require(world_spelunk_visibility_move_peek(runtime, 1, 1));
	require(world_spelunk_visibility_is_explored(runtime, 3, 22));
	require(world_spelunk_visibility_is_visible(runtime, 3, 22));
	require(world_spelunk_view_capture(runtime, WIDTH, HEIGHT, snapshot,
		N_ELEMENTS(snapshot), &view));
	eq(world_spelunk_view_cell_at(&view, 3, 22),
		WORLD_SPELUNK_VIEW_AIR);
	spelunking_view_cell_appearance(&view, 3, 22, &attr, &glyph);
	eq(glyph, L'.');
	eq(attr, COLOUR_SLATE);
	/* The ray reaches the bottom.  Its visible open column derives exactly one
	 * enclosing rock skin, so the shaft sides are continuous without becoming
	 * subject-visible or propagating into the rock mass. */
	require(world_spelunk_visibility_is_visible(runtime, 3, 31));
	require(!world_spelunk_visibility_is_visible(runtime, 4, 22));
	require(world_spelunk_visibility_is_surface_visible(runtime, 4, 22));
	require(world_spelunk_visibility_is_terrain_visible(runtime, 4, 22));
	require(world_spelunk_visibility_is_explored(runtime, 4, 22));
	require(!world_spelunk_visibility_is_explored(runtime, 5, 22));
	world_spelunk_visibility_end_peek(runtime);
	require(world_spelunk_visibility_is_explored(runtime, 3, 22));
	require(!world_spelunk_visibility_is_visible(runtime, 3, 22));
	require(!world_spelunk_visibility_is_surface_visible(runtime, 4, 22));
	require(!world_spelunk_visibility_is_terrain_visible(runtime, 4, 22));
	require(world_spelunk_visibility_is_explored(runtime, 4, 22));
	require(world_spelunk_view_capture(runtime, WIDTH, HEIGHT, snapshot,
		N_ELEMENTS(snapshot), &view));
	eq(world_spelunk_view_cell_at(&view, 3, 22),
		WORLD_SPELUNK_VIEW_REMEMBERED_AIR);
	eq(world_spelunk_view_cell_at(&view, 4, 22),
		WORLD_SPELUNK_VIEW_REMEMBERED_ROCK);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_visible_surface_skin_follows_open_passage(void *unused)
{
	enum { WIDTH = 10, HEIGHT = 7 };
	enum world_spelunk_tile cells[WIDTH * HEIGHT];
	struct world_spelunk_map map = { cells, WIDTH, HEIGHT, WIDTH };
	struct world_spelunk_rules rules;
	struct world_spelunk_state state;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_inspect inspect;
	int x;
	int y;

	(void)unused;
	fixture_rules(&rules, 100);
	for (y = 0; y < HEIGHT; y++) {
		for (x = 0; x < WIDTH; x++) cells[y * WIDTH + x] = WORLD_SPELUNK_ROCK;
	}
	for (x = 1; x <= 8; x++) cells[3 * WIDTH + x] = WORLD_SPELUNK_AIR;
	require(world_spelunk_state_init(&state, &map, &rules, 1, 3, 100));
	runtime = world_spelunk_runtime_create("synthetic.surface.passage", &state);
	notnull(runtime);

	/* The passage itself has direct sight. Its distant ceiling and floor do
	 * not, because nearer rock centres occlude them, but their exposed faces
	 * form the derived surface skin and present as currently visible rock. */
	require(world_spelunk_visibility_is_visible(runtime, 8, 3));
	require(!world_spelunk_visibility_is_visible(runtime, 8, 2));
	require(!world_spelunk_visibility_is_visible(runtime, 8, 4));
	require(world_spelunk_visibility_is_surface_visible(runtime, 8, 2));
	require(world_spelunk_visibility_is_surface_visible(runtime, 8, 4));
	require(world_spelunk_view_inspect(runtime, 8, 4, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_ROCK);
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_VISIBLE);
	/* The skin is one cell thick and cannot disclose rock behind the face. */
	require(!world_spelunk_visibility_is_explored(runtime, 8, 5));
	require(world_spelunk_runtime_is_valid(runtime));
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_view_snapshot_clamps_and_rejects_small_storage(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_view view;
	uint8_t cells[TEST_WIDTH * TEST_HEIGHT];

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	require(fixture_init(&f, 0, 8, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	require(world_spelunk_view_capture(runtime, 5, 20, cells,
		N_ELEMENTS(cells), &view));
	eq(view.cols, 5);
	eq(view.rows, TEST_HEIGHT);
	eq(view.source_x, 0);
	eq(view.source_y, 0);
	eq(world_spelunk_view_cell_at(&view, -1, 0),
		WORLD_SPELUNK_VIEW_VOID);
	eq(world_spelunk_view_cell_at(&view, view.cols, 0),
		WORLD_SPELUNK_VIEW_VOID);
	require(!world_spelunk_view_capture(runtime, 5, 20, cells, 49, &view));
	null(view.cells);
	eq(view.cell_count, 0);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_view_camera_follows_one_cell_at_a_time(void *unused)
{
	struct spelunk_fixture f;
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_view first;
	struct world_spelunk_view second;
	uint8_t first_cells[25];
	uint8_t second_cells[25];

	(void)unused;
	fixture_open(&f);
	fixture_ground_row(&f, 9);
	require(fixture_init(&f, 3, 8, 100));
	runtime = world_spelunk_runtime_create("core.spelunk.test.001",
		&f.state);
	notnull(runtime);
	require(world_spelunk_view_capture(runtime, 5, 5, first_cells,
		N_ELEMENTS(first_cells), &first));
	runtime->state.x = 4;
	require(world_spelunk_runtime_is_valid(runtime));
	require(world_spelunk_view_capture(runtime, 5, 5, second_cells,
		N_ELEMENTS(second_cells), &second));
	eq(first.source_x, 1);
	eq(second.source_x, 2);
	eq(second.source_x - first.source_x, 1);
	eq(first.player_col, second.player_col);
	world_spelunk_runtime_free(runtime);
	ok;
}

static int test_physical_directions_translate_once(void *unused)
{
	struct textui_spelunking_binding binding;
	struct keypress key = { EVT_KBRD, ARROW_LEFT, 0 };

	(void)unused;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_MOVE);
	eq(binding.action.dx, -1);
	eq(binding.action.dy, 0);
	key.code = '9';
	key.mods = KC_MOD_KEYPAD;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_MOVE);
	eq(binding.action.dx, 1);
	eq(binding.action.dy, -1);
	key.code = ARROW_DOWN;
	key.mods = KC_MOD_SHIFT;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_BLOCKED);
	ok;
}

static int test_physical_actions_and_exit_translate(void *unused)
{
	struct textui_spelunking_binding binding;
	struct keypress key = { EVT_KBRD, '.', 0 };

	(void)unused;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_WAIT);
	key.code = 'H';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_GRIP);
	key.code = 'h';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_GRIP);
	key.mods = KC_MOD_SHIFT;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_GRIP);
	key.mods = 0;
	key.code = 'J';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_JUMP);
	key.code = 'j';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_JUMP);
	key.mods = KC_MOD_SHIFT;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_ACTION);
	eq(binding.action.kind, WORLD_SPELUNK_COMMAND_JUMP);
	key.mods = 0;
	key.code = '\'';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_LOCAL);
	eq(binding.local_action, WORLD_SPELUNK_LOCAL_PITON);
	key.code = ';';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_LOCAL);
	eq(binding.local_action, WORLD_SPELUNK_LOCAL_ROPE);
	key.code = 'g';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_LOCAL);
	eq(binding.local_action, WORLD_SPELUNK_LOCAL_PICKUP);
	key.code = 'G';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_UNHANDLED);
	key.code = 'D';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_DROP);
	key.code = 'p';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_PEEK);
	eq(binding.peek_action, WORLD_SPELUNK_PEEK_TOGGLE);
	key.code = KC_MODE_PEEK_BEGIN;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_PEEK);
	eq(binding.peek_action, WORLD_SPELUNK_PEEK_BEGIN);
	key.code = KC_MODE_PEEK_END;
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_PEEK);
	eq(binding.peek_action, WORLD_SPELUNK_PEEK_END);
	key.code = '<';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_PASSAGE);
	eq(binding.passage_direction, WORLD_SPELUNK_PASSAGE_UP);
	key.code = '>';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_PASSAGE);
	eq(binding.passage_direction, WORLD_SPELUNK_PASSAGE_DOWN);
	key.code = 'q';
	eq(textui_spelunking_translate_key(key, &binding),
		TEXTUI_SPELUNKING_UNHANDLED);
	eq(textui_spelunking_translate_key(key, NULL),
		TEXTUI_SPELUNKING_BLOCKED);
	ok;
}

static int test_item_effect_policy_is_fail_closed(void *unused)
{
	struct effect healing = { NULL, EF_HEAL_HP, NULL, 0, 0, 0, 0, 0, NULL };
	struct effect timed = { &healing, EF_TIMED_INC, NULL, 0, 0, 0, 0, 0,
		NULL };
	struct effect nourish = { &timed, EF_NOURISH, NULL, 0, 0, 0, 0, 0,
		NULL };
	struct effect breath = { NULL, EF_BREATH, NULL, 0, 0, 0, 0, 0, NULL };
	struct effect select = { &breath, EF_SELECT, NULL, 0, 0, 0, 0, 0, NULL };
	struct effect light = { NULL, EF_LIGHT_LEVEL, NULL, 0, 0, 0, 0, 0,
		NULL };
	struct effect recall = { NULL, EF_RECALL, NULL, 0, 0, 0, 0, 0, NULL };
	struct effect unsupported = { NULL, EF_SUMMON, NULL, 0, 0, 0, 0, 0,
		NULL };
	struct effect bolt = { NULL, EF_BOLT, NULL, 0, 0, 0, 0, 0, NULL };
	struct effect status_bolt = { NULL, EF_BOLT_AWARE, NULL, 0, 0, 0, 0, 0,
		NULL };
	struct effect shapechange = {
		.index = EF_SHAPECHANGE,
		.subtype = 17
	};
	struct player_shape shape = { 0 };
	struct player_shape *saved_shapes = shapes;

	(void)unused;
	require(world_spelunk_item_effect_is_safe(&nourish));
	require(world_spelunk_item_effect_is_safe(&select));
	require(world_spelunk_item_effect_is_safe(&light));
	require(world_spelunk_item_effect_is_safe(&bolt));
	require(!world_spelunk_item_effect_is_safe(&status_bolt));
	require(!world_spelunk_effect_chain_is_player_local(&recall));
	require(world_spelunk_item_effect_is_safe(&recall));
	require(!world_spelunk_item_effect_is_safe(&unsupported));
	shape.sidx = 17;
	shape.effect = &unsupported;
	shapes = &shape;
	require(!world_spelunk_item_effect_is_safe(&shapechange));
	shape.effect = &healing;
	require(world_spelunk_item_effect_is_safe(&shapechange));
	/* Recursive authored shape effects fail closed rather than looping. */
	shape.effect = &shapechange;
	require(!world_spelunk_item_effect_is_safe(&shapechange));
	shapes = saved_shapes;
	require(!world_spelunk_item_effect_is_safe(NULL));
	ok;
}

static bool spelunking_policy_expected(cmd_code code)
{
	return code == CMD_SPELUNK_ACTION || code == CMD_SPELUNK_LOCAL ||
		code == CMD_SPELUNK_PEEK ||
		code == CMD_SPELUNK_PASSAGE ||
		code == CMD_SPELUNK_EXIT || code == CMD_INSCRIBE ||
		code == CMD_UNINSCRIBE || code == CMD_WIELD ||
		code == CMD_TAKEOFF || code == CMD_DROP || code == CMD_REFILL ||
		code == CMD_EAT || code == CMD_QUAFF || code == CMD_READ_SCROLL ||
		code == CMD_USE_STAFF || code == CMD_USE_WAND ||
		code == CMD_USE_ROD || code == CMD_ACTIVATE || code == CMD_USE ||
		code == CMD_BROWSE_SPELL || code == CMD_STUDY || code == CMD_CAST ||
		code == CMD_FIRE || code == CMD_THROW ||
		code == CMD_FISHING_START || code == CMD_FISHING_ACTION ||
		code == CMD_FISHING_STOP || code == CMD_SLEEP || code == CMD_RETIRE;
}

static int test_command_policy_is_fail_closed(void *unused)
{
	int code;

	(void)unused;
	for (code = CMD_NULL; code <= CMD_COMMAND_MONSTER; code++) {
		eq(cmd_mode_policy_allows(WORLD_TURN_CONTEXT_SPELUNKING,
			CTX_GAME, (cmd_code)code),
			spelunking_policy_expected((cmd_code)code));
		require(cmd_mode_policy_allows(WORLD_TURN_CONTEXT_TOP_DOWN,
			CTX_GAME, (cmd_code)code));
		require(!cmd_mode_policy_allows(WORLD_TURN_CONTEXT_INVALID,
			CTX_GAME, (cmd_code)code));
		/* Non-world command loops retain their existing lifecycle policy. */
		require(cmd_mode_policy_allows(WORLD_TURN_CONTEXT_INVALID,
			CTX_BIRTH, (cmd_code)code));
	}
	ok;
}

static int test_mode_capabilities_are_semantic_and_fail_closed(void *unused)
{
	int capability;

	(void)unused;
	require(world_turn_context_is_valid(WORLD_TURN_CONTEXT_TOP_DOWN));
	require(world_turn_context_is_valid(WORLD_TURN_CONTEXT_SPELUNKING));
	require(!world_turn_context_is_valid(WORLD_TURN_CONTEXT_INVALID));
	for (capability = 0; capability < WORLD_MODE_CAP_MAX; capability++) {
		require(!world_turn_context_has_capability(
			WORLD_TURN_CONTEXT_INVALID,
			(enum world_mode_capability)capability));
	}
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN, WORLD_MODE_CAP_NATIVE_CAVE));
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN, WORLD_MODE_CAP_MOUSE));
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN, WORLD_MODE_CAP_FISHING));
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN,
		WORLD_MODE_CAP_LEGACY_LEVEL_CHANGE));
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_SPELUNKING, WORLD_MODE_CAP_SIDE_VIEW));
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_SPELUNKING, WORLD_MODE_CAP_PRIMARY_INPUT));
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_SPELUNKING, WORLD_MODE_CAP_LOCAL_STORAGE));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN, WORLD_MODE_CAP_SIDE_VIEW));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN, WORLD_MODE_CAP_PRIMARY_INPUT));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN, WORLD_MODE_CAP_LOCAL_STORAGE));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_SPELUNKING, WORLD_MODE_CAP_NATIVE_CAVE));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_SPELUNKING, WORLD_MODE_CAP_MOUSE));
	require(world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_SPELUNKING, WORLD_MODE_CAP_FISHING));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_SPELUNKING,
		WORLD_MODE_CAP_LEGACY_LEVEL_CHANGE));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN,
		(enum world_mode_capability)-1));
	require(!world_turn_context_has_capability(
		WORLD_TURN_CONTEXT_TOP_DOWN,
		(enum world_mode_capability)WORLD_MODE_CAP_MAX));
	ok;
}


const char *suite_name = "game/spelunking-visibility";
struct test tests[] = {
	{ "view snapshot centers and marks state",
		test_view_snapshot_centers_and_marks_state },
	{ "visibility occlusion peek and exploration",
		test_visibility_occlusion_peek_and_exploration },
	{ "directional sight and downward peek",
		test_directional_sight_and_downward_peek },
	{ "visible surface skin follows open passage",
		test_visible_surface_skin_follows_open_passage },
	{ "view snapshot clamps and rejects small storage",
		test_view_snapshot_clamps_and_rejects_small_storage },
	{ "view camera follows one cell at a time",
		test_view_camera_follows_one_cell_at_a_time },
	{ "physical directions translate once",
		test_physical_directions_translate_once },
	{ "physical actions and exit translate",
		test_physical_actions_and_exit_translate },
	{ "item effect policy is fail closed",
		test_item_effect_policy_is_fail_closed },
	{ "command policy is fail closed",
		test_command_policy_is_fail_closed },
	{ "mode capabilities are semantic and fail closed",
		test_mode_capabilities_are_semantic_and_fail_closed },
	{ NULL, NULL }
};
