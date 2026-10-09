/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/spelunking-presenter.c */

#include "unit-test.h"

#include "datafile.h"
#include "map-visual-data.h"
#include "object.h"
#include "obj-pile.h"
#include "parser.h"
#include "sdl3/spelunking-presenter.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-visibility.h"
#include "world-spelunking-view-terrain.h"
#include "z-color.h"

#define TEST_WIDTH 48
#define TEST_HEIGHT 30

struct fixture {
	enum world_spelunk_tile cells[TEST_WIDTH * TEST_HEIGHT];
	struct world_spelunk_map map;
	struct world_spelunk_rules rules;
	struct world_spelunk_state state;
	struct world_spelunk_runtime *runtime;
};

static void test_spelunk_rules(struct world_spelunk_rules *rules)
{
	memset(rules, 0, sizeof(*rules));
	rules->action_energy = 100;
	rules->safe_fall_tiles = 2;
	rules->fall_base_damage = 10;
	rules->jump_stamina_cost = 10;
	rules->grip_up_stamina_cost = 5;
	rules->grip_lateral_stamina_cost = 5;
	rules->grip_down_stamina_cost = 5;
	rules->rest_stamina_gain = 15;
	rules->rope_max_length = 20;
	rules->rope_up_stamina_cost = 1;
	rules->rope_lateral_stamina_cost = 1;
	rules->rope_down_stamina_cost = 1;
	rules->breath_turns = 6;
	rules->drowning_damage = 10;
	rules->swim_turn_stamina_cost = 2;
}

static bool fixture_open(struct fixture *f)
{
	int x;

	memset(f, 0, sizeof(*f));
	for (x = 0; x < TEST_WIDTH * TEST_HEIGHT; x++) {
		f->cells[x] = WORLD_SPELUNK_AIR;
	}
	for (x = 0; x < TEST_WIDTH; x++) {
		f->cells[(TEST_HEIGHT - 1) * TEST_WIDTH + x] = WORLD_SPELUNK_ROCK;
	}
	f->map.cells = f->cells;
	f->map.width = TEST_WIDTH;
	f->map.height = TEST_HEIGHT;
	f->map.stride = TEST_WIDTH;
	test_spelunk_rules(&f->rules);
	if (!world_spelunk_state_init(&f->state, &f->map, &f->rules,
			TEST_WIDTH / 2, TEST_HEIGHT - 2, 100)) {
		return false;
	}
	f->runtime = world_spelunk_runtime_create("core.spelunk.zoom.001",
		&f->state);
	if (!f->runtime) return false;
	f->runtime->actor_roster_initialized = true;
	return world_spelunk_runtime_add_actor(f->runtime,
		WORLD_SPELUNK_CHASM_SKITTER_ID, TEST_WIDTH / 2 + 2,
		TEST_HEIGHT - 2);
}

static void fixture_close(struct fixture *f)
{
	world_spelunk_runtime_free(f->runtime);
	f->runtime = NULL;
}

int setup_tests(void **data)
{
	static const char *lines[] = {
		"actor:core.spelunk.actor.chasm-skitter:chasm skitter",
		"death-cause:a chasm skitter",
		"appearance:k:Light Green",
		"combat:4:24:1:2",
		"vitals:4:110:2",
		"spawn:core.spelunk.spawn.test:core.spelunk.zoom.001:core.spelunk.actor.chasm-skitter",
		"candidate:1:26:28"
	};
	struct parser *p = spelunking_actor_parser.init();
	int i;

	(void)data;
	if (!p) return 1;
	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		if (parser_parse(p, lines[i]) != PARSE_ERROR_NONE) return 1;
	}
	if (spelunking_actor_parser.finish(p) != PARSE_ERROR_NONE) return 1;
	p = map_visual_parser.init();
	if (parser_parse(p, "tile:cave:rock:feature:cave-rock") ||
			parser_parse(p, "tile:cave:water-body:feature:cave-water-body") ||
			parser_parse(p, "tile:cave-actor:core.spelunk.actor.chasm-skitter:actor:chasm-skitter")) return 1;
	return map_visual_parser.finish(p) == PARSE_ERROR_NONE ? 0 : 1;
}

int teardown_tests(void *data)
{
	(void)data;
	spelunking_actor_parser.cleanup();
	map_visual_parser.cleanup();
	return 0;
}

