/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/spelunking-system */
/* Exercise topology parsing and deterministic connected-system planning. */

#include "unit-test.h"

#include "parser.h"
#include "world-spelunking-generation-system.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-system-data.h"
#include "z-rand.h"

#include <string.h>

NOSETUP
NOTEARDOWN

static const char *system_lines[] = {
	"system:deepwell.test:1",
	"name:Test Deepwell",
	"description:A bounded test system.",
	"root:deepwell.test.root",
	"location:core.spelunk.test.001",
	"portal:deepwell.test.surface:shaft.surface:root:2:18:upper-rail:open",
	"portal:deepwell.test.practice:shaft.practice:root:2:18:upper-rail:open",
	"sections:6:9",
	"shape:2:3:3:3:5"
};

static const char *recipe_lines[] = {
	"recipe:deepwell.test.root:deepwell.test:root:100:1:90",
	"name:Test Root",
	"description:The root section.",
	"size:64:72:56:68",
	"macro:chamber-chain",
	"route:5:7:4:8:2:4:2:4:6:2:4:7:9",
	"rules:2:10:10:5:15:20:1:6:10:2",
	"stamina-costs:8:5:3:3:2:1",
	"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark",
	"water:deepwell.test.water",
	"material:deepwell.test.geology",
	"traversal:deepwell.test.launch",
	"population:deepwell.test.upper",
	"passage:down:60:96:3",
	"landmark:deepwell.test.object:object:20:65:tool:Yew Longline Rod",
	"landmark:deepwell.test.fishing:fishing-stance:60:94:none:-",
	"recipe:deepwell.test.gallery:deepwell.test:optional:70:1:90",
	"name:Test Gallery",
	"description:An optional gallery.",
	"size:64:72:56:68",
	"macro:chamber-chain",
	"route:5:7:4:8:2:4:2:4:6:2:4:7:9",
	"rules:2:10:10:5:15:20:1:6:10:2",
	"stamina-costs:8:5:3:3:2:1",
	"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark",
	"water:deepwell.test.water",
	"material:deepwell.test.geology",
	"traversal:deepwell.test.expedition",
	"population:deepwell.test.gallery",
	"passage:up:5:42:1",
	"passage:down:60:96:3",
	"recipe:deepwell.test.fault:deepwell.test:deep:30:1:90",
	"name:Test Fault",
	"description:A deep fault.",
	"size:64:72:56:68",
	"macro:chamber-chain",
	"route:5:7:4:8:2:4:2:4:6:2:4:7:9",
	"rules:2:10:10:5:15:20:1:6:10:2",
	"stamina-costs:8:5:3:3:2:1",
	"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark",
	"water:deepwell.test.water",
	"material:deepwell.test.geology",
	"traversal:deepwell.test.expedition",
	"population:deepwell.test.fault",
	"passage:up:5:42:1"
};

static enum parser_error parse_lines(struct parser *p,
		const char *const *lines, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		enum parser_error result = parser_parse(p, lines[i]);

		if (result != PARSE_ERROR_NONE) return result;
	}
	return PARSE_ERROR_NONE;
}

static enum parser_error load_recipes(void)
{
	struct parser *p = spelunking_recipe_parser.init();
	enum parser_error result = parse_lines(p, recipe_lines,
		N_ELEMENTS(recipe_lines));

	if (result != PARSE_ERROR_NONE) {
		spelunking_recipe_parser.finish(p);
		return result;
	}
	return spelunking_recipe_parser.finish(p);
}

static enum parser_error load_system(void)
{
	struct parser *p = spelunking_system_parser.init();
	enum parser_error result = parse_lines(p, system_lines,
		N_ELEMENTS(system_lines));

	if (result != PARSE_ERROR_NONE) {
		spelunking_system_parser.finish(p);
		return result;
	}
	return spelunking_system_parser.finish(p);
}

