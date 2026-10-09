/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file artifact-balance-data.c
 * \brief Parser for release-safe aggregate randart calibration.
 */

#include "angband.h"
#include "artifact-balance-data.h"
#include "datafile.h"
#include "obj-power.h"
#include "obj-tval.h"

#include <ctype.h>
#include <string.h>

struct artifact_balance_type {
	char *code;
	int count;
	int weight;
	int minimum_power;
	int mean_power;
	int maximum_power;
	struct artifact_balance_type *next;
};

struct artifact_balance_registry {
	char *origin;
	char *review;
	int generated_slots;
	int stable_slots;
	int maximum_power;
	int negative_power_count;
	int cost_divisor;
	int cost_rounding;
	int cost_minimum;
	int cost_maximum;
	bool has_generated_slots;
	bool has_stable_slots;
	bool has_maximum_power;
	bool has_negative_power_count;
	bool has_cost_divisor;
	bool has_cost_rounding;
	bool has_cost_minimum;
	bool has_cost_maximum;
	int property_values[ART_IDX_TOTAL];
	bool property_seen[ART_IDX_TOTAL];
	struct artifact_balance_type *types;
	struct artifact_balance_type *last_type;
	int type_count;
};

struct artifact_balance_parse_state {
	struct artifact_balance_registry registry;
};

static struct artifact_balance_registry balance_registry;
static bool balance_loaded;

static const char *const property_names[] = {
	#define ART_IDX(a, b) #a,
	#include "list-randart-properties.h"
	#undef ART_IDX
};

static void free_registry(struct artifact_balance_registry *registry)
{
	struct artifact_balance_type *type = registry->types;

	while (type) {
		struct artifact_balance_type *next = type->next;

		string_free(type->code);
		mem_free(type);
		type = next;
	}
	string_free(registry->origin);
	string_free(registry->review);
	memset(registry, 0, sizeof(*registry));
}

static struct artifact_balance_type *find_type(
		const struct artifact_balance_registry *registry, const char *code)
{
	struct artifact_balance_type *type;

	for (type = registry->types; type; type = type->next) {
		if (streq(type->code, code)) return type;
	}
	return NULL;
}

static int property_index(const char *name)
{
	int i;

	for (i = 0; i < ART_IDX_TOTAL; i++) {
		if (streq(property_names[i], name)) return i;
	}
	return -1;
}

static bool valid_type_code(const char *code)
{
	const unsigned char *cursor = (const unsigned char *)code;

	if (!cursor[0]) return false;
	for (; *cursor; cursor++) {
		if (!isupper(*cursor) && !isdigit(*cursor) && *cursor != '_') {
			return false;
		}
	}
	return true;
}