static int test_zoom_changes_only_map_source_density(void *unused)
{
	struct fixture f;
	struct sdl3_spelunking_presenter presenter = { 0 };
	struct sdl3_map_view view;
	const struct sdl3_cell *cell;
	const struct sdl3_terrain_variant *variant;

	(void)unused;
	require(fixture_open(&f));
	world_spelunk_visibility_reveal_area(f.runtime, 10, 10);
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		7, 4, 20, 10, 100, &view));
	require(view.active);
	require(view.expanded_source);
	eq(view.col, 7);
	eq(view.row, 4);
	eq(view.cols, 20);
	eq(view.rows, 10);
	eq(view.source_cols, 22);
	eq(view.source_rows, 12);
	cell = sdl3_grid_cell(view.source, view.focus_col, view.focus_row);
	notnull(cell);
	eq(cell->codepoint, L'@');
	eq(cell->foreground, COLOUR_L_BLUE);
	cell = sdl3_grid_cell(view.source, view.focus_col + 2, view.focus_row);
	notnull(cell);
	eq(cell->codepoint, L'k');
	eq(cell->foreground, COLOUR_L_GREEN);
	notnull(view.terrain);
	require(!world_spelunk_visibility_is_visible(f.runtime,
		TEST_WIDTH / 2 + 8, TEST_HEIGHT - 1));
	require(world_spelunk_visibility_is_surface_visible(f.runtime,
		TEST_WIDTH / 2 + 8, TEST_HEIGHT - 1));
	variant = sdl3_terrain_overlay_get(view.terrain, view.focus_col + 8,
		view.focus_row + 1);
	notnull(variant);
	require(variant->in_view);
	variant = sdl3_terrain_overlay_get(view.terrain, view.focus_col + 2,
		view.focus_row);
	notnull(variant);
	require(variant->in_view);
	variant = sdl3_terrain_overlay_get(view.terrain, view.focus_col,
		view.focus_row - 8);
	notnull(variant);
	require(!variant->in_view);
	eq(variant->foreground, COLOUR_SLATE);
	eq(variant->remembered_brightness, 61);

	/* Zooming out exposes more semantic cave cells without changing the
	 * composite-terminal rectangle occupied by the map. */
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		7, 4, 20, 10, 50, &view));
	eq(view.col, 7);
	eq(view.row, 4);
	eq(view.cols, 20);
	eq(view.rows, 10);
	eq(view.source_cols, 42);
	eq(view.source_rows, 22);
	eq(view.zoom_percent, 50);

	/* Giant zoom remains bounded to the small source window the camera needs. */
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		7, 4, 20, 10, 800, &view));
	eq(view.source_cols, 3);
	eq(view.source_rows, 2);
	eq(view.zoom_percent, 800);
	cell = sdl3_grid_cell(view.source, view.focus_col, view.focus_row);
	notnull(cell);
	eq(cell->codepoint, L'@');

	sdl3_spelunking_presenter_free(&presenter);
	fixture_close(&f);
	ok;
}

static int test_peek_drives_camera_focus(void *unused)
{
	struct fixture f;
	struct sdl3_spelunking_presenter presenter = { 0 };
	struct sdl3_map_view view;
	const struct sdl3_cell *cell;

	(void)unused;
	require(fixture_open(&f));
	require(world_spelunk_visibility_begin_peek(f.runtime));
	require(world_spelunk_visibility_move_peek(f.runtime, 1, -1));
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		0, 0, 10, 8, 200, &view));
	eq(view.source_cols, 5);
	eq(view.source_rows, 4);
	cell = sdl3_grid_cell(view.source, view.focus_col, view.focus_row);
	notnull(cell);
	eq(cell->codepoint, L'X');
	eq(cell->foreground, COLOUR_L_BLUE);

	sdl3_spelunking_presenter_free(&presenter);
	fixture_close(&f);
	ok;
}

static int test_look_focus_drives_cursor_without_moving_player(void *unused)
{
	struct fixture f;
	struct sdl3_spelunking_presenter presenter = { 0 };
	struct sdl3_map_view view;
	const struct sdl3_cell *cell;
	int player_x;
	int player_y;

	(void)unused;
	require(fixture_open(&f));
	player_x = f.runtime->state.x;
	player_y = f.runtime->state.y;
	require(sdl3_spelunking_presenter_configure_focus(&presenter, NULL,
		f.runtime,
		0, 0, 20, 10, 200, player_x + 2, player_y, true, &view));
	eq(view.cursor_col, view.focus_col);
	eq(view.cursor_row, view.focus_row);
	cell = sdl3_grid_cell(view.source, view.cursor_col, view.cursor_row);
	notnull(cell);
	eq(cell->codepoint, L'k');
	eq(cell->foreground, COLOUR_L_GREEN);
	eq(f.runtime->state.x, player_x);
	eq(f.runtime->state.y, player_y);

	sdl3_spelunking_presenter_free(&presenter);
	fixture_close(&f);
	ok;
}

