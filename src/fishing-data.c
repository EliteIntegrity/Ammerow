/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file fishing-data.c
 * \brief Parser and immutable registry for fishing species and balance.
 *
 *
 */

#include "angband.h"
#include "datafile.h"
#include "fishing-data.h"
#include "game-world.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "world-spelunking-system-data.h"

#include <ctype.h>
#include <string.h>

enum fishing_species_field {
	FISHING_FIELD_GRAPHICS = 0x01,
	FISHING_FIELD_DEPTH = 0x02,
	FISHING_FIELD_BEHAVIOR = 0x04,
	FISHING_FIELD_DANGER = 0x08,
	FISHING_FIELD_LARDER = 0x10,
	FISHING_FIELD_EXPERIENCE = 0x20,
	FISHING_FIELD_ALL = 0x3f
};

enum fishing_rig_field {
	FISHING_RIG_FIELD_ITEM = 0x01,
	FISHING_RIG_FIELD_CAPABILITIES = 0x02,
	FISHING_RIG_FIELD_ALL = 0x03
};

struct fishing_species_record {
	struct world_fishing_species species;
	uint32_t fields;
	uint32_t habitats;
	struct fishing_species_record *next;
};

struct fishing_rig_record {
	struct world_fishing_rig rig;
	uint32_t fields;
	struct fishing_rig_record *next;
};

struct fishing_discovery_record {
	struct world_fishing_discovery discovery;
	struct fishing_discovery_record *next;
};

struct fishing_parse_state {
	struct world_fishing_rules rules;
	bool population_seen;
	bool motion_seen;
	bool evasion_seen;
	bool patience_seen;
	bool attraction_seen;
	int species_count;
	struct fishing_species_record *species;
	struct fishing_species_record *current;
	int rig_count;
	struct fishing_rig_record *rigs;
	struct fishing_rig_record *current_rig;
	int discovery_count;
	struct fishing_discovery_record *discoveries;
};

static struct world_fishing_rules fishing_rules;
static struct world_fishing_species *fishing_species;
static int fishing_species_count;
static struct world_fishing_rig *fishing_rigs;
static int fishing_rig_count;
static struct world_fishing_discovery *fishing_discoveries;
static int fishing_discovery_count;
static bool fishing_data_loaded;

static bool valid_species_id(const char *id)
{
	const unsigned char *cursor = (const unsigned char *)id;

	if (!cursor || !*cursor) return false;
	while (*cursor) {
		if (!(islower(*cursor) || isdigit(*cursor) || *cursor == '-' ||
				*cursor == '_')) {
			return false;
		}
		cursor++;
	}
	return true;
}

static int fishing_color_attr(const char *color)
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

const char *world_fishing_habitat_name(enum world_fishing_habitat habitat)
{
	switch (habitat) {
	case WORLD_FISHING_HABITAT_RAINWATER:
		return "Rainwater pond";
	case WORLD_FISHING_HABITAT_OPEN_LAKE:
		return "Open lake";
	case WORLD_FISHING_HABITAT_FOREST_POOL:
		return "Forest pool";
	case WORLD_FISHING_HABITAT_MARSH_POOL:
		return "Marsh pool";
	case WORLD_FISHING_HABITAT_RUIN_CISTERN:
		return "Ruin cistern";
	case WORLD_FISHING_HABITAT_CAVE_POOL:
		return "Cave pool";
	case WORLD_FISHING_HABITAT_NONE:
	case WORLD_FISHING_HABITAT_MAX:
	default:
		return "No fish habitat";
	}
}

