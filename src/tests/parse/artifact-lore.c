/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/artifact-lore */

#include "unit-test.h"

#include "artifact-lore-data.h"
#include "datafile.h"
#include "parser.h"
#include "z-rand.h"

NOSETUP
NOTEARDOWN

static enum parser_error parse_complete(struct parser *p)
{
	static const char *header[] = {
		"origin:ammerow-original",
		"review:release",
		"pool:place",
		"value:Dry Basin",
		"value:Quiet Shaft",
		"value:Red Cistern"
	};
	static const char *tags[] = {
		"any", "weapon", "armor", "jewelry", "ranged", "speed",
		"stealth", "watch", "element", "light", "digging", "curse"
	};
	int i;

	for (i = 0; i < (int)N_ELEMENTS(header); i++) {
		enum parser_error result = parser_parse(p, header[i]);

		if (result != PARSE_ERROR_NONE) return result;
	}
	for (i = 0; i < (int)N_ELEMENTS(tags); i++) {
		char line[128];

		strnfmt(line, sizeof(line), "frame:test-%s", tags[i]);
		if (parser_parse(p, line)) return PARSE_ERROR_GENERIC;
		if (parser_parse(p, "tone:practical")) return PARSE_ERROR_GENERIC;
		if (parser_parse(p, "weight:1")) return PARSE_ERROR_GENERIC;
		strnfmt(line, sizeof(line), "tag:%s", tags[i]);
		if (parser_parse(p, line)) return PARSE_ERROR_GENERIC;
		strnfmt(line, sizeof(line), "name:{place} %s", tags[i]);
		if (parser_parse(p, line)) return PARSE_ERROR_GENERIC;
		if (parser_parse(p, "text:A {kind} from {place}.")) {
			return PARSE_ERROR_GENERIC;
		}
	}
	return PARSE_ERROR_NONE;
}

static int test_complete_reviewed_registry(void *unused)
{
	struct parser *p = artifact_lore_parser.init();
	struct artifact art = { 0 };
	char *name = NULL;
	char *description = NULL;

	(void)unused;
	notnull(p);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(artifact_lore_parser.finish(p), PARSE_ERROR_NONE);
	require(artifact_lore_data_is_loaded());
	require(streq(artifact_lore_origin(), "ammerow-original"));
	require(streq(artifact_lore_review(), "release"));
	eq(artifact_lore_pool_count(), 1);
	eq(artifact_lore_frame_count(), ARTIFACT_LORE_TAG_MAX);

	art.tval = TV_BOOTS;
	art.modifiers[OBJ_MOD_SPEED] = 2;
	eq(artifact_lore_tag_for_artifact(&art), ARTIFACT_LORE_SPEED);
	Rand_begin_deterministic(12345);
	require(artifact_lore_generate(&art, NULL, &name, &description));
	Rand_end_deterministic();
	notnull(strstr(name, "speed"));
	notnull(strstr(description, "boots"));
	string_free(name);
	string_free(description);
	artifact_lore_parser.cleanup();
	ok;
}

static int test_policy_and_templates_fail_closed(void *unused)
{
	struct parser *p = artifact_lore_parser.init();

	(void)unused;
	eq(parser_parse(p, "pool:place"), PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "origin:legacy-derived"), PARSE_ERROR_INVALID_VALUE);
	parser_destroy(p);

	p = artifact_lore_parser.init();
	eq(parser_parse(p, "origin:ammerow-original"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "review:release"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "pool:place"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "value:One"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "value:Two"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "value:Three"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "frame:bad-frame"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "tone:practical"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "weight:1"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "tag:any"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:{unreviewed} object"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "text:Text."), PARSE_ERROR_NONE);
	eq(artifact_lore_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	require(!artifact_lore_data_is_loaded());
	ok;
}

const char *suite_name = "parse/artifact-lore";
struct test tests[] = {
	{ "complete reviewed registry", test_complete_reviewed_registry },
	{ "policy and templates fail closed", test_policy_and_templates_fail_closed },
	{ NULL, NULL }
};
