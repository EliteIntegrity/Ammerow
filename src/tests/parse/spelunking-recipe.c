/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* parse/spelunking-recipe */
/* Exercise procedural recipe parsing and deterministic candidate generation. */

#include "unit-test.h"

#include "parser.h"
#include "world-spelunking-generation-endpoint.h"
#include "world-spelunking-generation-geology.h"
#include "world-spelunking-generation.h"
#include "world-spelunking-geology-data.h"
#include "world-spelunking-population-data.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-traversal-data.h"
#include "world-spelunking-traversal-proof.h"
#include "world-spelunking-water-data.h"
#include "z-color.h"
#include "z-rand.h"

#include <string.h>

NOSETUP
NOTEARDOWN

static const char *complete_lines[] = {
	"recipe:deepwell.test.root:deepwell.test:root:75:1:90",
	"name:Test Upper Reaches",
	"description:A deterministic test cave.",
	"size:64:72:56:68",
	"macro:chamber-chain",
	"route:5:7:4:8:2:4:2:4:6:2:4:7:9",
	"rules:2:10:10:5:15:20:1:6:10:2",
	"stamina-costs:8:5:3:3:2:1",
	"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark",
	"water:deepwell.test.lower-basin",
	"material:deepwell.test.limestone",
	"traversal:deepwell.test.launch",
	"population:deepwell.test.sparse",
	"passage:down:60:96:3",
	"landmark:deepwell.test.longline:object:20:65:tool:Yew Longline Rod",
	"landmark:deepwell.test.fishing:fishing-stance:60:94:none:-"
};

static const char *complete_water_lines[] = {
	"profile:deepwell.test.lower-basin:lower-basin:1",
	"surface:70:84",
	"width:20:36",
	"depth:3:6",
	"shore:4"
};

