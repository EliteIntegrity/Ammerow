/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/store-art.c */
/* Exercise deterministic shopkeeper state and reserved store layout. */

#include "unit-test.h"

#include "sdl3/store-art.h"

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

static int test_state(void *state)
{
	struct sdl3_store_art art;
	(void)state;

	sdl3_store_art_clear(&art);
	require(!art.active);
	require(!art.asset_key[0]);
	sdl3_store_art_show(&art,
		"bramble-fen-the-provisioner|general-store");
	require(art.active);
	require(streq(art.asset_key,
		"bramble-fen-the-provisioner|general-store"));
	sdl3_store_art_show(&art, NULL);
	require(!art.active);
	require(!art.asset_key[0]);
	ok;
}

static int test_content_width(void *state)
{
	(void)state;

	eq(sdl3_store_art_content_cols(0), 0);
	eq(sdl3_store_art_content_cols(62), 62);
	eq(sdl3_store_art_content_cols(78), 78);
	eq(sdl3_store_art_content_cols(79), 62);
	eq(sdl3_store_art_content_cols(80), 62);
	eq(sdl3_store_art_content_cols(100), 66);
	eq(sdl3_store_art_content_cols(120), 79);
	eq(sdl3_store_art_content_cols(140), 97);
	eq(sdl3_store_art_content_cols(200), 157);
	ok;
}

const char *suite_name = "sdl3/store-art";
struct test tests[] = {
	{ "state", test_state },
	{ "content width", test_content_width },
	{ NULL, NULL },
};