static enum parser_error parse_balance_origin(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	const char *origin = parser_getsym(p, "origin");

	if (state->registry.origin) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (!streq(origin, "anonymous-aggregate")) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->registry.origin = string_make(origin);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_review(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	const char *review = parser_getsym(p, "review");

	if (state->registry.review) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (!streq(review, "release")) return PARSE_ERROR_INVALID_VALUE;
	state->registry.review = string_make(review);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_generated_slots(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	unsigned int value = parser_getuint(p, "count");

	if (state->registry.has_generated_slots) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!value) return PARSE_ERROR_INVALID_VALUE;
	state->registry.generated_slots = (int)value;
	state->registry.has_generated_slots = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_stable_slots(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	unsigned int value = parser_getuint(p, "count");

	if (state->registry.has_stable_slots) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->registry.stable_slots = (int)value;
	state->registry.has_stable_slots = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_maximum_power(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	unsigned int value = parser_getuint(p, "power");

	if (state->registry.has_maximum_power) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!value || value >= INHIBIT_POWER) return PARSE_ERROR_INVALID_VALUE;
	state->registry.maximum_power = (int)value;
	state->registry.has_maximum_power = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_negative_power_count(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);

	if (state->registry.has_negative_power_count) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->registry.negative_power_count =
		(int)parser_getuint(p, "count");
	state->registry.has_negative_power_count = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_cost_value(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	const char *field = parser_getsym(p, "field");
	unsigned int value = parser_getuint(p, "value");
	int *destination = NULL;
	bool *seen = NULL;

	if (streq(field, "divisor")) {
		destination = &state->registry.cost_divisor;
		seen = &state->registry.has_cost_divisor;
	} else if (streq(field, "rounding")) {
		destination = &state->registry.cost_rounding;
		seen = &state->registry.has_cost_rounding;
	} else if (streq(field, "minimum")) {
		destination = &state->registry.cost_minimum;
		seen = &state->registry.has_cost_minimum;
	} else if (streq(field, "maximum")) {
		destination = &state->registry.cost_maximum;
		seen = &state->registry.has_cost_maximum;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (*seen) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (!value || value > 10000000) return PARSE_ERROR_INVALID_VALUE;
	*destination = (int)value;
	*seen = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_type(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	struct artifact_balance_type *type;
	const char *code = parser_getsym(p, "type");
	unsigned int count = parser_getuint(p, "count");
	unsigned int weight = parser_getuint(p, "weight");
	unsigned int minimum = parser_getuint(p, "minimum");
	unsigned int mean = parser_getuint(p, "mean");
	unsigned int maximum = parser_getuint(p, "maximum");

	if (!state->registry.origin || !state->registry.review) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!valid_type_code(code) || find_type(&state->registry, code) ||
			!count || !weight || !minimum || minimum > mean ||
			mean > maximum || maximum >= INHIBIT_POWER) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	type = mem_zalloc(sizeof(*type));
	type->code = string_make(code);
	type->count = (int)count;
	type->weight = (int)weight;
	type->minimum_power = (int)minimum;
	type->mean_power = (int)mean;
	type->maximum_power = (int)maximum;
	if (state->registry.last_type) {
		state->registry.last_type->next = type;
	} else {
		state->registry.types = type;
	}
	state->registry.last_type = type;
	state->registry.type_count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_balance_property(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	int index = property_index(parser_getsym(p, "property"));

	if (index < 0) return PARSE_ERROR_INVALID_VALUE;
	if (state->registry.property_seen[index]) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->registry.property_values[index] =
		(int)parser_getuint(p, "weight");
	state->registry.property_seen[index] = true;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_artifact_balance(void)
{
	struct parser *p = parser_new();
	struct artifact_balance_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "origin sym origin", parse_balance_origin);
	parser_reg(p, "review sym review", parse_balance_review);
	parser_reg(p, "generated-slots uint count", parse_balance_generated_slots);
	parser_reg(p, "stable-slots uint count", parse_balance_stable_slots);
	parser_reg(p, "maximum-power uint power", parse_balance_maximum_power);
	parser_reg(p, "negative-power-count uint count",
		parse_balance_negative_power_count);
	parser_reg(p, "cost sym field uint value", parse_balance_cost_value);
	parser_reg(p,
		"type sym type uint count uint weight uint minimum uint mean uint maximum",
		parse_balance_type);
	parser_reg(p, "property sym property uint weight", parse_balance_property);
	return p;
}

static errr run_parse_artifact_balance(struct parser *p)
{
	return parse_file_quit_not_found(p, "artifact_balance");
}

static errr validate_registry(const struct artifact_balance_registry *registry)
{
	int i;

	if (!registry->origin || !registry->review ||
			!registry->has_generated_slots || !registry->has_stable_slots ||
			!registry->has_maximum_power ||
			!registry->has_negative_power_count ||
			!registry->has_cost_divisor || !registry->has_cost_rounding ||
			!registry->has_cost_minimum || !registry->has_cost_maximum ||
			registry->cost_minimum > registry->cost_maximum ||
			registry->type_count < 1) {
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (i = 0; i < ART_IDX_TOTAL; i++) {
		if (!registry->property_seen[i]) return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	return PARSE_ERROR_NONE;
}

static void cleanup_artifact_balance(void)
{
	free_registry(&balance_registry);
	balance_loaded = false;
}

static errr finish_parse_artifact_balance(struct parser *p)
{
	struct artifact_balance_parse_state *state = parser_priv(p);
	errr result = validate_registry(&state->registry);

	if (result != PARSE_ERROR_NONE) {
		free_registry(&state->registry);
		mem_free(state);
		parser_destroy(p);
		return result;
	}
	cleanup_artifact_balance();
	balance_registry = state->registry;
	memset(&state->registry, 0, sizeof(state->registry));
	balance_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser artifact_balance_parser = {
	"artifact_balance",
	init_parse_artifact_balance,
	run_parse_artifact_balance,
	finish_parse_artifact_balance,
	cleanup_artifact_balance
};

bool artifact_balance_data_is_loaded(void)
{
	return balance_loaded;
}

const char *artifact_balance_origin(void)
{
	return balance_loaded ? balance_registry.origin : NULL;
}

const char *artifact_balance_review(void)
{
	return balance_loaded ? balance_registry.review : NULL;
}

int artifact_balance_generated_slots(void)
{
	return balance_loaded ? balance_registry.generated_slots : 0;
}

int artifact_balance_stable_slots(void)
{
	return balance_loaded ? balance_registry.stable_slots : 0;
}

int artifact_balance_cost_for_power(int power)
{
	int64_t raw;
	int rounded;

	if (!balance_loaded || power <= 0) return 0;
	raw = ((int64_t)power * power) / balance_registry.cost_divisor;
	if (raw > balance_registry.cost_maximum) {
		return balance_registry.cost_maximum;
	}
	rounded = (int)(((raw + balance_registry.cost_rounding / 2) /
		balance_registry.cost_rounding) * balance_registry.cost_rounding);
	return MIN(balance_registry.cost_maximum,
		MAX(balance_registry.cost_minimum, rounded));
}

static int tval_from_code(const char *code)
{
	char name[40];
	size_t i;

	my_strcpy(name, code, sizeof(name));
	for (i = 0; name[i]; i++) {
		name[i] = (name[i] == '_') ? ' ' : (char)tolower((unsigned char)name[i]);
	}
	return tval_find_idx(name);
}

bool artifact_balance_apply(struct artifact_set_data *data,
		int generated_slots, int stable_slots)
{
	struct artifact_balance_type *type;
	int i;

	if (!balance_loaded || !data ||
			generated_slots != balance_registry.generated_slots ||
			stable_slots != balance_registry.stable_slots) {
		return false;
	}
	data->max_power = balance_registry.maximum_power;
	data->neg_power_total = balance_registry.negative_power_count;
	for (i = 0; i < ART_IDX_TOTAL; i++) {
		data->art_probs[i] = balance_registry.property_values[i];
	}
	for (type = balance_registry.types; type; type = type->next) {
		int tval = tval_from_code(type->code);

		if (tval <= TV_NULL || tval >= TV_MAX || data->tv_probs[tval]) {
			return false;
		}
		data->tv_num[tval] = type->count;
		data->tv_probs[tval] = type->weight;
		data->min_tv_power[tval] = type->minimum_power;
		data->avg_tv_power[tval] = type->mean_power;
		data->max_tv_power[tval] = type->maximum_power;
	}
	for (i = 0; i < TV_MAX; i++) {
		data->tv_freq[i] = data->tv_probs[i] +
			(i ? data->tv_freq[i - 1] : 0);
	}
	return data->tv_freq[TV_MAX - 1] > 0;
}
