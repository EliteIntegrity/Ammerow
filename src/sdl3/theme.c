/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/theme.c
 * \brief Presentation-only colour themes for the SDL3 frontend.
 *
 *
 */

#include "sdl3/theme.h"

#define RGBA(r, g, b, a) { r, g, b, a }
#define RGB(r, g, b) RGBA(r, g, b, SDL_ALPHA_OPAQUE)

/* A cool, low-glare palette that preserves Angband's colour families. */
static const SDL_Color midnight_palette[MAX_COLORS] = {
	RGB(10, 13, 18),    RGB(230, 237, 243), RGB(125, 133, 144),
	RGB(227, 164, 81),  RGB(201, 81, 93),   RGB(79, 175, 130),
	RGB(94, 129, 209),  RGB(155, 107, 62),  RGB(60, 70, 82),
	RGB(184, 194, 204), RGB(199, 146, 234), RGB(230, 200, 110),
	RGB(255, 107, 119), RGB(120, 214, 163), RGB(121, 184, 255),
	RGB(212, 167, 106), RGB(157, 108, 189), RGB(181, 139, 235),
	RGB(79, 179, 184),  RGB(135, 132, 92),  RGB(255, 232, 154),
	RGB(231, 122, 175), RGB(126, 221, 215), RGB(208, 180, 255),
	RGB(255, 166, 166), RGB(201, 180, 88),  RGB(143, 175, 196),
	RGB(105, 197, 255), RGB(24, 32, 42)
};

/* Warm charcoal, parchment neutrals, copper accents, and restrained colour. */
static const SDL_Color ember_palette[MAX_COLORS] = {
	RGB(17, 12, 10),    RGB(242, 230, 212), RGB(139, 123, 112),
	RGB(228, 154, 69),  RGB(201, 79, 70),   RGB(93, 154, 104),
	RGB(95, 120, 168),  RGB(152, 97, 55),   RGB(74, 60, 53),
	RGB(200, 184, 168), RGB(188, 121, 166), RGB(221, 189, 98),
	RGB(255, 113, 98),  RGB(130, 201, 135), RGB(140, 169, 214),
	RGB(214, 161, 107), RGB(142, 89, 124),  RGB(168, 110, 160),
	RGB(78, 155, 145),  RGB(136, 122, 79),  RGB(247, 217, 137),
	RGB(218, 115, 148), RGB(122, 195, 177), RGB(194, 154, 194),
	RGB(245, 160, 154), RGB(191, 166, 80),  RGB(142, 154, 165),
	RGB(135, 185, 213), RGB(38, 26, 22)
};

/* An explicit accessibility option with maximum separation on true black. */
static const SDL_Color high_contrast_palette[MAX_COLORS] = {
	RGB(0, 0, 0),       RGB(255, 255, 255), RGB(160, 160, 160),
	RGB(255, 153, 0),   RGB(217, 38, 46),   RGB(0, 184, 98),
	RGB(77, 117, 255),  RGB(181, 112, 48),  RGB(96, 96, 96),
	RGB(220, 220, 220), RGB(255, 79, 255),  RGB(255, 235, 0),
	RGB(255, 75, 75),   RGB(66, 255, 66),   RGB(53, 228, 255),
	RGB(255, 184, 91),  RGB(187, 79, 220),  RGB(203, 126, 255),
	RGB(0, 210, 210),   RGB(166, 158, 70),  RGB(255, 255, 128),
	RGB(255, 91, 185),  RGB(92, 255, 229),  RGB(220, 190, 255),
	RGB(255, 174, 174), RGB(220, 204, 64),  RGB(157, 201, 230),
	RGB(75, 207, 255),  RGB(42, 42, 42)
};

