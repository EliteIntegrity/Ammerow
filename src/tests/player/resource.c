/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* player/resource */

#include "unit-test.h"

#include "init.h"
#include "player-resource.h"
#include "player.h"

int setup_tests(void **state)
{
	struct player *p = mem_zalloc(sizeof(*p));

	z_info = mem_zalloc(sizeof(*z_info));
	z_info->stamina_base = 50;
	z_info->stamina_str_weight = 1;
	z_info->stamina_dex_weight = 1;
	z_info->stamina_con_weight = 2;
	z_info->stamina_cap = 150;
	z_info->stamina_recovery = 1;
	z_info->stamina_wait_recovery = 15;
	z_info->air_capacity = 6;
	p->upkeep = mem_zalloc(sizeof(*p->upkeep));
	p->state.stat_ind[STAT_STR] = 10;
	p->state.stat_ind[STAT_DEX] = 10;
	p->state.stat_ind[STAT_CON] = 10;
	player_resources_reset(p);
	*state = p;
	return 0;
}

int teardown_tests(void *state)
{
	struct player *p = state;

	mem_free(p->upkeep);
	mem_free(p);
	mem_free(z_info);
	z_info = NULL;
	return 0;
}

static int test_derived_maximum(void *state)
{
	struct player *p = state;

	eq(player_stamina_maximum_for_indices(7, 7, 7), 78);
	eq(player_stamina_maximum(p), 90);
	eq(player_stamina_current(p), 90);
	eq(player_stamina_maximum_for_indices(100, 100, 100), 150);
	ok;
}

static int test_spend_and_recovery(void *state)
{
	struct player *p = state;

	require(player_stamina_spend(p, 20));
	eq(player_stamina_current(p), 70);
	eq(p->upkeep->stamina_turn, PLAYER_STAMINA_TURN_EXERTED);
	p->upkeep->energy_use = 100;
	player_stamina_process_turn(p);
	eq(player_stamina_current(p), 70);
	eq(p->upkeep->stamina_turn, PLAYER_STAMINA_TURN_NORMAL);
	player_stamina_process_turn(p);
	eq(player_stamina_current(p), 71);
	player_stamina_mark_waiting(p);
	player_stamina_process_turn(p);
	eq(player_stamina_current(p), 86);
	player_stamina_mark_resolved(p);
	player_stamina_process_turn(p);
	eq(player_stamina_current(p), 86);
	require(!player_stamina_spend(p, 87));
	require(!player_stamina_spend(p, -1));
	eq(player_stamina_current(p), 86);
	ok;
}

static int test_maximum_change_preserves_fraction(void *state)
{
	struct player *p = state;

	p->resources.stamina.current = 45;
	p->state.stat_ind[STAT_STR] = 15;
	p->state.stat_ind[STAT_DEX] = 15;
	p->state.stat_ind[STAT_CON] = 15;
	player_resources_update_max(p);
	eq(player_stamina_maximum(p), 110);
	eq(player_stamina_current(p), 55);
	ok;
}

static int test_legacy_migration_and_air(void *state)
{
	struct player *p = state;

	p->state.stat_ind[STAT_STR] = 10;
	p->state.stat_ind[STAT_DEX] = 10;
	p->state.stat_ind[STAT_CON] = 10;
	player_resources_migrate_legacy(p, 25, 100, 3);
	eq(player_stamina_maximum(p), 90);
	eq(player_stamina_current(p), 23);
	eq(player_air_maximum(p), 6);
	eq(player_air_current(p), 3);
	player_air_set(p, -1);
	eq(player_air_current(p), 0);
	player_air_set(p, 100);
	eq(player_air_current(p), 6);
	ok;
}

const char *suite_name = "player/resource";
struct test tests[] = {
	{ "derived maximum", test_derived_maximum },
	{ "spend and recovery", test_spend_and_recovery },
	{ "maximum change", test_maximum_change_preserves_fraction },
	{ "legacy migration and air", test_legacy_migration_and_air },
	{ NULL, NULL }
};
