/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-objective-data.c
 * \brief Parser and derived-state adapter for optional objectives.
 */

#include "angband.h"
#include "datafile.h"
#include "message.h"
#include "obj-util.h"
#include "obj-tval.h"
#include "player.h"
#include "world-larder-data.h"
#include "world-objective-data.h"

enum objective_field {
	OBJECTIVE_DESCRIPTION = 0x01,
	OBJECTIVE_TARGET = 0x02,
	OBJECTIVE_REWARD = 0x04,
	OBJECTIVE_COMPLETION_SOUND = 0x08,
	OBJECTIVE_ALL = 0x07
};

struct objective_record {
	struct world_objective_definition definition;
	unsigned int fields;
	struct objective_record *next;
};

struct objective_parse_state {
	int count;
	struct objective_record *records;
	struct objective_record *current;
};

static struct world_objective_definition *objectives;
static int objective_count;

static void free_definition(struct world_objective_definition *definition)
{
	if (!definition) return;
	string_free((char *)definition->id);
	string_free((char *)definition->title);
	string_free((char *)definition->description);
	string_free((char *)definition->reward);
	string_free((char *)definition->target_id);
	string_free((char *)definition->tval_name);
}

static void cleanup_objectives(void)
{
	int i;

	for (i = 0; i < objective_count; i++) free_definition(&objectives[i]);
	mem_free(objectives);
	objectives = NULL;
	objective_count = 0;
}

static void free_parse_state(struct objective_parse_state *state)
{
	struct objective_record *record;

	if (!state) return;
	record = state->records;
	while (record) {
		struct objective_record *next = record->next;

		free_definition(&record->definition);
		mem_free(record);
		record = next;
	}
	mem_free(state);
}

static enum parser_error parse_objective(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *kind = parser_getsym(p, "kind");
	const char *title = parser_getstr(p, "title");
	struct objective_record *record;

	if (!world_id_is_valid(id) || !title[0] ||
			(!streq(kind, "item") && !streq(kind, "larder") &&
			 !streq(kind, "route"))) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->definition.id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	if (state->count >= WORLD_OBJECTIVE_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	record = mem_zalloc(sizeof(*record));
	record->definition.id = string_make(id);
	record->definition.title = string_make(title);
	record->definition.kind = streq(kind, "item") ? WORLD_OBJECTIVE_ITEM :
		streq(kind, "larder") ? WORLD_OBJECTIVE_LARDER :
		WORLD_OBJECTIVE_ROUTE;
	record->next = state->records;
	state->records = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_description(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->fields & OBJECTIVE_DESCRIPTION) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->definition.description = string_make(text);
	state->current->fields |= OBJECTIVE_DESCRIPTION;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_item_target(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	const char *tval = parser_getsym(p, "tval");
	const char *name = parser_getstr(p, "name");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->definition.kind != WORLD_OBJECTIVE_ITEM ||
			!tval[0] || !name[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->fields & OBJECTIVE_TARGET) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->definition.tval_name = string_make(tval);
	state->current->definition.target_id = string_make(name);
	state->current->definition.target = 1;
	state->current->fields |= OBJECTIVE_TARGET;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_larder_target(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "milestone");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->definition.kind != WORLD_OBJECTIVE_LARDER ||
			!world_larder_milestone_by_id(id)) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->fields & OBJECTIVE_TARGET) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->definition.target_id = string_make(id);
	state->current->definition.target =
		world_larder_milestone_by_id(id)->target;
	state->current->fields |= OBJECTIVE_TARGET;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_route_target(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "route");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->definition.kind != WORLD_OBJECTIVE_ROUTE ||
			!world_route_by_id(id)) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->fields & OBJECTIVE_TARGET) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->definition.target_id = string_make(id);
	state->current->definition.target = 1;
	state->current->fields |= OBJECTIVE_TARGET;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_reward(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->fields & OBJECTIVE_REWARD) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->definition.reward = string_make(text);
	state->current->fields |= OBJECTIVE_REWARD;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_completion_sound(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	int message;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & OBJECTIVE_COMPLETION_SOUND) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	message = message_lookup_by_name(parser_getsym(p, "type"));
	if (message <= MSG_GENERIC || message >= MSG_MAX) {
		return PARSE_ERROR_INVALID_MESSAGE;
	}
	state->current->definition.completion_message = message;
	state->current->fields |= OBJECTIVE_COMPLETION_SOUND;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_objectives(void)
{
	struct parser *p = parser_new();
	struct objective_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "objective sym id sym kind str title", parse_objective);
	parser_reg(p, "description str text", parse_description);
	parser_reg(p, "item sym tval str name", parse_item_target);
	parser_reg(p, "larder sym milestone", parse_larder_target);
	parser_reg(p, "route sym route", parse_route_target);
	parser_reg(p, "reward str text", parse_reward);
	parser_reg(p, "completion-sound sym type", parse_completion_sound);
	return p;
}

