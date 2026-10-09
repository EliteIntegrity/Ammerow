/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/story */
/* Exercise the immutable data-authored story-scene registry. */

#include "unit-test.h"

#include "datafile.h"
#include "parser.h"
#include "world-story-data.h"

NOSETUP
NOTEARDOWN

static int test_complete_scene(void *unused)
{
	struct parser *p = story_parser.init();
	const struct world_story_scene *scene;

	(void)unused;
	notnull(p);
	eq(parser_parse(p, "scene:first:First sounding"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "subtitle:The rain tastes of iron."),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "history:Began the test sounding."),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "victory-summary:Secured the test watershed."),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "body:Find the source."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "body:Return with clean water."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "footer:Press any key."), PARSE_ERROR_NONE);
	eq(story_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_story_scene_count(), 1);
	scene = world_story_scene_by_id("first");
	notnull(scene);
	require(streq(scene->title, "First sounding"));
	require(streq(scene->history, "Began the test sounding."));
	require(streq(scene->victory_summary,
		"Secured the test watershed."));
	require(streq(world_story_victory_summary(),
		"Secured the test watershed."));
	require(strstr(scene->body, "Find the source.\n\nReturn") != NULL);
	null(world_story_scene_by_id("missing"));
	story_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete(void *unused)
{
	struct parser *p = story_parser.init();

	(void)unused;
	eq(parser_parse(p, "body:No header"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "scene:Bad_ID:Bad"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "scene:valid:Valid"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "scene:valid:Duplicate"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(story_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_story_scene_count(), 0);
	ok;
}

static int test_single_victory_summary(void *unused)
{
	struct parser *p = story_parser.init();

	(void)unused;
	eq(parser_parse(p, "scene:first:First"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "body:First body."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "victory-summary:First outcome."),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "scene:second:Second"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "body:Second body."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "victory-summary:Second outcome."),
		PARSE_ERROR_NONE);
	eq(story_parser.finish(p), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(world_story_scene_count(), 0);
	ok;
}

static int test_intro_order_and_roles(void *unused)
{
	struct parser *p = story_parser.init();
	(void)unused;
	eq(parser_parse(p, "intro:1:prose"), PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "scene:people:People"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "body:Regional origins."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "intro:0:prose"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "intro:7:prose"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "intro:1:unknown"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "intro:2:origins"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "intro:2:origins"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "scene:today:Today"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "body:An ordinary beginning."), PARSE_ERROR_NONE);
	eq(parser_parse(p, "intro:1:prose"), PARSE_ERROR_NONE);
	eq(story_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_story_intro_count(), 2);
	require(streq(world_story_intro_at(0)->id, "today"));
	eq(world_story_intro_at(1)->intro_content, WORLD_STORY_INTRO_ORIGINS);
	null(world_story_intro_at(-1));
	null(world_story_intro_at(2));
	story_parser.cleanup();
	ok;
}

static int test_intro_invalid_sequences(void *unused)
{
	(void)unused;
	for (int i = 0; i < 3; ++i) {
		struct parser *p = story_parser.init();
		eq(parser_parse(p, "scene:first:First"), PARSE_ERROR_NONE);
		eq(parser_parse(p, "body:First body."), PARSE_ERROR_NONE);
		eq(parser_parse(p, i == 0 ? "intro:2:prose" : "intro:1:prose"), PARSE_ERROR_NONE);
		if (i == 1) {
			eq(parser_parse(p, "scene:duplicate:Duplicate"), PARSE_ERROR_NONE);
			eq(parser_parse(p, "body:Duplicate order."), PARSE_ERROR_NONE);
			eq(parser_parse(p, "intro:1:prose"), PARSE_ERROR_NONE);
		} else if (i == 2) {
			eq(parser_parse(p, "history:Must not record a run event."), PARSE_ERROR_NONE);
		}
		eq(story_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
		eq(world_story_intro_count(), 0);
	}
	ok;
}

const char *suite_name = "parse/story";
struct test tests[] = {
	{ "complete scene", test_complete_scene },
	{ "invalid and incomplete", test_invalid_and_incomplete },
	{ "single victory summary", test_single_victory_summary },
	{ "intro order and roles", test_intro_order_and_roles },
	{ "intro invalid sequences", test_intro_invalid_sequences },
	{ NULL, NULL }
};
