/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking-survey.c
 * \brief Knowledge-safe semantic model for a connected cave survey.
 */

#include "angband.h"

#include "ui-spelunking-survey.h"
#include "world-spelunking-recipe-data.h"
#include "z-color.h"

#include <string.h>

bool ui_spelunk_survey_build(const struct world_spelunk_system *system,
		struct ui_spelunk_survey_model *model)
{
	int mapping[WORLD_SPELUNK_SYSTEM_NODE_MAX];
	size_t i;

	if (!system || !model || !world_spelunk_system_is_valid(system)) {
		return false;
	}
	memset(model, 0, sizeof(*model));
	model->current_node = -1;
	for (i = 0; i < WORLD_SPELUNK_SYSTEM_NODE_MAX; i++) mapping[i] = -1;
	for (i = 0; i < system->node_count; i++) {
		const struct world_spelunk_system_node *source = &system->nodes[i];
		const struct world_spelunk_recipe_definition *recipe;
		const struct level *level;
		const char *fallback_name;
		struct ui_screen_map_node *node;
		struct ui_screen_row *row;
		int total_at_depth = 0;
		int ordinal_at_depth = 0;
		int j;
		bool current;

		if (!source->discovered) continue;
		if (model->node_count >= WORLD_SPELUNK_SYSTEM_NODE_MAX) return false;
		mapping[i] = model->node_count;
		node = &model->nodes[model->node_count];
		row = &model->rows[model->node_count];
		recipe = source->recipe_id[0] ?
			world_spelunk_recipe_definition_by_id(source->recipe_id) : NULL;
		level = source->runtime ?
			level_by_id(source->runtime->location_id) : NULL;
		fallback_name = level ? world_level_area_name(level) : NULL;
		my_strcpy(model->labels[model->node_count],
			recipe ? recipe->name : fallback_name ? fallback_name :
				"Known cave section",
			sizeof(model->labels[model->node_count]));
		strnfmt(model->details[model->node_count],
			sizeof(model->details[model->node_count]),
			"Survey depth %u | %s", source->depth,
			source->runtime ? "visited" : "charted");
		my_strcpy(model->descriptions[model->node_count], recipe ?
			recipe->description :
			"This known section predates the connected procedural survey.",
			sizeof(model->descriptions[model->node_count]));
		for (j = 0; j < (int)system->node_count; j++) {
			if (!system->nodes[j].discovered ||
					system->nodes[j].depth != source->depth) continue;
			if (j < (int)i) ordinal_at_depth++;
			total_at_depth++;
		}
		current = streq(source->id, system->active_node_id);
		node->label = model->labels[model->node_count];
		node->detail = model->details[model->node_count];
		node->description = model->descriptions[model->node_count];
		my_strcpy(node->stamp[0], source->runtime ? "[===]" : "[...]",
			sizeof(node->stamp[0]));
		my_strcpy(node->stamp[1], source->runtime ? "|...|" : ":   :",
			sizeof(node->stamp[1]));
		my_strcpy(node->stamp[2], source->runtime ? "[===]" : "[...]",
			sizeof(node->stamp[2]));
		node->x = (2 * ordinal_at_depth - (total_at_depth - 1)) * 10;
		node->y = MIN((int)source->depth, 1000) * 6;
		node->glyph = L'@';
		node->attr = source->runtime ? COLOUR_L_GREEN : COLOUR_SLATE;
		node->current = current;
		row->label = node->label;
		row->detail = node->detail;
		row->attr = node->attr;
		row->enabled = true;
		if (current) model->current_node = model->node_count;
		model->node_count++;
	}
	for (i = 0; i < system->edge_count; i++) {
		const struct world_spelunk_system_edge *source = &system->edges[i];
		const struct world_spelunk_system_node *source_a =
			world_spelunk_system_node_by_id(system, source->node_a_id);
		const struct world_spelunk_system_node *source_b =
			world_spelunk_system_node_by_id(system, source->node_b_id);
		int a = -1;
		int b = -1;
		size_t j;

		if (!source->discovered) continue;
		for (j = 0; j < system->node_count; j++) {
			if (streq(system->nodes[j].id, source->node_a_id)) {
				a = mapping[j];
			}
			if (streq(system->nodes[j].id, source->node_b_id)) {
				b = mapping[j];
			}
		}
		if (a < 0 || b < 0 || model->edge_count >=
				WORLD_SPELUNK_SYSTEM_EDGE_MAX) {
			return false;
		}
		model->edges[model->edge_count].from = a;
		model->edges[model->edge_count].to = b;
		model->edges[model->edge_count].attr = COLOUR_L_BLUE;
		model->edges[model->edge_count].ready = source_a && source_b &&
			source_a->runtime && source_b->runtime;
		model->edge_count++;
	}
	return model->node_count > 0 && model->current_node >= 0;
}
