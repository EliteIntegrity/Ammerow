/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/spelunking-traversal */
/* Exercise traversal proof-budget parsing and immutable lookup. */

#include "unit-test.h"

#include "parser.h"
#include "world-spelunking-traversal-data.h"

#include <string.h>

NOSETUP
NOTEARDOWN

static enum parser_error parse_complete(struct parser *p)
{
	static const char *lines[] = {
		"profile:deepwell.test.launch:2",
		"name:Test launch route",
		"description:A deterministic traversal contract.",
		"equipment:4:24",
		"rope-shafts:1",
		"safety:0:6",
		"return:1"
	};
	int i;

	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		enum parser_error result = parser_parse(p, lines[i]);

		if (result != PARSE_ERROR_NONE) return result;
	}
	return PARSE_ERROR_NONE;
}

static int test_complete_registry(void *unused)
{
	struct parser *p = spelunking_traversal_parser.init();
	const struct world_spelunk_traversal_profile *profile;

	(void)unused;
	notnull(p);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_traversal_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_spelunk_traversal_profile_count(), 1);
	profile = world_spelunk_traversal_profile_by_id(
		"deepwell.test.launch");
	notnull(profile);
	ptreq(profile, world_spelunk_traversal_profile_by_index(0));
	require(streq(profile->name, "Test launch route"));
	eq(profile->version, 2);
	eq(profile->piton_budget, 4);
	eq(profile->rope_segment_budget, 24);
	eq(profile->rope_shaft_count, 1);
	eq(profile->max_mandatory_fall_damage, 0);
	eq(profile->max_submerged_turns, 6);
	require(profile->return_required);
	null(world_spelunk_traversal_profile_by_id("missing"));
	spelunking_traversal_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete_fail_closed(void *unused)
{
	struct parser *p = spelunking_traversal_parser.init();

	(void)unused;
	eq(parser_parse(p, "equipment:4:24"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "profile:Bad ID:1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "profile:deepwell.test.incomplete:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:Incomplete"), PARSE_ERROR_NONE);
	eq(spelunking_traversal_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_spelunk_traversal_profile_count(), 0);

	p = spelunking_traversal_parser.init();
	eq(parser_parse(p, "profile:deepwell.test.bad-rope:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:Bad rope"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "description:Rope without an anchor."),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "equipment:0:1"), PARSE_ERROR_INVALID_VALUE);
	spelunking_traversal_parser.finish(p);

	p = spelunking_traversal_parser.init();
	eq(parser_parse(p, "profile:deepwell.test.bad-demonstration:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:Bad demonstration"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "description:More rope shafts than pitons."),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "equipment:1:24"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rope-shafts:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "safety:0:0"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "return:1"), PARSE_ERROR_NONE);
	eq(spelunking_traversal_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);

	p = spelunking_traversal_parser.init();
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rope-shafts:0"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "return:0"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "profile:deepwell.test.launch:3"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(spelunking_traversal_parser.finish(p), PARSE_ERROR_NONE);
	spelunking_traversal_parser.cleanup();
	ok;
}

const char *suite_name = "parse/spelunking-traversal";
struct test tests[] = {
	{ "complete registry", test_complete_registry },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ NULL, NULL }
};
