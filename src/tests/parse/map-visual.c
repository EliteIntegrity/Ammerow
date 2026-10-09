/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "unit-test.h"
#include "parser.h"
#include "map-visual-data.h"

NOSETUP
NOTEARDOWN

static int test_bindings_and_validation(void *unused)
{
	struct parser *p = map_visual_parser.init();
	const struct map_visual_binding *binding;
	(void)unused;
	notnull(p);
	eq(parser_parse(p, "tile:cave:rock:feature:cave-rock"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "tile:cave:rock:feature:other-rock"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "tile:cave:water:monster:fish"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "tile:cave:water:feature:../fish"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "tile:cave:../hidden:feature:fish"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "tile:typo:rock:feature:cave-rock"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "tile:cave-actor:core.spelunk.actor.skitter:actor:skitter"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "tile:map:visible-pile:object:loot-pile"), PARSE_ERROR_NONE);
	eq(map_visual_parser.finish(p), PARSE_ERROR_NONE);
	binding = map_visual_for("cave-actor", "core.spelunk.actor.skitter");
	notnull(binding);
	require(streq(binding->key, "actor:skitter"));
	require(streq(map_visual_for("map", "visible-pile")->key, "object:loot-pile"));
	require(!map_visual_for("cave", "unknown"));
	require(!map_visual_for(NULL, "rock"));
	map_visual_parser.cleanup();
	require(!map_visual_for("cave", "rock"));
	eq(map_visual_parser.finish(map_visual_parser.init()), PARSE_ERROR_TOO_FEW_ENTRIES);
	ok;
}

const char *suite_name = "parse/map-visual";
struct test tests[] = {
	{ "bindings validation namespaces and cleanup", test_bindings_and_validation },
	{ NULL, NULL }
};
