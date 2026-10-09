/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-recipe-data.c
 * \brief Parser and immutable registry for procedural cave recipes.
 */

#include "angband.h"
#include "datafile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "world-spelunking-data-util.h"
#include "world-spelunking-geology-data.h"
#include "world-spelunking-population-data.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-traversal-data.h"
#include "world-spelunking-water-data.h"

#include <limits.h>
#include <string.h>

enum recipe_field {
	RECIPE_FIELD_NAME = 0x001,
	RECIPE_FIELD_DESCRIPTION = 0x002,
	RECIPE_FIELD_SIZE = 0x004,
	RECIPE_FIELD_MACRO = 0x008,
	RECIPE_FIELD_ROUTE = 0x010,
	RECIPE_FIELD_RULES = 0x020,
	RECIPE_FIELD_PERCEPTION = 0x040,
	RECIPE_FIELD_WATER = 0x080,
	RECIPE_FIELD_MATERIAL = 0x100,
	RECIPE_FIELD_LANDMARKS = 0x200,
	RECIPE_FIELD_PASSAGES = 0x400,
	RECIPE_FIELD_TRAVERSAL = 0x800,
	RECIPE_FIELD_POPULATION = 0x1000,
	RECIPE_FIELD_STAMINA_COSTS = 0x2000,
	RECIPE_FIELD_REQUIRED = 0x3dff
};

struct recipe_record {
	struct world_spelunk_recipe_definition definition;
	unsigned int fields;
	struct recipe_record *next;
};

struct recipe_parse_state {
	int count;
	struct recipe_record *records;
	struct recipe_record *current;
};

static struct world_spelunk_recipe_definition *definitions;
static int definition_count;
static bool recipe_data_loaded;

static bool role_from_name(const char *name,
		enum world_spelunk_recipe_role *role)
{
	if (streq(name, "root")) {
		*role = WORLD_SPELUNK_RECIPE_ROOT;
	} else if (streq(name, "optional")) {
		*role = WORLD_SPELUNK_RECIPE_OPTIONAL;
	} else if (streq(name, "deep")) {
		*role = WORLD_SPELUNK_RECIPE_DEEP;
	} else if (streq(name, "link")) {
		*role = WORLD_SPELUNK_RECIPE_LINK;
	} else {
		return false;
	}
	return true;
}

static bool macro_from_name(const char *name,
		enum world_spelunk_macro_family *family)
{
	if (!streq(name, "chamber-chain")) return false;
	*family = WORLD_SPELUNK_MACRO_CHAMBER_CHAIN;
	return true;
}

static bool landmark_kind_from_name(const char *name,
		enum world_spelunk_landmark_kind *kind)
{
	if (streq(name, "object")) {
		*kind = WORLD_SPELUNK_LANDMARK_OBJECT;
	} else if (streq(name, "fishing-stance")) {
		*kind = WORLD_SPELUNK_LANDMARK_FISHING_STANCE;
	} else if (streq(name, "section-exit")) {
		*kind = WORLD_SPELUNK_LANDMARK_SECTION_EXIT;
	} else {
		return false;
	}
	return true;
}

static struct recipe_record *record_by_id(
		const struct recipe_parse_state *state, const char *id)
{
	struct recipe_record *record;

	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->definition.id, id)) return record;
	}
	return NULL;
}

static void free_definition_strings(
		struct world_spelunk_recipe_definition *definition)
{
	int i;

	if (!definition) return;
	string_free((char *)definition->id);
	string_free((char *)definition->system_id);
	string_free((char *)definition->name);
	string_free((char *)definition->description);
	string_free((char *)definition->water_profile_id);
	string_free((char *)definition->material_profile_id);
	string_free((char *)definition->traversal_profile_id);
	string_free((char *)definition->population_profile_id);
	for (i = 0; i < definition->landmark_count; i++) {
		string_free((char *)definition->landmarks[i].id);
		string_free((char *)definition->landmarks[i].object_tval);
		string_free((char *)definition->landmarks[i].object_sval);
	}
}

static void free_record(struct recipe_record *record)
{
	if (!record) return;
	free_definition_strings(&record->definition);
	mem_free(record);
}

static void free_parse_state(struct recipe_parse_state *state)
{
	struct recipe_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_record(record);
	}
	mem_free(state);
}

