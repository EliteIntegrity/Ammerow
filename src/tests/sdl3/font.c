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
static int test_square_ink_cache(void *state)
{
	SDL_Surface *surface;
	SDL_Renderer *renderer;
	struct sdl3_font font;
	struct sdl3_font_resource_counts before, after;
	SDL_Color white = {255, 255, 255, 255};
	(void)state;
	require(TTF_Init());
	surface = SDL_CreateSurface(320, 240, SDL_PIXELFORMAT_RGBA32);
	require(surface);
	renderer = SDL_CreateSoftwareRenderer(surface);
	require(renderer);
	sdl3_font_get_resource_counts(&before);
	for (int f = 0; f < sdl3_font_bundled_count(); f++) {
		require(sdl3_font_init_lazy(&font, renderer, sdl3_font_bundled_name(f),
			1280, 720, 100, 40, 100));
		require(sdl3_font_draw_square(&font, renderer, '@', white, 0, 0, 22, 22));
		require(sdl3_font_draw_square(&font, renderer, '.', white, 22, 0, 22, 22));
		require(font.glyphs['.' - 32].square_ink.h < font.glyphs['@' - 32].square_ink.h);
		for (int c = 33; c <= 126; c++) {
			require(sdl3_font_draw_square(&font, renderer, c, white, 0, 0, 22, 22));
			const SDL_Rect *ink = &font.glyphs[c - 32].square_ink;
			require(ink->w <= font.ink_width && ink->h <= font.ink_height);
		}
		sdl3_font_get_resource_counts(&after);
		eq(after.square_textures, before.square_textures + 94);
		require(sdl3_font_draw_square(&font, renderer, '@', white, 0, 0, 22, 22));
		sdl3_font_get_resource_counts(&after);
		eq(after.square_textures, before.square_textures + 94);
		require(sdl3_font_fit(&font, 1920, 1080, 100, 40, 100));
		sdl3_font_get_resource_counts(&after);
		eq(after.square_textures, before.square_textures);
		require(sdl3_font_draw_square(&font, renderer, '@', white, 0, 0, 30, 30));
		sdl3_font_free(&font);
	}
	sdl3_font_get_resource_counts(&after);
	eq(after.square_textures, before.square_textures);
	eq(after.fonts, before.fonts);
	eq(after.engines, before.engines);
	eq(after.texts, before.texts);
	SDL_DestroyRenderer(renderer);
	SDL_DestroySurface(surface);
	TTF_Quit();
	ok;
}

static SDL_Rect ink_bounds(SDL_Surface *surface)
{
	int left = surface->w, top = surface->h, right = -1, bottom = -1;
	for (int y = 0; y < surface->h; y++) {
		for (int x = 0; x < surface->w; x++) {
			Uint8 r, g, b, a;
			if (SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a) && r) {
				left = SDL_min(left, x); right = SDL_max(right, x);
				top = SDL_min(top, y); bottom = SDL_max(bottom, y);
			}
		}
	}
	return (SDL_Rect){ left, top, right - left + 1, bottom - top + 1 };
}

static int test_square_wall_is_bounded_and_keeps_height(void *state)
{
	SDL_Surface *surface;
	SDL_Renderer *renderer;
	struct sdl3_font font;
	SDL_Color white = { 255, 255, 255, 255 };
	(void)state;
	require(TTF_Init());
	surface = SDL_CreateSurface(128, 128, SDL_PIXELFORMAT_RGBA32);
	require(surface);
	renderer = SDL_CreateSoftwareRenderer(surface);
	require(renderer);
	for (int f = 0; f < sdl3_font_bundled_count(); f++) {
		require(sdl3_font_init_lazy(&font, renderer, sdl3_font_bundled_name(f),
			1920, 1080, 100, 40, 200));
		for (int side = 16; side <= 64; side *= 2) {
			SDL_Rect normal, wall, restored;
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
			SDL_RenderClear(renderer);
			require(sdl3_font_draw_square(&font, renderer, '#', white,
				16, 16, (float)side, (float)side));
			SDL_RenderPresent(renderer);
			normal = ink_bounds(surface);
			SDL_RenderClear(renderer);
			require(sdl3_font_draw_square_wall(&font, renderer, '#', white,
				16, 16, (float)side, (float)side));
			SDL_RenderPresent(renderer);
			wall = ink_bounds(surface);
			require(wall.w >= normal.w);
			eq(wall.y, normal.y); eq(wall.h, normal.h);
			require(wall.x >= 17 && wall.x + wall.w <= 16 + side - 1);
			/* Styling never changes the shared texture or subsequent draws. */
			SDL_RenderClear(renderer);
			require(sdl3_font_draw_square(&font, renderer, '#', white,
				16, 16, (float)side, (float)side));
			SDL_RenderPresent(renderer);
			restored = ink_bounds(surface);
			eq(restored.x, normal.x); eq(restored.w, normal.w);
			eq(restored.y, normal.y); eq(restored.h, normal.h);
		}
		sdl3_font_free(&font);
	}
	SDL_DestroyRenderer(renderer);
	SDL_DestroySurface(surface);
	TTF_Quit();
	ok;
}

struct test tests[] = {
	{ "square wall width, unchanged height and bounded ink", test_square_wall_is_bounded_and_keeps_height },
	{ "square glyph bounds, punctuation scale and cache lifecycle", test_square_ink_cache },
	{ "renderer uses nearest atlas sampling",
		test_renderer_uses_nearest_atlas_sampling },
	{ "centered origins are physical pixels",
		test_centered_origins_are_physical_pixels },
	{ NULL, NULL },
};
