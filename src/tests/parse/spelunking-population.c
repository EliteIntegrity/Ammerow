/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/spelunking-population */
/* Exercise immutable generated-cave population profiles. */

#include "unit-test.h"

#include "parser.h"
#include "world-spelunking-population-data.h"

NOSETUP
NOTEARDOWN

static const char *complete_lines[] = {
	"profile:deepwell.test.sparse:1",
	"name:Test sparse life",
	"description:A deterministic test population.",
	"exclusion:5:3:4",
	"actor:core.spelunk.actor.test:1:3:20:85",
	"object:potion:1:2:25:90:Cure Light Wounds",
	"object:scroll:0:1:35:90:Magic Mapping"
};

static int test_complete_profile(void *unused)
{
	struct parser *p = spelunking_population_parser.init();
	const struct world_spelunk_population_profile *profile;
	int i;

	(void)unused;
	for (i = 0; i < (int)N_ELEMENTS(complete_lines); i++) {
		eq(parser_parse(p, complete_lines[i]), PARSE_ERROR_NONE);
	}
	eq(spelunking_population_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_spelunk_population_profile_count(), 1);
	profile = world_spelunk_population_profile_by_id(
		"deepwell.test.sparse");
	notnull(profile);
	ptreq(profile, world_spelunk_population_profile_by_index(0));
	require(streq(profile->name, "Test sparse life"));
	eq(profile->version, 1);
	eq(profile->entrance_exclusion_radius, 5);
	eq(profile->landmark_exclusion_radius, 3);
	eq(profile->endpoint_exclusion_radius, 4);
	eq(profile->actor_entry_count, 1);
	eq(profile->actors[0].minimum_count, 1);
	eq(profile->actors[0].maximum_count, 3);
	require(streq(profile->actors[0].actor_id,
		"core.spelunk.actor.test"));
	eq(profile->object_entry_count, 2);
	require(streq(profile->objects[0].object_tval, "potion"));
	require(streq(profile->objects[0].object_sval,
		"Cure Light Wounds"));
	null(world_spelunk_population_profile_by_id("missing"));
	spelunking_population_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete_fail_closed(void *unused)
{
	struct parser *p = spelunking_population_parser.init();

	(void)unused;
	eq(parser_parse(p, "name:No profile"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "profile:deepwell.test.sparse:0"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "profile:deepwell.test.sparse:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "profile:deepwell.test.sparse:1"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "name:Test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "description:Test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "exclusion:21:3:4"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "exclusion:5:3:4"), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"actor:core.spelunk.actor.test:3:2:20:85"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p,
		"object:potion:0:0:20:85:Cure Light Wounds"),
		PARSE_ERROR_INVALID_VALUE);
	eq(spelunking_population_parser.finish(p),
		PARSE_ERROR_TOO_FEW_ENTRIES);
	ok;
}

const char *suite_name = "parse/spelunking-population";
struct test tests[] = {
	{ "complete profile", test_complete_profile },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ NULL, NULL }
};
