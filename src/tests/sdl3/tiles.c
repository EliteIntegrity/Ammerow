/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/tiles.c */
/* Exercise stable, namespaced tile identity construction. */

#include "unit-test.h"

#include "sdl3/tiles.h"
#include "sdl3/map-view.h"
#include "init.h"
#include "test-utils.h"

NOSETUP
NOTEARDOWN

static int test_stable_keys_and_normalization(void *unused)
{
	char key[SDL3_TILE_KEY_CAPACITY];
	(void)unused;

	require(sdl3_tile_make_key(key, sizeof(key), SDL3_TILE_MONSTER,
		"giant-white-mouse"));
	require(streq(key, "monster:giant-white-mouse"));
	require(sdl3_tile_make_key(key, sizeof(key), SDL3_TILE_OBJECT,
		"soft armor"));
	require(streq(key, "object:soft-armor"));
	require(sdl3_tile_make_key(key, sizeof(key), SDL3_TILE_FEATURE,
		"Patch_of Grass"));
	require(streq(key, "feature:patch-of-grass"));
	require(sdl3_tile_make_key(key, sizeof(key), SDL3_TILE_ACTOR, "chasm-skitter"));
	require(streq(key, "actor:chasm-skitter"));
	require(!sdl3_tile_make_key(key, sizeof(key), SDL3_TILE_FEATURE, "../"));
	require(!sdl3_tile_make_key(key, 8, SDL3_TILE_MONSTER,
		"giant-white-mouse"));
	ok;
}

static int test_hybrid_loading_and_cleanup(void *unused)
{
	struct sdl3_tile_registry *registry = mem_zalloc(sizeof(*registry));
	SDL_Surface *surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA32);
	SDL_Renderer *renderer;
	char path[1024];
	int count;
	(void)unused;
	require(surface);
	renderer = SDL_CreateSoftwareRenderer(surface);
	require(renderer);
	set_file_paths();
	require(!sdl3_tiles_init(registry, renderer, "../tiles.txt"));
	require(!sdl3_tiles_init(registry, renderer, "missing-hybrid.txt"));
	require(!registry->loaded);
	eq(registry->atlas_count, 0);
	/* Public source builds intentionally have no separately licensed sheets. */
	path_build(path, sizeof(path), ANGBAND_DIR_TILES, "pixel-32-001.png");
	if (file_exists(path)) {
		require(sdl3_tiles_init(registry, renderer, "hybrid-32.txt"));
		require(registry->loaded);
		eq(registry->atlases[0].cell_width, 32);
		count = registry->entry_count;
		require(count > 0);
		sdl3_tiles_free(registry);
		eq(registry->entry_count, 0);
		require(!registry->loaded);
		require(sdl3_tiles_init(registry, renderer, "hybrid-64.txt"));
		eq(registry->atlases[0].cell_width, 64);
		eq(registry->entry_count, count);
	}
	sdl3_tiles_free(registry);
	mem_free(registry);
	SDL_DestroyRenderer(renderer);
	SDL_DestroySurface(surface);
	ok;
}

/* Synthetic art keeps these drawing regressions runnable in public source
 * builds, which deliberately omit the separately licensed portrait sheets. */
