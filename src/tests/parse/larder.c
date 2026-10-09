/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/larder */
/* Exercise append-only village-larder milestone identities and messages. */

#include "unit-test.h"

#include "datafile.h"
#include "parser.h"
#include "world-larder-data.h"

NOSETUP
NOTEARDOWN

static int test_complete_registry(void *unused)
{
	struct parser *p = larder_parser.init();
	const struct world_larder_milestone_definition *definition;

	(void)unused;
	notnull(p);
	eq(parser_parse(p,
		"milestone:1:emergency-reserve:20:first emergency reserve"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "message:The reserve is secure."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "reward:Expedition Rations are stocked."),
		PARSE_ERROR_NONE);
	eq(larder_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_larder_milestone_count(), 1);
	definition = world_larder_milestone_by_save_index(1);
	notnull(definition);
	ptreq(definition, world_larder_milestone_by_id("emergency-reserve"));
	eq(definition->save_index, 1);
	eq(definition->target, 20U);
	require(streq(definition->name, "first emergency reserve"));
	require(streq(definition->message, "The reserve is secure."));
	require(streq(definition->reward, "Expedition Rations are stocked."));
	null(world_larder_milestone_by_save_index(0));
	null(world_larder_milestone_by_id("missing"));
	larder_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete_fail_closed(void *unused)
{
	struct parser *p = larder_parser.init();

	(void)unused;
	eq(parser_parse(p, "message:No header"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "milestone:0:bad:20:Bad"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "milestone:1:first:20:First"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "milestone:1:duplicate:30:Duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "message:Reached."), PARSE_ERROR_NONE);
	eq(larder_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	eq(world_larder_milestone_count(), 0);

	p = larder_parser.init();
	eq(parser_parse(p, "milestone:2:second:30:Second"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "message:Reached."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "reward:Reward."), PARSE_ERROR_NONE);
	eq(larder_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	ok;
}

const char *suite_name = "parse/larder";
struct test tests[] = {
	{ "complete registry", test_complete_registry },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ NULL, NULL }
};