static const struct sdl3_theme themes[] = {
	{
		"classic", "Classic", NULL,
		RGB(0, 0, 0), RGB(12, 12, 12), RGB(128, 128, 128),
		RGB(255, 255, 0), RGB(255, 255, 255), RGB(160, 160, 160),
		RGB(0, 255, 255), RGBA(72, 72, 72, 220), 88
	},
	{
		"midnight", "Midnight", midnight_palette,
		RGB(5, 7, 10), RGB(17, 24, 34), RGB(58, 78, 99),
		RGB(230, 200, 110), RGB(230, 237, 243), RGB(143, 158, 174),
		RGB(105, 197, 255), RGBA(42, 67, 88, 225), 88
	},
	{
		"ember", "Ember", ember_palette,
		RGB(8, 5, 4), RGB(31, 22, 18), RGB(104, 72, 54),
		RGB(228, 154, 69), RGB(242, 230, 212), RGB(170, 146, 128),
		RGB(255, 113, 98), RGBA(91, 57, 41, 225), 88
	},
	{
		"high-contrast", "High Contrast", high_contrast_palette,
		RGB(0, 0, 0), RGB(0, 0, 0), RGB(255, 255, 255),
		RGB(255, 235, 0), RGB(255, 255, 255), RGB(200, 200, 200),
		RGB(53, 228, 255), RGBA(64, 64, 64, 245), 104
	}
};

int sdl3_theme_count(void)
{
	return (int)N_ELEMENTS(themes);
}

const struct sdl3_theme *sdl3_theme_by_index(int index)
{
	int default_index = 1;

	if (index < 0 || index >= sdl3_theme_count()) index = default_index;
	return &themes[index];
}

const struct sdl3_theme *sdl3_theme_by_id(const char *id)
{
	int i;

	if (id && id[0]) {
		for (i = 0; i < sdl3_theme_count(); i++) {
			if (my_stricmp(themes[i].id, id) == 0) return &themes[i];
		}
	}
	return sdl3_theme_by_index(1);
}

int sdl3_theme_index(const struct sdl3_theme *theme)
{
	int i;

	for (i = 0; i < sdl3_theme_count(); i++) {
		if (theme == &themes[i]) return i;
	}
	return 1;
}

SDL_Color sdl3_theme_color(const struct sdl3_theme *theme, uint8_t index)
{
	SDL_Color color;

	index %= MAX_COLORS;
	if (theme && theme->palette && index < BASIC_COLORS) {
		return theme->palette[index];
	}
	color.r = angband_color_table[index][1];
	color.g = angband_color_table[index][2];
	color.b = angband_color_table[index][3];
	color.a = SDL_ALPHA_OPAQUE;
	return color;
}

SDL_Color sdl3_world_color(const struct sdl3_theme *theme, uint8_t index)
{
	SDL_Color color;

	index %= MAX_COLORS;
	/* High Contrast is an accessibility contract rather than a mood skin, so
	 * it intentionally remains allowed to remap categorical world colours. */
	if (theme == &themes[3] && index < BASIC_COLORS) {
		return high_contrast_palette[index];
	}
	color.r = angband_color_table[index][1];
	color.g = angband_color_table[index][2];
	color.b = angband_color_table[index][3];
	color.a = SDL_ALPHA_OPAQUE;
	return color;
}

SDL_Color sdl3_dim_world_color(SDL_Color visible, uint8_t brightness)
{
	int grey = (visible.r * 3 + visible.g * 6 + visible.b) / 10;

	/* Keep enough material hue to identify terrain, but make present sight
	 * immediately stronger than memory in every visual mode. */
	visible.r = (uint8_t)(((visible.r * 3 + grey * 2) / 5) *
		brightness / 255);
	visible.g = (uint8_t)(((visible.g * 3 + grey * 2) / 5) *
		brightness / 255);
	visible.b = (uint8_t)(((visible.b * 3 + grey * 2) / 5) *
		brightness / 255);
	return visible;
}

SDL_Color sdl3_theme_remembered_world_color(const struct sdl3_theme *theme,
		SDL_Color visible)
{
	return sdl3_dim_world_color(visible,
		theme ? theme->remembered_world_brightness : 88);
}

#undef RGB
#undef RGBA
