/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-data-util.c
 * \brief Shared parsing vocabulary for authored and procedural cave data.
 */

#include "angband.h"
#include "init.h"
#include "world-spelunking-data-util.h"
#include "world-spelunking-perception.h"

#include <limits.h>

enum parser_error world_spelunk_parse_rules_fields(struct parser *p,
		struct world_spelunk_rules *rules)
{
	int values[10];
	int i;

	if (!p || !rules) return PARSE_ERROR_INVALID_VALUE;
	values[0] = parser_getint(p, "safe-fall");
	values[1] = parser_getint(p, "fall-damage");
	values[2] = parser_getint(p, "jump-cost");
	values[3] = parser_getint(p, "grip-cost");
	values[4] = parser_getint(p, "rest-gain");
	values[5] = parser_getint(p, "rope-length");
	values[6] = parser_getint(p, "rope-cost");
	values[7] = parser_getint(p, "breath");
	values[8] = parser_getint(p, "drowning-damage");
	values[9] = parser_getint(p, "swim-cost");
	for (i = 0; i < 10; i++) {
		if (values[i] < 0 || values[i] > UINT16_MAX) {
			return PARSE_ERROR_INVALID_VALUE;
		}
	}
	if (!values[5] || !values[7] || !values[8]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	rules->safe_fall_tiles = values[0];
	rules->fall_base_damage = values[1];
	rules->jump_stamina_cost = values[2];
	/* Legacy/default interpretation until an explicit stamina-costs record. */
	rules->grip_up_stamina_cost = values[3];
	rules->grip_lateral_stamina_cost = values[3];
	rules->grip_down_stamina_cost = values[3];
	rules->rest_stamina_gain = values[4];
	rules->rope_max_length = values[5];
	rules->rope_up_stamina_cost = values[6];
	rules->rope_lateral_stamina_cost = values[6];
	rules->rope_down_stamina_cost = values[6];
	rules->breath_turns = values[7];
	rules->drowning_damage = values[8];
	rules->swim_turn_stamina_cost = values[9];
	return PARSE_ERROR_NONE;
}

enum parser_error world_spelunk_parse_stamina_cost_fields(struct parser *p,
		struct world_spelunk_rules *rules)
{
	int values[6];
	int i;

	if (!p || !rules) return PARSE_ERROR_INVALID_VALUE;
	values[0] = parser_getint(p, "grip-up");
	values[1] = parser_getint(p, "grip-side");
	values[2] = parser_getint(p, "grip-down");
	values[3] = parser_getint(p, "rope-up");
	values[4] = parser_getint(p, "rope-side");
	values[5] = parser_getint(p, "rope-down");
	for (i = 0; i < 6; i++) {
		if (values[i] < 0 || values[i] > UINT16_MAX) {
			return PARSE_ERROR_INVALID_VALUE;
		}
	}
	rules->grip_up_stamina_cost = values[0];
	rules->grip_lateral_stamina_cost = values[1];
	rules->grip_down_stamina_cost = values[2];
	rules->rope_up_stamina_cost = values[3];
	rules->rope_lateral_stamina_cost = values[4];
	rules->rope_down_stamina_cost = values[5];
	return PARSE_ERROR_NONE;
}

bool world_spelunk_rules_match_player_resources(
		const struct world_spelunk_rules *rules)
{
	if (!rules) return false;
	/* Narrow parser tests may intentionally supply no complete constants
	 * record. Production constants are positive and make both mirrors strict. */
	if (!z_info) return true;
	return (!z_info->stamina_wait_recovery ||
			rules->rest_stamina_gain == z_info->stamina_wait_recovery) &&
		(!z_info->air_capacity ||
			rules->breath_turns == z_info->air_capacity);
}

enum parser_error world_spelunk_parse_perception_fields(struct parser *p,
		struct world_spelunk_perception *perception)
{
	const char *visible_color;
	const char *remembered_color;
	wchar_t visible_glyph;
	wchar_t remembered_glyph;
	int visible_attr;
	int remembered_attr;
	int horizontal;
	int upward;
	int downward;
	int peek;
	int downward_peek;
	int lateral;
	int close;
	int memory;