static int test_tile_subject_priority_and_unknown_objects(void *unused)
{
	struct fixture f;
	struct sdl3_spelunking_presenter presenter = { 0 };
	struct sdl3_map_view view;
	struct object_kind kind = { 0 };
	struct object_base base = { 0 };
	struct flavor flavor = { 0 };
	struct object *object;
	int x, y, i;
	bool found = false;
	(void)unused;
	require(fixture_open(&f));
	x = f.runtime->state.x; y = f.runtime->state.y;
	kind.tval = TV_POTION; kind.d_char = L'!'; kind.d_attr = COLOUR_RED;
	kind.art_id = "secret-potion"; kind.base = &base; kind.flavor = &flavor;
	flavor.d_char = L'!'; flavor.d_attr = COLOUR_SLATE;
	base.unaware_art_id = "unidentified-potion";
	object = object_new(); object->kind = &kind; object->number = 1;
	object->known = object_new(); object->known->kind = &kind;
	object->known->number = 1;
	object->tval = TV_POTION;
	require(world_spelunk_runtime_add_ground_object(f.runtime, object, x, y));
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		0, 0, 20, 10, 100, &view));
	for (i = 0; i < view.art_subject_count; i++) {
		require(view.art_subjects[i].col != view.focus_col ||
			view.art_subjects[i].row != view.focus_row);
	}
	eq(sdl3_terrain_overlay_get(view.terrain, view.focus_col,
		view.focus_row)->tile_glyph_policy, SDL3_TILE_GLYPH_PRESERVE);
	/* Move only the fixture object: the player remains in place. */
	object->grid = loc(x + 1, y);
	object->known->grid = object->grid;
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		0, 0, 20, 10, 100, &view));
	for (i = 0; i < view.art_subject_count; i++) {
		if (view.art_subjects[i].col == view.focus_col + 1 &&
				view.art_subjects[i].row == view.focus_row) {
			require(streq(view.art_subjects[i].tile_key, "object:unidentified-potion"));
			require(streq(view.art_subjects[i].tile_fallback_key, "object:potion"));
			found = true;
		}
	}
	require(found);
	require(view.art_subject_count >= 2); /* Object plus independent cave actor. */
	require(streq(view.art_subjects[view.art_subject_count - 1].tile_key,
		"actor:chasm-skitter"));
	kind.aware = true;
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		0, 0, 20, 10, 100, &view));
	require(streq(view.art_subjects[0].tile_key, "object:secret-potion"));
	/* Detection preserves the existing glyph but grants no bitmap identity. */
	memset(f.runtime->visible, 0, TEST_WIDTH * TEST_HEIGHT);
	f.runtime->detected[y * TEST_WIDTH + x + 1] = 1;
	f.runtime->detected[y * TEST_WIDTH + x + 2] = 1;
	require(sdl3_spelunking_presenter_configure(&presenter, NULL, f.runtime,
		0, 0, 20, 10, 100, &view));
	eq(view.art_subject_count, 0);
	sdl3_spelunking_presenter_free(&presenter);
	fixture_close(&f);
	ok;
}

static int test_tile_adjacency_uses_only_explored_geometry(void *unused)
{
	struct fixture f;
	struct world_spelunk_view view = { 0 };
	struct world_spelunk_view_terrain terrain;
	uint8_t cell = WORLD_SPELUNK_VIEW_WATER;
	int x = 10, y = 10, index = y * TEST_WIDTH + x;
	(void)unused;
	require(fixture_open(&f));
	view.cells = &cell; view.cell_count = 1; view.cols = view.rows = 1;
	view.source_x = x; view.source_y = y;
	memset(f.runtime->explored, 0, TEST_WIDTH * TEST_HEIGHT);
	f.runtime->cells[index] = WORLD_SPELUNK_WATER;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(!terrain.base && !terrain.overlay);
	f.runtime->explored[index] = 1;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.base, "water-body"));
	f.runtime->cells[index - TEST_WIDTH] = WORLD_SPELUNK_ROCK;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.base, "water-body"));
	f.runtime->cells[index - TEST_WIDTH] = WORLD_SPELUNK_AIR;
	f.runtime->explored[index - TEST_WIDTH] = 1;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.base, "water-surface"));
	cell = WORLD_SPELUNK_VIEW_ROPE;
	f.runtime->cells[index] = WORLD_SPELUNK_AIR;
	f.runtime->ropes[index] = 1;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.overlay, "rope-middle")); /* Unknown continuation. */
	f.runtime->explored[index + TEST_WIDTH] = 1;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.overlay, "rope-end"));
	f.runtime->ropes[index + TEST_WIDTH] = 1;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.overlay, "rope-middle"));
	cell = WORLD_SPELUNK_VIEW_PITON;
	f.runtime->cells[index - 1] = WORLD_SPELUNK_ROCK;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(!terrain.overlay && terrain.preserve_glyph);
	f.runtime->explored[index - 1] = 1;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.overlay, "piton-left"));
	cell = WORLD_SPELUNK_VIEW_EXIT;
	world_spelunk_view_terrain_at(f.runtime, &view, 0, 0, &terrain);
	require(streq(terrain.overlay, "exit") && terrain.preserve_glyph);
	fixture_close(&f);
	ok;
}

const char *suite_name = "sdl3/spelunking-presenter";
struct test tests[] = {
	{ "tile occupant priority and unidentified objects", test_tile_subject_priority_and_unknown_objects },
	{ "tile adjacency reveals no unexplored geometry", test_tile_adjacency_uses_only_explored_geometry },
	{ "zoom changes only map source density",
		test_zoom_changes_only_map_source_density },
	{ "peek drives camera focus", test_peek_drives_camera_focus },
	{ "Look focus drives cursor without moving player",
		test_look_focus_drives_cursor_without_moving_player },
	{ NULL, NULL },
};
