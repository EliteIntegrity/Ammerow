/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/effects.c */
/* Exercise fixed-state combat presentation timing. */

#include "unit-test.h"

#include "sdl3/effects.h"

int setup_tests(void **data)
{
	(void)data;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	return 0;
}

static int test_melee_lifecycle(void *state)
{
	struct sdl3_combat_effects effects;
	struct sdl3_combat_effect *effect;
	(void)state;

	sdl3_effects_clear(&effects);
	sdl3_effects_start_melee(&effects, 100, loc(4, 5), loc(5, 5), true);
	effect = &effects.items[0];
	require(effects.active);
	require(!effect->presented);
	require(sdl3_effects_present(&effects, 120));
	require(effect->presented);
	eq(effect->started_ms, 120);
	eq(effect->kind, SDL3_EFFECT_MELEE);
	require(effect->hit);
	eq(sdl3_effect_phase(effect, 120, 8), 0);
	eq(sdl3_effect_phase(effect, 240, 8), 4);
	require(!sdl3_effects_advance(&effects, 130));
	require(sdl3_effects_advance(&effects, 144));
	require(effects.active);
	require(sdl3_effects_advance(&effects, 360));
	require(!effects.active);
	ok;
}

static int test_projectile_timing(void *state)
{
	struct sdl3_combat_effects effects;
	struct sdl3_combat_effect *effect;
	(void)state;

	sdl3_effects_clear(&effects);
	sdl3_effects_start_projectile(&effects, SDL3_EFFECT_MISSILE, 50, 40,
		loc(1, 1), loc(2, 1), L'-', 11);
	effect = &effects.items[0];
	require(effects.active);
	eq(effect->duration_ms, SDL3_PROJECTILE_EFFECT_MS);
	require(sdl3_effects_present(&effects, 50));
	eq(effect->next_frame_ms, 74);
	eq(sdl3_effect_phase(effect, 80, 4), 0);
	require(sdl3_effects_advance(&effects, 74));
	eq(effect->next_frame_ms, 98);
	require(sdl3_effects_advance(&effects, 230));
	require(!effects.active);
	ok;
}

static int test_invalid_projectile_is_ignored(void *state)
{
	struct sdl3_combat_effects effects;
	(void)state;

	sdl3_effects_clear(&effects);
	sdl3_effects_start_projectile(&effects, SDL3_EFFECT_MELEE, 0, 40,
		loc(0, 0), loc(1, 0), L'*', 1);
	require(!effects.active);
	ok;
}

static int test_damage_coexists_with_motion(void *state)
{
	struct sdl3_combat_effects effects;
	(void)state;

	sdl3_effects_clear(&effects);
	sdl3_effects_start_melee(&effects, 100, loc(4, 5), loc(5, 5), true);
	sdl3_effects_start_damage(&effects, 105, loc(5, 5), 17, false);
	require(effects.active);
	eq(effects.items[0].kind, SDL3_EFFECT_MELEE);
	eq(effects.items[1].kind, SDL3_EFFECT_DAMAGE);
	eq(effects.items[1].amount, 17);
	require(!effects.items[1].target_is_player);
	sdl3_effects_present(&effects, 105);
	require(sdl3_effects_advance(&effects, 345));
	require(!effects.items[0].active);
	require(effects.items[1].active);
	require(sdl3_effects_advance(&effects, 1105));
	require(!effects.active);
	ok;
}

static int test_clear_projectiles_preserves_damage(void *state)
{
	struct sdl3_combat_effects effects;
	(void)state;

	sdl3_effects_clear(&effects);
	sdl3_effects_start_projectile(&effects, SDL3_EFFECT_BOLT, 10, 50,
		loc(0, 0), loc(1, 0), L'-', 1);
	sdl3_effects_start_damage(&effects, 10, loc(1, 0), 8, true);
	require(sdl3_effects_has_projectile(&effects));
	sdl3_effects_clear_projectiles(&effects);
	require(!sdl3_effects_has_projectile(&effects));
	require(effects.active);
	eq(effects.items[1].kind, SDL3_EFFECT_DAMAGE);
	require(effects.items[1].target_is_player);
	ok;
}

static int test_repeated_melee_coalesces(void *state)
{
	struct sdl3_combat_effects effects;
	(void)state;

	sdl3_effects_clear(&effects);
	sdl3_effects_start_melee(&effects, 100, loc(4, 5), loc(5, 5), false);
	sdl3_effects_start_melee(&effects, 110, loc(4, 5), loc(5, 5), true);
	require(effects.items[0].active);
	require(effects.items[0].hit);
	eq(effects.items[0].started_ms, 110);
	require(!effects.items[1].active);
	ok;
}

static int test_projectile_path_coalesces_and_restarts_on_present(void *state)
{
	struct sdl3_combat_effects effects;
	(void)state;

	sdl3_effects_clear(&effects);
	sdl3_effects_start_projectile(&effects, SDL3_EFFECT_MISSILE, 10, 40,
		loc(1, 1), loc(2, 1), L'-', 11);
	sdl3_effects_present(&effects, 12);
	sdl3_effects_start_projectile(&effects, SDL3_EFFECT_MISSILE, 50, 40,
		loc(2, 1), loc(3, 1), L'-', 11);
	require(!effects.items[0].presented);
	require(!effects.items[1].active);
	require(loc_eq(effects.items[0].source, loc(2, 1)));
	require(loc_eq(effects.items[0].target, loc(3, 1)));
	sdl3_effects_present(&effects, 55);
	eq(effects.items[0].started_ms, 55);
	ok;
}

const char *suite_name = "sdl3/effects";
struct test tests[] = {
	{ "melee lifecycle", test_melee_lifecycle },
	{ "projectile timing", test_projectile_timing },
	{ "invalid projectile is ignored", test_invalid_projectile_is_ignored },
	{ "damage coexists with motion", test_damage_coexists_with_motion },
	{ "clearing projectiles preserves damage",
		test_clear_projectiles_preserves_damage },
	{ "repeated melee coalesces", test_repeated_melee_coalesces },
	{ "projectile path coalesces and restarts on present",
		test_projectile_path_coalesces_and_restarts_on_present },
	{ NULL, NULL },
};