	if (!p || !perception) return PARSE_ERROR_INVALID_VALUE;
	horizontal = parser_getint(p, "horizontal");
	upward = parser_getint(p, "upward");
	downward = parser_getint(p, "downward");
	peek = parser_getint(p, "peek");
	downward_peek = parser_getint(p, "down-peek");
	lateral = parser_getint(p, "lateral");
	close = parser_getint(p, "close");
	memory = parser_getint(p, "memory");
	visible_glyph = parser_getchar(p, "visible-background");
	visible_color = parser_getsym(p, "visible-color");
	visible_attr = visible_color && visible_color[0] && !visible_color[1] ?
		color_char_to_attr(visible_color[0]) : color_text_to_attr(visible_color);
	remembered_glyph = parser_getchar(p, "remembered-background");
	remembered_color = parser_getsym(p, "remembered-color");
	remembered_attr = remembered_color && remembered_color[0] &&
		!remembered_color[1] ? color_char_to_attr(remembered_color[0]) :
		color_text_to_attr(remembered_color);
	if (horizontal < 1 || horizontal > 64 || upward < 1 || upward > 64 ||
			downward < 0 || downward > upward || peek < horizontal ||
			peek > 96 || downward_peek < peek || downward_peek > 128 ||
			lateral < 1 || lateral > 32 || close < 1 || close > peek ||
			memory < 1 || memory > 100 ||
			visible_glyph < 32 || visible_glyph > 126 ||
			remembered_glyph < 32 || remembered_glyph > 126 ||
			visible_attr < 0 || remembered_attr < 0) {
		return visible_attr < 0 || remembered_attr < 0 ?
			PARSE_ERROR_INVALID_COLOR : PARSE_ERROR_INVALID_VALUE;
	}
	perception->sight_horizontal_reach = (uint16_t)horizontal;
	perception->sight_upward_reach = (uint16_t)upward;
	perception->sight_downward_reach = (uint16_t)downward;
	perception->peek_reach = (uint16_t)peek;
	perception->downward_peek_reach = (uint16_t)downward_peek;
	perception->peek_lateral_reach = (uint16_t)lateral;
	perception->peek_close_reach = (uint16_t)close;
	perception->remembered_brightness_percent = (uint8_t)memory;
	perception->visible_air_glyph = visible_glyph;
	perception->visible_air_attr = (uint8_t)visible_attr;
	perception->remembered_air_glyph = remembered_glyph;
	perception->remembered_air_attr = (uint8_t)remembered_attr;
	return PARSE_ERROR_NONE;
}

bool world_spelunk_perception_is_valid(
		const struct world_spelunk_perception *perception)
{
	return perception && perception->sight_horizontal_reach >= 1 &&
		perception->sight_horizontal_reach <= 64 &&
		perception->sight_upward_reach >= 1 &&
		perception->sight_upward_reach <= 64 &&
		perception->sight_downward_reach <= perception->sight_upward_reach &&
		perception->peek_reach >= perception->sight_horizontal_reach &&
		perception->peek_reach <= 96 &&
		perception->downward_peek_reach >= perception->peek_reach &&
		perception->downward_peek_reach <= 128 &&
		perception->peek_lateral_reach >= 1 &&
		perception->peek_lateral_reach <= 32 &&
		perception->peek_close_reach >= 1 &&
		perception->peek_close_reach <= perception->peek_reach &&
		perception->remembered_brightness_percent >= 1 &&
		perception->remembered_brightness_percent <= 100 &&
		perception->visible_air_glyph >= 32 &&
		perception->visible_air_glyph <= 126 &&
		perception->remembered_air_glyph >= 32 &&
		perception->remembered_air_glyph <= 126 &&
		perception->visible_air_attr < MAX_COLORS &&
		perception->remembered_air_attr < MAX_COLORS;
}

bool world_spelunk_perception_matches(
		const struct world_spelunk_perception *a,
		const struct world_spelunk_perception *b)
{
	return a && b &&
		a->sight_horizontal_reach == b->sight_horizontal_reach &&
		a->sight_upward_reach == b->sight_upward_reach &&
		a->sight_downward_reach == b->sight_downward_reach &&
		a->peek_reach == b->peek_reach &&
		a->downward_peek_reach == b->downward_peek_reach &&
		a->peek_lateral_reach == b->peek_lateral_reach &&
		a->peek_close_reach == b->peek_close_reach &&
		a->remembered_brightness_percent ==
			b->remembered_brightness_percent &&
		a->visible_air_glyph == b->visible_air_glyph &&
		a->visible_air_attr == b->visible_air_attr &&
		a->remembered_air_glyph == b->remembered_air_glyph &&
		a->remembered_air_attr == b->remembered_air_attr;
}