static enum parser_error parse_recipe(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *system_id = parser_getsym(p, "system");
	const char *role_name = parser_getsym(p, "role");
	int weight = parser_getint(p, "weight");
	int version = parser_getint(p, "version");
	int stamina = parser_getint(p, "stamina");
	struct recipe_record *record;
	enum world_spelunk_recipe_role role;

	if (!world_id_is_valid(id) || !world_id_is_valid(system_id) ||
			!role_from_name(role_name, &role) || weight < 1 ||
			weight > UINT16_MAX || version < 1 || version > UINT16_MAX ||
			stamina < 1 || stamina > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_SPELUNK_RECIPE_MAX || record_by_id(state, id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	record = mem_zalloc(sizeof(*record));
	record->definition.id = string_make(id);
	record->definition.system_id = string_make(system_id);
	record->definition.role = role;
	record->definition.weight = (uint16_t)weight;
	record->definition.generator_version = (uint16_t)version;
	record->definition.initial_stamina = (uint16_t)stamina;
	record->next = state->records;
	state->records = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_owned_text(struct parser *p,
		unsigned int field, const char *token, const char **destination)
{
	struct recipe_parse_state *state = parser_priv(p);
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
	struct recipe_parse_state *state = parser_priv(p);

	return parse_owned_text(p, RECIPE_FIELD_NAME, "name",
		state->current ? &state->current->definition.name : NULL);
}

static enum parser_error parse_description(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);

	return parse_owned_text(p, RECIPE_FIELD_DESCRIPTION, "description",
		state->current ? &state->current->definition.description : NULL);
}

static enum parser_error parse_profile_id(struct parser *p,
		unsigned int field, const char *token, const char **destination)
{
	struct recipe_parse_state *state = parser_priv(p);
	const char *id;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & field) return PARSE_ERROR_REPEATED_DIRECTIVE;
	id = parser_getsym(p, token);
	if (!world_id_is_valid(id)) return PARSE_ERROR_INVALID_VALUE;
	*destination = string_make(id);
	state->current->fields |= field;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_water(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);

	return parse_profile_id(p, RECIPE_FIELD_WATER, "profile",
		state->current ? &state->current->definition.water_profile_id : NULL);
}

static enum parser_error parse_material(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);

	return parse_profile_id(p, RECIPE_FIELD_MATERIAL, "profile",
		state->current ? &state->current->definition.material_profile_id : NULL);
}

static enum parser_error parse_traversal(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);

	return parse_profile_id(p, RECIPE_FIELD_TRAVERSAL, "profile",
		state->current ?
			&state->current->definition.traversal_profile_id : NULL);
}

static enum parser_error parse_population(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);

	return parse_profile_id(p, RECIPE_FIELD_POPULATION, "profile",
		state->current ?
			&state->current->definition.population_profile_id : NULL);
}

