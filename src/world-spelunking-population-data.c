/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-population-data.c
 * \brief Parser and registry for generated cave population profiles.
 */

#include "angband.h"
#include "datafile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-population-data.h"

#include <string.h>

enum population_field {
	POPULATION_FIELD_NAME = 0x01,
	POPULATION_FIELD_DESCRIPTION = 0x02,
	POPULATION_FIELD_EXCLUSION = 0x04,
	POPULATION_FIELD_CONTENT = 0x08,
	POPULATION_FIELD_REQUIRED = 0x0f
};

struct population_record {
	struct world_spelunk_population_profile profile;
	unsigned int fields;
	struct population_record *next;
};

struct population_parse_state {
	int count;
	struct population_record *records;
	struct population_record *current;
};

static struct world_spelunk_population_profile *profiles;
static int profile_count;
static bool population_data_loaded;

static struct population_record *record_by_id(
		const struct population_parse_state *state, const char *id)
{
	struct population_record *record;

	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->profile.id, id)) return record;
	}
	return NULL;
}

static void free_profile_strings(
		struct world_spelunk_population_profile *profile)
{
	int i;

	if (!profile) return;
	string_free((char *)profile->id);
	string_free((char *)profile->name);
	string_free((char *)profile->description);
	for (i = 0; i < profile->actor_entry_count; i++) {
		string_free((char *)profile->actors[i].actor_id);
	}
	for (i = 0; i < profile->object_entry_count; i++) {
		string_free((char *)profile->objects[i].object_tval);
		string_free((char *)profile->objects[i].object_sval);
	}
}

static void free_record(struct population_record *record)
{
	if (!record) return;
	free_profile_strings(&record->profile);
	mem_free(record);
}

static void free_parse_state(struct population_parse_state *state)
{
	struct population_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_record(record);
	}
	mem_free(state);
}

static enum parser_error parse_profile(struct parser *p)
{
	struct population_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	int version = parser_getint(p, "version");
	struct population_record *record;

	if (!world_id_is_valid(id) || version < 1 || version > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_SPELUNK_POPULATION_PROFILE_MAX ||
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
	struct population_parse_state *state = parser_priv(p);
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
	struct population_parse_state *state = parser_priv(p);

	return parse_owned_text(p, POPULATION_FIELD_NAME, "name",
		state->current ? &state->current->profile.name : NULL);
}

static enum parser_error parse_description(struct parser *p)
{
	struct population_parse_state *state = parser_priv(p);

	return parse_owned_text(p, POPULATION_FIELD_DESCRIPTION, "description",
		state->current ? &state->current->profile.description : NULL);
}

static enum parser_error parse_exclusion(struct parser *p)
{
	struct population_parse_state *state = parser_priv(p);
	int entrance = parser_getint(p, "entrance");
	int landmark = parser_getint(p, "landmark");
	int endpoint = parser_getint(p, "endpoint");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & POPULATION_FIELD_EXCLUSION) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (entrance < 0 || entrance > 20 || landmark < 0 || landmark > 20 ||
			endpoint < 0 || endpoint > 20) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->profile.entrance_exclusion_radius = (uint16_t)entrance;
	state->current->profile.landmark_exclusion_radius = (uint16_t)landmark;
	state->current->profile.endpoint_exclusion_radius = (uint16_t)endpoint;
	state->current->fields |= POPULATION_FIELD_EXCLUSION;
	return PARSE_ERROR_NONE;
}

static bool count_and_depth_are_valid(int minimum, int maximum,
		int minimum_depth, int maximum_depth)
{
	return minimum >= 0 && minimum <= maximum && maximum > 0 &&
		maximum <= WORLD_SPELUNK_POPULATION_COUNT_MAX && minimum_depth >= 0 &&
		minimum_depth <= maximum_depth && maximum_depth <= 100;
}