static int test_complete_registry_and_plans(void *unused)
{
	const struct world_spelunk_system_profile *profile;
	struct world_spelunk_system_plan first;
	struct world_spelunk_system_plan second;
	struct world_spelunk_system_plan different;
	struct randomizer_state gameplay_before;
	struct randomizer_state gameplay_after;
	uint32_t seed;

	(void)unused;
	eq(load_recipes(), PARSE_ERROR_NONE);
	eq(load_system(), PARSE_ERROR_NONE);
	eq(world_spelunk_system_profile_count(), 1);
	profile = world_spelunk_system_profile_by_id("deepwell.test");
	notnull(profile);
	ptreq(profile, world_spelunk_system_profile_by_index(0));
	ptreq(profile, world_spelunk_system_profile_by_location(
		"core.spelunk.test.001"));
	null(world_spelunk_system_profile_by_location("core.spelunk.missing"));
	require(streq(profile->name, "Test Deepwell"));
	require(streq(profile->root_recipe_id, "deepwell.test.root"));
	require(streq(profile->location_id, "core.spelunk.test.001"));
	eq(profile->portal_count, 2);
	require(streq(profile->portals[0].id,
		"deepwell.test.surface"));
	require(streq(profile->portals[1].entry_id, "shaft.practice"));
	eq(profile->portals[0].min_depth_percent, 2);
	eq(profile->portals[0].anchor, WORLD_SPELUNK_PORTAL_UPPER_RAIL);
	eq(profile->min_sections, 6);
	eq(profile->max_depth, 5);
	require(world_spelunk_system_data_validate_recipes());
	Rand_state_init(0xabcdef01U);
	Rand_state_export(&gameplay_before);
	eq(world_spelunk_plan_system(profile, 0x12345678U, &first),
		WORLD_SPELUNK_SYSTEM_PLAN_OK);
	Rand_state_export(&gameplay_after);
	require(memcmp(&gameplay_before, &gameplay_after,
		sizeof(gameplay_before)) == 0);
	eq(world_spelunk_plan_system(profile, 0x12345678U, &second),
		WORLD_SPELUNK_SYSTEM_PLAN_OK);
	require(memcmp(&first, &second, sizeof(first)) == 0);
	require(world_spelunk_system_plan_is_valid(&first, profile));
	require(first.node_count >= 6 && first.node_count <= 9);
	eq(first.edge_count, first.node_count - 1);
	require(streq(first.nodes[0].recipe_id, "deepwell.test.root"));
	eq(world_spelunk_plan_system(profile, 0x12345679U, &different),
		WORLD_SPELUNK_SYSTEM_PLAN_OK);
	require(memcmp(&first, &different, sizeof(first)) != 0);
	for (seed = 0; seed < 128; seed++) {
		struct world_spelunk_system_plan plan;

		eq(world_spelunk_plan_system(profile, seed, &plan),
			WORLD_SPELUNK_SYSTEM_PLAN_OK);
		require(world_spelunk_system_plan_is_valid(&plan, profile));
	}
	spelunking_system_parser.cleanup();
	spelunking_recipe_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete_fail_closed(void *unused)
{
	struct parser *p = spelunking_system_parser.init();

	(void)unused;
	eq(parser_parse(p, "sections:6:9"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "system:Bad ID:1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p,
		"system:abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz:1"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "system:deepwell.test:1"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:Incomplete"), PARSE_ERROR_NONE);
	eq(spelunking_system_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_spelunk_system_profile_count(), 0);

	p = spelunking_system_parser.init();
	eq(parse_lines(p, system_lines, N_ELEMENTS(system_lines)),
		PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"portal:deepwell.test.other:shaft.surface:root:5:20:upper-rail:open"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "sections:7:9"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(spelunking_system_parser.finish(p), PARSE_ERROR_NONE);
	spelunking_system_parser.cleanup();
	ok;
}

const char *suite_name = "parse/spelunking-system";
struct test tests[] = {
	{ "complete registry and plans", test_complete_registry_and_plans },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ NULL, NULL }
};
