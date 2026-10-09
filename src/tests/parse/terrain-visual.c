/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/terrain-visual */
/* Exercise parsing and fallback used for terrain_visuals.txt. */

#include "unit-test.h"

#include "parser.h"
#include "terrain-visual-data.h"

NOSETUP
NOTEARDOWN

static enum parser_error parse_semantics(struct parser *p,
		const char *material)
{
	char line[64];
	enum parser_error result;
	const char *const names[] = { "water", "road", "wood" };
	int i;

	for (i = 0; i < (int)N_ELEMENTS(names); i++) {
		strnfmt(line, sizeof(line), "semantic:%s:%s", names[i], material);
		result = parser_parse(p, line);
		if (result != PARSE_ERROR_NONE) return result;
	}
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_recipe(struct parser *p, const char *profile,
		const char *material, const char *base)
{
	char line[128];
	enum parser_error result;

	strnfmt(line, sizeof(line), "profile:%s", profile);
	if ((result = parser_parse(p, line)) != PARSE_ERROR_NONE) return result;
	strnfmt(line, sizeof(line), "material:%s", material);
	if ((result = parser_parse(p, line)) != PARSE_ERROR_NONE) return result;
	strnfmt(line, sizeof(line), "base:%s", base);
	return parser_parse(p, line);
}

static int test_registry_and_profile_fallback(void *unused)
{
	struct parser *p = terrain_visual_parser.init();
	const struct terrain_visual_recipe *recipe;

	(void)unused;
	notnull(p);
	eq(parse_semantics(p, "floor"), PARSE_ERROR_NONE);
	eq(parse_recipe(p, "default", "floor", "90:100:110"),
		PARSE_ERROR_NONE);
	eq(parse_recipe(p, "meadow", "floor", "70:130:60"),
		PARSE_ERROR_NONE);
	eq(terrain_visual_parser.finish(p), PARSE_ERROR_NONE);
	recipe = terrain_visual_recipe_for("meadow", "floor");
	notnull(recipe);
	require(streq(recipe->profile, "meadow"));
	eq(recipe->base.g, 130);
	recipe = terrain_visual_recipe_for("unknown", "floor");
	notnull(recipe);
	require(streq(recipe->profile, "default"));
	eq(recipe->base.r, 90);
	require(terrain_visual_material_is_known("floor"));
	require(!terrain_visual_material_is_known("missing"));
	require(streq(terrain_visual_material_for_semantic(
		TERRAIN_VISUAL_SEMANTIC_WATER), "floor"));
	terrain_visual_parser.cleanup();
	ok;
}

static int test_headers_duplicates_and_ranges(void *unused)
{
	struct parser *p = terrain_visual_parser.init();

	(void)unused;
	eq(parser_parse(p, "semantic:bog:floor"), PARSE_ERROR_INVALID_VALUE);
	eq(parse_semantics(p, "floor"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "semantic:water:floor"),
		PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "material:floor"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "profile:Bad_ID"), PARSE_ERROR_INVALID_VALUE);
	/* Profiles are authored environment IDs rather than a renderer-owned
	 * enumeration of outdoor biomes. */
	eq(parser_parse(p, "profile:unknown-biome"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "material:floor"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "base:2:3:4"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "profile:default"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "material:floor"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "material:floor"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "base:256:0:0"), PARSE_ERROR_OUT_OF_BOUNDS);
	eq(parser_parse(p, "base:1:2:3"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "base:1:2:3"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "tile:open-floor"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "tile:other-floor"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(terrain_visual_parser.finish(p), PARSE_ERROR_NONE);
	require(terrain_visual_profile_is_known("unknown-biome"));
	require(!terrain_visual_profile_is_known("missing-profile"));
	terrain_visual_parser.cleanup();
	ok;
}

static int test_override_requires_default_material(void *unused)
{
	struct parser *p = terrain_visual_parser.init();

	(void)unused;
	eq(parse_semantics(p, "floor"), PARSE_ERROR_NONE);
	eq(parse_recipe(p, "meadow", "floor", "70:130:60"),
		PARSE_ERROR_NONE);
	eq(terrain_visual_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	ok;
}

const char *suite_name = "parse/terrain-visual";
struct test tests[] = {
	{ "registry and fallback", test_registry_and_profile_fallback },
	{ "headers duplicates and ranges", test_headers_duplicates_and_ranges },
	{ "override requires default", test_override_requires_default_material },
	{ NULL, NULL }
};
