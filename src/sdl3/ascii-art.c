/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/ascii-art.c
 * \brief Shared identities for all compiled coloured-ASCII subjects.
 */

#include "angband.h"

#include "datafile.h"
#include "sdl3/ascii-art.h"

static const char *namespace_prefix(
		enum sdl3_ascii_art_namespace name_space)
{
	switch (name_space) {
	case SDL3_ASCII_ART_MONSTER: return "";
	case SDL3_ASCII_ART_SHOPKEEPER: return "shopkeeper:";
	case SDL3_ASCII_ART_ITEM_KIND: return "item-kind:";
	case SDL3_ASCII_ART_ITEM_UNIDENTIFIED: return "item-unidentified:";
	case SDL3_ASCII_ART_TERRAIN: return "terrain:";
	}
	return NULL;
}

void sdl3_ascii_art_make_key(char *destination, size_t capacity,
		enum sdl3_ascii_art_namespace name_space, const char *art_id,
		const char *fallback_art_id)
{
	char ids[SDL3_ASCII_ART_ID_CAPACITY];
	const char *prefix_value;

	if (!destination || !capacity) return;
	destination[0] = '\0';
	prefix_value = namespace_prefix(name_space);
	if (!prefix_value || !datafile_art_id_is_valid(art_id)) return;
	if (fallback_art_id && fallback_art_id[0]) {
		if (!datafile_art_id_is_valid(fallback_art_id)) return;
		strnfmt(ids, sizeof(ids), "%s|%s", art_id, fallback_art_id);
	} else {
		my_strcpy(ids, art_id, sizeof(ids));
	}
	strnfmt(destination, capacity, "%s%s", prefix_value, ids);
}

void sdl3_ascii_art_make_item_key(char *destination, size_t capacity,
		const char *kind_art_id, const char *unaware_art_id, bool flavored,
		bool aware)
{
	if (flavored && !aware) {
		sdl3_ascii_art_make_key(destination, capacity,
			SDL3_ASCII_ART_ITEM_UNIDENTIFIED, unaware_art_id, NULL);
	} else {
		sdl3_ascii_art_make_key(destination, capacity,
			SDL3_ASCII_ART_ITEM_KIND, kind_art_id, NULL);
	}
}
