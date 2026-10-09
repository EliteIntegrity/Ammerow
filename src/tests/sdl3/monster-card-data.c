/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/monster-card-data.c */
/* Exercise lore-gated SDL3 examine-card text fields. */

#include "unit-test.h"

#include "sdl3/monster-card-data.h"

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

static int test_unknown_defense_hides_values(void *state)
{
	struct sdl3_monster_card_knowledge knowledge = {
		.armour_known = false,
		.armour = 93,
		.average_life = 777
	};
	struct sdl3_monster_card_facts facts;
	(void)state;

	sdl3_monster_card_format_knowledge(&knowledge, &facts);
	require(streq(facts.defense, "Defense: not yet learned"));
	require(!strstr(facts.defense, "93"));
	require(!strstr(facts.defense, "777"));
	require(streq(facts.attacks, "Attacks: not yet learned"));
	require(!facts.powers[0]);
	ok;
}

static int test_known_defense(void *state)
{
	struct sdl3_monster_card_knowledge knowledge = {
		.armour_known = true,
		.armour = 12,
		.average_life = 8
	};
	struct sdl3_monster_card_facts facts;
	(void)state;

	sdl3_monster_card_format_knowledge(&knowledge, &facts);
	require(streq(facts.defense, "Defense: AC 12 | Life 8"));
	ok;
}

static int test_learned_details(void *state)
{
	struct sdl3_monster_card_knowledge knowledge = {
		.relative_speed = 10,
		.drop_known = true,
		.maximum_drops = 2,
		.drop_kind = SDL3_MONSTER_DROP_OBJECTS,
		.drop_quality = SDL3_MONSTER_DROP_GOOD,
		.attacks = { { "bite to poison (2d4)" }, 1, 1 },
		.powers = { { "breathe fire", "heal-self" }, 2, 2 },
		.weaknesses = { { "cold" }, 1, 1 },
		.resistances = { { "fire", "poison" }, 2, 2 },
		.traits = { { "open doors", "regenerates" }, 2, 2 }
	};
	struct sdl3_monster_card_facts facts;
	(void)state;

	sdl3_monster_card_format_knowledge(&knowledge, &facts);
	require(streq(facts.profile,
		"Speed: fast | Drops: up to 2 good objects"));
	require(streq(facts.attacks, "Attacks: bite to poison (2d4)"));
	require(streq(facts.powers, "Powers: breathe fire; heal-self"));
	require(streq(facts.elements,
		"Weak: cold | Resists: fire; poison"));
	require(streq(facts.traits, "Traits: open doors; regenerates"));
	ok;
}

static int test_known_no_physical_attacks(void *state)
{
	struct sdl3_monster_card_knowledge knowledge = {
		.no_physical_attacks = true,
		.all_known = true,
		.drop_known = true
	};
	struct sdl3_monster_card_facts facts;
	(void)state;

	sdl3_monster_card_format_knowledge(&knowledge, &facts);
	require(streq(facts.attacks, "Attacks: none"));
	require(streq(facts.powers, "Powers: none"));
	require(strstr(facts.profile, "Drops: none"));
	ok;
}

static int test_list_reports_hidden_remainder(void *state)
{
	struct sdl3_monster_card_knowledge knowledge = {
		.attacks = { { "hit", "bite" }, 2, 5 }
	};
	struct sdl3_monster_card_facts facts;
	(void)state;

	sdl3_monster_card_format_knowledge(&knowledge, &facts);
	require(streq(facts.attacks, "Attacks: hit; bite; +3 more"));
	ok;
}

const char *suite_name = "sdl3/monster-card-data";
struct test tests[] = {
	{ "unknown defense hides values", test_unknown_defense_hides_values },
	{ "known defense", test_known_defense },
	{ "learned details", test_learned_details },
	{ "known no physical attacks", test_known_no_physical_attacks },
	{ "list reports hidden remainder", test_list_reports_hidden_remainder },
	{ NULL, NULL },
};
