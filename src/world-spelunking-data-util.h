/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-data-util.h
 * \brief Shared parsing vocabulary for authored and procedural cave data.
 */

#ifndef WORLD_SPELUNKING_DATA_UTIL_H
#define WORLD_SPELUNKING_DATA_UTIL_H

#include "parser.h"
#include "world-spelunking.h"

struct world_spelunk_perception;

enum parser_error world_spelunk_parse_rules_fields(struct parser *p,
		struct world_spelunk_rules *rules);
enum parser_error world_spelunk_parse_stamina_cost_fields(struct parser *p,
		struct world_spelunk_rules *rules);
enum parser_error world_spelunk_parse_perception_fields(struct parser *p,
		struct world_spelunk_perception *perception);
bool world_spelunk_perception_is_valid(
		const struct world_spelunk_perception *perception);
bool world_spelunk_perception_matches(
		const struct world_spelunk_perception *a,
		const struct world_spelunk_perception *b);
bool world_spelunk_rules_match_player_resources(
		const struct world_spelunk_rules *rules);

#endif /* !WORLD_SPELUNKING_DATA_UTIL_H */