bool world_fishing_habitat_from_name(const char *name,
		enum world_fishing_habitat *habitat)
{
	if (!name || !habitat) return false;
	if (strcmp(name, "rainwater") == 0) {
		*habitat = WORLD_FISHING_HABITAT_RAINWATER;
	} else if (strcmp(name, "open-lake") == 0) {
		*habitat = WORLD_FISHING_HABITAT_OPEN_LAKE;
	} else if (strcmp(name, "forest-pool") == 0) {
		*habitat = WORLD_FISHING_HABITAT_FOREST_POOL;
	} else if (strcmp(name, "marsh-pool") == 0) {
		*habitat = WORLD_FISHING_HABITAT_MARSH_POOL;
	} else if (strcmp(name, "ruin-cistern") == 0) {
		*habitat = WORLD_FISHING_HABITAT_RUIN_CISTERN;
	} else if (strcmp(name, "cave-pool") == 0) {
		*habitat = WORLD_FISHING_HABITAT_CAVE_POOL;
	} else {
		return false;
	}
	return true;
}

static struct fishing_species_record *record_by_id(
		const struct fishing_parse_state *state, const char *id)
{
	struct fishing_species_record *record;

	for (record = state ? state->species : NULL; record;
			record = record->next) {
		if (streq(record->species.id, id)) return record;
	}
	return NULL;
}

static struct fishing_rig_record *rig_record_by_id(
		const struct fishing_parse_state *state, const char *id)
{
	struct fishing_rig_record *record;

	for (record = state ? state->rigs : NULL; record;
			record = record->next) {
		if (streq(record->rig.id, id)) return record;
	}
	return NULL;
}

static void free_record(struct fishing_species_record *record)
{
	if (!record) return;
	string_free((char *)record->species.id);
	string_free((char *)record->species.name);
	mem_free(record);
}

static void free_rig_record(struct fishing_rig_record *record)
{
	if (!record) return;
	string_free((char *)record->rig.id);
	string_free((char *)record->rig.name);
	string_free((char *)record->rig.tval_name);
	string_free((char *)record->rig.item_name);
	mem_free(record);
}

static void free_discovery(struct world_fishing_discovery *discovery)
{
	if (!discovery) return;
	string_free((char *)discovery->id);
	string_free((char *)discovery->species_id);
	string_free((char *)discovery->rig_id);
	string_free((char *)discovery->location_id);
	string_free((char *)discovery->system_id);
	string_free((char *)discovery->portal_id);
	string_free((char *)discovery->out_route_id);
	string_free((char *)discovery->in_route_id);
	string_free((char *)discovery->message);
}

static void free_parse_state(struct fishing_parse_state *state)
{
	struct fishing_species_record *record;

	if (!state) return;
	record = state->species;
	while (record) {
		struct fishing_species_record *next = record->next;

		free_record(record);
		record = next;
	}
	{
		struct fishing_rig_record *rig = state->rigs;

		while (rig) {
			struct fishing_rig_record *next = rig->next;

			free_rig_record(rig);
			rig = next;
		}
	}
	{
		struct fishing_discovery_record *discovery = state->discoveries;

		while (discovery) {
			struct fishing_discovery_record *next = discovery->next;

			free_discovery(&discovery->discovery);
			mem_free(discovery);
			discovery = next;
		}
	}
	mem_free(state);
}