static enum parser_error parse_actor(struct parser *p)
{
	struct population_parse_state *state = parser_priv(p);
	struct world_spelunk_population_profile *profile;
	struct world_spelunk_population_actor_entry *entry;
	const char *actor_id = parser_getsym(p, "actor");
	int minimum = parser_getint(p, "minimum");
	int maximum = parser_getint(p, "maximum");
	int minimum_depth = parser_getint(p, "minimum-depth");
	int maximum_depth = parser_getint(p, "maximum-depth");
	int i;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	profile = &state->current->profile;
	if (!world_id_is_valid(actor_id) ||
			!count_and_depth_are_valid(minimum, maximum, minimum_depth,
				maximum_depth)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (profile->actor_entry_count >=
			WORLD_SPELUNK_POPULATION_ACTOR_ENTRY_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	for (i = 0; i < profile->actor_entry_count; i++) {
		if (streq(profile->actors[i].actor_id, actor_id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	entry = &profile->actors[profile->actor_entry_count++];
	entry->actor_id = string_make(actor_id);
	entry->minimum_count = (uint16_t)minimum;
	entry->maximum_count = (uint16_t)maximum;
	entry->minimum_depth_percent = (uint16_t)minimum_depth;
	entry->maximum_depth_percent = (uint16_t)maximum_depth;
	state->current->fields |= POPULATION_FIELD_CONTENT;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_object(struct parser *p)
{
	struct population_parse_state *state = parser_priv(p);
	struct world_spelunk_population_profile *profile;
	struct world_spelunk_population_object_entry *entry;
	const char *tval = parser_getsym(p, "tval");
	const char *sval = parser_getstr(p, "sval");
	int minimum = parser_getint(p, "minimum");
	int maximum = parser_getint(p, "maximum");
	int minimum_depth = parser_getint(p, "minimum-depth");
	int maximum_depth = parser_getint(p, "maximum-depth");
	int i;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	profile = &state->current->profile;
	if (!tval || !tval[0] || !sval || !sval[0] ||
			strlen(tval) >= WORLD_SPELUNK_POPULATION_TVAL_LEN ||
			strlen(sval) >= WORLD_SPELUNK_POPULATION_SVAL_LEN ||
			!count_and_depth_are_valid(minimum, maximum, minimum_depth,
				maximum_depth)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (profile->object_entry_count >=
			WORLD_SPELUNK_POPULATION_OBJECT_ENTRY_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	for (i = 0; i < profile->object_entry_count; i++) {
		if (streq(profile->objects[i].object_tval, tval) &&
				streq(profile->objects[i].object_sval, sval)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	entry = &profile->objects[profile->object_entry_count++];
	entry->object_tval = string_make(tval);
	entry->object_sval = string_make(sval);
	entry->minimum_count = (uint16_t)minimum;
	entry->maximum_count = (uint16_t)maximum;
	entry->minimum_depth_percent = (uint16_t)minimum_depth;
	entry->maximum_depth_percent = (uint16_t)maximum_depth;
	state->current->fields |= POPULATION_FIELD_CONTENT;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_population(void)
{
	struct parser *p = parser_new();
	struct population_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "profile sym id int version", parse_profile);
	parser_reg(p, "name str name", parse_name);
	parser_reg(p, "description str description", parse_description);
	parser_reg(p, "exclusion int entrance int landmark int endpoint",
		parse_exclusion);
	parser_reg(p, "actor sym actor int minimum int maximum int minimum-depth int maximum-depth",
		parse_actor);
	parser_reg(p, "object sym tval int minimum int maximum int minimum-depth int maximum-depth str sval",
		parse_object);
	return p;
}

static errr run_parse_population(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_population");
}

static void cleanup_population_data(void)
{
	int i;

	for (i = 0; i < profile_count; i++) free_profile_strings(&profiles[i]);
	mem_free(profiles);
	profiles = NULL;
	profile_count = 0;
	population_data_loaded = false;
}

static errr finish_parse_population(struct parser *p)
{
	struct population_parse_state *state = parser_priv(p);
	struct population_record *record;
	struct population_record *next;
	int index;

	if (!state || state->count < 1) {
		free_parse_state(state);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (record = state->records; record; record = record->next) {
		unsigned int actor_total = 0;
		int i;

		if ((record->fields & POPULATION_FIELD_REQUIRED) !=
				POPULATION_FIELD_REQUIRED) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
		for (i = 0; i < record->profile.actor_entry_count; i++) {
			actor_total += record->profile.actors[i].maximum_count;
		}
		if (actor_total > WORLD_SPELUNK_ACTOR_MAX) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_MANY_ENTRIES;
		}
	}
	cleanup_population_data();
	profiles = mem_zalloc((size_t)state->count * sizeof(*profiles));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		profiles[index] = record->profile;
		mem_free(record);
	}
	profile_count = state->count;
	population_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_population_parser = {
	"spelunking_population",
	init_parse_population,
	run_parse_population,
	finish_parse_population,
	cleanup_population_data
};

int world_spelunk_population_profile_count(void)
{
	return population_data_loaded ? profile_count : 0;
}

const struct world_spelunk_population_profile *
world_spelunk_population_profile_by_index(int index)
{
	if (!population_data_loaded || index < 0 || index >= profile_count) {
		return NULL;
	}
	return &profiles[index];
}

const struct world_spelunk_population_profile *
world_spelunk_population_profile_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < profile_count; i++) {
		if (streq(profiles[i].id, id)) return &profiles[i];
	}
	return NULL;
}

bool world_spelunk_population_data_validate_world(void)
{
	int i;

	if (!population_data_loaded) return false;
	for (i = 0; i < profile_count; i++) {
		const struct world_spelunk_population_profile *profile = &profiles[i];
		int j;

		for (j = 0; j < profile->actor_entry_count; j++) {
			if (!world_spelunk_actor_by_id(profile->actors[j].actor_id)) {
				plog_fmt("Spelunking population %s has unknown actor %s",
					profile->id, profile->actors[j].actor_id);
				return false;
			}
		}
		for (j = 0; j < profile->object_entry_count; j++) {
			const struct world_spelunk_population_object_entry *entry =
				&profile->objects[j];
			int tval = tval_find_idx(entry->object_tval);

			if (tval <= 0 || lookup_sval(tval, entry->object_sval) < 0) {
				plog_fmt("Spelunking population %s has unknown object %s:%s",
					profile->id, entry->object_tval, entry->object_sval);
				return false;
			}
		}
	}
	return profile_count > 0;
}
