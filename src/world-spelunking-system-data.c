/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-system-data.c
 * \brief Parser and immutable registry for procedural cave-system topology.
 */

#include "angband.h"
#include "datafile.h"
#include "game-world.h"
#include "world-entry.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-system-data.h"
#include "world-spelunking-system.h"

#include <limits.h>
#include <string.h>

enum system_field {
	SYSTEM_FIELD_NAME = 0x01,
	SYSTEM_FIELD_DESCRIPTION = 0x02,
	SYSTEM_FIELD_ROOT = 0x04,
	SYSTEM_FIELD_SECTIONS = 0x08,
	SYSTEM_FIELD_SHAPE = 0x10,
	SYSTEM_FIELD_LOCATION = 0x20,
	SYSTEM_FIELD_PORTALS = 0x40,
	SYSTEM_FIELD_ALL = 0x7f
};

struct system_record {
	struct world_spelunk_system_profile profile;
	unsigned int fields;
	struct system_record *next;
};

struct system_parse_state {
	int count;
	struct system_record *records;
	struct system_record *current;
};

static struct world_spelunk_system_profile *profiles;
static int profile_count;
static bool system_data_loaded;

static struct system_record *record_by_id(
		const struct system_parse_state *state, const char *id)
{
	struct system_record *record;

	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->profile.id, id)) return record;
	}
	return NULL;
}

static void free_profile_strings(struct world_spelunk_system_profile *profile)
{
	int i;

	if (!profile) return;
	string_free((char *)profile->id);
	string_free((char *)profile->name);
	string_free((char *)profile->description);
	string_free((char *)profile->root_recipe_id);
	string_free((char *)profile->location_id);
	for (i = 0; i < profile->portal_count; i++) {
		string_free((char *)profile->portals[i].id);
		string_free((char *)profile->portals[i].entry_id);
	}
}

static void free_parse_state(struct system_parse_state *state)
{
	struct system_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_profile_strings(&record->profile);
		mem_free(record);
	}
	mem_free(state);
}

static enum parser_error parse_system(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	int version = parser_getint(p, "version");
	struct system_record *record;

	/* Planned node and passage IDs append ".section.000" or an equally
	 * long suffix. Reject a profile which could only generate truncated,
	 * colliding stable identities. */
	if (!world_id_is_valid(id) ||
			strlen(id) + sizeof(".section.000") > WORLD_ID_LEN ||
			version < 1 || version > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_SPELUNK_SYSTEM_PROFILE_MAX ||
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

static enum parser_error parse_text(struct parser *p, unsigned int field,
		const char *token, const char **destination, bool stable_id)
{
	struct system_parse_state *state = parser_priv(p);
	const char *value;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & field) return PARSE_ERROR_REPEATED_DIRECTIVE;
	value = stable_id ? parser_getsym(p, token) : parser_getstr(p, token);
	if (!value || !value[0] || (stable_id && !world_id_is_valid(value))) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	*destination = string_make(value);
	state->current->fields |= field;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_name(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);

	return parse_text(p, SYSTEM_FIELD_NAME, "name",
		state->current ? &state->current->profile.name : NULL, false);
}

static enum parser_error parse_description(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);

	return parse_text(p, SYSTEM_FIELD_DESCRIPTION, "description",
		state->current ? &state->current->profile.description : NULL, false);
}

static enum parser_error parse_root(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);

	return parse_text(p, SYSTEM_FIELD_ROOT, "recipe",
		state->current ? &state->current->profile.root_recipe_id : NULL, true);
}

static enum parser_error parse_location(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);

	return parse_text(p, SYSTEM_FIELD_LOCATION, "location",
		state->current ? &state->current->profile.location_id : NULL, true);
}