static enum parser_error parse_fishing_population(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	int minimum = parser_getint(p, "minimum");
	int maximum = parser_getint(p, "maximum");

	if (state->population_seen) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (minimum < 1 || maximum < minimum ||
			maximum > WORLD_FISHING_MAX_FISH) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->rules.minimum_fish = minimum;
	state->rules.maximum_fish = maximum;
	state->population_seen = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_motion(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	int horizontal = parser_getint(p, "horizontal");
	int vertical = parser_getint(p, "vertical");

	if (state->motion_seen) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (horizontal < 0 || horizontal > 100 || vertical < 0 ||
			vertical > 100) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->rules.move_chance = horizontal;
	state->rules.depth_move_chance = vertical;
	state->motion_seen = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_evasion(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	int chance = parser_getint(p, "chance");
	int radius = parser_getint(p, "radius");

	if (state->evasion_seen) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (chance < 0 || chance > 100 || radius < 0 ||
			radius >= WORLD_FISHING_COLUMNS) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->rules.evasion_chance = chance;
	state->rules.evasion_radius = radius;
	state->evasion_seen = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_patience(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	int turns = parser_getint(p, "turns");

	if (state->patience_seen) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (turns < 1 || turns > 1000) return PARSE_ERROR_INVALID_VALUE;
	state->rules.patience_turns = turns;
	state->patience_seen = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_attraction(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	int horizontal = parser_getint(p, "horizontal");
	int vertical = parser_getint(p, "vertical");

	if (state->attraction_seen) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (horizontal < 1 || horizontal >= WORLD_FISHING_COLUMNS ||
			vertical < 1 || vertical > WORLD_FISHING_DEPTH_SCALE) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->rules.attraction_horizontal = horizontal;
	state->rules.attraction_vertical = vertical;
	state->attraction_seen = true;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_species(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *name = parser_getstr(p, "name");
	struct fishing_species_record *record;

	if (!valid_species_id(id) || !name[0]) return PARSE_ERROR_INVALID_VALUE;
	if (record_by_id(state, id)) return PARSE_ERROR_REPEATED_DIRECTIVE;
	for (record = state->species; record; record = record->next) {
		if (!my_stricmp(record->species.name, name)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	if (state->species_count >= WORLD_FISHING_MAX_SPECIES) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	record = mem_zalloc(sizeof(*record));
	record->species.id = string_make(id);
	record->species.name = string_make(name);
	record->next = state->species;
	state->species = record;
	state->current = record;
	state->current_rig = NULL;
	state->species_count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_rig(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *name = parser_getstr(p, "name");
	struct fishing_rig_record *record;

	if (!valid_species_id(id) || !name[0]) return PARSE_ERROR_INVALID_VALUE;
	if (rig_record_by_id(state, id)) return PARSE_ERROR_REPEATED_DIRECTIVE;
	for (record = state->rigs; record; record = record->next) {
		if (!my_stricmp(record->rig.name, name)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	if (state->rig_count >= WORLD_FISHING_MAX_RIGS) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	record = mem_zalloc(sizeof(*record));
	record->rig.id = string_make(id);
	record->rig.name = string_make(name);
	record->next = state->rigs;
	state->rigs = record;
	state->current = NULL;
	state->current_rig = record;
	state->rig_count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_rig_item(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_rig_record *record = state->current_rig;
	const char *tval = parser_getsym(p, "tval");
	const char *name = parser_getstr(p, "name");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_RIG_FIELD_ITEM) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!tval[0] || !name[0]) return PARSE_ERROR_INVALID_VALUE;
	record->rig.tval_name = string_make(tval);
	record->rig.item_name = string_make(name);
	record->fields |= FISHING_RIG_FIELD_ITEM;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_rig_capabilities(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_rig_record *record = state->current_rig;
	int priority = parser_getint(p, "priority");
	int reach = parser_getint(p, "reach");
	int depth = parser_getint(p, "depth");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_RIG_FIELD_CAPABILITIES) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (priority < 1 || priority > 1000 || reach < 1 ||
			reach >= WORLD_FISHING_COLUMNS || depth < 1 ||
			depth > WORLD_FISHING_DEPTH_SCALE) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	record->rig.priority = priority;
	record->rig.maximum_reach = reach;
	record->rig.maximum_depth = depth;
	record->fields |= FISHING_RIG_FIELD_CAPABILITIES;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_graphics(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record = state->current;
	const char *color = parser_getsym(p, "color");
	wchar_t glyph = parser_getchar(p, "glyph");
	int attr;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_FIELD_GRAPHICS) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	attr = fishing_color_attr(color);
	if (attr < 0) return PARSE_ERROR_INVALID_COLOR;
	if (glyph < 32 || glyph > 126) return PARSE_ERROR_INVALID_VALUE;
	record->species.glyph = (char)glyph;
	record->species.attr = (uint8_t)attr;
	record->fields |= FISHING_FIELD_GRAPHICS;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_depth(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record = state->current;
	int minimum = parser_getint(p, "minimum");
	int maximum = parser_getint(p, "maximum");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_FIELD_DEPTH) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (minimum < 1 || maximum < minimum ||
			maximum > WORLD_FISHING_DEPTH_SCALE) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	record->species.depth_min = minimum;
	record->species.depth_max = maximum;
	record->fields |= FISHING_FIELD_DEPTH;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_behavior(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record = state->current;
	int run = parser_getint(p, "run");
	int wind = parser_getint(p, "wind");
	int timeout = parser_getint(p, "timeout");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_FIELD_BEHAVIOR) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (run < 0 || run > 95 || wind < 1 || wind > 100 || timeout < 1 ||
			timeout > 100) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	record->species.run_chance = run;
	record->species.wind_needed = wind;
	record->species.bite_timeout = timeout;
	record->fields |= FISHING_FIELD_BEHAVIOR;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_danger(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record = state->current;
	int bonus = parser_getint(p, "bonus");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_FIELD_DANGER) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (bonus < 0 || bonus > 100) return PARSE_ERROR_INVALID_VALUE;
	record->species.danger_weight = bonus;
	record->fields |= FISHING_FIELD_DANGER;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_larder(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record = state->current;
	unsigned int value = parser_getuint(p, "value");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_FIELD_LARDER) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (!value) return PARSE_ERROR_INVALID_VALUE;
	record->species.larder_value = value;
	record->fields |= FISHING_FIELD_LARDER;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_experience(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record = state->current;
	int caught = parser_getint(p, "catch");
	int donated = parser_getint(p, "donation");

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (record->fields & FISHING_FIELD_EXPERIENCE) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (caught < 0 || caught > UINT16_MAX ||
			donated < 0 || donated > UINT16_MAX) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	record->species.catch_experience = (uint16_t)caught;
	record->species.donation_experience = (uint16_t)donated;
	record->fields |= FISHING_FIELD_EXPERIENCE;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_habitat(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record = state->current;
	enum world_fishing_habitat habitat;
	int weight = parser_getint(p, "weight");
	uint32_t bit;

	if (!record) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (!world_fishing_habitat_from_name(parser_getsym(p, "habitat"),
			&habitat)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (weight < 0 || weight > 10000) return PARSE_ERROR_INVALID_VALUE;
	bit = UINT32_C(1) << habitat;
	if (record->habitats & bit) return PARSE_ERROR_REPEATED_DIRECTIVE;
	record->species.habitat_weights[habitat] = weight;
	record->habitats |= bit;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_fishing_discovery(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_discovery_record *record;
	struct fishing_discovery_record **tail;
	struct world_fishing_discovery *discovery;
	const char *id = parser_getsym(p, "id");
	const char *species = parser_getsym(p, "species");
	const char *rig = parser_getsym(p, "rig");
	const char *location = parser_getsym(p, "location");
	const char *system = parser_getsym(p, "system");
	const char *portal = parser_getsym(p, "portal");
	const char *out_route = parser_getsym(p, "out-route");
	const char *in_route = parser_getsym(p, "in-route");
	const char *message = parser_getstr(p, "message");
	enum world_fishing_habitat habitat;
	int minimum_depth = parser_getint(p, "minimum-depth");

	if (!valid_species_id(id) || !valid_species_id(species) ||
			!valid_species_id(rig) || !world_id_is_valid(location) ||
			!world_id_is_valid(system) || !world_id_is_valid(portal) ||
			!world_id_is_valid(out_route) || !world_id_is_valid(in_route) ||
			!message[0] || minimum_depth < 1 ||
			minimum_depth > WORLD_FISHING_DEPTH_SCALE ||
			!world_fishing_habitat_from_name(parser_getsym(p, "habitat"),
				&habitat)) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	if (state->discovery_count >= WORLD_FISHING_MAX_DISCOVERIES) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	for (record = state->discoveries; record; record = record->next) {
		if (streq(record->discovery.id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	record = mem_zalloc(sizeof(*record));
	discovery = &record->discovery;
	discovery->id = string_make(id);
	discovery->species_id = string_make(species);
	discovery->rig_id = string_make(rig);
	discovery->location_id = string_make(location);
	discovery->habitat = habitat;
	discovery->minimum_depth = minimum_depth;
	discovery->system_id = string_make(system);
	discovery->portal_id = string_make(portal);
	discovery->out_route_id = string_make(out_route);
	discovery->in_route_id = string_make(in_route);
	discovery->message = string_make(message);
	tail = &state->discoveries;
	while (*tail) tail = &(*tail)->next;
	*tail = record;
	state->discovery_count++;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_fishing(void)
{
	struct parser *p = parser_new();
	struct fishing_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "population int minimum int maximum",
		parse_fishing_population);
	parser_reg(p, "motion int horizontal int vertical", parse_fishing_motion);
	parser_reg(p, "evasion int chance int radius", parse_fishing_evasion);
	parser_reg(p, "patience int turns", parse_fishing_patience);
	parser_reg(p, "attraction int horizontal int vertical",
		parse_fishing_attraction);
	parser_reg(p, "rig sym id str name", parse_fishing_rig);
	parser_reg(p, "rig-item sym tval str name", parse_fishing_rig_item);
	parser_reg(p, "rig-capabilities int priority int reach int depth",
		parse_fishing_rig_capabilities);
	parser_reg(p, "species sym id str name", parse_fishing_species);
	parser_reg(p, "graphics char glyph sym color", parse_fishing_graphics);
	parser_reg(p, "depth-band int minimum int maximum", parse_fishing_depth);
	parser_reg(p, "behavior int run int wind int timeout",
		parse_fishing_behavior);
	parser_reg(p, "danger-bonus int bonus", parse_fishing_danger);
	parser_reg(p, "larder-value uint value", parse_fishing_larder);
	parser_reg(p, "experience int catch int donation", parse_fishing_experience);
	parser_reg(p, "habitat sym habitat int weight", parse_fishing_habitat);
	parser_reg(p, "discovery sym id sym species sym rig sym location "
			"sym habitat int minimum-depth sym system sym portal "
			"sym out-route sym in-route str message", parse_fishing_discovery);
	return p;
}

static errr run_parse_fishing(struct parser *p)
{
	return parse_file_quit_not_found(p, "fishing");
}

static errr validate_fishing_state(const struct fishing_parse_state *state)
{
	const uint32_t all_habitats =
		(UINT32_C(1) << WORLD_FISHING_HABITAT_MAX) - 2;
	struct fishing_species_record *record;
	struct fishing_rig_record *rig;
	int habitat;

	if (!state->population_seen || !state->motion_seen ||
			!state->evasion_seen || !state->patience_seen ||
			!state->attraction_seen ||
			state->species_count < 1 ||
			state->rig_count < 1) {
		plog("Fishing data requires population, motion, patience, "
			"attraction, rigs, and species");
		return PARSE_ERROR_TOO_FEW_ENTRIES;
	}
	for (rig = state->rigs; rig; rig = rig->next) {
		struct fishing_rig_record *other;

		if (rig->fields != FISHING_RIG_FIELD_ALL) {
			plog_fmt("Fishing rig %s is incomplete", rig->rig.id);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
		for (other = rig->next; other; other = other->next) {
			if (rig->rig.priority == other->rig.priority ||
					(streq(rig->rig.tval_name, other->rig.tval_name) &&
					!my_stricmp(rig->rig.item_name, other->rig.item_name))) {
				plog_fmt("Fishing rig %s duplicates a priority or item",
					rig->rig.id);
				return PARSE_ERROR_REPEATED_DIRECTIVE;
			}
		}
	}
	for (record = state->species; record; record = record->next) {
		if (record->fields != FISHING_FIELD_ALL ||
				record->habitats != all_habitats) {
			plog_fmt("Fishing species %s is incomplete", record->species.id);
			return PARSE_ERROR_TOO_FEW_ENTRIES;
		}
	}
	for (habitat = WORLD_FISHING_HABITAT_NONE + 1;
			habitat < WORLD_FISHING_HABITAT_MAX; habitat++) {
		int total = 0;

		for (record = state->species; record; record = record->next) {
			total += record->species.habitat_weights[habitat];
		}
		if (!total) {
			plog_fmt("Fishing habitat %s has no species weight",
				world_fishing_habitat_name(
					(enum world_fishing_habitat)habitat));
			return PARSE_ERROR_INVALID_VALUE;
		}
	}
	return PARSE_ERROR_NONE;
}

static void cleanup_fishing(void)
{
	int i;

	for (i = 0; i < fishing_species_count; i++) {
		string_free((char *)fishing_species[i].id);
		string_free((char *)fishing_species[i].name);
	}
	mem_free(fishing_species);
	fishing_species = NULL;
	fishing_species_count = 0;
	for (i = 0; i < fishing_rig_count; i++) {
		string_free((char *)fishing_rigs[i].id);
		string_free((char *)fishing_rigs[i].name);
		string_free((char *)fishing_rigs[i].tval_name);
		string_free((char *)fishing_rigs[i].item_name);
	}
	mem_free(fishing_rigs);
	fishing_rigs = NULL;
	fishing_rig_count = 0;
	for (i = 0; i < fishing_discovery_count; i++) {
		free_discovery(&fishing_discoveries[i]);
	}
	mem_free(fishing_discoveries);
	fishing_discoveries = NULL;
	fishing_discovery_count = 0;
	memset(&fishing_rules, 0, sizeof(fishing_rules));
	fishing_data_loaded = false;
}

static errr finish_parse_fishing(struct parser *p)
{
	struct fishing_parse_state *state = parser_priv(p);
	struct fishing_species_record *record;
	struct fishing_species_record *next;
	struct fishing_rig_record *rig;
	struct fishing_rig_record *next_rig;
	struct fishing_discovery_record *discovery;
	struct fishing_discovery_record *next_discovery;
	errr result = validate_fishing_state(state);
	int index;

	if (result != PARSE_ERROR_NONE) {
		free_parse_state(state);
		parser_destroy(p);
		return result;
	}
	cleanup_fishing();
	fishing_species = mem_zalloc((size_t)state->species_count *
		sizeof(*fishing_species));
	index = state->species_count - 1;
	for (record = state->species; record; record = next, index--) {
		next = record->next;
		fishing_species[index] = record->species;
		mem_free(record);
	}
	fishing_species_count = state->species_count;
	fishing_rigs = mem_zalloc((size_t)state->rig_count *
		sizeof(*fishing_rigs));
	index = state->rig_count - 1;
	for (rig = state->rigs; rig; rig = next_rig, index--) {
		next_rig = rig->next;
		fishing_rigs[index] = rig->rig;
		mem_free(rig);
	}
	fishing_rig_count = state->rig_count;
	fishing_discoveries = mem_zalloc((size_t)state->discovery_count *
		sizeof(*fishing_discoveries));
	index = 0;
	for (discovery = state->discoveries; discovery;
			discovery = next_discovery, index++) {
		next_discovery = discovery->next;
		fishing_discoveries[index] = discovery->discovery;
		mem_free(discovery);
	}
	fishing_discovery_count = state->discovery_count;
	fishing_rules = state->rules;
	fishing_data_loaded = true;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser fishing_parser = {
	"fishing",
	init_parse_fishing,
	run_parse_fishing,
	finish_parse_fishing,
	cleanup_fishing
};

const struct world_fishing_rules *world_fishing_rules(void)
{
	return fishing_data_loaded ? &fishing_rules : NULL;
}

int world_fishing_species_count(void)
{
	return fishing_data_loaded ? fishing_species_count : 0;
}

const struct world_fishing_species *world_fishing_species_by_kind(
		world_fishing_kind kind)
{
	if (!fishing_data_loaded || kind < 0 || kind >= fishing_species_count) {
		return NULL;
	}
	return &fishing_species[kind];
}

world_fishing_kind world_fishing_kind_by_id(const char *id)
{
	int i;

	if (!fishing_data_loaded || !id) return WORLD_FISHING_KIND_NONE;
	for (i = 0; i < fishing_species_count; i++) {
		if (streq(fishing_species[i].id, id)) return (world_fishing_kind)i;
	}
	return WORLD_FISHING_KIND_NONE;
}

int world_fishing_rig_count(void)
{
	return fishing_data_loaded ? fishing_rig_count : 0;
}

const struct world_fishing_rig *world_fishing_rig_by_index(int index)
{
	if (!fishing_data_loaded || index < 0 || index >= fishing_rig_count) {
		return NULL;
	}
	return &fishing_rigs[index];
}

const struct world_fishing_rig *world_fishing_rig_by_id(const char *id)
{
	int i;

	if (!fishing_data_loaded || !id) return NULL;
	for (i = 0; i < fishing_rig_count; i++) {
		if (streq(fishing_rigs[i].id, id)) return &fishing_rigs[i];
	}
	return NULL;
}

int world_fishing_discovery_count(void)
{
	return fishing_data_loaded ? fishing_discovery_count : 0;
}

const struct world_fishing_discovery *world_fishing_discovery_by_index(
		int index)
{
	if (!fishing_data_loaded || index < 0 ||
			index >= fishing_discovery_count) {
		return NULL;
	}
	return &fishing_discoveries[index];
}

bool world_fishing_validate_discoveries(void)
{
	int discovery_index;

	for (discovery_index = 0; discovery_index < fishing_discovery_count;
			discovery_index++) {
		const struct world_fishing_discovery *discovery =
			&fishing_discoveries[discovery_index];
		const struct world_fishing_rig *rig =
			world_fishing_rig_by_id(discovery->rig_id);
		const struct level *level = level_by_id(discovery->location_id);
		const struct world_spelunk_system_profile *profile =
			world_spelunk_system_profile_by_id(discovery->system_id);
		const struct world_route *out_route =
			world_route_by_id(discovery->out_route_id);
		const struct world_route *in_route =
			world_route_by_id(discovery->in_route_id);
		const struct world_spelunk_system_portal_contract *portal = NULL;
		int i;

		for (i = 0; profile && i < profile->portal_count; i++) {
			if (streq(profile->portals[i].id, discovery->portal_id)) {
				portal = &profile->portals[i];
				break;
			}
		}
		if (world_fishing_kind_by_id(discovery->species_id) ==
				WORLD_FISHING_KIND_NONE || !rig ||
				rig->maximum_depth < discovery->minimum_depth || !level ||
				level->fishing_habitat != discovery->habitat || !profile ||
				!streq(profile->location_id, discovery->location_id) ||
				!portal || portal->initially_enabled || !out_route ||
				!in_route || !out_route->requires_unlock ||
				!in_route->requires_unlock ||
				!streq(out_route->from, discovery->location_id) ||
				!streq(out_route->from_entry, portal->entry_id) ||
				!streq(in_route->to, discovery->location_id) ||
				!streq(in_route->to_entry, portal->entry_id) ||
				!streq(out_route->to, in_route->from) ||
				!streq(out_route->to_entry, in_route->from_entry)) {
			plog_fmt("Fishing discovery %s has invalid cross-references",
				discovery->id);
			return false;
		}
	}
	return true;
}

bool world_fishing_link_rig_items(void)
{
	int i;

	if (!fishing_data_loaded || fishing_rig_count < 1) return false;
	for (i = 0; i < fishing_rig_count; i++) {
		struct world_fishing_rig *rig = &fishing_rigs[i];

		rig->tval = tval_find_idx(rig->tval_name);
		rig->sval = rig->tval > 0 ?
			lookup_sval(rig->tval, rig->item_name) : -1;
		if (rig->tval <= 0 || rig->sval < 0 ||
				!lookup_kind(rig->tval, rig->sval)) {
			plog_fmt("Fishing rig %s has no matching %s object %s",
				rig->id, rig->tval_name, rig->item_name);
			return false;
		}
	}
	return true;
}

const struct world_fishing_rig *world_fishing_rig_for_kind(
		const struct object_kind *kind)
{
	int i;

	if (!kind || !fishing_data_loaded) return NULL;
	for (i = 0; i < fishing_rig_count; i++) {
		if (fishing_rigs[i].tval == kind->tval &&
				fishing_rigs[i].sval == kind->sval) {
			return &fishing_rigs[i];
		}
	}
	return NULL;
}
