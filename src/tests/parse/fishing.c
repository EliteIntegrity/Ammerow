/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/fishing */
/* Exercise parsing and validation used for fishing.txt. */

#include "unit-test.h"

#include "datafile.h"
#include "fishing-data.h"
#include "parser.h"
#include "world-fishing.h"
#include "z-color.h"

NOSETUP
NOTEARDOWN

static const char *complete_lines[] = {
	"population:2:4",
	"motion:61:23",
	"evasion:47:2",
	"patience:37",
	"attraction:5:2",
	"rig:test-rod:Test Rod",
	"rig-item:tool:Test Rod",
	"rig-capabilities:7:33:12",
	"species:test-fish:Test Fish",
	"graphics:=:Light Blue",
	"depth-band:3:14",
	"behavior:17:5:6",
	"danger-bonus:4",
	"larder-value:9",
	"experience:7:3",
	"habitat:rainwater:11",
	"habitat:open-lake:12",
	"habitat:forest-pool:13",
	"habitat:marsh-pool:14",
	"habitat:ruin-cistern:15",
	"habitat:cave-pool:16"
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
	struct parser *p = fishing_parser.init();
	const struct world_fishing_rules *rules;
	const struct world_fishing_species *species;
	const struct world_fishing_rig *rig;
	world_fishing_kind kind;

	(void)unused;
	notnull(p);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(fishing_parser.finish(p), PARSE_ERROR_NONE);
	rules = world_fishing_rules();
	notnull(rules);
	eq(rules->minimum_fish, 2);
	eq(rules->maximum_fish, 4);
	eq(rules->move_chance, 61);
	eq(rules->depth_move_chance, 23);
	eq(rules->evasion_chance, 47);
	eq(rules->evasion_radius, 2);
	eq(rules->patience_turns, 37);
	eq(rules->attraction_horizontal, 5);
	eq(rules->attraction_vertical, 2);
	eq(world_fishing_species_count(), 1);
	eq(world_fishing_rig_count(), 1);
	rig = world_fishing_rig_by_id("test-rod");
	notnull(rig);
	require(streq(rig->name, "Test Rod"));
	require(streq(rig->tval_name, "tool"));
	require(streq(rig->item_name, "Test Rod"));
	eq(rig->priority, 7);
	eq(rig->maximum_reach, 33);
	eq(rig->maximum_depth, 12);
	ptreq(rig, world_fishing_rig_by_index(0));
	null(world_fishing_rig_by_id("missing"));
	kind = world_fishing_kind_by_id("test-fish");
	eq(kind, 0);
	species = world_fishing_species_by_kind(kind);
	notnull(species);
	require(streq(species->name, "Test Fish"));
	eq(species->glyph, '=');
	eq(species->attr, color_text_to_attr("Light Blue"));
	eq(species->depth_min, 3);
	eq(species->depth_max, 14);
	eq(species->run_chance, 17);
	eq(species->wind_needed, 5);
	eq(species->bite_timeout, 6);
	eq(species->danger_weight, 4);
	eq(species->larder_value, 9);
	eq(species->catch_experience, 7);
	eq(species->donation_experience, 3);
	eq(world_fishing_kind_catch_experience(kind), 7);
	eq(world_fishing_kind_donation_experience(kind), 3);
	eq(world_fishing_kind_catch_experience(WORLD_FISHING_KIND_NONE), 0);
	eq(world_fishing_kind_donation_experience(WORLD_FISHING_KIND_NONE), 0);
	eq(world_fishing_habitat_weight(WORLD_FISHING_HABITAT_OPEN_LAKE,
		3, kind), 24);
	eq(world_fishing_kind_by_id("missing"), WORLD_FISHING_KIND_NONE);
	null(world_fishing_species_by_kind(WORLD_FISHING_KIND_NONE));
	fishing_parser.cleanup();
	ok;
}

static int test_headers_duplicates_and_ranges(void *unused)
{
	struct parser *p = fishing_parser.init();

	(void)unused;
	eq(parser_parse(p, "graphics:.:Blue"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "experience:1:1"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "population:0:4"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "population:2:4"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "population:2:4"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "motion:101:0"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "motion:50:20"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "evasion:50:72"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "evasion:50:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "patience:0"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "patience:40"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "patience:41"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "attraction:0:2"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "attraction:4:17"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "attraction:4:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "attraction:5:2"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "rig-item:tool:No Header"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "rig:bad id:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "rig:test-rod:Test Rod"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rig-capabilities:1:72:4"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "rig-capabilities:1:20:4"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rig-item:tool:Test Rod"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "species:Bad ID:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "species:test-fish:Test Fish"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "species:test-fish:Duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "species:other-fish:Test Fish"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "species:other-fish:test fish"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "graphics:?:Not A Color"),
		PARSE_ERROR_INVALID_COLOR);
	eq(parser_parse(p, "graphics:\x7f:Blue"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "depth-band:0:16"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "behavior:96:1:1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "danger-bonus:-1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "larder-value:0"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "experience:-1:1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "experience:1:-1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "experience:65536:1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "experience:1:65536"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "experience:0:0"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "experience:1:1"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "habitat:unknown:1"), PARSE_ERROR_INVALID_VALUE);
	eq(fishing_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_fishing_species_count(), 0);
	ok;
}

static int test_incomplete_species_fails_closed(void *unused)
{
	struct parser *p = fishing_parser.init();

	(void)unused;
	eq(parser_parse(p, "population:1:1"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "motion:0:0"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "evasion:0:0"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "patience:40"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "attraction:4:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rig:test-rod:Test Rod"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rig-item:tool:Test Rod"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rig-capabilities:1:20:4"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "species:unfinished:Unfinished Fish"),
		PARSE_ERROR_NONE);
	eq(fishing_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	null(world_fishing_rules());
	eq(world_fishing_species_count(), 0);
	ok;
}

static int test_missing_experience_fails_closed(void *unused)
{
	struct parser *p = fishing_parser.init();
	int i;

	(void)unused;
	for (i = 0; i < (int)N_ELEMENTS(complete_lines); i++) {
		if (streq(complete_lines[i], "experience:7:3")) continue;
		eq(parser_parse(p, complete_lines[i]), PARSE_ERROR_NONE);
	}
	eq(fishing_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	null(world_fishing_rules());
	ok;
}

static int test_habitat_validation(void *unused)
{
	struct parser *p = fishing_parser.init();
	int i;

	(void)unused;
	for (i = 0; i < (int)N_ELEMENTS(complete_lines); i++) {
		const char *line = complete_lines[i];

		if (streq(line, "habitat:cave-pool:16")) {
			line = "habitat:cave-pool:0";
		}
		eq(parser_parse(p, line), PARSE_ERROR_NONE);
	}
	eq(parser_parse(p, "habitat:rainwater:4"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(fishing_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	null(world_fishing_rules());
	ok;
}

const char *suite_name = "parse/fishing";
struct test tests[] = {
	{ "complete registry", test_complete_registry },
	{ "headers duplicates and ranges", test_headers_duplicates_and_ranges },
	{ "incomplete species fails closed", test_incomplete_species_fails_closed },
	{ "habitat validation", test_habitat_validation },
	{ "missing experience fails closed", test_missing_experience_fails_closed },
	{ NULL, NULL }
};
