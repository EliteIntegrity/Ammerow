/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-layout-data.c
 * \brief Parser and immutable registry for authored spelunking layouts.
 */

#include "angband.h"
#include "datafile.h"
#include "game-world.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "world-entry.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-data-util.h"
#include "world-spelunking-layout-data.h"

#include <limits.h>
#include <string.h>

enum layout_field {
	LAYOUT_FIELD_RULES = 0x01,
	LAYOUT_FIELD_FILL = 0x02,
	LAYOUT_FIELD_PERCEPTION = 0x04,
	LAYOUT_FIELD_STAMINA_COSTS = 0x08,
	LAYOUT_FIELD_ALL = 0x0f
};

struct layout_record {
	struct world_spelunk_layout_definition definition;
	unsigned int fields;
	struct layout_record *next;
};

struct layout_parse_state {
	int count;
	struct layout_record *records;
	struct layout_record *current;
};

static struct world_spelunk_layout_definition *definitions;
static int definition_count;
static bool layout_data_loaded;

static struct layout_record *record_by_id(
		const struct layout_parse_state *state, const char *id)
{
	struct layout_record *record;

	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if (streq(record->definition.id, id)) return record;
	}
	return NULL;
}

static void free_record(struct layout_record *record)
{
	int i;

	if (!record) return;
	string_free((char *)record->definition.id);
	for (i = 0; i < record->definition.object_count; i++) {
		string_free((char *)record->definition.objects[i].id);
		string_free((char *)record->definition.objects[i].tval_name);
		string_free((char *)record->definition.objects[i].item_name);
	}
	mem_free(record);
}

static void free_parse_state(struct layout_parse_state *state)
{
	struct layout_record *record;

	if (!state) return;
	while ((record = state->records) != NULL) {
		state->records = record->next;
		free_record(record);
	}
	mem_free(state);
}

static bool tile_from_name(const char *name, enum world_spelunk_tile *tile)
{
	if (streq(name, "air")) {
		*tile = WORLD_SPELUNK_AIR;
	} else if (streq(name, "rock")) {
		*tile = WORLD_SPELUNK_ROCK;
	} else if (streq(name, "water")) {
		*tile = WORLD_SPELUNK_WATER;
	} else {
		return false;
	}
	return true;
}

static bool match_from_name(const char *name,
		enum world_spelunk_layout_match *match)
{
	if (streq(name, "any")) {
		*match = WORLD_SPELUNK_LAYOUT_MATCH_ANY;
	} else if (streq(name, "air")) {
		*match = WORLD_SPELUNK_LAYOUT_MATCH_AIR;
	} else if (streq(name, "rock")) {
		*match = WORLD_SPELUNK_LAYOUT_MATCH_ROCK;
	} else if (streq(name, "water")) {
		*match = WORLD_SPELUNK_LAYOUT_MATCH_WATER;
	} else {
		return false;
	}
	return true;
}

static enum parser_error parse_layout(struct parser *p)
{
	struct layout_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	int width = parser_getint(p, "width");
	int height = parser_getint(p, "height");
	int stamina = parser_getint(p, "stamina");
	int revision = parser_getint(p, "revision");
	struct layout_record *record;

