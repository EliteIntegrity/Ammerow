/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/scenario.h
 * \brief Bounded data contract for deterministic development scenarios.
 */

#ifndef HEADLESS_SCENARIO_H
#define HEADLESS_SCENARIO_H

#include "h-basic.h"

#include <stddef.h>
#include <stdint.h>

#define HEADLESS_SCENARIO_TEXT 128
#define HEADLESS_SCENARIO_PATH 1024
#define HEADLESS_SCENARIO_ACTION 48
#define HEADLESS_SCENARIO_MAX_ACTIONS 4096
#define HEADLESS_SCENARIO_MAX_STEPS 100000U
#define HEADLESS_SCENARIO_MAX_INVENTORY 32
#define HEADLESS_CAPTURE_MAX_FPS 60U
#define HEADLESS_CAPTURE_MAX_DURATION_MS 600000U

enum headless_bot_kind {
	HEADLESS_BOT_WAIT = 0,
	HEADLESS_BOT_RANDOM_WALK,
	HEADLESS_BOT_SCRIPTED,
	HEADLESS_BOT_CAVE_LARDER_LOOP,
	HEADLESS_BOT_CAVE_FATAL_FALL
};

enum headless_capture_kind {
	HEADLESS_CAPTURE_NONE = 0,
	HEADLESS_CAPTURE_INITIAL,
	HEADLESS_CAPTURE_FINAL,
	HEADLESS_CAPTURE_BOTH,
	HEADLESS_CAPTURE_SEQUENCE
};

enum headless_expected_result {
	HEADLESS_EXPECT_COMPLETE = 0,
	HEADLESS_EXPECT_DEAD,
	HEADLESS_EXPECT_ANY
};

struct headless_scenario {
	char name[HEADLESS_SCENARIO_TEXT];
	uint32_t seed;
	char race[HEADLESS_SCENARIO_TEXT];
	char player_class[HEADLESS_SCENARIO_TEXT];
	char player_name[HEADLESS_SCENARIO_TEXT];
	char location[HEADLESS_SCENARIO_TEXT];
	char entry[HEADLESS_SCENARIO_TEXT];
	char enter_route[HEADLESS_SCENARIO_TEXT];
	enum headless_bot_kind bot;
	unsigned int steps;
	enum headless_capture_kind capture;
	unsigned int capture_fps;
	unsigned int capture_preroll_ms;
	unsigned int capture_action_ms;
	unsigned int capture_postroll_ms;
	enum headless_expected_result expected_result;
	char interface_font[HEADLESS_SCENARIO_PATH];
	char map_font[HEADLESS_SCENARIO_PATH];
	char theme[HEADLESS_SCENARIO_TEXT];
	char map_style[HEADLESS_SCENARIO_TEXT];
	char map_presentation[HEADLESS_SCENARIO_TEXT];
	int map_zoom_percent;
	int screen_width;
	int screen_height;
	int start_x;
	int start_y;
	bool has_start_position;
	char start_anchor[HEADLESS_SCENARIO_TEXT];
	char encounter[HEADLESS_SCENARIO_TEXT];
	char inventory[HEADLESS_SCENARIO_MAX_INVENTORY][HEADLESS_SCENARIO_TEXT];
	uint8_t inventory_quantity[HEADLESS_SCENARIO_MAX_INVENTORY];
	size_t inventory_count;
	char equipment[HEADLESS_SCENARIO_MAX_INVENTORY][HEADLESS_SCENARIO_TEXT];
	size_t equipment_count;
	char actions[HEADLESS_SCENARIO_MAX_ACTIONS][HEADLESS_SCENARIO_ACTION];
	size_t action_count;
};

void headless_scenario_defaults(struct headless_scenario *scenario);
bool headless_scenario_load(const char *path,
		struct headless_scenario *scenario, char *error, size_t error_size);

#endif /* HEADLESS_SCENARIO_H */