static const char *complete_geology_lines[] = {
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

static const char *complete_traversal_lines[] = {
	"profile:deepwell.test.launch:1",
	"name:Test launch route",
	"description:A deterministic traversal contract.",
	"equipment:4:24",
	"rope-shafts:1",
	"safety:0:0",
	"return:1"
};

static const char *complete_population_lines[] = {
	"profile:deepwell.test.sparse:1",
	"name:Test sparse life",
	"description:A deterministic test population.",
	"exclusion:5:3:4",
	"actor:core.spelunk.actor.test:1:2:20:85"
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

static enum parser_error load_complete_water(void)
{
	struct parser *p = spelunking_water_parser.init();
	int i;

	for (i = 0; i < (int)N_ELEMENTS(complete_water_lines); i++) {
		enum parser_error result = parser_parse(p, complete_water_lines[i]);

		if (result != PARSE_ERROR_NONE) {
			spelunking_water_parser.finish(p);
			return result;
		}
	}
	return spelunking_water_parser.finish(p);
}

static enum parser_error load_complete_geology(void)
{
	struct parser *p = spelunking_geology_parser.init();
	int i;

	for (i = 0; i < (int)N_ELEMENTS(complete_geology_lines); i++) {
		enum parser_error result = parser_parse(p, complete_geology_lines[i]);

		if (result != PARSE_ERROR_NONE) {
			spelunking_geology_parser.finish(p);
			return result;
		}
	}
	return spelunking_geology_parser.finish(p);
}

static enum parser_error load_complete_traversal(void)
{
	struct parser *p = spelunking_traversal_parser.init();
	int i;

	for (i = 0; i < (int)N_ELEMENTS(complete_traversal_lines); i++) {
		enum parser_error result = parser_parse(p,
			complete_traversal_lines[i]);

		if (result != PARSE_ERROR_NONE) {
			spelunking_traversal_parser.finish(p);
			return result;
		}
	}
	return spelunking_traversal_parser.finish(p);
}

static enum parser_error load_complete_population(void)
{
	struct parser *p = spelunking_population_parser.init();
	int i;

	for (i = 0; i < (int)N_ELEMENTS(complete_population_lines); i++) {
		enum parser_error result = parser_parse(p,
			complete_population_lines[i]);

		if (result != PARSE_ERROR_NONE) {
			spelunking_population_parser.finish(p);
			return result;
		}
	}
	return spelunking_population_parser.finish(p);
}

static int test_complete_registry(void *unused)
{
	struct parser *p = spelunking_recipe_parser.init();
	const struct world_spelunk_recipe_definition *recipe;
	struct world_spelunk_rules rules;

	(void)unused;
	notnull(p);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_recipe_parser.finish(p), PARSE_ERROR_NONE);
	eq(world_spelunk_recipe_definition_count(), 1);
	recipe = world_spelunk_recipe_definition_by_id("deepwell.test.root");
	notnull(recipe);
	ptreq(recipe, world_spelunk_recipe_definition_by_index(0));
	require(streq(recipe->system_id, "deepwell.test"));
	require(streq(recipe->name, "Test Upper Reaches"));
	require(streq(recipe->water_profile_id,
		"deepwell.test.lower-basin"));
	require(streq(recipe->traversal_profile_id,
		"deepwell.test.launch"));
	require(streq(recipe->population_profile_id,
		"deepwell.test.sparse"));
	eq(recipe->role, WORLD_SPELUNK_RECIPE_ROOT);
	eq(recipe->macro_family, WORLD_SPELUNK_MACRO_CHAMBER_CHAIN);
	eq(recipe->weight, 75);
	eq(recipe->generator_version, 1);
	eq(recipe->size.min_width, 64);
	eq(recipe->route.max_chambers, 7);
	eq(recipe->perception.visible_air_attr, COLOUR_SLATE);
	eq(recipe->perception.sight_horizontal_reach, 10);
	eq(recipe->perception.sight_upward_reach, 6);
	eq(recipe->perception.sight_downward_reach, 2);
	eq(recipe->perception.peek_close_reach, 2);
	eq(recipe->perception.remembered_brightness_percent, 24);
	eq(recipe->landmark_count, 2);
	eq(recipe->passages[WORLD_SPELUNK_PASSAGE_DOWN].min_depth_percent,
		60);
	eq(recipe->passages[WORLD_SPELUNK_PASSAGE_DOWN].max_depth_percent,
		96);
	eq(recipe->passages[WORLD_SPELUNK_PASSAGE_DOWN].capacity, 3);
	eq(recipe->passages[WORLD_SPELUNK_PASSAGE_UP].capacity, 0);
	require(streq(recipe->landmarks[0].object_sval,
		"Yew Longline Rod"));
	require(world_spelunk_recipe_rules(recipe, 120, &rules));
	eq(rules.action_energy, 120U);
	eq(rules.rope_max_length, 20);
	eq(rules.swim_turn_stamina_cost, 2);
	null(world_spelunk_recipe_definition_by_id("missing"));
	spelunking_recipe_parser.cleanup();
	ok;
}

static int test_generation_is_deterministic_and_bounded(void *unused)
{
	struct parser *p = spelunking_recipe_parser.init();
	const struct world_spelunk_recipe_definition *recipe;
	struct world_spelunk_generated_layout first;
	struct world_spelunk_generated_layout second;
	struct world_spelunk_generated_layout different;
	struct randomizer_state gameplay_before;
	struct randomizer_state gameplay_after;
	struct world_spelunk_traversal_witness witness;
	struct world_spelunk_proof_report proof_report;
	const struct world_spelunk_traversal_profile *traversal_profile;
	size_t bytes;
	bool differs;
	int rope_shafts = 0;
	int i;

	(void)unused;
	eq(load_complete_water(), PARSE_ERROR_NONE);
	eq(load_complete_geology(), PARSE_ERROR_NONE);
	eq(load_complete_traversal(), PARSE_ERROR_NONE);
	eq(load_complete_population(), PARSE_ERROR_NONE);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_recipe_parser.finish(p), PARSE_ERROR_NONE);
	recipe = world_spelunk_recipe_definition_by_index(0);
	notnull(recipe);
	Rand_state_init(0xabcdef01U);
	Rand_state_export(&gameplay_before);
	eq(world_spelunk_generate_layout(recipe, 0x12345678U, 100, &first),
		WORLD_SPELUNK_GENERATION_OK);
	Rand_state_export(&gameplay_after);
	require(memcmp(&gameplay_before, &gameplay_after,
		sizeof(gameplay_before)) == 0);
	eq(world_spelunk_generate_layout(recipe, 0x12345678U, 100, &second),
		WORLD_SPELUNK_GENERATION_OK);
	require(world_spelunk_generated_layout_is_structurally_valid(&first));
	require(world_spelunk_generated_layout_is_structurally_valid(&second));
	require(world_spelunk_generated_layout_is_hydrologically_valid(&first,
		world_spelunk_water_profile_by_index(0)));
	require(world_spelunk_generated_layout_has_valid_landmarks(&first,
		recipe));
	require(world_spelunk_generated_layout_has_valid_geology(&first,
		world_spelunk_geology_profile_by_index(0)));
	traversal_profile = world_spelunk_traversal_profile_by_index(0);
	notnull(traversal_profile);
	eq(first.landmark_count, 2);
	eq(first.initial_stamina, 90);
	eq(first.width, second.width);
	eq(first.height, second.height);
	eq(first.entrance_x, second.entrance_x);
	eq(first.deepest_y, second.deepest_y);
	eq(first.water_surface_y, second.water_surface_y);
	eq(first.water_left_x, second.water_left_x);
	eq(first.water_right_x, second.water_right_x);
	eq(first.water_bed_y, second.water_bed_y);
	eq(first.landmark_count, second.landmark_count);
	require(memcmp(first.landmarks, second.landmarks,
		first.landmark_count * sizeof(*first.landmarks)) == 0);
	bytes = (size_t)first.width * first.height * sizeof(*first.cells);
	require(memcmp(first.cells, second.cells, bytes) == 0);
	require(memcmp(first.material_tags, second.material_tags,
		(size_t)first.width * first.height) == 0);
	require(memcmp(first.decoration_tags, second.decoration_tags,
		(size_t)first.width * first.height) == 0);
	require(streq(first.traversal_profile_id, "deepwell.test.launch"));
	require(streq(first.population_profile_id, "deepwell.test.sparse"));
	require(first.route_station_count >= 3);
	eq(first.route_segment_count + 1, first.route_station_count);
	require(first.witness_steps != NULL);
	require(first.witness_step_count > 0);
	eq(first.witness_required_checkpoint_mask,
		(1u << first.landmark_count) - 1u);
	eq(first.route_station_count, second.route_station_count);
	eq(first.route_segment_count, second.route_segment_count);
	eq(first.witness_step_count, second.witness_step_count);
	require(memcmp(first.route_stations, second.route_stations,
		first.route_station_count * sizeof(*first.route_stations)) == 0);
	require(memcmp(first.route_segments, second.route_segments,
		first.route_segment_count * sizeof(*first.route_segments)) == 0);
	require(memcmp(first.witness_steps, second.witness_steps,
		first.witness_step_count * sizeof(*first.witness_steps)) == 0);
	for (i = 0; i < first.route_segment_count; i++) {
		if (first.route_segments[i].uses_rope) rope_shafts++;
	}
	eq(rope_shafts, traversal_profile->rope_shaft_count);
	memset(&witness, 0, sizeof(witness));
	witness.start_x = first.route_stations[0].x;
	witness.start_y = first.route_stations[0].y;
	witness.steps = first.witness_steps;
	witness.step_count = first.witness_step_count;
	witness.required_checkpoint_mask =
		first.witness_required_checkpoint_mask;
	eq(world_spelunk_replay_traversal_witness(&first, traversal_profile,
		&witness, &proof_report), WORLD_SPELUNK_PROOF_OK);
	eq(proof_report.pitons_used, traversal_profile->rope_shaft_count);
	eq(proof_report.fall_damage, 0);
	{
		const struct world_spelunk_endpoint_request requests[] = {
			{ "deepwell.test.surface", 2, 18, 0 },
			{ "deepwell.test.practice", 2, 18, 0 },
			{ "deepwell.test.down.1", 60, 96, -1 },
			{ "deepwell.test.down.2", 60, 96, -1 },
			{ "deepwell.test.down.3", 60, 96, -1 }
		};
		struct world_spelunk_endpoint_placement placements[
			N_ELEMENTS(requests)];
		struct world_spelunk_endpoint_placement duplicate[2];
		struct world_spelunk_endpoint_placement invalid;
		size_t endpoint;

		require(world_spelunk_place_endpoints(&first, requests,
			N_ELEMENTS(requests), placements));
		for (endpoint = 0; endpoint < N_ELEMENTS(requests); endpoint++) {
			require(placements[endpoint].route_station <
				first.route_station_count);
			eq(placements[endpoint].y, first.route_stations[
				placements[endpoint].route_station].y);
		}
		invalid = placements[0];
		invalid.y++;
		require(!world_spelunk_certify_endpoints(&first, &invalid, 1,
			traversal_profile, &proof_report));
		eq(first.route_visit_count, 0);
		duplicate[0] = placements[0];
		duplicate[1] = placements[0];
		require(!world_spelunk_certify_endpoints(&first, duplicate,
			N_ELEMENTS(duplicate), traversal_profile, &proof_report));
		eq(first.route_visit_count, 0);
		require(world_spelunk_certify_endpoints(&first, placements,
			N_ELEMENTS(requests), traversal_profile, &proof_report));
		eq(first.route_visit_count, N_ELEMENTS(requests));
		eq(first.witness_required_checkpoint_mask,
			(1u << (first.landmark_count + N_ELEMENTS(requests))) - 1u);
		eq(proof_report.result, WORLD_SPELUNK_PROOF_OK);
		eq(proof_report.fall_damage, 0);
	}
	eq(world_spelunk_generate_layout(recipe, 0x12345679U, 100, &different),
		WORLD_SPELUNK_GENERATION_OK);
	differs = first.width != different.width ||
		first.height != different.height ||
		first.entrance_x != different.entrance_x;
	if (!differs && first.width == different.width &&
			first.height == different.height) {
		differs = memcmp(first.cells, different.cells, bytes) != 0;
	}
	require(differs);
	world_spelunk_generated_layout_dispose(&different);
	world_spelunk_generated_layout_dispose(&second);
	world_spelunk_generated_layout_dispose(&first);
	spelunking_recipe_parser.cleanup();
	spelunking_geology_parser.cleanup();
	spelunking_water_parser.cleanup();
	spelunking_traversal_parser.cleanup();
	spelunking_population_parser.cleanup();
	ok;
}

