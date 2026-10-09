/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-larder-data.c
 * \brief Parser and immutable registry for village-larder milestones.
 */

#include "angband.h"
#include "datafile.h"
#include "game-world.h"
#include "world-larder.h"
#include "world-larder-data.h"

#include <limits.h>

enum larder_field {
	LARDER_FIELD_MESSAGE = 0x01,
	LARDER_FIELD_REWARD = 0x02,
	LARDER_FIELD_ALL = 0x03
};

struct larder_record {
	struct world_larder_milestone_definition definition;
	unsigned int fields;
	struct larder_record *next;
};

struct larder_parse_state {
	int count;
	struct larder_record *records;
	struct larder_record *current;
};

static struct world_larder_milestone_definition *milestones;
static int milestone_count;
static bool larder_data_loaded;

static void free_record(struct larder_record *record)
{
	if (!record) return;
	string_free((char *)record->definition.id);
	string_free((char *)record->definition.name);
	string_free((char *)record->definition.message);
	string_free((char *)record->definition.reward);
	mem_free(record);
}

static void free_parse_state(struct larder_parse_state *state)
{
	struct larder_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_record(record);
	}
	mem_free(state);
}

static enum parser_error parse_milestone(struct parser *p)
{
	struct larder_parse_state *state = parser_priv(p);
	unsigned int save_index = parser_getuint(p, "save-index");
	unsigned int target = parser_getuint(p, "target");
	const char *id = parser_getsym(p, "id");
	const char *name = parser_getstr(p, "name");
	struct larder_record *record;
	struct larder_record *other;

	if (!world_id_is_valid(id) || !name || !name[0] || !save_index ||
			save_index > UINT8_MAX || !target) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_LARDER_MILESTONE_DEFINITION_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	for (other = state->records; other; other = other->next) {
		if (other->definition.save_index == save_index ||
				streq(other->definition.id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	record = mem_zalloc(sizeof(*record));
	record->definition.save_index = (uint8_t)save_index;
	record->definition.id = string_make(id);
	record->definition.target = (uint32_t)target;
	record->definition.name = string_make(name);
	record->next = state->records;
	state->records = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_message(struct parser *p)
{
	struct larder_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!text || !text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->fields & LARDER_FIELD_MESSAGE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->definition.message = string_make(text);
	state->current->fields |= LARDER_FIELD_MESSAGE;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_reward(struct parser *p)
{
	struct larder_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!text || !text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->fields & LARDER_FIELD_REWARD) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->definition.reward = string_make(text);
	state->current->fields |= LARDER_FIELD_REWARD;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_larder(void)
{
	struct parser *p = parser_new();
	struct larder_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "milestone uint save-index sym id uint target str name",
		parse_milestone);
	parser_reg(p, "message str text", parse_message);
	parser_reg(p, "reward str text", parse_reward);
	return p;
}

static errr run_parse_larder(struct parser *p)
{
	return parse_file_quit_not_found(p, "larder");
}

static errr validate_parse_state(const struct larder_parse_state *state)
{
	uint32_t previous_target = 0;
	int index;

	if (!state || !state->count) return PARSE_ERROR_TOO_FEW_ENTRIES;
	for (index = 1; index <= state->count; index++) {
		const struct larder_record *record;

		for (record = state->records; record; record = record->next) {
			if (record->definition.save_index == index) break;
		}
		if (!record || record->fields != LARDER_FIELD_ALL ||
				record->definition.target <= previous_target) {
			return PARSE_ERROR_INVALID_VALUE;
		}
		previous_target = record->definition.target;
	}
	return PARSE_ERROR_NONE;
}

static void cleanup_larder_data(void)
{
	int i;

	for (i = 0; i < milestone_count; i++) {
		string_free((char *)milestones[i].id);
		string_free((char *)milestones[i].name);
		string_free((char *)milestones[i].message);
		string_free((char *)milestones[i].reward);
	}
	mem_free(milestones);
	milestones = NULL;
	milestone_count = 0;
	larder_data_loaded = false;
}

static errr finish_parse_larder(struct parser *p)
{
	struct larder_parse_state *state = parser_priv(p);
	struct larder_record *record;
	struct larder_record *next;
	errr result = validate_parse_state(state);

	if (result != PARSE_ERROR_NONE) {
		free_parse_state(state);
		parser_destroy(p);
		return result;
	}
	cleanup_larder_data();
	milestones = mem_zalloc((size_t)state->count * sizeof(*milestones));
	for (record = state->records; record; record = next) {
		next = record->next;
		milestones[record->definition.save_index - 1] = record->definition;
		mem_free(record);
	}
	milestone_count = state->count;
	larder_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser larder_parser = {
	"larder",
	init_parse_larder,
	run_parse_larder,
	finish_parse_larder,
	cleanup_larder_data
};

int world_larder_milestone_count(void)
{
	return larder_data_loaded ? milestone_count : 0;
}

const struct world_larder_milestone_definition *
world_larder_milestone_by_save_index(uint8_t save_index)
{
	if (!larder_data_loaded || !save_index || save_index > milestone_count) {
		return NULL;
	}
	return &milestones[save_index - 1];
}

const struct world_larder_milestone_definition *
world_larder_milestone_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < milestone_count; i++) {
		if (streq(milestones[i].id, id)) return &milestones[i];
	}
	return NULL;
}

const struct world_larder_milestone_definition *
world_larder_next_milestone(const struct world_larder_state *state)
{
	if (!state || state->milestone >= milestone_count) return NULL;
	return world_larder_milestone_by_save_index(state->milestone + 1);
}

const struct world_larder_milestone_definition *
world_larder_current_milestone(const struct world_larder_state *state)
{
	return state ? world_larder_milestone_by_save_index(state->milestone) : NULL;
}

bool world_larder_has_milestone(const struct world_larder_state *state,
		const char *id)
{
	const struct world_larder_milestone_definition *definition =
		world_larder_milestone_by_id(id);

	return state && definition && state->milestone >= definition->save_index;
}
