/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-actor-data.c
 * \brief Parser and immutable registry for side-view actors and spawns.
 *
 *
 */

#include "angband.h"
#include "datafile.h"
#include "game-world.h"
#include "world-spelunking-actor-data.h"

#include <limits.h>
#include <string.h>

enum actor_field {
	ACTOR_FIELD_DEATH_CAUSE = 0x01,
	ACTOR_FIELD_COMBAT = 0x02,
	ACTOR_FIELD_VITALS = 0x04,
	ACTOR_FIELD_APPEARANCE = 0x08,
	ACTOR_FIELD_ALL = 0x0f
};

struct actor_record {
	struct world_spelunk_actor_definition actor;
	unsigned int fields;
	struct actor_record *next;
};

struct spawn_record {
	struct world_spelunk_spawn_definition spawn;
	char *actor_id;
	struct spawn_record *next;
};

struct actor_parse_state {
	int actor_count;
	int spawn_count;
	struct actor_record *actors;
	struct actor_record *current_actor;
	struct spawn_record *spawns;
	struct spawn_record *current_spawn;
};

static struct world_spelunk_actor_definition *actor_definitions;
static int actor_definition_count;
static struct world_spelunk_spawn_definition *spawn_definitions;
static int spawn_definition_count;
static bool actor_data_loaded;

static struct actor_record *actor_record_by_id(
		const struct actor_parse_state *state, const char *id)
{
	struct actor_record *record;

	for (record = state ? state->actors : NULL; record;
			record = record->next) {
		if (streq(record->actor.id, id)) return record;
	}
	return NULL;
}

static struct spawn_record *spawn_record_by_id(
		const struct actor_parse_state *state, const char *id)
{
	struct spawn_record *record;

	for (record = state ? state->spawns : NULL; record;
			record = record->next) {
		if (streq(record->spawn.id, id)) return record;
	}
	return NULL;
}

static void free_actor_record(struct actor_record *record)
{
	if (!record) return;
	string_free((char *)record->actor.id);
	string_free((char *)record->actor.name);
	string_free((char *)record->actor.death_cause);
	mem_free(record);
}

static void free_spawn_record(struct spawn_record *record)
{
	if (!record) return;
	string_free((char *)record->spawn.id);
	string_free((char *)record->spawn.location_id);
	string_free(record->actor_id);
	mem_free(record);
}

static void free_parse_state(struct actor_parse_state *state)
{
	struct actor_record *actor;
	struct spawn_record *spawn;

	if (!state) return;
	while ((actor = state->actors) != NULL) {
		state->actors = actor->next;
		free_actor_record(actor);
	}
	while ((spawn = state->spawns) != NULL) {
		state->spawns = spawn->next;
		free_spawn_record(spawn);
	}
	mem_free(state);
}