static enum parser_error parse_landmark(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	struct world_spelunk_recipe_definition *definition;
	struct world_spelunk_landmark_contract *landmark;
	const char *id;
	const char *kind_name;
	const char *tval;
	const char *sval;
	enum world_spelunk_landmark_kind kind;
	int minimum;
	int maximum;
	int i;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	definition = &state->current->definition;
	if (definition->landmark_count >= WORLD_SPELUNK_LANDMARK_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	id = parser_getsym(p, "id");
	kind_name = parser_getsym(p, "kind");
	minimum = parser_getint(p, "minimum");
	maximum = parser_getint(p, "maximum");
	tval = parser_getsym(p, "tval");
	sval = parser_getstr(p, "sval");
	if (!world_id_is_valid(id) ||
			!landmark_kind_from_name(kind_name, &kind) || minimum < 0 ||
			minimum > maximum || maximum > 100 || !tval || !sval ||
			!sval[0] || strlen(tval) >= WORLD_SPELUNK_LANDMARK_TVAL_LEN ||
			strlen(sval) >= WORLD_SPELUNK_LANDMARK_SVAL_LEN) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if ((kind == WORLD_SPELUNK_LANDMARK_OBJECT &&
			(streq(tval, "none") || streq(sval, "-"))) ||
			(kind != WORLD_SPELUNK_LANDMARK_OBJECT &&
			(!streq(tval, "none") || !streq(sval, "-")))) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < definition->landmark_count; i++) {
		if (streq(definition->landmarks[i].id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	landmark = &definition->landmarks[definition->landmark_count++];
	landmark->id = string_make(id);
	landmark->kind = kind;
	landmark->min_depth_percent = (uint16_t)minimum;
	landmark->max_depth_percent = (uint16_t)maximum;
	landmark->object_tval = string_make(tval);
	landmark->object_sval = string_make(sval);
	state->current->fields |= RECIPE_FIELD_LANDMARKS;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_passage(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	struct world_spelunk_passage_contract *passage;
	const char *direction_name;
	enum world_spelunk_passage_direction direction;
	int minimum;
	int maximum;
	int capacity;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	direction_name = parser_getsym(p, "direction");
	if (streq(direction_name, "up")) {
		direction = WORLD_SPELUNK_PASSAGE_UP;
	} else if (streq(direction_name, "down")) {
		direction = WORLD_SPELUNK_PASSAGE_DOWN;
	} else {
		return PARSE_ERROR_INVALID_VALUE;
	}
	minimum = parser_getint(p, "minimum");
	maximum = parser_getint(p, "maximum");
	capacity = parser_getint(p, "capacity");
	passage = &state->current->definition.passages[direction];
	if (passage->capacity) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (minimum < 0 || minimum > maximum || maximum > 100 ||
			capacity < 1 || capacity > 4) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	passage->min_depth_percent = (uint16_t)minimum;
	passage->max_depth_percent = (uint16_t)maximum;
	passage->capacity = (uint16_t)capacity;
	state->current->fields |= RECIPE_FIELD_PASSAGES;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_size(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	struct world_spelunk_size_profile *size;
	int min_width;
	int max_width;
	int min_height;
	int max_height;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & RECIPE_FIELD_SIZE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	min_width = parser_getint(p, "min-width");
	max_width = parser_getint(p, "max-width");
	min_height = parser_getint(p, "min-height");
	max_height = parser_getint(p, "max-height");
	if (min_width < 24 || min_width > max_width ||
			max_width > WORLD_SPELUNK_WIDTH_MAX || min_height < 24 ||
			min_height > max_height || max_height > WORLD_SPELUNK_HEIGHT_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	size = &state->current->definition.size;
	size->min_width = (uint16_t)min_width;
	size->max_width = (uint16_t)max_width;
	size->min_height = (uint16_t)min_height;
	size->max_height = (uint16_t)max_height;
	state->current->fields |= RECIPE_FIELD_SIZE;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_macro(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	enum world_spelunk_macro_family family;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & RECIPE_FIELD_MACRO) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!macro_from_name(parser_getsym(p, "family"), &family)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->definition.macro_family = family;
	state->current->fields |= RECIPE_FIELD_MACRO;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_route(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	struct world_spelunk_route_profile *route;
	int values[13];
	int i;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & RECIPE_FIELD_ROUTE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	values[0] = parser_getint(p, "min-chambers");
	values[1] = parser_getint(p, "max-chambers");
	values[2] = parser_getint(p, "min-radius-x");
	values[3] = parser_getint(p, "max-radius-x");
	values[4] = parser_getint(p, "min-radius-y");
	values[5] = parser_getint(p, "max-radius-y");
	values[6] = parser_getint(p, "min-branches");
	values[7] = parser_getint(p, "max-branches");
	values[8] = parser_getint(p, "side-margin");
	values[9] = parser_getint(p, "entrance-y");
	values[10] = parser_getint(p, "bottom-margin");
	values[11] = parser_getint(p, "min-gap");
	values[12] = parser_getint(p, "max-gap");
	for (i = 0; i < 13; i++) {
		if (values[i] < 0 || values[i] > UINT16_MAX) {
			return PARSE_ERROR_INVALID_VALUE;
		}
	}
	if (values[0] < 2 || values[0] > values[1] || values[1] > 24 ||
			values[2] < 2 || values[2] > values[3] || values[3] > 24 ||
			values[4] < 1 || values[4] > values[5] || values[5] > 12 ||
			values[6] > values[7] || values[7] > 16 || values[8] < 2 ||
			values[9] < 1 || values[10] < 2 || values[11] < 4 ||
			values[11] > values[12] || values[12] > 24) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	route = &state->current->definition.route;
	route->min_chambers = (uint16_t)values[0];
	route->max_chambers = (uint16_t)values[1];
	route->min_radius_x = (uint16_t)values[2];
	route->max_radius_x = (uint16_t)values[3];
	route->min_radius_y = (uint16_t)values[4];
	route->max_radius_y = (uint16_t)values[5];
	route->min_branches = (uint16_t)values[6];
	route->max_branches = (uint16_t)values[7];
	route->side_margin = (uint16_t)values[8];
	route->entrance_y = (uint16_t)values[9];
	route->bottom_margin = (uint16_t)values[10];
	route->min_vertical_gap = (uint16_t)values[11];
	route->max_vertical_gap = (uint16_t)values[12];
	state->current->fields |= RECIPE_FIELD_ROUTE;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_rules(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	enum parser_error result;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & RECIPE_FIELD_RULES) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	result = world_spelunk_parse_rules_fields(p,
		&state->current->definition.rules);
	if (result != PARSE_ERROR_NONE) return result;
	state->current->fields |= RECIPE_FIELD_RULES;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_perception(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	enum parser_error result;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & RECIPE_FIELD_PERCEPTION) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	result = world_spelunk_parse_perception_fields(p,
		&state->current->definition.perception);
	if (result != PARSE_ERROR_NONE) return result;
	state->current->fields |= RECIPE_FIELD_PERCEPTION;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_stamina_costs(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	enum parser_error result;

	if (!state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->fields & RECIPE_FIELD_STAMINA_COSTS) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	result = world_spelunk_parse_stamina_cost_fields(p,
		&state->current->definition.rules);
	if (result != PARSE_ERROR_NONE) return result;
	state->current->fields |= RECIPE_FIELD_STAMINA_COSTS;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_recipes(void)
{
	struct parser *p = parser_new();
	struct recipe_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "recipe sym id sym system sym role int weight int version int stamina", parse_recipe);
	parser_reg(p, "name str name", parse_name);
	parser_reg(p, "description str description", parse_description);
	parser_reg(p, "size int min-width int max-width int min-height int max-height", parse_size);
	parser_reg(p, "macro sym family", parse_macro);
	parser_reg(p, "route int min-chambers int max-chambers int min-radius-x int max-radius-x int min-radius-y int max-radius-y int min-branches int max-branches int side-margin int entrance-y int bottom-margin int min-gap int max-gap", parse_route);
	parser_reg(p, "rules int safe-fall int fall-damage int jump-cost int grip-cost int rest-gain int rope-length int rope-cost int breath int drowning-damage int swim-cost", parse_rules);
	parser_reg(p, "stamina-costs int grip-up int grip-side int grip-down int rope-up int rope-side int rope-down", parse_stamina_costs);
	parser_reg(p, "perception int horizontal int upward int downward int peek int down-peek int lateral int close int memory char visible-background sym visible-color char remembered-background sym remembered-color", parse_perception);
	parser_reg(p, "water sym profile", parse_water);
	parser_reg(p, "material sym profile", parse_material);
	parser_reg(p, "traversal sym profile", parse_traversal);
	parser_reg(p, "population sym profile", parse_population);
	parser_reg(p, "landmark sym id sym kind int minimum int maximum sym tval str sval", parse_landmark);
	parser_reg(p, "passage sym direction int minimum int maximum int capacity",
		parse_passage);
	return p;
}

static errr run_parse_recipes(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_recipe");
}

static errr validate_record(const struct recipe_record *record)
{
	const struct world_spelunk_recipe_definition *definition =
		&record->definition;
	const struct world_spelunk_route_profile *route = &definition->route;
	const struct world_spelunk_size_profile *size = &definition->size;
	unsigned int minimum_height;
	unsigned int minimum_width;
	bool has_object = false;
	int fishing_stance_count = 0;
	int i;

	if ((record->fields & RECIPE_FIELD_REQUIRED) != RECIPE_FIELD_REQUIRED) {
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	minimum_width = 2u * (route->side_margin + route->max_radius_x) + 3u;
	minimum_height = route->entrance_y + route->max_radius_y + 3u +
		(route->min_chambers - 1u) * route->min_vertical_gap +
		route->max_radius_y + route->bottom_margin + 1u;
	if (size->min_width < minimum_width || size->min_height < minimum_height) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < definition->landmark_count; i++) {
		switch (definition->landmarks[i].kind) {
		case WORLD_SPELUNK_LANDMARK_OBJECT:
			has_object = true;
			break;
		case WORLD_SPELUNK_LANDMARK_FISHING_STANCE:
			fishing_stance_count++;
			break;
		case WORLD_SPELUNK_LANDMARK_SECTION_EXIT:
			break;
		}
	}
	if (fishing_stance_count > 1) return PARSE_ERROR_INVALID_VALUE;
	if (definition->role == WORLD_SPELUNK_RECIPE_ROOT &&
			(!has_object || fishing_stance_count != 1 ||
			 !definition->passages[WORLD_SPELUNK_PASSAGE_DOWN].capacity ||
			 definition->passages[WORLD_SPELUNK_PASSAGE_UP].capacity)) {
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	if (definition->role == WORLD_SPELUNK_RECIPE_OPTIONAL &&
			(!definition->passages[WORLD_SPELUNK_PASSAGE_UP].capacity ||
			 !definition->passages[WORLD_SPELUNK_PASSAGE_DOWN].capacity)) {
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	if (definition->role == WORLD_SPELUNK_RECIPE_DEEP &&
			(!definition->passages[WORLD_SPELUNK_PASSAGE_UP].capacity ||
			 definition->passages[WORLD_SPELUNK_PASSAGE_DOWN].capacity)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	return PARSE_ERROR_NONE;
}

static void cleanup_recipe_data(void)
{
	int i;

	for (i = 0; i < definition_count; i++) {
		free_definition_strings(&definitions[i]);
	}
	mem_free(definitions);
	definitions = NULL;
	definition_count = 0;
	recipe_data_loaded = false;
}

static errr finish_parse_recipes(struct parser *p)
{
	struct recipe_parse_state *state = parser_priv(p);
	struct recipe_record *record;
	struct recipe_record *next;
	int index;

	if (!state || state->count < 1) {
		free_parse_state(state);
		parser_destroy(p);
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (record = state->records; record; record = record->next) {
		errr result = validate_record(record);
		struct recipe_record *other;
		bool found_root = false;

		if (result != PARSE_ERROR_NONE) {
			free_parse_state(state);
			parser_destroy(p);
			return result;
		}
		for (other = state->records; other; other = other->next) {
			if (!streq(record->definition.system_id,
					other->definition.system_id) ||
					other->definition.role != WORLD_SPELUNK_RECIPE_ROOT) {
				continue;
			}
			if (found_root) {
				free_parse_state(state);
				parser_destroy(p);
				return PARSE_ERROR_REPEATED_DIRECTIVE;
			}
			found_root = true;
		}
		if (!found_root) {
			free_parse_state(state);
			parser_destroy(p);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	cleanup_recipe_data();
	definitions = mem_zalloc((size_t)state->count * sizeof(*definitions));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		definitions[index] = record->definition;
		mem_free(record);
	}
	definition_count = state->count;
	recipe_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_recipe_parser = {
	"spelunking_recipe",
	init_parse_recipes,
	run_parse_recipes,
	finish_parse_recipes,
	cleanup_recipe_data
};

int world_spelunk_recipe_definition_count(void)
{
	return recipe_data_loaded ? definition_count : 0;
}

const struct world_spelunk_recipe_definition *
world_spelunk_recipe_definition_by_index(int index)
{
	if (!recipe_data_loaded || index < 0 || index >= definition_count) {
		return NULL;
	}
	return &definitions[index];
}

const struct world_spelunk_recipe_definition *
world_spelunk_recipe_definition_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < definition_count; i++) {
		if (streq(definitions[i].id, id)) return &definitions[i];
	}
	return NULL;
}

bool world_spelunk_recipe_rules(
		const struct world_spelunk_recipe_definition *definition,
		unsigned int action_energy, struct world_spelunk_rules *rules)
{
	if (!definition || !rules || !action_energy) return false;
	*rules = definition->rules;
	rules->action_energy = action_energy;
	return true;
}

bool world_spelunk_recipe_data_validate_world(void)
{
	int i;

	for (i = 0; i < definition_count; i++) {
		const struct world_spelunk_recipe_definition *definition =
			&definitions[i];
		const struct world_spelunk_traversal_profile *traversal_profile;
		int j;

		if (!world_spelunk_water_profile_by_id(
				definition->water_profile_id)) {
			plog_fmt("Spelunking recipe %s has unknown water profile %s",
				definition->id, definition->water_profile_id);
			return false;
		}
		if (!world_spelunk_geology_profile_by_id(
				definition->material_profile_id)) {
			plog_fmt("Spelunking recipe %s has unknown geology profile %s",
				definition->id, definition->material_profile_id);
			return false;
		}
		traversal_profile = world_spelunk_traversal_profile_by_id(
			definition->traversal_profile_id);
		if (!traversal_profile) {
			plog_fmt("Spelunking recipe %s has unknown traversal profile %s",
				definition->id, definition->traversal_profile_id);
			return false;
		}
		if (!world_spelunk_population_profile_by_id(
				definition->population_profile_id)) {
			plog_fmt("Spelunking recipe %s has unknown population profile %s",
				definition->id, definition->population_profile_id);
			return false;
		}
		if (!world_spelunk_rules_match_player_resources(&definition->rules)) {
			plog_fmt("Spelunking recipe %s disagrees with player resource constants",
				definition->id);
			return false;
		}
		for (j = 0; j < definition->landmark_count; j++) {
			const struct world_spelunk_landmark_contract *landmark =
				&definition->landmarks[j];
			int tval;

			if (landmark->kind != WORLD_SPELUNK_LANDMARK_OBJECT) continue;
			tval = tval_find_idx(landmark->object_tval);
			if (tval <= 0 || lookup_sval(tval, landmark->object_sval) < 0) {
				plog_fmt("Spelunking landmark %s has unknown object %s:%s",
					landmark->id, landmark->object_tval,
					landmark->object_sval);
				return false;
			}
		}
	}
	return definition_count > 0;
}
