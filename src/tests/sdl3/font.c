/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/font.c */
/* Exercise the resolution-independent glyph sampling and placement policy. */

#include "unit-test.h"

#include "sdl3/font.h"

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

static int test_renderer_uses_nearest_atlas_sampling(void *state)
{
	SDL_Surface *surface;
	SDL_Renderer *renderer;
	SDL_ScaleMode mode = SDL_SCALEMODE_INVALID;
	(void)state;

	surface = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA8888);
	require(surface);
	renderer = SDL_CreateSoftwareRenderer(surface);
	require(renderer);
#if SDL_VERSION_ATLEAST(3, 4, 0)
	require(SDL_SetDefaultTextureScaleMode(renderer, SDL_SCALEMODE_LINEAR));
#endif
	require(sdl3_font_prepare_renderer(renderer));
#if SDL_VERSION_ATLEAST(3, 4, 0)
	require(SDL_GetDefaultTextureScaleMode(renderer, &mode));
	eq(mode, SDL_SCALEMODE_NEAREST);
#else
	(void)mode;
#endif
	SDL_DestroyRenderer(renderer);
	SDL_DestroySurface(surface);
	ok;
}

static int test_centered_origins_are_physical_pixels(void *state)
{
	float x;
	float y;
	(void)state;

	sdl3_font_glyph_origin(10.0f, 20.0f, 12.0f, 18.0f, 8, 10, &x, &y);
	eq((int)x, 12);
	eq((int)y, 24);
	require(x == SDL_floorf(x));
	require(y == SDL_floorf(y));

	/* Odd padding previously placed the atlas at x.5/y.5. */
	sdl3_font_glyph_origin(10.0f, 20.0f, 11.0f, 17.0f, 8, 10, &x, &y);
	eq((int)x, 12);
	eq((int)y, 24);
	require(x == SDL_floorf(x));
	require(y == SDL_floorf(y));

	/* Camera-scaled cells can themselves begin between physical pixels. */
	sdl3_font_glyph_origin(10.25f, 20.75f, 11.0f, 17.0f, 8, 10, &x, &y);
	eq((int)x, 12);
	eq((int)y, 24);
	require(x == SDL_floorf(x));
	require(y == SDL_floorf(y));
	ok;
}

const char *suite_name = "sdl3/font";
struct test tests[] = {
	{ "renderer uses nearest atlas sampling",
		test_renderer_uses_nearest_atlas_sampling },
	{ "centered origins are physical pixels",
		test_centered_origins_are_physical_pixels },
	{ NULL, NULL },
};