static enum parser_error parse_actor(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *name = parser_getstr(p, "name");
	struct actor_record *record;
	struct actor_record *other;

	if (!world_id_is_valid(id) || !name || !name[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->actor_count >= WORLD_SPELUNK_ACTOR_DEFINITION_MAX ||
			actor_record_by_id(state, id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	for (other = state->actors; other; other = other->next) {
		if (!my_stricmp(other->actor.name, name)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	record = mem_zalloc(sizeof(*record));
	record->actor.id = string_make(id);
	record->actor.name = string_make(name);
	record->next = state->actors;
	state->actors = record;
	state->current_actor = record;
	state->current_spawn = NULL;
	state->actor_count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_death_cause(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	const char *cause = parser_getstr(p, "cause");
	struct actor_record *record = state->current_actor;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & ACTOR_FIELD_DEATH_CAUSE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!cause || !cause[0]) return PARSE_ERROR_INVALID_VALUE;
	record->actor.death_cause = string_make(cause);
	record->fields |= ACTOR_FIELD_DEATH_CAUSE;
	return PARSE_ERROR_NONE;
}

static int actor_color_attr(const char *color)
{
	int attr;

	if (!color || !color[0]) return -1;
	for (attr = 0; attr < MAX_COLORS; attr++) {
		if ((color[1] == '\0' && color_table[attr].index_char == color[0]) ||
				!my_stricmp(color_table[attr].name, color)) {
			return attr;
		}
	}
	return -1;
}

static enum parser_error parse_appearance(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	struct actor_record *record = state->current_actor;
	wchar_t glyph = parser_getchar(p, "glyph");
	int attr = actor_color_attr(parser_getsym(p, "color"));

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & ACTOR_FIELD_APPEARANCE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (glyph < 32 || glyph > 126) return PARSE_ERROR_INVALID_VALUE;
	if (attr < 0) return PARSE_ERROR_INVALID_COLOR;
	record->actor.glyph = glyph;
	record->actor.attr = (uint8_t)attr;
	record->fields |= ACTOR_FIELD_APPEARANCE;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_combat(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	struct actor_record *record = state->current_actor;
	int armour = parser_getint(p, "armour");
	int to_hit = parser_getint(p, "to-hit");
	int dice = parser_getint(p, "dice");
	int sides = parser_getint(p, "sides");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & ACTOR_FIELD_COMBAT) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (armour < 0 || armour > 255 || to_hit < 0 || to_hit > 1000 ||
			dice < 1 || dice > 255 || sides < 1 || sides > 255) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	record->actor.armour = armour;
	record->actor.to_hit = to_hit;
	record->actor.damage_dice = dice;
	record->actor.damage_sides = sides;
	record->fields |= ACTOR_FIELD_COMBAT;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_vitals(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	struct actor_record *record = state->current_actor;
	int hitpoints = parser_getint(p, "hitpoints");
	int speed = parser_getint(p, "speed");
	int experience = parser_getint(p, "experience");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & ACTOR_FIELD_VITALS) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (hitpoints < 1 || hitpoints > UINT16_MAX || speed < 1 ||
			speed > 199 || experience < 0 || experience > INT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	record->actor.hitpoints = hitpoints;
	record->actor.speed = speed;
	record->actor.experience = experience;
	record->fields |= ACTOR_FIELD_VITALS;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_spawn(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *location = parser_getsym(p, "location");
	const char *actor = parser_getsym(p, "actor");
	struct spawn_record *record;

	if (!world_id_is_valid(id) || !world_id_is_valid(location) ||
			!world_id_is_valid(actor)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->spawn_count >= WORLD_SPELUNK_SPAWN_MAX ||
			spawn_record_by_id(state, id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	record = mem_zalloc(sizeof(*record));
	record->spawn.id = string_make(id);
	record->spawn.location_id = string_make(location);
	record->actor_id = string_make(actor);
	record->next = state->spawns;
	state->spawns = record;
	state->current_spawn = record;
	state->current_actor = NULL;
	state->spawn_count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_candidate(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	struct spawn_record *record = state->current_spawn;
	struct world_spelunk_spawn_candidate *candidate;
	int priority = parser_getint(p, "priority");
	int x = parser_getint(p, "x");
	int y = parser_getint(p, "y");
	int i;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (priority < 1 || x < 0 || x >= WORLD_LOCATION_WIDTH_MAX || y < 0 ||
			y >= WORLD_LOCATION_HEIGHT_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (record->spawn.candidate_count >= WORLD_SPELUNK_SPAWN_CANDIDATE_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	for (i = 0; i < record->spawn.candidate_count; i++) {
		candidate = &record->spawn.candidates[i];
		if (candidate->priority == priority ||
				(candidate->x == x && candidate->y == y)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	candidate = &record->spawn.candidates[record->spawn.candidate_count++];
	candidate->priority = priority;
	candidate->x = x;
	candidate->y = y;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_actors(void)
{
	struct parser *p = parser_new();
	struct actor_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "actor sym id str name", parse_actor);
	parser_reg(p, "death-cause str cause", parse_death_cause);
	parser_reg(p, "appearance char glyph sym color", parse_appearance);
	parser_reg(p, "combat int armour int to-hit int dice int sides",
		parse_combat);
	parser_reg(p, "vitals int hitpoints int speed int experience",
		parse_vitals);
	parser_reg(p, "spawn sym id sym location sym actor", parse_spawn);
	parser_reg(p, "candidate int priority int x int y", parse_candidate);
	return p;
}

static errr run_parse_actors(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_actor");
}

static int compare_candidate(const void *left, const void *right)
{
	const struct world_spelunk_spawn_candidate *a = left;
	const struct world_spelunk_spawn_candidate *b = right;

	return (a->priority > b->priority) - (a->priority < b->priority);
}

static errr validate_parse_state(const struct actor_parse_state *state)
{
	struct actor_record *actor;
	struct spawn_record *spawn;

	if (!state || state->actor_count < 1 || state->spawn_count < 1) {
		plog("Spelunking actor data requires actors and authored spawns");
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (actor = state->actors; actor; actor = actor->next) {
		if (actor->fields != ACTOR_FIELD_ALL) {
			plog_fmt("Spelunking actor %s is incomplete", actor->actor.id);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	for (spawn = state->spawns; spawn; spawn = spawn->next) {
		if (!spawn->spawn.candidate_count ||
				!actor_record_by_id(state, spawn->actor_id)) {
			plog_fmt("Spelunking spawn %s is incomplete or has an unknown actor",
				spawn->spawn.id);
			return PARSE_ERROR_INVALID_VALUE;
		}
	}
	return PARSE_ERROR_NONE;
}

static void cleanup_actor_data(void)
{
	int i;

	for (i = 0; i < spawn_definition_count; i++) {
		string_free((char *)spawn_definitions[i].id);
		string_free((char *)spawn_definitions[i].location_id);
	}
	mem_free(spawn_definitions);
	spawn_definitions = NULL;
	spawn_definition_count = 0;
	for (i = 0; i < actor_definition_count; i++) {
		string_free((char *)actor_definitions[i].id);
		string_free((char *)actor_definitions[i].name);
		string_free((char *)actor_definitions[i].death_cause);
	}
	mem_free(actor_definitions);
	actor_definitions = NULL;
	actor_definition_count = 0;
	actor_data_loaded = false;
}

static errr finish_parse_actors(struct parser *p)
{
	struct actor_parse_state *state = parser_priv(p);
	struct actor_record *actor;
	struct actor_record *next_actor;
	struct spawn_record *spawn;
	struct spawn_record *next_spawn;
	errr result = validate_parse_state(state);
	int index;

	if (result != PARSE_ERROR_NONE) {
		free_parse_state(state);
		parser_destroy(p);
		return result;
	}
	cleanup_actor_data();
	actor_definitions = mem_zalloc((size_t)state->actor_count *
		sizeof(*actor_definitions));
	index = state->actor_count - 1;
	for (actor = state->actors; actor; actor = next_actor, index--) {
		next_actor = actor->next;
		actor_definitions[index] = actor->actor;
		mem_free(actor);
	}
	actor_definition_count = state->actor_count;
	spawn_definitions = mem_zalloc((size_t)state->spawn_count *
		sizeof(*spawn_definitions));
	index = state->spawn_count - 1;
	for (spawn = state->spawns; spawn; spawn = next_spawn, index--) {
		next_spawn = spawn->next;
		spawn_definitions[index] = spawn->spawn;
		spawn_definitions[index].actor = world_spelunk_actor_by_id(
			spawn->actor_id);
		qsort(spawn_definitions[index].candidates,
			(size_t)spawn_definitions[index].candidate_count,
			sizeof(spawn_definitions[index].candidates[0]),
			compare_candidate);
		string_free(spawn->actor_id);
		mem_free(spawn);
	}
	spawn_definition_count = state->spawn_count;
	actor_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_actor_parser = {
	"spelunking_actor",
	init_parse_actors,
	run_parse_actors,
	finish_parse_actors,
	cleanup_actor_data
};

int world_spelunk_actor_definition_count(void)
{
	return actor_data_loaded ? actor_definition_count : 0;
}

const struct world_spelunk_actor_definition *world_spelunk_actor_by_index(
		int index)
{
	if (!actor_data_loaded || index < 0 || index >= actor_definition_count) {
		return NULL;
	}
	return &actor_definitions[index];
}

const struct world_spelunk_actor_definition *world_spelunk_actor_by_id(
		const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < actor_definition_count; i++) {
		if (streq(actor_definitions[i].id, id)) return &actor_definitions[i];
	}
	return NULL;
}

int world_spelunk_spawn_definition_count(void)
{
	return actor_data_loaded ? spawn_definition_count : 0;
}

const struct world_spelunk_spawn_definition *world_spelunk_spawn_by_index(
		int index)
{
	if (!actor_data_loaded || index < 0 || index >= spawn_definition_count) {
		return NULL;
	}
	return &spawn_definitions[index];
}

const struct world_spelunk_spawn_definition *world_spelunk_spawn_by_id(
		const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < spawn_definition_count; i++) {
		if (streq(spawn_definitions[i].id, id)) return &spawn_definitions[i];
	}
	return NULL;
}

bool world_spelunk_actor_data_validate_world(void)
{
	int i;

	if (!actor_data_loaded) return false;
	for (i = 0; i < spawn_definition_count; i++) {
		const struct world_spelunk_spawn_definition *spawn =
			&spawn_definitions[i];
		const struct level *level = level_by_id(spawn->location_id);
		int j;

		if (!spawn->actor || !level || level->mode != WORLD_MODE_SPELUNKING) {
			plog_fmt("Spelunking spawn %s references an invalid actor or location",
				spawn->id);
			return false;
		}
		for (j = 0; j < spawn->candidate_count; j++) {
			const struct world_spelunk_spawn_candidate *candidate =
				&spawn->candidates[j];

			if (candidate->x >= level->width || candidate->y >= level->height) {
				plog_fmt("Spelunking spawn %s has an out-of-bounds candidate",
					spawn->id);
				return false;
			}
		}
	}
	return true;
}
