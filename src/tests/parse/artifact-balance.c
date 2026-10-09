/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "unit-test.h"
#include "artifact-balance-data.h"
#include "datafile.h"
#include "obj-tval.h"
#include "parser.h"

static const char *const property_names[] = {
	#define ART_IDX(a, b) #a,
	#include "list-randart-properties.h"
	#undef ART_IDX
};

NOSETUP
NOTEARDOWN

static enum parser_error parse_complete_properties(struct parser *p)
{
	int i;

	for (i = 0; i < ART_IDX_TOTAL; i++) {
		char line[80];

		strnfmt(line, sizeof(line), "property:%s:%d",
			property_names[i], i + 1);
		enum parser_error result = parser_parse(p, line);

		if (result != PARSE_ERROR_NONE) return result;
	}
	return PARSE_ERROR_NONE;
}

static void free_data(struct artifact_set_data *data)
{
	mem_free(data->art_probs);
	mem_free(data->tv_probs);
	mem_free(data->tv_num);
	mem_free(data->tv_freq);
	mem_free(data->avg_tv_power);
	mem_free(data->min_tv_power);
	mem_free(data->max_tv_power);
}

static int test_complete(void *state)
{
	struct parser *p = artifact_balance_parser.init();
	struct artifact_set_data data = { 0 };

	eq(parser_parse(p, "origin:anonymous-aggregate"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "review:release"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "generated-slots:3"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "stable-slots:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "maximum-power:900"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "negative-power-count:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:divisor:1"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:rounding:1000"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:minimum:1000"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:maximum:1000000"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "type:SWORD:3:2:20:100:300"), PARSE_ERROR_NONE);
	eq(parse_complete_properties(p), PARSE_ERROR_NONE);
	eq(artifact_balance_parser.finish(p), PARSE_ERROR_NONE);
	require(artifact_balance_data_is_loaded());
	require(streq(artifact_balance_origin(), "anonymous-aggregate"));
	require(streq(artifact_balance_review(), "release"));
	eq(artifact_balance_generated_slots(), 3);
	eq(artifact_balance_stable_slots(), 2);
	eq(artifact_balance_cost_for_power(-10), 0);
	eq(artifact_balance_cost_for_power(10), 1000);
	eq(artifact_balance_cost_for_power(200), 40000);
	eq(artifact_balance_cost_for_power(2000), 1000000);

	data.art_probs = mem_zalloc(ART_IDX_TOTAL * sizeof(*data.art_probs));
	data.tv_probs = mem_zalloc(TV_MAX * sizeof(*data.tv_probs));
	data.tv_num = mem_zalloc(TV_MAX * sizeof(*data.tv_num));
	data.tv_freq = mem_zalloc(TV_MAX * sizeof(*data.tv_freq));
	data.avg_tv_power = mem_zalloc(TV_MAX * sizeof(*data.avg_tv_power));
	data.min_tv_power = mem_zalloc(TV_MAX * sizeof(*data.min_tv_power));
	data.max_tv_power = mem_zalloc(TV_MAX * sizeof(*data.max_tv_power));
	require(artifact_balance_apply(&data, 3, 2));
	eq(data.max_power, 900);
	eq(data.neg_power_total, 2);
	eq(data.tv_num[TV_SWORD], 3);
	eq(data.tv_probs[TV_SWORD], 2);
	eq(data.avg_tv_power[TV_SWORD], 100);
	eq(data.art_probs[ART_IDX_GEN_AC_SUPER], ART_IDX_GEN_AC_SUPER + 1);
	require(!artifact_balance_apply(&data, 4, 2));
	free_data(&data);
	artifact_balance_parser.cleanup();
	ok;
}

static int test_fail_closed(void *state)
{
	struct parser *p = artifact_balance_parser.init();

	eq(parser_parse(p, "origin:legacy-records"), PARSE_ERROR_INVALID_VALUE);
	parser_destroy(p);

	p = artifact_balance_parser.init();
	eq(parser_parse(p, "origin:anonymous-aggregate"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "review:release"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "generated-slots:3"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "stable-slots:2"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "maximum-power:900"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "negative-power-count:0"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:divisor:1"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:rounding:1000"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:minimum:1000"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "cost:maximum:1000000"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "type:SWORD:3:2:20:100:300"), PARSE_ERROR_NONE);
	eq(artifact_balance_parser.finish(p), PARSE_ERROR_TOO_FEW_ENTRIES);
	require(!artifact_balance_data_is_loaded());
	ok;
}

const char *suite_name = "parse/artifact-balance";
struct test tests[] = {
	{ "complete", test_complete },
	{ "fail-closed", test_fail_closed },
	{ NULL, NULL }
};
