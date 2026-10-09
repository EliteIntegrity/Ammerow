/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-traversal-data.c
 * \brief Parser and registry for procedural cave traversal proof budgets.
 */

#include "angband.h"
#include "datafile.h"
#include "world-spelunking-traversal-data.h"

#include <limits.h>

enum traversal_field {
	TRAVERSAL_FIELD_NAME = 0x01,
	TRAVERSAL_FIELD_DESCRIPTION = 0x02,
	TRAVERSAL_FIELD_EQUIPMENT = 0x04,
	TRAVERSAL_FIELD_SAFETY = 0x08,
	TRAVERSAL_FIELD_RETURN = 0x10,
	TRAVERSAL_FIELD_ROPE_SHAFTS = 0x20,
	TRAVERSAL_FIELD_ALL = 0x3f
};

struct traversal_record {
	struct world_spelunk_traversal_profile profile;
	unsigned int fields;
	struct traversal_record *next;
};

struct traversal_parse_state {
	int count;
	struct traversal_record *records;
	struct traversal_record *current;
};

static struct world_spelunk_traversal_profile *profiles;
static int profile_count;
static bool traversal_data_loaded;

static struct traversal_record *record_by_id(
		const struct traversal_parse_state *state, const char *id)
{
	struct traversal_record *record;

	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->profile.id, id)) return record;
	}
	return NULL;
}

static void free_profile_strings(
		struct world_spelunk_traversal_profile *profile)
{
	if (!profile) return;
	string_free((char *)profile->id);
	string_free((char *)profile->name);
	string_free((char *)profile->description);
}

static void free_record(struct traversal_record *record)
{
	if (!record) return;
	free_profile_strings(&record->profile);
	mem_free(record);
}

static void free_parse_state(struct traversal_parse_state *state)
{
	struct traversal_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_record(record);
	}
	mem_free(state);
}

static enum parser_error parse_profile(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	int version = parser_getint(p, "version");
	struct traversal_record *record;

	if (!world_id_is_valid(id) || version < 1 || version > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_SPELUNK_TRAVERSAL_PROFILE_MAX ||
			record_by_id(state, id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	record = mem_zalloc(sizeof(*record));
	record->profile.id = string_make(id);
	record->profile.version = (uint16_t)version;
	record->next = state->records;
	state->records = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_owned_text(struct parser *p,
		unsigned int field, const char *token, const char **destination)
{
	struct traversal_parse_state *state = parser_priv(p);
	const char *value;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & field) return PARSE_ERROR_REPEATED_DIRECTIVE;
	value = parser_getstr(p, token);
	if (!value || !value[0]) return PARSE_ERROR_INVALID_VALUE;
	*destination = string_make(value);
	state->current->fields |= field;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_name(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);

	return parse_owned_text(p, TRAVERSAL_FIELD_NAME, "name",
		state->current ? &state->current->profile.name : NULL);
}

static enum parser_error parse_description(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);

	return parse_owned_text(p, TRAVERSAL_FIELD_DESCRIPTION, "description",
		state->current ? &state->current->profile.description : NULL);
}

static enum parser_error parse_equipment(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);
	int pitons;
	int rope_segments;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & TRAVERSAL_FIELD_EQUIPMENT) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	pitons = parser_getint(p, "pitons");
	rope_segments = parser_getint(p, "rope-segments");
	if (pitons < 0 || pitons > 64 || rope_segments < 0 ||
			rope_segments > 512 || (!pitons && rope_segments)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->profile.piton_budget = (uint16_t)pitons;
	state->current->profile.rope_segment_budget = (uint16_t)rope_segments;
	state->current->fields |= TRAVERSAL_FIELD_EQUIPMENT;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_safety(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);
	int fall_damage;
	int submerged_turns;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & TRAVERSAL_FIELD_SAFETY) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	fall_damage = parser_getint(p, "fall-damage");
	submerged_turns = parser_getint(p, "submerged-turns");
	if (fall_damage < 0 || fall_damage > UINT16_MAX ||
			submerged_turns < 0 || submerged_turns > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->profile.max_mandatory_fall_damage =
		(uint16_t)fall_damage;
	state->current->profile.max_submerged_turns =
		(uint16_t)submerged_turns;
	state->current->fields |= TRAVERSAL_FIELD_SAFETY;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_rope_shafts(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);
	int count;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & TRAVERSAL_FIELD_ROPE_SHAFTS) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	count = parser_getint(p, "count");
	if (count < 0 || count > WORLD_SPELUNK_TRAVERSAL_ROPE_SHAFT_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->profile.rope_shaft_count = (uint16_t)count;
	state->current->fields |= TRAVERSAL_FIELD_ROPE_SHAFTS;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_return(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);
	int required;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & TRAVERSAL_FIELD_RETURN) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	required = parser_getint(p, "required");
	if (required != 0 && required != 1) return PARSE_ERROR_INVALID_VALUE;
	state->current->profile.return_required = required != 0;
	state->current->fields |= TRAVERSAL_FIELD_RETURN;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_traversal(void)
{
	struct parser *p = parser_new();
	struct traversal_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "profile sym id int version", parse_profile);
	parser_reg(p, "name str name", parse_name);
	parser_reg(p, "description str description", parse_description);
	parser_reg(p, "equipment int pitons int rope-segments", parse_equipment);
	parser_reg(p, "rope-shafts int count", parse_rope_shafts);
	parser_reg(p, "safety int fall-damage int submerged-turns", parse_safety);
	parser_reg(p, "return int required", parse_return);
	return p;
}

static errr run_parse_traversal(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_traversal");
}

static void cleanup_traversal_data(void)
{
	int i;

	for (i = 0; i < profile_count; i++) {
		free_profile_strings(&profiles[i]);
	}
	mem_free(profiles);
	profiles = NULL;
	profile_count = 0;
	traversal_data_loaded = false;
}

static errr finish_parse_traversal(struct parser *p)
{
	struct traversal_parse_state *state = parser_priv(p);
	struct traversal_record *record;
	struct traversal_record *next;
	int index;

	if (!state || state->count < 1) {
		free_parse_state(state);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (record = state->records; record; record = record->next) {
		if (record->fields != TRAVERSAL_FIELD_ALL ||
				record->profile.rope_shaft_count >
					record->profile.piton_budget ||
				(record->profile.rope_shaft_count > 0 &&
				 !record->profile.rope_segment_budget)) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	cleanup_traversal_data();
	profiles = mem_zalloc((size_t)state->count * sizeof(*profiles));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		profiles[index] = record->profile;
		mem_free(record);
	}
	profile_count = state->count;
	traversal_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_traversal_parser = {
	"spelunking_traversal",
	init_parse_traversal,
	run_parse_traversal,
	finish_parse_traversal,
	cleanup_traversal_data
};

int world_spelunk_traversal_profile_count(void)
{
	return traversal_data_loaded ? profile_count : 0;
}

const struct world_spelunk_traversal_profile *
world_spelunk_traversal_profile_by_index(int index)
{
	if (!traversal_data_loaded || index < 0 || index >= profile_count) {
		return NULL;
	}
	return &profiles[index];
}

const struct world_spelunk_traversal_profile *
world_spelunk_traversal_profile_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < profile_count; i++) {
		if (streq(profiles[i].id, id)) return &profiles[i];
	}
	return NULL;
}
