/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/font.c
 * \brief Font selection and cached glyphs for the SDL3 frontend.
 *
 *
 */

#include "angband.h"
#include "init.h"
#include "sdl3/font.h"
#include "sdl3/zoom.h"

#define SDL3_FONT_MIN_SIZE 6
#define SDL3_FONT_MAX_SIZE 512
#define SDL3_FONT_PAD_X 2
#define SDL3_FONT_PAD_Y 2

static const char *const bundled_font_names[] = {
	"RobotoMono-SemiBold.ttf",
	"IBMPlexMono-Regular.ttf",
	"ShareTechMono-Regular.ttf",
	"CozetteVector.ttf",
	"VT323-Regular.ttf",
	NULL
};

static struct sdl3_font_resource_counts resource_counts;

bool sdl3_font_prepare_renderer(SDL_Renderer *renderer)
{
	if (!renderer) return false;
#if SDL_VERSION_ATLEAST(3, 4, 0)
	/* SDL3 textures default to linear filtering.  SDL_ttf's renderer engine
	 * stores many glyphs in shared atlas textures, so linear sampling can pull
	 * a row or column from a neighbouring glyph.  Make nearest sampling the
	 * renderer default before any text engine is created.  Ammerow's other
	 * cached textures set their own scale modes explicitly. */
	return SDL_SetDefaultTextureScaleMode(renderer, SDL_SCALEMODE_NEAREST);
#else
	/* Pixel-aligned draw origins below prevent interpolation on older SDL3
	 * versions which do not expose a renderer-wide texture default. */
	return true;
#endif
}

void sdl3_font_glyph_origin(float cell_x, float cell_y, float cell_width,
		float cell_height, int glyph_width, int glyph_height, float *draw_x,
		float *draw_y)
{
	if (draw_x) {
		*draw_x = SDL_roundf(cell_x + (cell_width - glyph_width) * 0.5f);
	}
	if (draw_y) {
		*draw_y = SDL_roundf(cell_y + (cell_height - glyph_height) * 0.5f);
	}
}

const char *sdl3_font_filename(const char *path)
{
	const char *slash;
	const char *backslash;
	const char *separator;

	if (!path) return "";
	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	separator = slash;
	if (!separator || (backslash && backslash > separator)) {
		separator = backslash;
	}
	return separator ? separator + 1 : path;
}

int sdl3_font_bundled_count(void)
{
	return (int)N_ELEMENTS(bundled_font_names) - 1;
}

const char *sdl3_font_bundled_name(int index)
{
	if (index < 0 || index >= sdl3_font_bundled_count()) return NULL;
	return bundled_font_names[index];
}

int sdl3_font_bundled_index(const char *path)
{
	const char *name = sdl3_font_filename(path);
	int i;

	for (i = 0; i < sdl3_font_bundled_count(); i++) {
		if (my_stricmp(name, bundled_font_names[i]) == 0) return i;
	}
	return -1;
}

static const char *const default_font_paths[] = {
#ifdef _WIN32
	"C:\\Windows\\Fonts\\CascadiaMono.ttf",
	"C:\\Windows\\Fonts\\CascadiaCode.ttf",
	"C:\\Windows\\Fonts\\consola.ttf",
	"C:\\Windows\\Fonts\\lucon.ttf",
#elif defined(__APPLE__)
	"/System/Library/Fonts/SFNSMono.ttf",
	"/System/Library/Fonts/Menlo.ttc",
	"/Library/Fonts/DejaVuSansMono.ttf",
#else
	"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
	"/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
	"/usr/share/fonts/TTF/DejaVuSansMono.ttf",
#endif
	NULL
};

static bool open_font(struct sdl3_font *font, const char *path)
{
	font->handle = TTF_OpenFont(path, 16.0f);
	if (!font->handle) return false;
	resource_counts.fonts++;
	resource_counts.fonts_opened++;

	SDL_strlcpy(font->path, path, sizeof(font->path));
	TTF_SetFontKerning(font->handle, false);
	TTF_SetFontHinting(font->handle, TTF_HINTING_LIGHT);
	font->fixed_width = TTF_FontIsFixedWidth(font->handle);
	return true;
}

static bool open_font_in_directory(struct sdl3_font *font,
		const char *directory, const char *name)
{
	char path[SDL3_FONT_PATH_CAPACITY];

	if (!directory || !directory[0] || !name || !name[0]) return false;
	path_build(path, sizeof(path), directory, name);
	return open_font(font, path);
}

static bool open_bundled_font(struct sdl3_font *font, const char *name)
{
	if (open_font_in_directory(font, ANGBAND_DIR_FONTS, name)) return true;
	if (open_font_in_directory(font, "assets/font", name)) return true;
	return open_font_in_directory(font, "assets/fonts", name);
}

static bool open_requested_font(struct sdl3_font *font, const char *name)
{
	if (open_font(font, name)) return true;
	return open_bundled_font(font, name);
}