static int test_seed_sweep_produces_valid_candidates(void *unused)
{
	struct parser *p = spelunking_recipe_parser.init();
	const struct world_spelunk_recipe_definition *recipe;
	uint32_t seed;

	(void)unused;
	eq(load_complete_water(), PARSE_ERROR_NONE);
	eq(load_complete_geology(), PARSE_ERROR_NONE);
	eq(load_complete_traversal(), PARSE_ERROR_NONE);
	eq(load_complete_population(), PARSE_ERROR_NONE);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_recipe_parser.finish(p), PARSE_ERROR_NONE);
	recipe = world_spelunk_recipe_definition_by_index(0);
	for (seed = 0; seed < 64; seed++) {
		struct world_spelunk_generated_layout generated;

		eq(world_spelunk_generate_layout(recipe, seed, 100, &generated),
			WORLD_SPELUNK_GENERATION_OK);
		require(world_spelunk_generated_layout_is_structurally_valid(
			&generated));
		require(world_spelunk_generated_layout_is_hydrologically_valid(
			&generated, world_spelunk_water_profile_by_index(0)));
		require(world_spelunk_generated_layout_has_valid_landmarks(
			&generated, recipe));
		require(world_spelunk_generated_layout_has_valid_geology(&generated,
			world_spelunk_geology_profile_by_index(0)));
		world_spelunk_generated_layout_dispose(&generated);
	}
	spelunking_recipe_parser.cleanup();
	spelunking_geology_parser.cleanup();
	spelunking_water_parser.cleanup();
	spelunking_traversal_parser.cleanup();
	spelunking_population_parser.cleanup();
	ok;
}