static errr run_parse_objectives(struct parser *p)
{
	return parse_file_quit_not_found(p, "objective");
}

static errr finish_parse_objectives(struct parser *p)
{
	struct objective_parse_state *state = parser_priv(p);
	struct objective_record *record;
	struct objective_record *next;
	errr result = PARSE_ERROR_NONE;
	int index = state ? state->count - 1 : -1;

	if (!state || !state->count) result = PARSE_ERROR_TOO_FEW_ENTRIES;
	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if ((record->fields & OBJECTIVE_ALL) != OBJECTIVE_ALL) {
			result = PARSE_ERROR_TOO_FEW_ENTRIES;
			break;
		}
		if (record->definition.kind == WORLD_OBJECTIVE_ITEM) {
			int tval = tval_find_idx(record->definition.tval_name);
			int sval = tval >= 0 ?
				lookup_sval(tval, record->definition.target_id) : -1;

			if (sval < 0 || !lookup_kind(tval, sval)) {
				result = PARSE_ERROR_UNRECOGNISED_SVAL;
				break;
			}
			record->definition.tval = tval;
			record->definition.sval = sval;
		}
	}
	if (result != PARSE_ERROR_NONE) {
		free_parse_state(state);
		parser_destroy(p);
		return result;
	}
	cleanup_objectives();
	objectives = mem_zalloc((size_t)state->count * sizeof(*objectives));
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		objectives[index] = record->definition;
		mem_free(record);
	}
	objective_count = state->count;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser objective_parser = {
	"objective", init_parse_objectives, run_parse_objectives,
	finish_parse_objectives, cleanup_objectives
};

int world_objective_count(void)
{
	return objective_count;
}

const struct world_objective_definition *world_objective_by_index(int index)
{
	return index >= 0 && index < objective_count ? &objectives[index] : NULL;
}

const struct world_objective_definition *world_objective_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < objective_count; i++) {
		if (streq(objectives[i].id, id)) return &objectives[i];
	}
	return NULL;
}

bool world_objective_progress(const struct player *p,
		const struct world_objective_definition *definition,
		struct world_objective_progress *progress)
{
	if (!p || !definition || !progress) return false;
	memset(progress, 0, sizeof(*progress));
	progress->target = definition->target;
	if (definition->kind == WORLD_OBJECTIVE_ITEM) {
		const struct object *obj;

		for (obj = p->gear; obj; obj = obj->next) {
			if (obj->kind && obj->kind->tval == definition->tval &&
					obj->kind->sval == definition->sval && obj->number > 0) {
				progress->current = 1;
				break;
			}
		}
	} else if (definition->kind == WORLD_OBJECTIVE_LARDER) {
		progress->current = MIN(p->larder.food_points, progress->target);
	} else {
		progress->current = world_player_route_is_unlocked(p,
			definition->target_id) ? 1 : 0;
	}
	progress->complete = progress->current >= progress->target;
	return true;
}

int world_objective_item_completion_message(const struct player *p,
		const struct object *obj)
{
	int i;

	if (!p || !obj || !obj->kind) return MSG_GENERIC;
	for (i = 0; i < objective_count; i++) {
		const struct world_objective_definition *definition = &objectives[i];
		struct world_objective_progress progress;

		if (definition->kind != WORLD_OBJECTIVE_ITEM ||
				definition->completion_message <= MSG_GENERIC ||
				definition->tval != obj->kind->tval ||
				definition->sval != obj->kind->sval ||
				!world_objective_progress(p, definition, &progress) ||
				progress.complete) {
			continue;
		}
		return definition->completion_message;
	}
	return MSG_GENERIC;
}
