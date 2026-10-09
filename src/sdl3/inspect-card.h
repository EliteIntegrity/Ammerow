/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/inspect-card.h
 * \brief Renderer-owned examine card shared by creatures, items and terrain.
 *
 *
 */

#ifndef INCLUDED_SDL3_INSPECT_CARD_H
#define INCLUDED_SDL3_INSPECT_CARD_H

#include <SDL3/SDL.h>
#include <wchar.h>

#include "sdl3/ascii-art.h"

#define SDL3_INSPECT_CARD_NAME_CAPACITY 128
#define SDL3_INSPECT_CARD_SUBTITLE_CAPACITY 128
#define SDL3_INSPECT_CARD_STATUS_CAPACITY 128
#define SDL3_INSPECT_CARD_DETAIL_CAPACITY 256
#define SDL3_INSPECT_CARD_DETAIL_COUNT 6
#define SDL3_INSPECT_CARD_FLAVOR_CAPACITY 1024
#define SDL3_INSPECT_CARD_FOOTER_CAPACITY 64

struct sdl3_map_view;
struct sdl3_theme;
struct sdl3_visual;

struct sdl3_inspect_card {
	bool active;
	bool big;
	wchar_t glyph;
	uint8_t attr;
	bool emphasis;
	int cursor_col;
	int cursor_row;
	char asset_key[SDL3_ASCII_ART_KEY_CAPACITY];
	char name[SDL3_INSPECT_CARD_NAME_CAPACITY];
	char subtitle[SDL3_INSPECT_CARD_SUBTITLE_CAPACITY];
	char status[SDL3_INSPECT_CARD_STATUS_CAPACITY];
	char details[SDL3_INSPECT_CARD_DETAIL_COUNT]
		[SDL3_INSPECT_CARD_DETAIL_CAPACITY];
	char flavor[SDL3_INSPECT_CARD_FLAVOR_CAPACITY];
	char footer[SDL3_INSPECT_CARD_FOOTER_CAPACITY];
};

void sdl3_inspect_card_draw(const struct sdl3_inspect_card *card,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme,
		const struct sdl3_map_view *map_view);

#endif /* INCLUDED_SDL3_INSPECT_CARD_H */
