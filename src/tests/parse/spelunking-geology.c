/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/spelunking-geology */
/* Exercise stable geology tags and fail-closed profile parsing. */

#include "unit-test.h"

#include "parser.h"
#include "world-spelunking-geology-data.h"
#include "z-color.h"

NOSETUP
NOTEARDOWN

static const char *complete_lines[] = {
	"profile:deepwell.test.limestone:1",
	"strata:4:9",
	"decoration-budget:9:18",
	"material:1:limestone:55:limestone:Weathered limestone",
	"material:2:shale:35:shale:Blue-grey shale",
	"material:3:iron-stained:10:iron-stained:Iron-stained stone",
	"decoration:1:calcite-seam:rock-face:30:4:%:Light White:Calcite seam",
	"decoration:2:stalactite:ceiling:25:5:v:Light Slate:Calcite stalactite",
	"decoration:3:stalagmite:floor:20:5:^:Slate:Calcite stalagmite",
	"decoration:4:shale-rubble:floor:15:4:*:Umber:Shale rubble",
	"decoration:5:pool-algae:water-edge:10:3:,:Light Green:Pool algae"
};

static enum parser_error parse_complete(struct parser *p)
{
	int i;

	for (i = 0; i < (int)N_ELEMENTS(complete_lines); i++) {
		enum parser_error result = parser_parse(p, complete_lines[i]);

		if (result != PARSE_ERROR_NONE) return result;
	}
	return PARSE_ERROR_NONE;
}

static int test_complete_registry(void *unused)
{
	struct parser *p = spelunking_geology_parser.init();
	const struct world_spelunk_geology_profile *profile;
	const struct world_spelunk_material_definition *material;
	const struct world_spelunk_decoration_definition *decoration;

	(void)unused;
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_geology_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_spelunk_geology_profile_count(), 1);
	profile = world_spelunk_geology_profile_by_id(
		"deepwell.test.limestone");
	notnull(profile);
	ptreq(profile, world_spelunk_geology_profile_by_index(0));
	eq(profile->version, 1);
	eq(profile->min_stratum_height, 4);
	eq(profile->max_stratum_height, 9);
	eq(profile->min_decoration_count, 9);
	eq(profile->material_count, 3);
	eq(profile->decoration_count, 5);
	material = world_spelunk_geology_material_by_tag(profile, 2);
	notnull(material);
	require(streq(material->id, "shale"));
	require(streq(material->visual_material_id, "shale"));
	decoration = world_spelunk_geology_decoration_by_tag(profile, 2);
	notnull(decoration);
	require(streq(decoration->id, "stalactite"));
	eq(decoration->placement, WORLD_SPELUNK_DECORATION_CEILING);
	eq(decoration->glyph, L'v');
	eq(decoration->attr, COLOUR_L_WHITE);
	null(world_spelunk_geology_material_by_tag(profile, 0));
	null(world_spelunk_geology_decoration_by_tag(profile, 99));
	null(world_spelunk_geology_profile_by_id("missing"));
	spelunking_geology_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete_fail_closed(void *unused)
{
	struct parser *p = spelunking_geology_parser.init();

	(void)unused;
	eq(parser_parse(p, "strata:4:9"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "profile:Bad ID:1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "profile:deepwell.test.incomplete:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "strata:4:9"), PARSE_ERROR_NONE);
	eq(spelunking_geology_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_spelunk_geology_profile_count(), 0);

	p = spelunking_geology_parser.init();
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"material:2:duplicate-tag:1:limestone:Duplicate tag"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(spelunking_geology_parser.finish(p), PARSE_ERROR_NONE);
	spelunking_geology_parser.cleanup();

	p = spelunking_geology_parser.init();
	eq(parser_parse(p, "profile:deepwell.test.bare:1"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "strata:4:9"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "decoration-budget:0:0"), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"material:1:limestone:1:limestone:Weathered limestone"),
		PARSE_ERROR_NONE);
	eq(spelunking_geology_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_spelunk_geology_profile_by_index(0)->decoration_count, 0);
	spelunking_geology_parser.cleanup();
	ok;
}

const char *suite_name = "parse/spelunking-geology";
struct test tests[] = {
	{ "complete registry", test_complete_registry },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ NULL, NULL }
};
