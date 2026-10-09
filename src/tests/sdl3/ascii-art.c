/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/ascii-art */

#include "unit-test.h"

#include "sdl3/ascii-art.h"

int setup_tests(void **state)
{
	*state = NULL;
	return 0;
}

int teardown_tests(void *state)
{
	return 0;
}

static int test_namespaces(void *state)
{
	char key[SDL3_ASCII_ART_KEY_CAPACITY];

	sdl3_ascii_art_make_key(key, sizeof(key), SDL3_ASCII_ART_MONSTER,
		"cave-orc", "orc");
	require(streq(key, "cave-orc|orc"));
	sdl3_ascii_art_make_key(key, sizeof(key), SDL3_ASCII_ART_TERRAIN,
		"granite-wall", NULL);
	require(streq(key, "terrain:granite-wall"));
	sdl3_ascii_art_make_key(key, sizeof(key), SDL3_ASCII_ART_ITEM_KIND,
		"Bad/Id", NULL);
	require(streq(key, ""));
	ok;
}

static int test_item_knowledge_gate(void *state)
{
	char key[SDL3_ASCII_ART_KEY_CAPACITY];

	sdl3_ascii_art_make_item_key(key, sizeof(key), "potion-of-healing",
		"unidentified-potion", true, false);
	require(streq(key, "item-unidentified:unidentified-potion"));
	sdl3_ascii_art_make_item_key(key, sizeof(key), "potion-of-healing",
		"unidentified-potion", true, true);
	require(streq(key, "item-kind:potion-of-healing"));
	sdl3_ascii_art_make_item_key(key, sizeof(key), "dagger", NULL, false,
		false);
	require(streq(key, "item-kind:dagger"));
	ok;
}

const char *suite_name = "sdl3/ascii-art";
struct test tests[] = {
	{ "namespaces", test_namespaces },
	{ "item_knowledge_gate", test_item_knowledge_gate },
	{ NULL, NULL }
};
