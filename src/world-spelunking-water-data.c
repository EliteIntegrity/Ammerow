/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-water-data.c
 * \brief Parser and immutable registry for procedural cave hydrology.
 */

#include "angband.h"
#include "datafile.h"
#include "world-spelunking-water-data.h"

#include <limits.h>

enum water_field {
	WATER_FIELD_SURFACE = 0x01,
	WATER_FIELD_WIDTH = 0x02,
	WATER_FIELD_DEPTH = 0x04,
	WATER_FIELD_SHORE = 0x08,
	WATER_FIELD_ALL = 0x0f
};

struct water_record {
	struct world_spelunk_water_profile profile;
	unsigned int fields;
	struct water_record *next;
};

struct water_parse_state {
	int count;
	struct water_record *records;
	struct water_record *current;
};

static struct world_spelunk_water_profile *profiles;
static int profile_count;
static bool water_data_loaded;

static bool family_from_name(const char *name,
		enum world_spelunk_water_family *family)
{
	if (!streq(name, "lower-basin")) return false;
	*family = WORLD_SPELUNK_WATER_LOWER_BASIN;
	return true;
}

static struct water_record *record_by_id(
		const struct water_parse_state *state, const char *id)
{
	struct water_record *record;

	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->profile.id, id)) return record;
	}
	return NULL;
}

static void free_record(struct water_record *record)
{
	if (!record) return;
	string_free((char *)record->profile.id);
	mem_free(record);
}

static void free_parse_state(struct water_parse_state *state)
{
	struct water_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_record(record);
	}
	mem_free(state);
}

static enum parser_error parse_profile(struct parser *p)
{
	struct water_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *family_name = parser_getsym(p, "family");
	int version = parser_getint(p, "version");
	struct water_record *record;
	enum world_spelunk_water_family family;

	if (!world_id_is_valid(id) ||
			!family_from_name(family_name, &family) || version < 1 ||
			version > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_SPELUNK_WATER_PROFILE_MAX ||
			record_by_id(state, id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	record = mem_zalloc(sizeof(*record));
	record->profile.id = string_make(id);
	record->profile.family = family;
	record->profile.version = (uint16_t)version;
	record->next = state->records;
	state->records = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_range(struct parser *p,
		unsigned int field, const char *minimum_name,
		const char *maximum_name, uint16_t *minimum, uint16_t *maximum,
		int lower_bound, int upper_bound)
{
	struct water_parse_state *state = parser_priv(p);
	int minimum_value;
	int maximum_value;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & field) return PARSE_ERROR_REPEATED_DIRECTIVE;
	minimum_value = parser_getint(p, minimum_name);
	maximum_value = parser_getint(p, maximum_name);
	if (minimum_value < lower_bound || minimum_value > maximum_value ||
			maximum_value > upper_bound) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	*minimum = (uint16_t)minimum_value;
	*maximum = (uint16_t)maximum_value;
	state->current->fields |= field;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_surface(struct parser *p)
{
	struct water_parse_state *state = parser_priv(p);

	return parse_range(p, WATER_FIELD_SURFACE, "minimum", "maximum",
		state->current ?
			&state->current->profile.min_surface_depth_percent : NULL,
		state->current ?
			&state->current->profile.max_surface_depth_percent : NULL,
		40, 94);
}

static enum parser_error parse_width(struct parser *p)
{
	struct water_parse_state *state = parser_priv(p);

	return parse_range(p, WATER_FIELD_WIDTH, "minimum", "maximum",
		state->current ? &state->current->profile.min_width_percent : NULL,
		state->current ? &state->current->profile.max_width_percent : NULL,
		8, 80);
}

static enum parser_error parse_depth(struct parser *p)
{
	struct water_parse_state *state = parser_priv(p);

	return parse_range(p, WATER_FIELD_DEPTH, "minimum", "maximum",
		state->current ? &state->current->profile.min_depth_tiles : NULL,
		state->current ? &state->current->profile.max_depth_tiles : NULL,
		2, 24);
}

static enum parser_error parse_shore(struct parser *p)
{
	struct water_parse_state *state = parser_priv(p);
	int tiles;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & WATER_FIELD_SHORE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	tiles = parser_getint(p, "tiles");
	if (tiles < 1 || tiles > 24) return PARSE_ERROR_INVALID_VALUE;
	state->current->profile.min_shore_tiles = (uint16_t)tiles;
	state->current->fields |= WATER_FIELD_SHORE;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_water(void)
{
	struct parser *p = parser_new();
	struct water_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "profile sym id sym family int version", parse_profile);
	parser_reg(p, "surface int minimum int maximum", parse_surface);
	parser_reg(p, "width int minimum int maximum", parse_width);
	parser_reg(p, "depth int minimum int maximum", parse_depth);
	parser_reg(p, "shore int tiles", parse_shore);
	return p;
}

static errr run_parse_water(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_water");
}

static void cleanup_water_data(void)
{
	int i;

	for (i = 0; i < profile_count; i++) {
		string_free((char *)profiles[i].id);
	}
	mem_free(profiles);
	profiles = NULL;
	profile_count = 0;
	water_data_loaded = false;
}

static errr finish_parse_water(struct parser *p)
{
	struct water_parse_state *state = parser_priv(p);
	struct water_record *record;
	struct water_record *next;
	int index;

	if (!state || state->count < 1) {
		free_parse_state(state);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (record = state->records; record; record = record->next) {
		if (record->fields != WATER_FIELD_ALL) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	cleanup_water_data();
	profiles = mem_zalloc((size_t)state->count * sizeof(*profiles));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		profiles[index] = record->profile;
		mem_free(record);
	}
	profile_count = state->count;
	water_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_water_parser = {
	"spelunking_water",
	init_parse_water,
	run_parse_water,
	finish_parse_water,
	cleanup_water_data
};

int world_spelunk_water_profile_count(void)
{
	return water_data_loaded ? profile_count : 0;
}

const struct world_spelunk_water_profile *
world_spelunk_water_profile_by_index(int index)
{
	if (!water_data_loaded || index < 0 || index >= profile_count) return NULL;
	return &profiles[index];
}

const struct world_spelunk_water_profile *
world_spelunk_water_profile_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < profile_count; i++) {
		if (streq(profiles[i].id, id)) return &profiles[i];
	}
	return NULL;
}
