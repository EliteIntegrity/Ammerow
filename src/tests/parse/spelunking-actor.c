/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/spelunking-actor */
/* Exercise parsing and validation used for spelunking_actor.txt. */

#include "unit-test.h"

#include "datafile.h"
#include "parser.h"
#include "world-spelunking-actor-data.h"
#include "z-color.h"

NOSETUP
NOTEARDOWN

static const char *complete_lines[] = {
	"actor:core.spelunk.actor.test:test crawler",
	"death-cause:a test crawler",
	"appearance:c:Light Blue",
	"combat:7:31:2:3",
	"vitals:12:115:9",
	"spawn:core.spelunk.spawn.test:core.spelunk.test.001:core.spelunk.actor.test",
	"candidate:30:7:5",
	"candidate:10:3:4",
	"candidate:20:5:4"
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
	struct parser *p = spelunking_actor_parser.init();
	const struct world_spelunk_actor_definition *actor;
	const struct world_spelunk_spawn_definition *spawn;

	(void)unused;
	notnull(p);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_actor_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_spelunk_actor_definition_count(), 1);
	eq(world_spelunk_spawn_definition_count(), 1);
	actor = world_spelunk_actor_by_id("core.spelunk.actor.test");
	notnull(actor);
	ptreq(actor, world_spelunk_actor_by_index(0));
	require(streq(actor->name, "test crawler"));
	require(streq(actor->death_cause, "a test crawler"));
	eq(actor->glyph, L'c');
	eq(actor->attr, COLOUR_L_BLUE);
	eq(actor->hitpoints, 12);
	eq(actor->armour, 7);
	eq(actor->to_hit, 31);
	eq(actor->damage_dice, 2);
	eq(actor->damage_sides, 3);
	eq(actor->speed, 115);
	eq(actor->experience, 9);
	spawn = world_spelunk_spawn_by_id("core.spelunk.spawn.test");
	notnull(spawn);
	ptreq(spawn, world_spelunk_spawn_by_index(0));
	ptreq(spawn->actor, actor);
	require(streq(spawn->location_id, "core.spelunk.test.001"));
	eq(spawn->candidate_count, 3);
	eq(spawn->candidates[0].priority, 10);
	eq(spawn->candidates[0].x, 3);
	eq(spawn->candidates[1].priority, 20);
	eq(spawn->candidates[2].priority, 30);
	null(world_spelunk_actor_by_id("missing"));
	null(world_spelunk_spawn_by_id("missing"));
	spelunking_actor_parser.cleanup();
	ok;
}

static int test_headers_duplicates_and_ranges(void *unused)
{
	struct parser *p = spelunking_actor_parser.init();

	(void)unused;
	eq(parser_parse(p, "combat:1:1:1:1"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "actor:Bad ID:bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "actor:core.actor.test:test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "actor:core.actor.test:duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "death-cause:a test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "death-cause:a duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "appearance:c:Not A Colour"),
		PARSE_ERROR_INVALID_COLOR);
	eq(parser_parse(p, "appearance:c:Light Green"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "appearance:x:Blue"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "combat:-1:20:1:2"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "combat:1:20:1:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "vitals:0:110:1"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "vitals:4:110:1"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "candidate:1:1:1"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p,
		"spawn:core.spawn.test:core.place.test:core.actor.test"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "candidate:10:2:3"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "candidate:10:4:3"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "candidate:20:2:3"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(spelunking_actor_parser.finish(p), PARSE_ERROR_NONE);
	spelunking_actor_parser.cleanup();
	ok;
}

static int test_incomplete_and_unknown_actor_fail_closed(void *unused)
{
	struct parser *p = spelunking_actor_parser.init();

	(void)unused;
	eq(parser_parse(p, "actor:core.actor.test:test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "death-cause:a test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "appearance:t:White"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "combat:1:20:1:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"spawn:core.spawn.test:core.place.test:core.actor.missing"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "candidate:1:2:3"), PARSE_ERROR_NONE);
	eq(spelunking_actor_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_spelunk_actor_definition_count(), 0);
	p = spelunking_actor_parser.init();
	eq(parse_complete(p), PARSE_ERROR_NONE);
	/* Replace the referenced actor after parsing by using a second bad spawn. */
	eq(parser_parse(p,
		"spawn:core.spelunk.spawn.bad:core.spelunk.test.001:core.actor.missing"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "candidate:1:1:1"), PARSE_ERROR_NONE);
	eq(spelunking_actor_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	eq(world_spelunk_actor_definition_count(), 0);
	ok;
}

const char *suite_name = "parse/spelunking-actor";
struct test tests[] = {
	{ "complete registry", test_complete_registry },
	{ "headers duplicates and ranges", test_headers_duplicates_and_ranges },
	{ "incomplete and unknown actor fail closed",
		test_incomplete_and_unknown_actor_fail_closed },
	{ NULL, NULL }
};