static int check_moving_subject(int detail)
{
	struct sdl3_tile_registry *registry = mem_zalloc(sizeof(*registry));
	struct sdl3_map_art_subject subject = { 0 };
	struct sdl3_cell cell = { L'o', COLOUR_RED, COLOUR_DARK, false };
	SDL_Surface *surface = SDL_CreateSurface(160, 128, SDL_PIXELFORMAT_RGBA32);
	SDL_Surface *art = SDL_CreateSurface(detail, detail, SDL_PIXELFORMAT_RGBA32);
	SDL_Renderer *renderer;
	SDL_Rect ink = { detail / 4, detail / 4, detail / 2, detail / 2 };
	SDL_FRect rect = { 24, 32, 64, 64 };
	Uint8 r, g, b, a;

	require(surface && art);
	renderer = SDL_CreateSoftwareRenderer(surface);
	require(renderer);
	require(SDL_FillSurfaceRect(art, NULL, SDL_MapSurfaceRGBA(art, 0, 0, 0, 0)));
	require(SDL_FillSurfaceRect(art, &ink, SDL_MapSurfaceRGBA(art, 0, 255, 0, 255)));
	registry->atlases[0].texture = SDL_CreateTextureFromSurface(renderer, art);
	require(registry->atlases[0].texture);
	SDL_SetTextureScaleMode(registry->atlases[0].texture, SDL_SCALEMODE_NEAREST);
	registry->atlases[0].width = registry->atlases[0].height = detail;
	registry->atlases[0].cell_width = registry->atlases[0].cell_height = detail;
	registry->atlas_count = registry->entry_count = 1;
	registry->loaded = true;
	my_strcpy(registry->entries[0].key, "monster:test", sizeof(registry->entries[0].key));
	subject.glyph = cell.codepoint;
	subject.attr = cell.foreground;
	my_strcpy(subject.tile_key, "monster:test", sizeof(subject.tile_key));

	SDL_SetRenderDrawColor(renderer, 8, 16, 24, 255);
	SDL_RenderClear(renderer);
	require(sdl3_tiles_draw_subject(registry, renderer, &subject, &cell, 3, 4, &rect, 255));
	SDL_RenderPresent(renderer);
	require(SDL_ReadSurfacePixel(surface, 56, 64, &r, &g, &b, &a));
	eq(r, 0); eq(g, 255); eq(b, 0);
	/* Transparent margin remains transparent. */
	require(SDL_ReadSurfacePixel(surface, 24, 32, &r, &g, &b, &a));
	eq(r, 8); eq(g, 16); eq(b, 24);

	/* A lunge changes the destination rectangle, not the selected art. */
	SDL_RenderClear(renderer);
	rect.x += 40;
	rect.y -= 12;
	require(sdl3_tiles_draw_subject(registry, renderer, &subject, &cell, 3, 4, &rect, 128));
	SDL_RenderPresent(renderer);
	require(SDL_ReadSurfacePixel(surface, 96, 52, &r, &g, &b, &a));
	eq(r, 0); eq(g, 128); eq(b, 0);
	require(SDL_ReadSurfacePixel(surface, 56, 64, &r, &g, &b, &a));
	eq(r, 8); eq(g, 16); eq(b, 24);

	my_strcpy(subject.tile_key, "monster:missing", sizeof(subject.tile_key));
	my_strcpy(subject.tile_fallback_key, "monster:test", sizeof(subject.tile_fallback_key));
	require(sdl3_tiles_draw_subject(registry, renderer, &subject, &cell, 3, 4, &rect, 255));
	/* A disappeared/replaced subject must not disclose its old portrait. */
	cell.codepoint = L'@';
	require(!sdl3_tiles_draw_subject(registry, renderer, &subject, &cell, 3, 4, &rect, 255));
	cell.codepoint = subject.glyph;
	cell.foreground = COLOUR_WHITE;
	require(!sdl3_tiles_draw_subject(registry, renderer, &subject, &cell, 3, 4, &rect, 255));
	cell.foreground = subject.attr;
	subject.tile_fallback_key[0] = '\0';
	require(!sdl3_tiles_draw_subject(registry, renderer, &subject, &cell, 3, 4, &rect, 255));
	require(!sdl3_tiles_draw_subject(registry, renderer, NULL, &cell, 3, 4, &rect, 255));
	sdl3_tiles_free(registry);
	my_strcpy(subject.tile_key, "monster:test", sizeof(subject.tile_key));
	require(!sdl3_tiles_draw_subject(registry, renderer, &subject, &cell, 3, 4, &rect, 255));
	mem_free(registry);
	SDL_DestroySurface(art);
	SDL_DestroyRenderer(renderer);
	SDL_DestroySurface(surface);
	ok;
}

static int test_moving_subject_32(void *unused)
{
	(void)unused;
	return check_moving_subject(32);
}

static int test_moving_subject_64(void *unused)
{
	(void)unused;
	return check_moving_subject(64);
}

const char *suite_name = "sdl3/tiles";
struct test tests[] = {
	{ "stable keys and normalization", test_stable_keys_and_normalization },
	{ "hybrid loading and cleanup", test_hybrid_loading_and_cleanup },
	{ "moving subject retains 32px sprite", test_moving_subject_32 },
	{ "moving subject retains 64px sprite", test_moving_subject_64 },
	{ NULL, NULL }
};
