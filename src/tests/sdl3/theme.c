/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/theme */
/* Keep interface mood palettes separate from canonical world identity. */

#include "unit-test.h"

#include "sdl3/theme.h"

NOSETUP
NOTEARDOWN

static bool same_color(SDL_Color a, SDL_Color b)
{
	return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static int test_mood_themes_do_not_remap_world(void *unused)
{
	const struct sdl3_theme *midnight = sdl3_theme_by_id("midnight");
	const struct sdl3_theme *ember = sdl3_theme_by_id("ember");
	SDL_Color midnight_world = sdl3_world_color(midnight, COLOUR_RED);
	SDL_Color ember_world = sdl3_world_color(ember, COLOUR_RED);
	SDL_Color canonical = {
		angband_color_table[COLOUR_RED][1],
		angband_color_table[COLOUR_RED][2],
		angband_color_table[COLOUR_RED][3],
		SDL_ALPHA_OPAQUE
	};

	(void)unused;
	require(same_color(midnight_world, ember_world));
	require(same_color(midnight_world, canonical));
	require(!same_color(sdl3_theme_color(midnight, COLOUR_RED),
		sdl3_theme_color(ember, COLOUR_RED)));
	ok;
}

static int test_high_contrast_is_explicit_world_exception(void *unused)
{
	const struct sdl3_theme *contrast = sdl3_theme_by_id("high-contrast");
	const struct sdl3_theme *midnight = sdl3_theme_by_id("midnight");
	SDL_Color contrast_world = sdl3_world_color(contrast, COLOUR_BLUE);

	(void)unused;
	require(same_color(contrast_world,
		sdl3_theme_color(contrast, COLOUR_BLUE)));
	require(!same_color(contrast_world,
		sdl3_world_color(midnight, COLOUR_BLUE)));
	ok;
}

static int test_remembered_world_is_distinctly_dimmer(void *unused)
{
	const struct sdl3_theme *midnight = sdl3_theme_by_id("midnight");
	const struct sdl3_theme *contrast = sdl3_theme_by_id("high-contrast");
	SDL_Color visible = { 180, 120, 60, 255 };
	SDL_Color remembered =
		sdl3_theme_remembered_world_color(midnight, visible);
	SDL_Color remembered_contrast =
		sdl3_theme_remembered_world_color(contrast, visible);
	SDL_Color cave_memory = sdl3_dim_world_color(visible, 61);

	(void)unused;
	require(midnight->remembered_world_brightness < 128);
	require(remembered.r < visible.r / 2);
	require(remembered.g < visible.g / 2);
	require(remembered.b < visible.b);
	require(remembered_contrast.r > remembered.r);
	require(cave_memory.r < remembered.r);
	require(cave_memory.g < remembered.g);
	ok;
}

const char *suite_name = "sdl3/theme";
struct test tests[] = {
	{ "mood themes preserve world", test_mood_themes_do_not_remap_world },
	{ "high contrast exception", test_high_contrast_is_explicit_world_exception },
	{ "remembered world contrast", test_remembered_world_is_distinctly_dimmer },
	{ NULL, NULL }
};