static int maximum_advance(TTF_Font *handle)
{
	int codepoint;
	int result = 0;

	for (codepoint = SDL3_FONT_FIRST_GLYPH;
			codepoint <= SDL3_FONT_LAST_GLYPH; codepoint++) {
		int advance;

		if (TTF_GetGlyphMetrics(handle, (Uint32)codepoint, NULL, NULL,
				NULL, NULL, &advance) && advance > result) {
			result = advance;
		}
	}
	return result;
}

static bool refresh_glyph_sizes(struct sdl3_font *font)
{
	int i;

	for (i = 0; i < SDL3_FONT_GLYPH_COUNT; i++) {
		if (!font->glyphs[i].text) continue;
		if (!TTF_UpdateText(font->glyphs[i].text) ||
				!TTF_GetTextSize(font->glyphs[i].text,
				&font->glyphs[i].width, &font->glyphs[i].height)) {
			return false;
		}
	}
	return true;
}

static bool create_glyph(struct sdl3_font *font, int index)
{
	char text[2];

	if (index < 0 || index >= SDL3_FONT_GLYPH_COUNT) return false;
	if (font->glyphs[index].text) return true;
	text[0] = (char)(SDL3_FONT_FIRST_GLYPH + index);
	text[1] = '\0';
	font->glyphs[index].text = TTF_CreateText(font->engine, font->handle,
		text, 1);
	if (font->glyphs[index].text) {
		resource_counts.texts++;
		resource_counts.texts_created++;
	}
	if (!font->glyphs[index].text ||
			!TTF_GetTextSize(font->glyphs[index].text,
			&font->glyphs[index].width, &font->glyphs[index].height)) {
		return false;
	}
	return true;
}

static bool create_glyphs(struct sdl3_font *font)
{
	int i;

	for (i = 0; i < SDL3_FONT_GLYPH_COUNT; i++) {
		if (!create_glyph(font, i)) return false;
	}
	return true;
}

bool sdl3_font_fit(struct sdl3_font *font, int output_width,
		int output_height, int cols, int rows, int zoom_percent)
{
	int available_height;
	int candidate;
	int fitted_size;
	int chosen_width = 0;
	int chosen_height = 0;

	if (!font || !font->handle || output_width <= 0 || output_height <= 0 ||
			cols <= 0 || rows <= 0) {
		return false;
	}

	zoom_percent = sdl3_zoom_clamp(zoom_percent);
	available_height = output_height / rows;
	candidate = SDL_min(available_height, SDL3_FONT_MAX_SIZE);
	for (; candidate >= SDL3_FONT_MIN_SIZE; candidate--) {
		int advance;
		int height;

		if (!TTF_SetFontSize(font->handle, (float)candidate)) continue;
		advance = maximum_advance(font->handle);
		height = TTF_GetFontHeight(font->handle);
		if (advance <= 0 || height <= 0) continue;

		chosen_width = advance + SDL3_FONT_PAD_X;
		chosen_height = height + SDL3_FONT_PAD_Y;
		if (chosen_width * cols <= output_width &&
				chosen_height * rows <= output_height) {
			break;
		}
	}

	if (candidate < SDL3_FONT_MIN_SIZE) return false;
	fitted_size = candidate;
	candidate = SDL_max(SDL3_FONT_MIN_SIZE,
		(fitted_size * zoom_percent + 50) / 100);
	if (!TTF_SetFontSize(font->handle, (float)candidate)) return false;
	chosen_width = maximum_advance(font->handle) + SDL3_FONT_PAD_X;
	chosen_height = TTF_GetFontHeight(font->handle) + SDL3_FONT_PAD_Y;
	if (chosen_width <= SDL3_FONT_PAD_X ||
			chosen_height <= SDL3_FONT_PAD_Y) {
		return false;
	}
	font->point_size = candidate;
	font->cell_width = chosen_width;
	font->cell_height = chosen_height;
	return refresh_glyph_sizes(font);
}

static bool prepare_font(struct sdl3_font *font, SDL_Renderer *renderer,
		const char *requested_path)
{
	const char *environment_path;
	const char *environment_name;
	int i;

	if (!font || !renderer) return false;
	SDL_memset(font, 0, sizeof(*font));

	if (requested_path && requested_path[0]) {
		if (!open_requested_font(font, requested_path)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Could not open requested SDL3 font '%s': %s",
				requested_path, SDL_GetError());
		}
	} else {
		environment_name = "AMMEROW_SDL3_FONT";
		environment_path = SDL_getenv(environment_name);
		if (!environment_path || !environment_path[0]) {
			environment_name = "AMMEROW_SDL3_FONT";
			environment_path = SDL_getenv(environment_name);
		}
		if (environment_path && environment_path[0] &&
				!open_requested_font(font, environment_path)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Could not open %s '%s': %s", environment_name,
				environment_path, SDL_GetError());
		}
	}
	for (i = 0; !font->handle && bundled_font_names[i]; i++) {
		open_bundled_font(font, bundled_font_names[i]);
	}
	for (i = 0; !font->handle && default_font_paths[i]; i++) {
		open_font(font, default_font_paths[i]);
	}
	if (!font->handle) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not find a usable monospaced SDL3 font");
		return false;
	}

	font->engine = TTF_CreateRendererTextEngine(renderer);
	if (!font->engine) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not create the SDL3_ttf renderer text engine: %s",
			SDL_GetError());
		sdl3_font_free(font);
		return false;
	}
	resource_counts.engines++;
	resource_counts.engines_created++;
	return true;
}

