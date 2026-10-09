/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-geology-data.c
 * \brief Parser and immutable registry for generated cave geology.
 */

#include "angband.h"
#include "datafile.h"
#include "terrain-visual-data.h"
#include "world-spelunking-geology-data.h"

#include <limits.h>

enum geology_field {
	GEOLOGY_FIELD_STRATA = 0x01,
	GEOLOGY_FIELD_DECORATION_BUDGET = 0x02,
	GEOLOGY_FIELD_ALL = 0x03
};

struct geology_record {
	struct world_spelunk_geology_profile profile;
	unsigned int fields;
	struct geology_record *next;
};

struct geology_parse_state {
	int count;
	struct geology_record *records;
	struct geology_record *current;
};

static struct world_spelunk_geology_profile *profiles;
static int profile_count;
static bool geology_data_loaded;

static struct geology_record *record_by_id(
		const struct geology_parse_state *state, const char *id)
{
	struct geology_record *record;

	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->profile.id, id)) return record;
	}
	return NULL;
}

static bool placement_from_name(const char *name,
		enum world_spelunk_decoration_placement *placement)
{
	if (streq(name, "rock-face")) {
		*placement = WORLD_SPELUNK_DECORATION_ROCK_FACE;
	} else if (streq(name, "ceiling")) {
		*placement = WORLD_SPELUNK_DECORATION_CEILING;
	} else if (streq(name, "floor")) {
		*placement = WORLD_SPELUNK_DECORATION_FLOOR;
	} else if (streq(name, "water-edge")) {
		*placement = WORLD_SPELUNK_DECORATION_WATER_EDGE;
	} else {
		return false;
	}
	return true;
}

static void free_profile_strings(struct world_spelunk_geology_profile *profile)
{
	int i;

	if (!profile) return;
	string_free((char *)profile->id);
	for (i = 0; i < profile->material_count; i++) {
		string_free((char *)profile->materials[i].id);
		string_free((char *)profile->materials[i].visual_material_id);
		string_free((char *)profile->materials[i].name);
	}
	for (i = 0; i < profile->decoration_count; i++) {
		string_free((char *)profile->decorations[i].id);
		string_free((char *)profile->decorations[i].name);
	}
}

static void free_record(struct geology_record *record)
{
	if (!record) return;
	free_profile_strings(&record->profile);
	mem_free(record);
}

static void free_parse_state(struct geology_parse_state *state)
{
	struct geology_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_record(record);
	}
	mem_free(state);
}

static enum parser_error parse_profile(struct parser *p)
{
	struct geology_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	int version = parser_getint(p, "version");
	struct geology_record *record;

