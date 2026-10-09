/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/monster-art.c */
/* Exercise explicit IDs, FROGART3 decoding, and plate-to-glyph policy. */

#include "unit-test.h"

#include "sdl3/monster-art.h"

int setup_tests(void **data)
{
	(void)data;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	return 0;
}

static int test_explicit_asset_ids(void *state)
{
	char key[SDL3_MONSTER_ART_ID_CAPACITY];
	(void)state;

	require(sdl3_monster_art_id_valid("sample-plate"));
	require(sdl3_monster_art_id_valid("base-42"));
	require(!sdl3_monster_art_id_valid("Sample Plate"));
	require(!sdl3_monster_art_id_valid("../sample-plate"));
	require(!sdl3_monster_art_id_valid("double--hyphen"));
	require(!sdl3_monster_art_id_valid("-leading"));
	require(!sdl3_monster_art_id_valid("trailing-"));
	require(!sdl3_monster_art_id_valid(NULL));
	sdl3_monster_art_make_key(key, sizeof(key), "sample-plate", "person");
	require(streq(key, "sample-plate|person"));
	sdl3_monster_art_make_key(key, sizeof(key), "sample-plate", NULL);
	require(streq(key, "sample-plate"));
	sdl3_monster_art_make_key(key, sizeof(key), "Sample Plate", "person");
	require(!key[0]);
	ok;
}

static int test_plate_decode(void *state)
{
	const uint8_t bytes[] = {
		'F', 'R', 'O', 'G', 'A', 'R', 'T', '3',
		1, 0, 1, 0, 0, 4, 0, 3,
		200, 160, 80, 40
	};
	struct sdl3_monster_art_plate plate;
	(void)state;

	require(sdl3_monster_art_plate_decode(bytes, sizeof(bytes), &plate));
	eq(plate.cols, 1);
	eq(plate.rows, 1);
	eq(plate.source_width, 1024);
	eq(plate.source_height, 768);
	eq(plate.coverage[0], 200);
	eq(plate.colors[0].r, 160);
	eq(plate.colors[0].g, 80);
	eq(plate.colors[0].b, 40);
	ok;
}

static int test_plate_rejects_malformed_data(void *state)
{
	uint8_t bytes[] = {
		'F', 'R', 'O', 'G', 'A', 'R', 'T', '3',
		1, 0, 1, 0, 1, 0, 1, 0,
		255, 255, 255, 255
	};
	struct sdl3_monster_art_plate plate;
	(void)state;

	require(!sdl3_monster_art_plate_decode(bytes, sizeof(bytes) - 1,
		&plate));
	bytes[0] = 'X';
	require(!sdl3_monster_art_plate_decode(bytes, sizeof(bytes), &plate));
	bytes[0] = 'F';
	bytes[8] = SDL3_MONSTER_ART_MAX_COLS + 1;
	require(!sdl3_monster_art_plate_decode(bytes, sizeof(bytes), &plate));
	ok;
}

static int test_plate_render_page_color(void *state)
{
	struct sdl3_monster_art_plate plate;
	struct sdl3_monster_art art;
	(void)state;

	SDL_memset(&plate, 0, sizeof(plate));
	plate.cols = 1;
	plate.rows = 1;
	plate.source_width = 100;
	plate.source_height = 100;
	plate.coverage[0] = 255;
	plate.colors[0] = (SDL_Color) { 200, 100, 50, 255 };
	require(sdl3_monster_art_plate_render(&plate, " .#@", 4, 1, 1,
		false, &art));
	eq(art.cols, 1);
	eq(art.rows, 1);
	require(art.cells[0].visible);
	require(art.cells[0].codepoint != ' ');
	eq(art.cells[0].color.r, 200);
	eq(art.cells[0].color.g, 100);
	eq(art.cells[0].color.b, 50);
	ok;
}

static int test_map_silhouette_policy(void *state)
{
	struct sdl3_monster_art_plate plate;
	struct sdl3_monster_art page;
	struct sdl3_monster_art map;
	(void)state;

	SDL_memset(&plate, 0, sizeof(plate));
	plate.cols = 2;
	plate.rows = 1;
	plate.source_width = 200;
	plate.source_height = 100;
	plate.coverage[0] = 255;
	plate.coverage[1] = 40;
	plate.colors[0] = (SDL_Color) { 40, 40, 40, 255 };
	plate.colors[1] = (SDL_Color) { 220, 220, 220, 255 };
	require(sdl3_monster_art_plate_render(&plate, " .:+#@", 6, 2, 1,
		false, &page));
	require(sdl3_monster_art_plate_render(&plate, " .:+#@", 6, 2, 1,
		true, &map));
	require(page.cells[1].visible);
	require(!map.cells[1].visible);
	require(map.cells[0].visible);
	require(map.cells[0].codepoint == '+');
	ok;
}

static int test_render_validation(void *state)
{
	struct sdl3_monster_art_plate plate;
	struct sdl3_monster_art art;
	(void)state;

	SDL_memset(&plate, 0, sizeof(plate));
	plate.cols = 1;
	plate.rows = 1;
	plate.source_width = 1;
	plate.source_height = 1;
	require(!sdl3_monster_art_plate_render(&plate, " .", 2, 1, 1,
		false, &art));
	plate.coverage[0] = 255;
	plate.colors[0] = (SDL_Color) { 255, 255, 255, 255 };
	require(!sdl3_monster_art_plate_render(&plate, " ", 1, 1, 1,
		false, &art));
	require(!sdl3_monster_art_plate_render(&plate, " .", 2,
		SDL3_MONSTER_ART_MAX_COLS + 1, 1, false, &art));
	ok;
}

static int test_map_policy_and_pool_lifecycle(void *state)
{
	struct sdl3_monster_art_pool pool;
	int i;
	(void)state;

	require(!sdl3_monster_art_map_enabled(
		SDL3_MONSTER_ART_MAP_MIN_ZOOM - 1));
	require(sdl3_monster_art_map_enabled(
		SDL3_MONSTER_ART_MAP_MIN_ZOOM));
	require(sdl3_monster_art_map_enabled(800));
	SDL_memset(&pool, 0xff, sizeof(pool));
	sdl3_monster_art_pool_init(&pool);
	eq(pool.use_clock, 0);
	for (i = 0; i < SDL3_MONSTER_ART_POOL_CAPACITY; i++) {
		null(pool.entries[i].cache);
		eq(pool.entries[i].last_used, 0);
	}
	sdl3_monster_art_pool_free(&pool);
	eq(pool.use_clock, 0);
	ok;
}

const char *suite_name = "sdl3/monster-art";
struct test tests[] = {
	{ "explicit asset ids", test_explicit_asset_ids },
	{ "plate decode", test_plate_decode },
	{ "plate rejects malformed data", test_plate_rejects_malformed_data },
	{ "plate render page color", test_plate_render_page_color },
	{ "map silhouette policy", test_map_silhouette_policy },
	{ "render validation", test_render_validation },
	{ "map policy and pool lifecycle", test_map_policy_and_pool_lifecycle },
	{ NULL, NULL },
};
