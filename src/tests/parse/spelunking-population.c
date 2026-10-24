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
static int test_discoveries(void *unused)
{
	int variant, i;
	(void)unused;
	for (variant = 0; variant < 3; variant++) {
		struct parser *p = spelunking_population_parser.init();
		const struct world_spelunk_population_profile *profile;
		eq(parser_parse(p, "discovery-chance:40"), PARSE_ERROR_MISSING_RECORD_HEADER);
		eq(parser_parse(p, "discovery:test.find:1:10:80"), PARSE_ERROR_MISSING_RECORD_HEADER);
		for (i = 0; i < (int)N_ELEMENTS(complete_lines); i++)
			eq(parser_parse(p, complete_lines[i]), PARSE_ERROR_NONE);
		eq(parser_parse(p, "discovery-chance:101"), PARSE_ERROR_INVALID_VALUE);
		eq(parser_parse(p, "discovery:test.find:0:10:80"), PARSE_ERROR_INVALID_VALUE);
		eq(parser_parse(p, "discovery:test.find:1:90:80"), PARSE_ERROR_INVALID_VALUE);
		if (variant != 1) {
			eq(parser_parse(p, "discovery-chance:40"), PARSE_ERROR_NONE);
			eq(parser_parse(p, "discovery-chance:40"), PARSE_ERROR_REPEATED_DIRECTIVE);
		}
		if (variant != 2) {
			eq(parser_parse(p, "discovery:test.find:2:24:85"), PARSE_ERROR_NONE);
			eq(parser_parse(p, "discovery:test.find:2:24:85"), PARSE_ERROR_REPEATED_DIRECTIVE);
		}
		eq(spelunking_population_parser.finish(p), variant ? PARSE_ERROR_TOO_FEW_ENTRIES : PARSE_ERROR_NONE);
		if (!variant) {
			profile = world_spelunk_population_profile_by_index(0); notnull(profile);
			eq(profile->discovery_chance, 40); eq(profile->discovery_entry_count, 1);
			eq(profile->discoveries[0].weight, 2);
			eq(profile->discoveries[0].minimum_depth_percent, 24);
		}
		spelunking_population_parser.cleanup();
	}
	ok;
}
static int test_guardians(void *unused)
{
	int variant, i;
	(void)unused;
	for (variant = 0; variant < 4; variant++) {
		struct parser *p = spelunking_population_parser.init();
		eq(parser_parse(p, "discovery-guardian:60:core.spelunk.actor.test"), PARSE_ERROR_MISSING_RECORD_HEADER);
		for (i = 0; i < (int)N_ELEMENTS(complete_lines); i++)
			eq(parser_parse(p, complete_lines[i]), PARSE_ERROR_NONE);
		eq(parser_parse(p, "discovery-guardian:0:core.spelunk.actor.test"), PARSE_ERROR_INVALID_VALUE);
		eq(parser_parse(p, "discovery-guardian:101:core.spelunk.actor.test"), PARSE_ERROR_INVALID_VALUE);
		eq(parser_parse(p, "discovery-guardian:60:bad id"), PARSE_ERROR_INVALID_VALUE);
		eq(parser_parse(p, variant == 2 ? "discovery-guardian:60:missing" :
			"discovery-guardian:60:core.spelunk.actor.test"), PARSE_ERROR_NONE);
		eq(parser_parse(p, "discovery-guardian:60:core.spelunk.actor.test"), PARSE_ERROR_REPEATED_DIRECTIVE);
		if (variant != 1) {
			eq(parser_parse(p, "discovery-chance:40"), PARSE_ERROR_NONE);
			eq(parser_parse(p, "discovery:test.find:2:24:85"), PARSE_ERROR_NONE);
		}
		if (variant == 3)
			eq(parser_parse(p, "actor:core.spelunk.actor.other:1:13:0:100"), PARSE_ERROR_NONE);
		eq(spelunking_population_parser.finish(p), variant == 0 ? PARSE_ERROR_NONE :
			(variant == 3 ? PARSE_ERROR_TOO_MANY_ENTRIES : PARSE_ERROR_INVALID_VALUE));
		if (!variant) {
			const struct world_spelunk_population_profile *profile = world_spelunk_population_profile_by_index(0);
			notnull(profile); eq(profile->discovery_guardian_chance, 60);
			require(streq(profile->discovery_guardian_id, "core.spelunk.actor.test"));
		}
		spelunking_population_parser.cleanup();
	}
	ok;
}

struct test tests[] = {
	{ "complete profile", test_complete_profile },
	{ "bounded optional discovery roster", test_discoveries },
	{ "optional guardian requires discovery roster member and capacity", test_guardians },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ NULL, NULL }
};
