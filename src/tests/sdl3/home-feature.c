/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/home-feature.c */
/* Exercise the curated Home feature registry. */

#include "unit-test.h"

#include "datafile.h"
#include "parser.h"
#include "sdl3/home-feature.h"

static enum parser_error parse_features(const char *const *lines)
{
	struct parser *p = home_feature_parser.init();
	int i;

	for (i = 0; lines[i]; i++) {
		enum parser_error result = parser_parse(p, lines[i]);

		if (result != PARSE_ERROR_NONE) {
			parser_destroy(p);
			return result;
		}
	}
	return home_feature_parser.finish(p);
}

int setup_tests(void **data)
{
	static const char *const valid[] = {
		"hero:first-monster",
		"hero:second-monster",
		"hero:third-monster",
		NULL
	};
	(void)data;
	return parse_features(valid) == PARSE_ERROR_NONE ? 0 : 1;
}

int teardown_tests(void *data)
{
	(void)data;
	home_feature_parser.cleanup();
	return 0;
}

static int test_registry_and_selection(void *state)
{
	(void)state;

	eq(sdl3_home_feature_count(), 3);
	require(streq(sdl3_home_feature_at(0), "first-monster"));
	require(streq(sdl3_home_feature_for_seed(0), "first-monster"));
	require(streq(sdl3_home_feature_for_seed(4), "second-monster"));
	require(!sdl3_home_feature_at(3));
	ok;
}

static int test_duplicate_does_not_replace_registry(void *state)
{
	static const char *const duplicate[] = {
		"hero:one",
		"hero:one",
		NULL
	};
	(void)state;

	eq(parse_features(duplicate), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(sdl3_home_feature_count(), 3);
	ok;
}

static int test_empty_does_not_replace_registry(void *state)
{
	static const char *const empty[] = { NULL };
	(void)state;

	eq(parse_features(empty), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(sdl3_home_feature_count(), 3);
	ok;
}

const char *suite_name = "sdl3/home-feature";
struct test tests[] = {
	{ "registry and selection", test_registry_and_selection },
	{ "duplicate does not replace registry",
		test_duplicate_does_not_replace_registry },
	{ "empty does not replace registry", test_empty_does_not_replace_registry },
	{ NULL, NULL },
};
