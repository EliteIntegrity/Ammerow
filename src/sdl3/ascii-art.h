/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/ascii-art.h
 * \brief Shared identities for all compiled coloured-ASCII subjects.
 *
 * Game data owns the unprefixed IDs.  This presentation-only key adds the
 * namespace needed by the common FROGART3 renderer without exposing paths to
 * gameplay code.
 */

#ifndef INCLUDED_SDL3_ASCII_ART_H
#define INCLUDED_SDL3_ASCII_ART_H

#include <stdbool.h>
#include <stddef.h>

#define SDL3_ASCII_ART_ID_CAPACITY 128
#define SDL3_ASCII_ART_KEY_CAPACITY 160

enum sdl3_ascii_art_namespace {
	SDL3_ASCII_ART_MONSTER = 0,
	SDL3_ASCII_ART_SHOPKEEPER,
	SDL3_ASCII_ART_ITEM_KIND,
	SDL3_ASCII_ART_ITEM_UNIDENTIFIED,
	SDL3_ASCII_ART_TERRAIN
};

void sdl3_ascii_art_make_key(char *destination, size_t capacity,
		enum sdl3_ascii_art_namespace name_space, const char *art_id,
		const char *fallback_art_id);
void sdl3_ascii_art_make_item_key(char *destination, size_t capacity,
		const char *kind_art_id, const char *unaware_art_id, bool flavored,
		bool aware);

#endif /* INCLUDED_SDL3_ASCII_ART_H */
