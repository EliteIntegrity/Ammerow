/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/spelunking-layout */
/* Exercise revisioned parsing and application for spelunking_layout.txt. */

#include "unit-test.h"

#include "datafile.h"
#include "parser.h"
#include "world-spelunking-layout-data.h"
#include "z-color.h"

NOSETUP
NOTEARDOWN

static const char *complete_lines[] = {
	"layout:core.spelunk.test.001:8:8:75:2",
	"rules:3:11:9:4:16:18:2:7:12:3",
	"stamina-costs:8:4:3:4:2:1",
	"perception:9:6:2:17:26:4:2:25:.:Slate:.:Light Dark",
	"fill:rock",
	"rectangle:0:20:any:rock:0:5:7:7",
	"rectangle:0:10:any:air:1:1:6:4",
	"rectangle:1:10:rock:air:3:5:4:5",
	"rectangle:1:20:air:water:5:3:6:3",
	"object:2:10:core.spelunk.object.test:2:4:tool:Test Rod"
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

static int test_complete_registry_and_application(void *unused)
{
	struct parser *p = spelunking_layout_parser.init();
	const struct world_spelunk_layout_definition *definition;
	struct world_spelunk_rules rules;
	enum world_spelunk_tile cells[64];

	(void)unused;
	notnull(p);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_layout_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_spelunk_layout_definition_count(), 1);
	definition = world_spelunk_layout_definition_by_id(
		"core.spelunk.test.001");
	notnull(definition);
	ptreq(definition, world_spelunk_layout_definition_by_index(0));
	eq(definition->width, 8);
	eq(definition->height, 8);
	eq(definition->initial_stamina, 75);
	eq(definition->current_revision, 2);
	eq(definition->perception.sight_horizontal_reach, 9);
	eq(definition->perception.sight_upward_reach, 6);
	eq(definition->perception.sight_downward_reach, 2);
	eq(definition->perception.peek_reach, 17);
	eq(definition->perception.downward_peek_reach, 26);
	eq(definition->perception.peek_lateral_reach, 4);
	eq(definition->perception.peek_close_reach, 2);
	eq(definition->perception.remembered_brightness_percent, 25);
	eq(definition->perception.visible_air_glyph, L'.');
	eq(definition->perception.visible_air_attr, COLOUR_SLATE);
	eq(definition->perception.remembered_air_glyph, L'.');
	eq(definition->perception.remembered_air_attr, COLOUR_L_DARK);
	eq(definition->operation_count, 4);
	eq(definition->object_count, 1);
	require(streq(definition->objects[0].id,
		"core.spelunk.object.test"));
	eq(definition->objects[0].revision, 2);
	eq(definition->objects[0].x, 2);
	require(streq(definition->objects[0].item_name, "Test Rod"));
	/* File order is irrelevant; explicit revision and priority are authority. */
	eq(definition->operations[0].revision, 0);
	eq(definition->operations[0].priority, 10);
	eq(definition->operations[2].revision, 1);
	require(world_spelunk_layout_rules(definition, 120, &rules));
	eq(rules.action_energy, 120U);
	eq(rules.safe_fall_tiles, 3);
	eq(rules.grip_up_stamina_cost, 8);
	eq(rules.grip_lateral_stamina_cost, 4);
	eq(rules.grip_down_stamina_cost, 3);
	eq(rules.rope_up_stamina_cost, 4);
	eq(rules.rope_lateral_stamina_cost, 2);
	eq(rules.rope_down_stamina_cost, 1);
	eq(rules.breath_turns, 7);
	eq(rules.swim_turn_stamina_cost, 3);
	require(world_spelunk_layout_build_cells(definition, cells, 8, 8));
	eq(cells[1 * 8 + 1], WORLD_SPELUNK_AIR);
	eq(cells[5 * 8 + 3], WORLD_SPELUNK_AIR);
	eq(cells[3 * 8 + 5], WORLD_SPELUNK_WATER);
	eq(cells[7 * 8 + 7], WORLD_SPELUNK_ROCK);
	null(world_spelunk_layout_definition_by_id("missing"));
	spelunking_layout_parser.cleanup();
	ok;
}

static int test_append_only_migration_preserves_edits(void *unused)
{
	struct parser *p = spelunking_layout_parser.init();
	const struct world_spelunk_layout_definition *definition;
	enum world_spelunk_tile cells[64];
	int i;

	(void)unused;
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_layout_parser.finish(p), PARSE_ERROR_NONE);
	definition = world_spelunk_layout_definition_by_index(0);
	notnull(definition);
	for (i = 0; i < 64; i++) cells[i] = WORLD_SPELUNK_ROCK;
	/* Revision zero had this gallery.  A later rock edit in the water bounds
	 * and a water edit in the carve bounds must both survive matching. */
	for (i = 1; i <= 4; i++) {
		int x;
		for (x = 1; x <= 6; x++) cells[i * 8 + x] = WORLD_SPELUNK_AIR;
	}
	cells[3 * 8 + 5] = WORLD_SPELUNK_ROCK;
	cells[5 * 8 + 3] = WORLD_SPELUNK_WATER;
	require(world_spelunk_layout_migrate_cells(definition, cells, 8, 8, 0));
	eq(cells[3 * 8 + 5], WORLD_SPELUNK_ROCK);
	eq(cells[5 * 8 + 3], WORLD_SPELUNK_WATER);
	eq(cells[5 * 8 + 4], WORLD_SPELUNK_AIR);
	require(!world_spelunk_layout_migrate_cells(definition, cells, 8, 8, 3));
	spelunking_layout_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete_fail_closed(void *unused)
{
	struct parser *p = spelunking_layout_parser.init();

	(void)unused;
	eq(parser_parse(p, "rules:1:1:1:1:1:1:1:1:1:1"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p,
		"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "layout:Bad ID:8:8:50:0"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "layout:core.layout.test:8:8:50:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "fill:rock"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "rules:2:10:10:5:15:20:1:6:10:2"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "stamina-costs:8:5:3:3:2:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "rectangle:0:10:any:air:1:1:6:4"),
		PARSE_ERROR_NONE);
	/* Revisions cannot erase player work and every declared revision is real. */
	eq(parser_parse(p, "rectangle:1:10:any:rock:1:1:2:2"),
		PARSE_ERROR_INVALID_VALUE);
	eq(spelunking_layout_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_spelunk_layout_definition_count(), 0);

	p = spelunking_layout_parser.init();
	eq(parser_parse(p, "layout:core.layout.test:8:8:50:0"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "fill:rock"), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "rectangle:0:10:any:air:1:1:6:4"),
		PARSE_ERROR_NONE);
	eq(spelunking_layout_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	ok;
}

const char *suite_name = "parse/spelunking-layout";
struct test tests[] = {
	{ "complete registry and application", test_complete_registry_and_application },
	{ "append-only migration preserves edits",
		test_append_only_migration_preserves_edits },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ NULL, NULL }
};
