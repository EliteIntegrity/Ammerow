/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Exercise deterministic, data-resolved terrain presentation metadata. */

#include "unit-test.h"

#include "sdl3/terrain.h"

NOSETUP
NOTEARDOWN

static const struct terrain_visual_recipe floor_recipe = {
	.profile = "default",
	.material = "floor",
	.base = { 94, 101, 106 }
};

static const struct terrain_visual_recipe wall_recipe = {
	.profile = "default",
	.material = "wall",
	.base = { 154, 163, 170 }
};

static int test_level_key_is_stable_and_sensitive(void *unused)
{
	uint32_t key = sdl3_terrain_level_key(1234, 17, 198, 44);

	(void)unused;
	eq(key, sdl3_terrain_level_key(1234, 17, 198, 44));
	require(key != sdl3_terrain_level_key(1235, 17, 198, 44));
	require(key != sdl3_terrain_level_key(1234, 18, 198, 44));
	require(key != sdl3_terrain_level_key(1234, 17, 99, 44));
	ok;
}

static int test_natural_resolves_material(void *unused)
{
	struct sdl3_terrain_variant variant;

	(void)unused;
	require(sdl3_terrain_variant(&floor_recipe, LIGHTING_LOS, L'.',
		COLOUR_WHITE, SDL3_TERRAIN_NATURAL, &variant));
	require(variant.active);
	ptreq(variant.recipe, &floor_recipe);
	eq(variant.codepoint, L'.');
	eq(variant.foreground, COLOUR_WHITE);
	eq(variant.style, SDL3_TERRAIN_NATURAL);
	ok;
}

static int test_classic_and_missing_recipes_fail_closed(void *unused)
{
	struct sdl3_terrain_variant variant;

	(void)unused;
	require(!sdl3_terrain_variant(&wall_recipe, LIGHTING_LOS, L'#',
		COLOUR_WHITE, SDL3_TERRAIN_CLASSIC, &variant));
	require(!variant.active);
	require(!sdl3_terrain_variant(NULL, LIGHTING_LOS, L'#', COLOUR_WHITE,
		SDL3_TERRAIN_NATURAL, &variant));
	require(!variant.active);
	ok;
}

static int test_remembered_terrain_keeps_material(void *unused)
{
	struct sdl3_terrain_variant variant;

	(void)unused;
	require(sdl3_terrain_variant(&wall_recipe, LIGHTING_LIT, L'#',
		COLOUR_WHITE, SDL3_TERRAIN_NATURAL, &variant));
	require(variant.active);
	ptreq(variant.recipe, &wall_recipe);
	eq(variant.lighting, LIGHTING_LIT);
	ok;
}

static int test_style_names_and_cycle(void *unused)
{
	enum sdl3_terrain_style parsed;

	(void)unused;
	require(streq(sdl3_terrain_style_name(SDL3_TERRAIN_CLASSIC), "Classic"));
	require(streq(sdl3_terrain_style_name(SDL3_TERRAIN_NATURAL), "Natural"));
	require(sdl3_terrain_style_from_name("classic", &parsed));
	eq(parsed, SDL3_TERRAIN_CLASSIC);
	require(sdl3_terrain_style_from_name("NATURAL", &parsed));
	eq(parsed, SDL3_TERRAIN_NATURAL);
	require(!sdl3_terrain_style_from_name("textured", &parsed));
	eq(sdl3_terrain_style_change(SDL3_TERRAIN_CLASSIC, 1),
		SDL3_TERRAIN_NATURAL);
	eq(sdl3_terrain_style_change(SDL3_TERRAIN_NATURAL, 1),
		SDL3_TERRAIN_CLASSIC);
	eq(sdl3_terrain_style_change(SDL3_TERRAIN_CLASSIC, -1),
		SDL3_TERRAIN_NATURAL);
	ok;
}

static int test_overlay_stores_recipe_metadata(void *unused)
{
	struct sdl3_terrain_overlay overlay = { 0 };
	struct sdl3_terrain_variant variant = {
		.codepoint = L'.',
		.foreground = COLOUR_SLATE,
		.lighting = LIGHTING_LOS,
		.style = SDL3_TERRAIN_NATURAL,
		.recipe = &floor_recipe,
		.in_view = true,
		.present = true,
		.active = true
	};
	const struct sdl3_terrain_variant *stored;

	(void)unused;
	require(sdl3_terrain_overlay_resize(&overlay, 80, 24));
	require(sdl3_terrain_overlay_set(&overlay, 17, 9, &variant));
	stored = sdl3_terrain_overlay_get(&overlay, 17, 9);
	notnull(stored);
	ptreq(stored->recipe, &floor_recipe);
	eq(stored->codepoint, L'.');
	require(!sdl3_terrain_overlay_set(&overlay, 17, 9, &variant));
	variant.foreground = COLOUR_WHITE;
	require(sdl3_terrain_overlay_set(&overlay, 17, 9, &variant));
	variant.tile_overlay_key = "feature:cave-rope-middle";
	variant.tile_glyph_policy = SDL3_TILE_GLYPH_PRESERVE;
	require(sdl3_terrain_overlay_set(&overlay, 17, 9, &variant));
	require(sdl3_terrain_variant(&floor_recipe, LIGHTING_LOS, L'.',
		COLOUR_SLATE, SDL3_TERRAIN_NATURAL, &variant));
	require(!variant.tile_overlay_key);
	eq(variant.tile_glyph_policy, SDL3_TILE_GLYPH_AUTO);
	require(sdl3_terrain_overlay_set(&overlay, 17, 9, NULL));
	null(sdl3_terrain_overlay_get(&overlay, 17, 9));
	sdl3_terrain_overlay_free(&overlay);
	ok;
}

const char *suite_name = "sdl3/terrain";
struct test tests[] = {
	{ "level key stability", test_level_key_is_stable_and_sensitive },
	{ "natural style", test_natural_resolves_material },
	{ "classic and missing recipe", test_classic_and_missing_recipes_fail_closed },
	{ "remembered material", test_remembered_terrain_keeps_material },
	{ "style names and cycle", test_style_names_and_cycle },
	{ "overlay metadata", test_overlay_stores_recipe_metadata },
	{ NULL, NULL }
};
