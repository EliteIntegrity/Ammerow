/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking-survey.h
 * \brief Knowledge-safe semantic model for a connected cave survey.
 */

#ifndef UI_SPELUNKING_SURVEY_H
#define UI_SPELUNKING_SURVEY_H

#include "ui-screen.h"
#include "world-spelunking-system.h"

#include <stdbool.h>

#define UI_SPELUNK_SURVEY_LABEL_MAX 160
#define UI_SPELUNK_SURVEY_DETAIL_MAX 80
#define UI_SPELUNK_SURVEY_DESCRIPTION_MAX 256

struct ui_spelunk_survey_model {
	struct ui_screen_row rows[WORLD_SPELUNK_SYSTEM_NODE_MAX];
	struct ui_screen_map_node nodes[WORLD_SPELUNK_SYSTEM_NODE_MAX];
	struct ui_screen_map_edge edges[WORLD_SPELUNK_SYSTEM_EDGE_MAX];
	char labels[WORLD_SPELUNK_SYSTEM_NODE_MAX][UI_SPELUNK_SURVEY_LABEL_MAX];
	char details[WORLD_SPELUNK_SYSTEM_NODE_MAX][UI_SPELUNK_SURVEY_DETAIL_MAX];
	char descriptions[WORLD_SPELUNK_SYSTEM_NODE_MAX]
		[UI_SPELUNK_SURVEY_DESCRIPTION_MAX];
	int node_count;
	int edge_count;
	int current_node;
};

bool ui_spelunk_survey_build(const struct world_spelunk_system *system,
		struct ui_spelunk_survey_model *model);

#endif /* !UI_SPELUNKING_SURVEY_H */