	if (!world_id_is_valid(id) || width < 1 ||
			width > WORLD_SPELUNK_WIDTH_MAX || height < 1 ||
			height > WORLD_SPELUNK_HEIGHT_MAX || stamina < 1 ||
			stamina > UINT16_MAX || revision < 0 ||
			revision >= WORLD_SPELUNK_LAYOUT_OPERATION_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->count >= WORLD_SPELUNK_LAYOUT_DEFINITION_MAX ||
			record_by_id(state, id)) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	record = mem_zalloc(sizeof(*record));
	record->definition.id = string_make(id);
	record->definition.width = (uint16_t)width;
	record->definition.height = (uint16_t)height;
	record->definition.initial_stamina = (uint16_t)stamina;
	record->definition.current_revision = (uint16_t)revision;
	record->next = state->records;
	state->records = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_rules(struct parser *p)
{
	struct layout_parse_state *state = parser_priv(p);
	struct layout_record *record = state->current;
	enum parser_error result;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & LAYOUT_FIELD_RULES) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	result = world_spelunk_parse_rules_fields(p, &record->definition.rules);
	if (result != PARSE_ERROR_NONE) return result;
	record->fields |= LAYOUT_FIELD_RULES;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_perception(struct parser *p)
{
	struct layout_parse_state *state = parser_priv(p);
	struct layout_record *record = state->current;
	enum parser_error result;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & LAYOUT_FIELD_PERCEPTION) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	result = world_spelunk_parse_perception_fields(p,
		&record->definition.perception);
	if (result != PARSE_ERROR_NONE) return result;
	record->fields |= LAYOUT_FIELD_PERCEPTION;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_stamina_costs(struct parser *p)
{
	struct layout_parse_state *state = parser_priv(p);
	struct layout_record *record = state->current;
	enum parser_error result;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & LAYOUT_FIELD_STAMINA_COSTS) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	result = world_spelunk_parse_stamina_cost_fields(p,
		&record->definition.rules);
	if (result != PARSE_ERROR_NONE) return result;
	record->fields |= LAYOUT_FIELD_STAMINA_COSTS;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fill(struct parser *p)
{
	struct layout_parse_state *state = parser_priv(p);
	struct layout_record *record = state->current;
	enum world_spelunk_tile tile;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & LAYOUT_FIELD_FILL) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!tile_from_name(parser_getsym(p, "tile"), &tile)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	record->definition.fill = tile;
	record->fields |= LAYOUT_FIELD_FILL;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_operation(struct parser *p,
		enum world_spelunk_layout_shape shape)
{
	struct layout_parse_state *state = parser_priv(p);
	struct layout_record *record = state->current;
	struct world_spelunk_layout_definition *definition;
	struct world_spelunk_layout_operation operation;
	const char *match_name;
	const char *result_name;
	int revision;
	int priority;
	int i;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	definition = &record->definition;
	if (definition->operation_count >= WORLD_SPELUNK_LAYOUT_OPERATION_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	revision = parser_getint(p, "revision");
	priority = parser_getint(p, "priority");
	match_name = parser_getsym(p, "match");
	result_name = parser_getsym(p, "result");
	memset(&operation, 0, sizeof(operation));
	if (revision < 0 || revision > definition->current_revision ||
			priority < 1 || priority > UINT16_MAX ||
			!match_from_name(match_name, &operation.match) ||
			!tile_from_name(result_name, &operation.result)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	operation.revision = (uint16_t)revision;
	operation.priority = (uint16_t)priority;
	operation.shape = shape;
	operation.x1 = parser_getint(p, "x1");
	operation.y1 = parser_getint(p, "y1");
	operation.x2 = parser_getint(p, "x2");
	operation.y2 = parser_getint(p, "y2");
	if (shape == WORLD_SPELUNK_LAYOUT_RECTANGLE) {
		if (operation.x1 < 0 || operation.y1 < 0 ||
				operation.x2 < operation.x1 || operation.y2 < operation.y1 ||
				operation.x2 >= definition->width ||
				operation.y2 >= definition->height) {
			return PARSE_ERROR_INVALID_VALUE;
		}
	} else if (operation.x1 <= 0 || operation.y1 <= 0 ||
			operation.x2 < 1 || operation.y2 < 1 ||
			operation.x1 - operation.x2 < 0 ||
			operation.y1 - operation.y2 < 0 ||
			operation.x1 + operation.x2 >= definition->width ||
			operation.y1 + operation.y2 >= definition->height) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	/* Post-release revisions may only add traversable space or water.  That
	 * makes every migration append-only with respect to player terrain edits. */
	if (revision > 0 && !((operation.match ==
				WORLD_SPELUNK_LAYOUT_MATCH_ROCK &&
			operation.result == WORLD_SPELUNK_AIR) ||
			(operation.match == WORLD_SPELUNK_LAYOUT_MATCH_AIR &&
			operation.result == WORLD_SPELUNK_WATER))) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < definition->operation_count; i++) {
		const struct world_spelunk_layout_operation *other =
			&definition->operations[i];

		if (other->revision == operation.revision &&
				other->priority == operation.priority) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	for (i = 0; i < definition->object_count; i++) {
		if (definition->objects[i].revision == operation.revision &&
				definition->objects[i].priority == operation.priority) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	definition->operations[definition->operation_count++] = operation;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_rectangle(struct parser *p)
{
	return parse_operation(p, WORLD_SPELUNK_LAYOUT_RECTANGLE);
}

static enum parser_error parse_ellipse(struct parser *p)
{
	return parse_operation(p, WORLD_SPELUNK_LAYOUT_ELLIPSE);
}

static enum parser_error parse_object(struct parser *p)
{
	struct layout_parse_state *state = parser_priv(p);
	struct layout_record *record = state->current;
	struct world_spelunk_layout_definition *definition;
	struct world_spelunk_layout_object object = { 0 };
	const char *id;
	const char *tval_name;
	const char *item_name;
	int revision;
	int priority;
	int i;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	definition = &record->definition;
	if (definition->object_count >= WORLD_SPELUNK_LAYOUT_OBJECT_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	revision = parser_getint(p, "revision");
	priority = parser_getint(p, "priority");
	id = parser_getsym(p, "id");
	object.x = parser_getint(p, "x");
	object.y = parser_getint(p, "y");
	tval_name = parser_getsym(p, "tval");
	item_name = parser_getstr(p, "sval");
	if (revision < 1 || revision > definition->current_revision ||
			priority < 1 || priority > UINT16_MAX ||
			!world_id_is_valid(id) || !tval_name || !tval_name[0] ||
			!item_name || !item_name[0] || object.x < 0 || object.y < 0 ||
			object.x >= definition->width ||
			object.y + 1 >= definition->height) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (i = 0; i < definition->operation_count; i++) {
		if (definition->operations[i].revision == revision &&
				definition->operations[i].priority == priority) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	for (i = 0; i < definition->object_count; i++) {
		const struct world_spelunk_layout_object *other =
			&definition->objects[i];

		if (streq(other->id, id) || (other->revision == revision &&
				other->priority == priority)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	object.id = string_make(id);
	object.revision = (uint16_t)revision;
	object.priority = (uint16_t)priority;
	object.tval_name = string_make(tval_name);
	object.item_name = string_make(item_name);
	definition->objects[definition->object_count++] = object;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_layouts(void)
{
	struct parser *p = parser_new();
	struct layout_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p,
		"layout sym id int width int height int stamina int revision",
		parse_layout);
	parser_reg(p, "rules int safe-fall int fall-damage int jump-cost int grip-cost int rest-gain int rope-length int rope-cost int breath int drowning-damage int swim-cost",
		parse_rules);
	parser_reg(p, "stamina-costs int grip-up int grip-side int grip-down int rope-up int rope-side int rope-down",
		parse_stamina_costs);
	parser_reg(p, "perception int horizontal int upward int downward int peek int down-peek int lateral int close int memory char visible-background sym visible-color char remembered-background sym remembered-color",
		parse_perception);
	parser_reg(p, "fill sym tile", parse_fill);
	parser_reg(p, "rectangle int revision int priority sym match sym result int x1 int y1 int x2 int y2",
		parse_rectangle);
	parser_reg(p, "ellipse int revision int priority sym match sym result int x1 int y1 int x2 int y2",
		parse_ellipse);
	parser_reg(p, "object int revision int priority sym id int x int y sym tval str sval",
		parse_object);
	return p;
}

static errr run_parse_layouts(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_layout");
}

static int compare_operation(const void *left, const void *right)
{
	const struct world_spelunk_layout_operation *a = left;
	const struct world_spelunk_layout_operation *b = right;

	if (a->revision != b->revision) {
		return (a->revision > b->revision) -
			(a->revision < b->revision);
	}
	return (a->priority > b->priority) - (a->priority < b->priority);
}

static int compare_object(const void *left, const void *right)
{
	const struct world_spelunk_layout_object *a = left;
	const struct world_spelunk_layout_object *b = right;

	if (a->revision != b->revision) {
		return (a->revision > b->revision) -
			(a->revision < b->revision);
	}
	return (a->priority > b->priority) - (a->priority < b->priority);
}

static errr validate_parse_state(const struct layout_parse_state *state)
{
	struct layout_record *record;

	if (!state || state->count < 1) return PARSE_ERROR_TOO_FEW_ENTRIES;
	for (record = state->records; record; record = record->next) {
		bool revisions[WORLD_SPELUNK_LAYOUT_OPERATION_MAX] = { false };
		int i;

		if (record->fields != LAYOUT_FIELD_ALL ||
				!record->definition.operation_count) {
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
		for (i = 0; i < record->definition.operation_count; i++) {
			revisions[record->definition.operations[i].revision] = true;
		}
		for (i = 0; i < record->definition.object_count; i++) {
			revisions[record->definition.objects[i].revision] = true;
		}
		for (i = 0; i <= record->definition.current_revision; i++) {
			if (!revisions[i]) {
				plog_fmt("Spelunking layout %s has no authored changes for revision %d",
					record->definition.id, i);
				return PARSE_ERROR_TOO_FEW_ENTRIES;
			}
		}
	}
	return PARSE_ERROR_NONE;
}

static void cleanup_layout_data(void)
{
	int i;

	for (i = 0; i < definition_count; i++) {
		int j;

		string_free((char *)definitions[i].id);
		for (j = 0; j < definitions[i].object_count; j++) {
			string_free((char *)definitions[i].objects[j].id);
			string_free((char *)definitions[i].objects[j].tval_name);
			string_free((char *)definitions[i].objects[j].item_name);
		}
	}
	mem_free(definitions);
	definitions = NULL;
	definition_count = 0;
	layout_data_loaded = false;
}

static errr finish_parse_layouts(struct parser *p)
{
	struct layout_parse_state *state = parser_priv(p);
	struct layout_record *record;
	struct layout_record *next;
	errr result = validate_parse_state(state);
	int index;

	if (result != PARSE_ERROR_NONE) {
		free_parse_state(state);
		parser_destroy(p);
		return result;
	}
	cleanup_layout_data();
	definitions = mem_zalloc((size_t)state->count * sizeof(*definitions));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		definitions[index] = record->definition;
		qsort(definitions[index].operations,
			(size_t)definitions[index].operation_count,
			sizeof(definitions[index].operations[0]), compare_operation);
		qsort(definitions[index].objects,
			(size_t)definitions[index].object_count,
			sizeof(definitions[index].objects[0]), compare_object);
		mem_free(record);
	}
	definition_count = state->count;
	layout_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser spelunking_layout_parser = {
	"spelunking_layout",
	init_parse_layouts,
	run_parse_layouts,
	finish_parse_layouts,
	cleanup_layout_data
};

int world_spelunk_layout_definition_count(void)
{
	return layout_data_loaded ? definition_count : 0;
}

const struct world_spelunk_layout_definition *
world_spelunk_layout_definition_by_index(int index)
{
	if (!layout_data_loaded || index < 0 || index >= definition_count) {
		return NULL;
	}
	return &definitions[index];
}

const struct world_spelunk_layout_definition *
world_spelunk_layout_definition_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < definition_count; i++) {
		if (streq(definitions[i].id, id)) return &definitions[i];
	}
	return NULL;
}

bool world_spelunk_layout_rules(
		const struct world_spelunk_layout_definition *definition,
		unsigned int action_energy, struct world_spelunk_rules *rules)
{
	if (!definition || !action_energy || !rules) return false;
	*rules = definition->rules;
	rules->action_energy = action_energy;
	return true;
}

static bool cell_matches(enum world_spelunk_tile cell,
		enum world_spelunk_layout_match match)
{
	switch (match) {
		case WORLD_SPELUNK_LAYOUT_MATCH_ANY: return true;
		case WORLD_SPELUNK_LAYOUT_MATCH_AIR:
			return cell == WORLD_SPELUNK_AIR;
		case WORLD_SPELUNK_LAYOUT_MATCH_ROCK:
			return cell == WORLD_SPELUNK_ROCK;
		case WORLD_SPELUNK_LAYOUT_MATCH_WATER:
			return cell == WORLD_SPELUNK_WATER;
	}
	return false;
}

static void apply_operation(const struct world_spelunk_layout_operation *op,
		enum world_spelunk_tile *cells, int width, int height)
{
	int x;
	int y;

	if (op->shape == WORLD_SPELUNK_LAYOUT_RECTANGLE) {
		for (y = op->y1; y <= op->y2; y++) {
			for (x = op->x1; x <= op->x2; x++) {
				size_t index = (size_t)y * width + x;

				if (cell_matches(cells[index], op->match)) {
					cells[index] = op->result;
				}
			}
		}
	} else {
		int rx2 = op->x2 * op->x2;
		int ry2 = op->y2 * op->y2;
		int limit = rx2 * ry2;

		for (y = op->y1 - op->y2; y <= op->y1 + op->y2; y++) {
			for (x = op->x1 - op->x2; x <= op->x1 + op->x2; x++) {
				int dx = x - op->x1;
				int dy = y - op->y1;
				size_t index;

				if (x <= 0 || y <= 0 || x >= width - 1 ||
						y >= height - 1 ||
						dx * dx * ry2 + dy * dy * rx2 > limit) {
					continue;
				}
				index = (size_t)y * width + x;
				if (cell_matches(cells[index], op->match)) {
					cells[index] = op->result;
				}
			}
		}
	}
}

bool world_spelunk_layout_migrate_cells(
		const struct world_spelunk_layout_definition *definition,
		enum world_spelunk_tile *cells, int width, int height,
		uint16_t from_revision)
{
	int i;

	if (!definition || !cells || width != definition->width ||
			height != definition->height ||
			from_revision > definition->current_revision) {
		return false;
	}
	for (i = 0; i < definition->operation_count; i++) {
		const struct world_spelunk_layout_operation *operation =
			&definition->operations[i];

		if (operation->revision > from_revision) {
			apply_operation(operation, cells, width, height);
		}
	}
	return true;
}

bool world_spelunk_layout_build_cells(
		const struct world_spelunk_layout_definition *definition,
		enum world_spelunk_tile *cells, int width, int height)
{
	size_t count;
	int i;

	if (!definition || !cells || width != definition->width ||
			height != definition->height) {
		return false;
	}
	count = (size_t)width * height;
	for (i = 0; i < (int)count; i++) cells[i] = definition->fill;
	for (i = 0; i < definition->operation_count; i++) {
		apply_operation(&definition->operations[i], cells, width, height);
	}
	return true;
}

bool world_spelunk_layout_data_validate_world(void)
{
	const struct level *level;
	int i;

	if (!layout_data_loaded) return false;
	for (level = world; level; level = level->next) {
		if (level->mode == WORLD_MODE_SPELUNKING &&
				!world_spelunk_layout_definition_by_id(level->id)) {
			plog_fmt("Spelunking location %s has no layout definition", level->id);
			return false;
		}
	}
	for (i = 0; i < definition_count; i++) {
		struct world_spelunk_layout_definition *definition =
			&definitions[i];
		const struct world_entry *entry;
		enum world_spelunk_tile *cells;
		bool found_entry = false;
		size_t count;
		int j;

		level = level_by_id(definition->id);
		if (!level || level->mode != WORLD_MODE_SPELUNKING ||
				level->width != definition->width ||
				level->height != definition->height) {
			plog_fmt("Spelunking layout %s disagrees with world metadata",
				definition->id);
			return false;
		}
		if (!world_spelunk_rules_match_player_resources(&definition->rules)) {
			plog_fmt("Spelunking layout %s disagrees with player resource constants",
				definition->id);
			return false;
		}
		count = (size_t)definition->width * definition->height;
		cells = mem_alloc(count * sizeof(*cells));
		if (!world_spelunk_layout_build_cells(definition, cells,
				definition->width, definition->height)) {
			mem_free(cells);
			return false;
		}
		for (j = 0; j < definition->object_count; j++) {
			struct world_spelunk_layout_object *object =
				&definition->objects[j];
			int tval = tval_find_idx(object->tval_name);
			int sval = lookup_sval(tval, object->item_name);

			object->kind = lookup_kind(tval, sval);
			if (!object->kind ||
					cells[(size_t)object->y * definition->width +
						object->x] != WORLD_SPELUNK_AIR ||
					cells[(size_t)(object->y + 1) * definition->width +
						object->x] != WORLD_SPELUNK_ROCK) {
				plog_fmt("Spelunking layout object %s lacks an item, air, or support",
					object->id);
				mem_free(cells);
				return false;
			}
		}
		for (entry = level->entries; entry; entry = entry->next) {
			found_entry = true;
			if (entry->kind != WORLD_ENTRY_GRID || entry->grid.x < 0 ||
					entry->grid.x >= definition->width || entry->grid.y < 0 ||
					entry->grid.y + 1 >= definition->height ||
					cells[(size_t)entry->grid.y * definition->width +
						entry->grid.x] != WORLD_SPELUNK_AIR ||
					cells[(size_t)(entry->grid.y + 1) * definition->width +
						entry->grid.x] != WORLD_SPELUNK_ROCK) {
				plog_fmt("Spelunking entry %s has no authored air and support",
					entry->id);
				mem_free(cells);
				return false;
			}
		}
		if (!found_entry) {
			plog_fmt("Spelunking layout %s has no named entry", definition->id);
			mem_free(cells);
			return false;
		}
		for (j = 0; j < world_spelunk_spawn_definition_count(); j++) {
			const struct world_spelunk_spawn_definition *spawn =
				world_spelunk_spawn_by_index(j);
			int k;

			if (!spawn || !streq(spawn->location_id, definition->id)) continue;
			for (k = 0; k < spawn->candidate_count; k++) {
				int x = spawn->candidates[k].x;
				int y = spawn->candidates[k].y;

				if (x < 0 || x >= definition->width || y < 0 ||
						y + 1 >= definition->height ||
						cells[(size_t)y * definition->width + x] !=
							WORLD_SPELUNK_AIR ||
						cells[(size_t)(y + 1) * definition->width + x] !=
							WORLD_SPELUNK_ROCK) {
					plog_fmt("Spelunking spawn %s candidate %d lacks air and support",
						spawn->id, spawn->candidates[k].priority);
					mem_free(cells);
					return false;
				}
			}
		}
		mem_free(cells);
	}
	return true;
}