	if (!world_id_is_valid(id) || version < 1 || version > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_SPELUNK_GEOLOGY_PROFILE_MAX ||
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

static enum parser_error parse_range(struct parser *p, unsigned int field,
		uint16_t *minimum, uint16_t *maximum, int lower, int upper)
{
	struct geology_parse_state *state = parser_priv(p);
	int minimum_value;
	int maximum_value;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & field) return PARSE_ERROR_REPEATED_DIRECTIVE;
	minimum_value = parser_getint(p, "minimum");
	maximum_value = parser_getint(p, "maximum");
	if (minimum_value < lower || minimum_value > maximum_value ||
			maximum_value > upper) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	*minimum = (uint16_t)minimum_value;
	*maximum = (uint16_t)maximum_value;
	state->current->fields |= field;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_strata(struct parser *p)
{
	struct geology_parse_state *state = parser_priv(p);

	return parse_range(p, GEOLOGY_FIELD_STRATA,
		state->current ? &state->current->profile.min_stratum_height : NULL,
		state->current ? &state->current->profile.max_stratum_height : NULL,
		1, 32);
}

static enum parser_error parse_decoration_budget(struct parser *p)
{
	struct geology_parse_state *state = parser_priv(p);

	return parse_range(p, GEOLOGY_FIELD_DECORATION_BUDGET,
		state->current ?
			&state->current->profile.min_decoration_count : NULL,
		state->current ?
			&state->current->profile.max_decoration_count : NULL,
		0, 512);
}

static enum parser_error parse_material(struct parser *p)
{
	struct geology_parse_state *state = parser_priv(p);
	struct world_spelunk_geology_profile *profile;
	struct world_spelunk_material_definition *material;
	const char *id;
	const char *visual;
	const char *name;
	int tag;
	int weight;
	int i;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	profile = &state->current->profile;
	if (profile->material_count >= WORLD_SPELUNK_MATERIAL_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	tag = parser_getint(p, "tag");
	id = parser_getsym(p, "id");
	weight = parser_getint(p, "weight");
	visual = parser_getsym(p, "visual");
	name = parser_getstr(p, "name");
	if (tag < 1 || tag > UINT8_MAX ||
			!datafile_art_id_is_valid(id) || weight < 1 ||
			weight > UINT16_MAX || !datafile_art_id_is_valid(visual) ||
			!name || !name[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < profile->material_count; i++) {
		if (profile->materials[i].tag == tag ||
				streq(profile->materials[i].id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	material = &profile->materials[profile->material_count++];
	material->tag = (uint8_t)tag;
	material->id = string_make(id);
	material->weight = (uint16_t)weight;
	material->visual_material_id = string_make(visual);
	material->name = string_make(name);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_decoration(struct parser *p)
{
	struct geology_parse_state *state = parser_priv(p);
	struct world_spelunk_geology_profile *profile;
	struct world_spelunk_decoration_definition *decoration;
	const char *id;
	const char *placement_name;
	const char *attr_name;
	const char *name;
	enum world_spelunk_decoration_placement placement;
	int tag;
	int weight;
	int spacing;
	int attr;
	wchar_t glyph;
	int i;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	profile = &state->current->profile;
	if (profile->decoration_count >= WORLD_SPELUNK_DECORATION_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	tag = parser_getint(p, "tag");
	id = parser_getsym(p, "id");
	placement_name = parser_getsym(p, "placement");
	weight = parser_getint(p, "weight");
	spacing = parser_getint(p, "spacing");
	glyph = (wchar_t)parser_getchar(p, "glyph");
	attr_name = parser_getsym(p, "attr");
	name = parser_getstr(p, "name");
	attr = color_text_to_attr(attr_name);
	if (tag < 1 || tag > UINT8_MAX ||
			!datafile_art_id_is_valid(id) ||
			!placement_from_name(placement_name, &placement) || weight < 1 ||
			weight > UINT16_MAX || spacing < 1 || spacing > 32 ||
			glyph < 33 || glyph > 126 || attr < 0 || attr > UINT8_MAX ||
			!name || !name[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < profile->decoration_count; i++) {
		if (profile->decorations[i].tag == tag ||
				streq(profile->decorations[i].id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	decoration = &profile->decorations[profile->decoration_count++];
	decoration->tag = (uint8_t)tag;
	decoration->id = string_make(id);
	decoration->placement = placement;
	decoration->weight = (uint16_t)weight;
	decoration->minimum_spacing = (uint16_t)spacing;
	decoration->glyph = glyph;
	decoration->attr = (uint8_t)attr;
	decoration->name = string_make(name);
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_geology(void)
{
	struct parser *p = parser_new();
	struct geology_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "profile sym id int version", parse_profile);
	parser_reg(p, "strata int minimum int maximum", parse_strata);
	parser_reg(p, "decoration-budget int minimum int maximum",
		parse_decoration_budget);
	parser_reg(p, "material int tag sym id int weight sym visual str name",
		parse_material);
	parser_reg(p, "decoration int tag sym id sym placement int weight int spacing char glyph sym attr str name",
		parse_decoration);
	return p;
}

static errr run_parse_geology(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_geology");
}

static void cleanup_geology_data(void)
{
	int i;

	for (i = 0; i < profile_count; i++) {
		free_profile_strings(&profiles[i]);
	}
	mem_free(profiles);
	profiles = NULL;
	profile_count = 0;
	geology_data_loaded = false;
}

static errr finish_parse_geology(struct parser *p)
{
	struct geology_parse_state *state = parser_priv(p);
	struct geology_record *record;
	struct geology_record *next;
	int index;

	if (!state || state->count < 1) {
		free_parse_state(state);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (record = state->records; record; record = record->next) {
		if (record->fields != GEOLOGY_FIELD_ALL ||
				!record->profile.material_count ||
				(record->profile.max_decoration_count > 0 &&
				 !record->profile.decoration_count)) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	cleanup_geology_data();
	profiles = mem_zalloc((size_t)state->count * sizeof(*profiles));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		profiles[index] = record->profile;
		mem_free(record);
	}
	profile_count = state->count;
	geology_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_geology_parser = {
	"spelunking_geology",
	init_parse_geology,
	run_parse_geology,
	finish_parse_geology,
	cleanup_geology_data
};

int world_spelunk_geology_profile_count(void)
{
	return geology_data_loaded ? profile_count : 0;
}

const struct world_spelunk_geology_profile *
world_spelunk_geology_profile_by_index(int index)
{
	if (!geology_data_loaded || index < 0 || index >= profile_count) return NULL;
	return &profiles[index];
}

const struct world_spelunk_geology_profile *
world_spelunk_geology_profile_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < profile_count; i++) {
		if (streq(profiles[i].id, id)) return &profiles[i];
	}
	return NULL;
}

const struct world_spelunk_material_definition *
world_spelunk_geology_material_by_tag(
		const struct world_spelunk_geology_profile *profile, uint8_t tag)
{
	int i;

	if (!profile || tag == WORLD_SPELUNK_GEOLOGY_TAG_NONE) return NULL;
	for (i = 0; i < profile->material_count; i++) {
		if (profile->materials[i].tag == tag) return &profile->materials[i];
	}
	return NULL;
}

const struct world_spelunk_decoration_definition *
world_spelunk_geology_decoration_by_tag(
		const struct world_spelunk_geology_profile *profile, uint8_t tag)
{
	int i;

	if (!profile || tag == WORLD_SPELUNK_GEOLOGY_TAG_NONE) return NULL;
	for (i = 0; i < profile->decoration_count; i++) {
		if (profile->decorations[i].tag == tag) {
			return &profile->decorations[i];
		}
	}
	return NULL;
}

bool world_spelunk_geology_data_validate_visuals(void)
{
	int i;

	for (i = 0; i < profile_count; i++) {
		const struct world_spelunk_geology_profile *profile = &profiles[i];
		int j;

		for (j = 0; j < profile->material_count; j++) {
			const struct world_spelunk_material_definition *material =
				&profile->materials[j];

			if (!terrain_visual_material_is_known(
					material->visual_material_id)) {
				plog_fmt("Spelunking geology %s has unknown visual material %s",
					profile->id, material->visual_material_id);
				return false;
			}
		}
	}
	return profile_count > 0;
}