static bool initialize_font(struct sdl3_font *font, SDL_Renderer *renderer,
		const char *requested_path, int output_width, int output_height,
		int cols, int rows, int zoom_percent, bool eager,
		int fallback_width, int fallback_height, bool *used_fallback)
{
	bool fitted;

	if (used_fallback) *used_fallback = false;
	if (!prepare_font(font, renderer, requested_path)) return false;
	fitted = sdl3_font_fit(font, output_width, output_height, cols, rows,
		zoom_percent);
	if (!fitted && fallback_width > 0 && fallback_height > 0) {
		fitted = sdl3_font_fit(font, fallback_width, fallback_height, cols,
			rows, zoom_percent);
		if (fitted && used_fallback) *used_fallback = true;
	}
	if (!fitted || (eager && !create_glyphs(font))) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not prepare the SDL3 glyph cache: %s", SDL_GetError());
		sdl3_font_free(font);
		return false;
	}

	SDL_Log("SDL3 font: %s, %d px, %s", font->path, font->point_size,
		font->fixed_width ? "fixed width" : "variable width");
	return true;
}

bool sdl3_font_init(struct sdl3_font *font, SDL_Renderer *renderer,
		const char *requested_path, int output_width, int output_height,
		int cols, int rows, int zoom_percent)
{
	return initialize_font(font, renderer, requested_path, output_width,
		output_height, cols, rows, zoom_percent, true, 0, 0, NULL);
}

bool sdl3_font_init_lazy(struct sdl3_font *font, SDL_Renderer *renderer,
		const char *requested_path, int output_width, int output_height,
		int cols, int rows, int zoom_percent)
{
	return initialize_font(font, renderer, requested_path, output_width,
		output_height, cols, rows, zoom_percent, false, 0, 0, NULL);
}

bool sdl3_font_init_lazy_fallback(struct sdl3_font *font,
		SDL_Renderer *renderer, const char *requested_path, int output_width,
		int output_height, int fallback_width, int fallback_height, int cols,
		int rows, int zoom_percent, bool *used_fallback)
{
	return initialize_font(font, renderer, requested_path, output_width,
		output_height, cols, rows, zoom_percent, false, fallback_width,
		fallback_height, used_fallback);
}

void sdl3_font_free(struct sdl3_font *font)
{
	int i;

	if (!font) return;
	for (i = 0; i < SDL3_FONT_GLYPH_COUNT; i++) {
		if (font->glyphs[i].text) {
			TTF_DestroyText(font->glyphs[i].text);
			resource_counts.texts--;
		}
	}
	if (font->engine) {
		TTF_DestroyRendererTextEngine(font->engine);
		resource_counts.engines--;
	}
	if (font->handle) {
		TTF_CloseFont(font->handle);
		resource_counts.fonts--;
	}
	SDL_memset(font, 0, sizeof(*font));
}

void sdl3_font_get_resource_counts(struct sdl3_font_resource_counts *counts)
{
	if (!counts) return;
	*counts = resource_counts;
}

bool sdl3_font_draw(struct sdl3_font *font, uint32_t codepoint,
		SDL_Color color, float cell_x, float cell_y, float cell_width,
		float cell_height)
{
	struct sdl3_glyph *glyph;
	int index;
	float x;
	float y;

	if (!font || !font->handle || codepoint == ' ') return true;
	if (codepoint < SDL3_FONT_FIRST_GLYPH ||
			codepoint > SDL3_FONT_LAST_GLYPH) {
		codepoint = '?';
	}
	index = (int)codepoint - SDL3_FONT_FIRST_GLYPH;
	glyph = &font->glyphs[index];
	if (!glyph->text && !create_glyph(font, index)) return false;

	/* Renderer text is backed by a shared atlas.  Whole physical-pixel origins
	 * keep sampling inside the selected glyph at every window size and DPI. */
	sdl3_font_glyph_origin(cell_x, cell_y, cell_width, cell_height,
		glyph->width, glyph->height, &x, &y);
	if (!TTF_SetTextColor(glyph->text, color.r, color.g, color.b, color.a)) {
		return false;
	}
	return TTF_DrawRendererText(glyph->text, x, y);
}
