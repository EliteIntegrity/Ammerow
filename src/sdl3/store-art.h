/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/store-art.h
 * \brief Presentation-only state and layout for SDL3 shopkeeper art.
 */

#ifndef INCLUDED_SDL3_STORE_ART_H
#define INCLUDED_SDL3_STORE_ART_H

#include <stdbool.h>

#define SDL3_STORE_ART_ID_CAPACITY 128
#define SDL3_STORE_ART_MIN_CONTENT_COLS 62
#define SDL3_STORE_ART_MIN_PORTRAIT_COLS 16
#define SDL3_STORE_ART_MAX_PORTRAIT_COLS 42

struct sdl3_store_art {
	char asset_key[SDL3_STORE_ART_ID_CAPACITY * 2];
	bool active;
};

void sdl3_store_art_clear(struct sdl3_store_art *art);
void sdl3_store_art_show(struct sdl3_store_art *art, const char *asset_key);
int sdl3_store_art_content_cols(int total_cols);

#endif /* INCLUDED_SDL3_STORE_ART_H */
