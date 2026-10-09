/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/store-art.c
 * \brief Presentation-only state and layout for SDL3 shopkeeper art.
 */

#include "sdl3/store-art.h"

#include <string.h>

void sdl3_store_art_clear(struct sdl3_store_art *art)
{
	if (art) memset(art, 0, sizeof(*art));
}

void sdl3_store_art_show(struct sdl3_store_art *art, const char *asset_key)
{
	size_t length;

	if (!art) return;
	sdl3_store_art_clear(art);
	if (!asset_key || !asset_key[0]) return;
	length = strlen(asset_key);
	if (length >= sizeof(art->asset_key)) length = sizeof(art->asset_key) - 1;
	memcpy(art->asset_key, asset_key, length);
	art->asset_key[length] = '\0';
	art->active = true;
}

int sdl3_store_art_content_cols(int total_cols)
{
	int portrait_cols;
	int content_cols;

	if (total_cols <= 0) return 0;
	portrait_cols = total_cols / 3;
	if (portrait_cols < SDL3_STORE_ART_MIN_PORTRAIT_COLS) {
		portrait_cols = SDL3_STORE_ART_MIN_PORTRAIT_COLS;
	}
	if (portrait_cols > SDL3_STORE_ART_MAX_PORTRAIT_COLS) {
		portrait_cols = SDL3_STORE_ART_MAX_PORTRAIT_COLS;
	}
	content_cols = total_cols - portrait_cols - 1;
	if (content_cols < SDL3_STORE_ART_MIN_CONTENT_COLS) {
		content_cols = SDL3_STORE_ART_MIN_CONTENT_COLS;
	}
	if (total_cols - content_cols - 1 <
			SDL3_STORE_ART_MIN_PORTRAIT_COLS) {
		return total_cols;
	}
	return content_cols;
}
