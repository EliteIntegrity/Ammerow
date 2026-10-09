/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Shared mechanics fixture for the focused spelunking suites. */

#ifndef TESTS_GAME_SPELUNKING_FIXTURE_H
#define TESTS_GAME_SPELUNKING_FIXTURE_H

#include "unit-test.h"
#include "cmd-mode-policy.h"
#include "datafile.h"
#include "effects.h"
#include "parser.h"
#include "player.h"
#include "ui-mode-input.h"
#include "ui-spelunking-appearance.h"
#include "ui-spelunking-input.h"
#include "ui-spelunking-survey.h"
#include "world-spelunking.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-system.h"
#include "world-spelunking-view.h"
#include "world-spelunking-visibility.h"
#include "z-color.h"

#define TEST_WIDTH 8
#define TEST_HEIGHT 10

struct spelunk_fixture {
	enum world_spelunk_tile cells[TEST_WIDTH * TEST_HEIGHT];
	struct world_spelunk_map map;
	struct world_spelunk_rules rules;
	struct world_spelunk_state state;
};

/** Explicit test balance; production balance comes from spelunking_layout.txt. */
static void fixture_rules(struct world_spelunk_rules *rules,
		unsigned int action_energy)
{
	memset(rules, 0, sizeof(*rules));
	rules->action_energy = action_energy;
	rules->safe_fall_tiles = 2;
	rules->fall_base_damage = 10;
	rules->jump_stamina_cost = 10;
	rules->grip_up_stamina_cost = 5;
	rules->grip_lateral_stamina_cost = 5;
	rules->grip_down_stamina_cost = 5;
	rules->rest_stamina_gain = 15;
	rules->rope_max_length = 20;
	rules->rope_up_stamina_cost = 1;
	rules->rope_lateral_stamina_cost = 1;
	rules->rope_down_stamina_cost = 1;
	rules->breath_turns = 6;
	rules->drowning_damage = 10;
	rules->swim_turn_stamina_cost = 2;
}

static void fixture_open(struct spelunk_fixture *f)
{
	int i;

	memset(f, 0, sizeof(*f));
	for (i = 0; i < TEST_WIDTH * TEST_HEIGHT; i++) {
		f->cells[i] = WORLD_SPELUNK_AIR;
	}
	f->map.cells = f->cells;
	f->map.width = TEST_WIDTH;
	f->map.height = TEST_HEIGHT;
	f->map.stride = TEST_WIDTH;
	fixture_rules(&f->rules, 100);
}

static void fixture_rock(struct spelunk_fixture *f, int x, int y)
{
	f->cells[y * TEST_WIDTH + x] = WORLD_SPELUNK_ROCK;
}

static void fixture_water(struct spelunk_fixture *f, int x, int y)
{
	f->cells[y * TEST_WIDTH + x] = WORLD_SPELUNK_WATER;
}

static void fixture_ground_row(struct spelunk_fixture *f, int y)
{
	int x;

	for (x = 0; x < TEST_WIDTH; x++) fixture_rock(f, x, y);
}

static bool fixture_init(struct spelunk_fixture *f, int x, int y,
		int stamina)
{
	return world_spelunk_state_init(&f->state, &f->map, &f->rules,
		x, y, stamina);
}
int setup_tests(void **data)
{
	static const char *lines[] = {
		"actor:core.spelunk.actor.chasm-skitter:chasm skitter",
		"death-cause:a chasm skitter",
		"appearance:k:Light Green",
		"combat:4:24:1:2",
		"vitals:4:110:2",
		"spawn:core.spelunk.spawn.test:core.spelunk.test.001:core.spelunk.actor.chasm-skitter",
		"candidate:1:4:5"
	};
	struct parser *p = spelunking_actor_parser.init();
	int i;

	(void)data;
	if (!p) return 1;
	for (i = 0; i < (int)N_ELEMENTS(lines); i++) {
		if (parser_parse(p, lines[i]) != PARSE_ERROR_NONE) return 1;
	}
	return spelunking_actor_parser.finish(p) == PARSE_ERROR_NONE ? 0 : 1;
}

int teardown_tests(void *data)
{
	(void)data;
	spelunking_actor_parser.cleanup();
	return 0;
}

#endif /* TESTS_GAME_SPELUNKING_FIXTURE_H */