static int test_water_profile_registry_fails_closed(void *unused)
{
	struct parser *p = spelunking_water_parser.init();
	const struct world_spelunk_water_profile *profile;

	(void)unused;
	eq(parser_parse(p, "surface:70:84"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p,
		"profile:deepwell.test.lower-basin:lower-basin:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "surface:70:84"), PARSE_ERROR_NONE);
	eq(spelunking_water_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_spelunk_water_profile_count(), 0);

	eq(load_complete_water(), PARSE_ERROR_NONE);
	eq(world_spelunk_water_profile_count(), 1);
	profile = world_spelunk_water_profile_by_id(
		"deepwell.test.lower-basin");
	notnull(profile);
	eq(profile->family, WORLD_SPELUNK_WATER_LOWER_BASIN);
	eq(profile->min_depth_tiles, 3);
	eq(profile->min_shore_tiles, 4);
	spelunking_water_parser.cleanup();
	ok;
}

static int test_candidate_gates_reject_corruption(void *unused)
{
	struct parser *p = spelunking_recipe_parser.init();
	const struct world_spelunk_recipe_definition *recipe;
	const struct world_spelunk_water_profile *profile;
	const struct world_spelunk_traversal_profile *traversal_profile;
	struct world_spelunk_generated_layout generated;
	struct world_spelunk_traversal_witness witness;
	struct world_spelunk_proof_report proof_report;
	enum world_spelunk_tile saved_bed;
	int saved_x;
	int saved_y;
	size_t bed_index;

	(void)unused;
	eq(load_complete_water(), PARSE_ERROR_NONE);
	eq(load_complete_geology(), PARSE_ERROR_NONE);
	eq(load_complete_traversal(), PARSE_ERROR_NONE);
	eq(load_complete_population(), PARSE_ERROR_NONE);
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(spelunking_recipe_parser.finish(p), PARSE_ERROR_NONE);
	recipe = world_spelunk_recipe_definition_by_index(0);
	profile = world_spelunk_water_profile_by_index(0);
	traversal_profile = world_spelunk_traversal_profile_by_index(0);
	notnull(recipe);
	notnull(profile);
	notnull(traversal_profile);
	eq(world_spelunk_generate_layout(recipe, 0x87654321U, 100, &generated),
		WORLD_SPELUNK_GENERATION_OK);

	bed_index = (size_t)generated.water_bed_y * generated.width +
		generated.water_left_x;
	saved_bed = generated.cells[bed_index];
	generated.cells[bed_index] = WORLD_SPELUNK_AIR;
	require(!world_spelunk_generated_layout_is_hydrologically_valid(
		&generated, profile));
	generated.cells[bed_index] = saved_bed;
	require(world_spelunk_generated_layout_is_hydrologically_valid(
		&generated, profile));

	saved_x = generated.landmarks[0].x;
	saved_y = generated.landmarks[0].y;
	generated.landmarks[0].x = generated.fishing_stance_x;
	generated.landmarks[0].y = generated.fishing_stance_y;
	require(!world_spelunk_generated_layout_has_valid_landmarks(&generated,
		recipe));
	generated.landmarks[0].x = saved_x;
	generated.landmarks[0].y = saved_y;
	require(world_spelunk_generated_layout_has_valid_landmarks(&generated,
		recipe));

	{
		size_t i;
		size_t cell_count = (size_t)generated.width * generated.height;
		uint8_t saved_material = 0;

		for (i = 0; i < cell_count; i++) {
			if (generated.cells[i] == WORLD_SPELUNK_ROCK) {
				saved_material = generated.material_tags[i];
				generated.material_tags[i] = 255;
				require(!world_spelunk_generated_layout_has_valid_geology(
					&generated, world_spelunk_geology_profile_by_index(0)));
				generated.material_tags[i] = saved_material;
				break;
			}
		}
		require(world_spelunk_generated_layout_has_valid_geology(&generated,
			world_spelunk_geology_profile_by_index(0)));
	}

	memset(&witness, 0, sizeof(witness));
	witness.start_x = generated.route_stations[0].x;
	witness.start_y = generated.route_stations[0].y;
	witness.steps = generated.witness_steps;
	witness.step_count = generated.witness_step_count;
	witness.required_checkpoint_mask =
		generated.witness_required_checkpoint_mask;
	generated.witness_steps[0].expected_x++;
	eq(world_spelunk_replay_traversal_witness(&generated,
		traversal_profile, &witness, &proof_report),
		WORLD_SPELUNK_PROOF_POSITION_MISMATCH);
	generated.witness_steps[0].expected_x--;
	eq(world_spelunk_replay_traversal_witness(&generated,
		traversal_profile, &witness, &proof_report),
		WORLD_SPELUNK_PROOF_OK);

	world_spelunk_generated_layout_dispose(&generated);
	spelunking_recipe_parser.cleanup();
	spelunking_geology_parser.cleanup();
	spelunking_water_parser.cleanup();
	spelunking_traversal_parser.cleanup();
	spelunking_population_parser.cleanup();
	ok;
}

static int test_invalid_and_incomplete_fail_closed(void *unused)
{
	struct parser *p = spelunking_recipe_parser.init();

	(void)unused;
	eq(parser_parse(p, "size:64:72:56:68"),
		PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "recipe:Bad ID:deepwell.test:root:1:1:90"),
		PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p,
		"recipe:deepwell.test.root:deepwell.test:root:1:1:90"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:Incomplete"), PARSE_ERROR_NONE);
	eq(spelunking_recipe_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	eq(world_spelunk_recipe_definition_count(), 0);

	p = spelunking_recipe_parser.init();
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"landmark:deepwell.test.second-fishing:fishing-stance:60:94:none:-"),
		PARSE_ERROR_NONE);
	eq(spelunking_recipe_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	eq(world_spelunk_recipe_definition_count(), 0);

	p = spelunking_recipe_parser.init();
	eq(parse_complete(p), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"recipe:deepwell.test.bad:deepwell.test:root:1:1:90"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "name:Impossible"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "description:Too narrow for its route."),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "size:24:24:24:24"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "macro:chamber-chain"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "route:5:5:4:8:2:4:0:0:6:2:4:7:7"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "rules:2:10:10:5:15:20:1:6:10:2"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "stamina-costs:8:5:3:3:2:1"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"perception:10:6:2:16:28:5:2:24:.:Slate:.:Light Dark"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "water:deepwell.test.none"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "material:deepwell.test.rock"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "traversal:deepwell.test.launch"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "population:deepwell.test.sparse"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p, "passage:down:60:96:3"), PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"landmark:deepwell.test.bad-object:object:20:65:tool:Yew Longline Rod"),
		PARSE_ERROR_NONE);
	eq(parser_parse(p,
		"landmark:deepwell.test.bad-fishing:fishing-stance:60:94:none:-"),
		PARSE_ERROR_NONE);
	eq(spelunking_recipe_parser.finish(p), PARSE_ERROR_INVALID_VALUE);
	eq(world_spelunk_recipe_definition_count(), 0);
	ok;
}

const char *suite_name = "parse/spelunking-recipe";
struct test tests[] = {
	{ "complete registry", test_complete_registry },
	{ "generation is deterministic and bounded",
		test_generation_is_deterministic_and_bounded },
	{ "seed sweep produces valid candidates",
		test_seed_sweep_produces_valid_candidates },
	{ "invalid and incomplete fail closed",
		test_invalid_and_incomplete_fail_closed },
	{ "water profile registry fails closed",
		test_water_profile_registry_fails_closed },
	{ "candidate gates reject corruption",
		test_candidate_gates_reject_corruption },
	{ NULL, NULL }
};