static enum parser_error parse_portal(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);
	struct world_spelunk_system_profile *profile;
	struct world_spelunk_system_portal_contract *portal;
	const char *id;
	const char *entry_id;
	const char *target;
	const char *anchor;
	const char *state_name;
	int minimum;
	int maximum;
	int i;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	profile = &state->current->profile;
	if (profile->portal_count >= WORLD_SPELUNK_SYSTEM_PORTAL_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	id = parser_getsym(p, "id");
	entry_id = parser_getsym(p, "entry");
	target = parser_getsym(p, "target");
	anchor = parser_getsym(p, "anchor");
	state_name = parser_getsym(p, "state");
	minimum = parser_getint(p, "minimum");
	maximum = parser_getint(p, "maximum");
	if (!world_id_is_valid(id) || !world_id_is_valid(entry_id) ||
			(!streq(target, "root") && !streq(target, "deepest")) ||
			(!streq(anchor, "route") && !streq(anchor, "upper-rail")) ||
			(!streq(state_name, "open") && !streq(state_name, "sealed")) ||
			(streq(anchor, "upper-rail") && !streq(target, "root")) ||
			minimum < 0 || minimum > maximum || maximum > 100) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < profile->portal_count; i++) {
		if (streq(profile->portals[i].id, id) ||
				streq(profile->portals[i].entry_id, entry_id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	portal = &profile->portals[profile->portal_count++];
	portal->id = string_make(id);
	portal->entry_id = string_make(entry_id);
	portal->min_depth_percent = (uint16_t)minimum;
	portal->max_depth_percent = (uint16_t)maximum;
	portal->target = streq(target, "root") ? WORLD_SPELUNK_PORTAL_ROOT :
		WORLD_SPELUNK_PORTAL_DEEPEST;
	portal->anchor = streq(anchor, "upper-rail") ?
		WORLD_SPELUNK_PORTAL_UPPER_RAIL : WORLD_SPELUNK_PORTAL_ANY_ROUTE;
	portal->initially_enabled = streq(state_name, "open");
	state->current->fields |= SYSTEM_FIELD_PORTALS;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_sections(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);
	int minimum;
	int maximum;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & SYSTEM_FIELD_SECTIONS) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	minimum = parser_getint(p, "minimum");
	maximum = parser_getint(p, "maximum");
	if (minimum < 1 || minimum > maximum ||
			maximum > WORLD_SPELUNK_SYSTEM_NODE_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->profile.min_sections = (uint16_t)minimum;
	state->current->profile.max_sections = (uint16_t)maximum;
	state->current->fields |= SYSTEM_FIELD_SECTIONS;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_shape(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);
	int minimum_root;
	int maximum_root;
	int maximum_children;
	int minimum_depth;
	int maximum_depth;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & SYSTEM_FIELD_SHAPE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	minimum_root = parser_getint(p, "min-root");
	maximum_root = parser_getint(p, "max-root");
	maximum_children = parser_getint(p, "max-children");
	minimum_depth = parser_getint(p, "min-depth");
	maximum_depth = parser_getint(p, "max-depth");
	if (minimum_root < 0 || minimum_root > maximum_root ||
			maximum_root > WORLD_SPELUNK_SYSTEM_NODE_MAX - 1 ||
			maximum_children < 1 || maximum_children > 4 ||
			maximum_root > maximum_children || minimum_depth < 0 ||
			minimum_depth > maximum_depth ||
			maximum_depth > WORLD_SPELUNK_SYSTEM_NODE_MAX - 1) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->profile.min_root_children = (uint16_t)minimum_root;
	state->current->profile.max_root_children = (uint16_t)maximum_root;
	state->current->profile.max_children = (uint16_t)maximum_children;
	state->current->profile.min_depth = (uint16_t)minimum_depth;
	state->current->profile.max_depth = (uint16_t)maximum_depth;
	state->current->fields |= SYSTEM_FIELD_SHAPE;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_systems(void)
{
	struct parser *p = parser_new();
	struct system_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "system sym id int version", parse_system);
	parser_reg(p, "name str name", parse_name);
	parser_reg(p, "description str description", parse_description);
	parser_reg(p, "root sym recipe", parse_root);
	parser_reg(p, "location sym location", parse_location);
	parser_reg(p, "portal sym id sym entry sym target int minimum int maximum "
			"sym anchor sym state", parse_portal);
	parser_reg(p, "sections int minimum int maximum", parse_sections);
	parser_reg(p, "shape int min-root int max-root int max-children int min-depth int max-depth",
		parse_shape);
	return p;
}

static errr run_parse_systems(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_system");
}

static void cleanup_system_data(void)
{
	int i;

	for (i = 0; i < profile_count; i++) free_profile_strings(&profiles[i]);
	mem_free(profiles);
	profiles = NULL;
	profile_count = 0;
	system_data_loaded = false;
}

static errr finish_parse_systems(struct parser *p)
{
	struct system_parse_state *state = parser_priv(p);
	struct system_record *record;
	struct system_record *next;
	int index;

	if (!state || state->count < 1) {
		free_parse_state(state);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (record = state->records; record; record = record->next) {
		const struct world_spelunk_system_profile *profile = &record->profile;

		if (record->fields != SYSTEM_FIELD_ALL) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
		if (profile->min_sections < profile->min_depth +
					profile->min_root_children ||
				profile->max_depth >= profile->max_sections ||
				profile->max_root_children >= profile->max_sections) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_INVALID_VALUE;
		}
	}
	cleanup_system_data();
	profiles = mem_zalloc((size_t)state->count * sizeof(*profiles));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		profiles[index] = record->profile;
		mem_free(record);
	}
	profile_count = state->count;
	system_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_system_parser = {
	"spelunking_system",
	init_parse_systems,
	run_parse_systems,
	finish_parse_systems,
	cleanup_system_data
};

int world_spelunk_system_profile_count(void)
{
	return system_data_loaded ? profile_count : 0;
}

const struct world_spelunk_system_profile *world_spelunk_system_profile_by_index(
		int index)
{
	if (!system_data_loaded || index < 0 || index >= profile_count) return NULL;
	return &profiles[index];
}

const struct world_spelunk_system_profile *world_spelunk_system_profile_by_id(
		const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < profile_count; i++) {
		if (streq(profiles[i].id, id)) return &profiles[i];
	}
	return NULL;
}

const struct world_spelunk_system_profile *
	world_spelunk_system_profile_by_location(const char *location_id)
{
	const struct world_spelunk_system_profile *found = NULL;
	int i;

	if (!location_id) return NULL;
	for (i = 0; i < profile_count; i++) {
		if (!streq(profiles[i].location_id, location_id)) continue;
		/* Validation rejects this ambiguity at startup.  Retain a fail-closed
		 * lookup in case a caller observes an unvalidated test registry. */
		if (found) return NULL;
		found = &profiles[i];
	}
	return found;
}

bool world_spelunk_system_data_validate_recipes(void)
{
	int i;

	for (i = 0; i < profile_count; i++) {
		const struct world_spelunk_system_profile *profile = &profiles[i];
		const struct world_spelunk_recipe_definition *root =
			world_spelunk_recipe_definition_by_id(profile->root_recipe_id);
		bool has_optional = false;
		bool has_deep = false;
		int j;

		if (!root || !streq(root->system_id, profile->id) ||
				root->role != WORLD_SPELUNK_RECIPE_ROOT) {
			plog_fmt("Spelunking system %s has invalid root recipe %s",
				profile->id, profile->root_recipe_id);
			return false;
		}
		for (j = 0; j < world_spelunk_recipe_definition_count(); j++) {
			const struct world_spelunk_recipe_definition *recipe =
				world_spelunk_recipe_definition_by_index(j);

			if (!recipe || !streq(recipe->system_id, profile->id)) continue;
			if ((recipe->role == WORLD_SPELUNK_RECIPE_ROOT &&
					recipe->passages[WORLD_SPELUNK_PASSAGE_DOWN].capacity <
						profile->max_root_children) ||
					(recipe->role == WORLD_SPELUNK_RECIPE_OPTIONAL &&
					 recipe->passages[WORLD_SPELUNK_PASSAGE_DOWN].capacity <
						profile->max_children) ||
					(recipe->role != WORLD_SPELUNK_RECIPE_ROOT &&
					 !recipe->passages[WORLD_SPELUNK_PASSAGE_UP].capacity)) {
				plog_fmt("Spelunking recipe %s lacks passage capacity",
					recipe->id);
				return false;
			}
			if (recipe->role == WORLD_SPELUNK_RECIPE_OPTIONAL) {
				has_optional = true;
			} else if (recipe->role == WORLD_SPELUNK_RECIPE_DEEP) {
				has_deep = true;
			}
		}
		if (profile->max_sections > 1 && (!has_optional || !has_deep)) {
			plog_fmt("Spelunking system %s needs optional and deep recipes",
				profile->id);
			return false;
		}
	}
	return profile_count > 0;
}

bool world_spelunk_system_data_validate_world(void)
{
	int i;

	for (i = 0; i < profile_count; i++) {
		const struct world_spelunk_system_profile *profile = &profiles[i];
		const struct level *level = level_by_id(profile->location_id);
		int j;
		int other;

		if (!level || level->mode != WORLD_MODE_SPELUNKING) {
			plog_fmt("Spelunking system %s has invalid location %s",
				profile->id, profile->location_id);
			return false;
		}
		for (other = i + 1; other < profile_count; other++) {
			if (streq(profile->location_id, profiles[other].location_id)) {
				plog_fmt("Spelunking systems %s and %s share location %s",
					profile->id, profiles[other].id, profile->location_id);
				return false;
			}
		}
		for (j = 0; j < profile->portal_count; j++) {
			const struct world_entry *entry = world_entry_by_id(level,
				profile->portals[j].entry_id);

			if (!entry || entry->kind != WORLD_ENTRY_GRID) {
				plog_fmt("Spelunking system %s has invalid portal %s",
					profile->id, profile->portals[j].entry_id);
				return false;
			}
		}
	}
	return profile_count > 0;
}

static struct world_spelunk_system_node *contract_target_node(
		struct world_spelunk_system *system,
		const struct world_spelunk_system_portal_contract *contract)
{
	struct world_spelunk_system_node *target;
	size_t i;

	if (!system || !system->node_count || !contract) return NULL;
	target = &system->nodes[0];
	if (contract->target == WORLD_SPELUNK_PORTAL_DEEPEST) {
		for (i = 1; i < system->node_count; i++) {
			if (system->nodes[i].depth > target->depth) {
				target = &system->nodes[i];
			}
		}
	}
	return target;
}

static bool place_migrated_portal(struct world_spelunk_system *system,
		const struct world_spelunk_system_portal_contract *contract,
		const struct world_spelunk_system_node *node)
{
	const struct world_spelunk_runtime *runtime = node ? node->runtime : NULL;
	int minimum_y;
	int maximum_y;
	int y;
	int x;

	if (!runtime) return true;
	minimum_y = contract->min_depth_percent *
		(runtime->state.map.height - 1) / 100;
	maximum_y = contract->max_depth_percent *
		(runtime->state.map.height - 1) / 100;
	minimum_y = MAX(0, MIN(runtime->state.map.height - 2, minimum_y));
	maximum_y = MAX(minimum_y,
		MIN(runtime->state.map.height - 2, maximum_y));
	for (y = minimum_y; y <= maximum_y; y++) {
		for (x = 1; x < runtime->state.map.width - 1; x++) {
			if (runtime->cells[(size_t)y * runtime->state.map.width + x] !=
					WORLD_SPELUNK_AIR ||
					runtime->cells[(size_t)(y + 1) *
						runtime->state.map.width + x] != WORLD_SPELUNK_ROCK ||
					world_spelunk_runtime_actor_at(runtime, x, y) ||
					world_spelunk_runtime_ground_object_at(runtime, x, y)) {
				continue;
			}
			if (world_spelunk_system_set_portal_grid(system, contract->id,
					x, y)) {
				return true;
			}
		}
	}
	plog_fmt("Could not place migrated spelunking portal %s", contract->id);
	return false;
}

bool world_spelunk_system_reconcile_portals(
		struct world_spelunk_system *system)
{
	const struct world_spelunk_system_profile *profile;
	int i;

	if (!system) return false;
	profile = world_spelunk_system_profile_by_id(system->id);
	if (!profile) return true;
	if (!streq(profile->location_id, system->location_id)) return false;
	/* Authored removal retires a saved portal without changing its chamber or
	 * the player's position. Marks and exits are derived from this ledger. */
	for (i = 0; i < (int)system->portal_count;) {
		int j;
		bool retained = false;
		for (j = 0; j < profile->portal_count; j++) {
			if (streq(system->portals[i].id, profile->portals[j].id))
				retained = true;
		}
		if (retained) { i++; continue; }
		memmove(&system->portals[i], &system->portals[i + 1],
			(system->portal_count - i - 1) * sizeof(system->portals[0]));
		system->portal_count--;
		memset(&system->portals[system->portal_count], 0,
			sizeof(system->portals[0]));
	}
	for (i = 0; i < profile->portal_count; i++) {
		const struct world_spelunk_system_portal_contract *contract =
			&profile->portals[i];
		const struct world_spelunk_system_portal *existing =
			world_spelunk_system_portal_by_id(system, contract->id);
		struct world_spelunk_system_node *target =
			contract_target_node(system, contract);

		if (!target) return false;
		if (existing) {
			if (!streq(existing->entry_id, contract->entry_id) ||
					!streq(existing->node_id, target->id)) {
				return false;
			}
			continue;
		}
		if (!world_spelunk_system_add_portal(system, contract->id,
				contract->entry_id, target->id,
				contract->initially_enabled) ||
				!place_migrated_portal(system, contract, target)) {
			return false;
		}
	}
	return true;
}
