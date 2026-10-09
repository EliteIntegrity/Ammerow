/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/font.h
 * \brief Font selection and cached glyphs for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_FONT_H
#define INCLUDED_SDL3_FONT_H

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#define SDL3_FONT_FIRST_GLYPH 32
#define SDL3_FONT_LAST_GLYPH 126
#define SDL3_FONT_GLYPH_COUNT \
	(SDL3_FONT_LAST_GLYPH - SDL3_FONT_FIRST_GLYPH + 1)
#define SDL3_FONT_PATH_CAPACITY 1024

struct sdl3_glyph {
	TTF_Text *text;
	int width;
	int height;
};

struct sdl3_font {
	TTF_Font *handle;
	TTF_TextEngine *engine;
	struct sdl3_glyph glyphs[SDL3_FONT_GLYPH_COUNT];
	char path[SDL3_FONT_PATH_CAPACITY];
	int point_size;
	int cell_width;
	int cell_height;
	bool fixed_width;
};

struct sdl3_font_resource_counts {
	int fonts;
	int engines;
	int texts;
	Uint64 fonts_opened;
	Uint64 engines_created;
	Uint64 texts_created;
};

/** Configure renderer defaults required by the glyph atlas. */
bool sdl3_font_prepare_renderer(SDL_Renderer *renderer);
/** Center a glyph in a cell and snap its origin to physical pixels. */
void sdl3_font_glyph_origin(float cell_x, float cell_y, float cell_width,
		float cell_height, int glyph_width, int glyph_height, float *draw_x,
		float *draw_y);
bool sdl3_font_init(struct sdl3_font *font, SDL_Renderer *renderer,
		const char *requested_path, int output_width, int output_height,
		int cols, int rows, int zoom_percent);
bool sdl3_font_init_lazy(struct sdl3_font *font, SDL_Renderer *renderer,
		const char *requested_path, int output_width, int output_height,
		int cols, int rows, int zoom_percent);
bool sdl3_font_init_lazy_fallback(struct sdl3_font *font,
		SDL_Renderer *renderer, const char *requested_path, int output_width,
		int output_height, int fallback_width, int fallback_height, int cols,
		int rows, int zoom_percent, bool *used_fallback);
void sdl3_font_free(struct sdl3_font *font);
bool sdl3_font_fit(struct sdl3_font *font, int output_width,
		int output_height, int cols, int rows, int zoom_percent);
bool sdl3_font_draw(struct sdl3_font *font, uint32_t codepoint,
		SDL_Color color, float cell_x, float cell_y, float cell_width,
		float cell_height);
void sdl3_font_get_resource_counts(
		struct sdl3_font_resource_counts *counts);
int sdl3_font_bundled_count(void);
const char *sdl3_font_bundled_name(int index);
int sdl3_font_bundled_index(const char *path);
const char *sdl3_font_filename(const char *path);

#endif /* INCLUDED_SDL3_FONT_H */
