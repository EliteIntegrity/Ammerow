/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-card-data.h
 * \brief Lore-gated text fields for the SDL3 monster examine card.
 *
 *
 */

#ifndef INCLUDED_SDL3_MONSTER_CARD_DATA_H
#define INCLUDED_SDL3_MONSTER_CARD_DATA_H

#include <stdbool.h>
#include <stddef.h>

#define SDL3_MONSTER_CARD_FACT_CAPACITY 384
#define SDL3_MONSTER_CARD_LIST_CAPACITY 8

enum sdl3_monster_drop_kind {
	SDL3_MONSTER_DROP_MIXED = 0,
	SDL3_MONSTER_DROP_OBJECTS,
	SDL3_MONSTER_DROP_TREASURE
};

enum sdl3_monster_drop_quality {
	SDL3_MONSTER_DROP_ORDINARY = 0,
	SDL3_MONSTER_DROP_GOOD,
	SDL3_MONSTER_DROP_EXCEPTIONAL
};

struct sdl3_monster_card_list {
	const char *items[SDL3_MONSTER_CARD_LIST_CAPACITY];
	int count;
	int total;
};

struct sdl3_monster_card_knowledge {
	bool armour_known;
	int armour;
	int average_life;
	bool no_physical_attacks;
	bool all_known;
	int relative_speed;
	bool never_moves;
	bool drop_known;
	int maximum_drops;
	int specific_drops;
	enum sdl3_monster_drop_kind drop_kind;
	enum sdl3_monster_drop_quality drop_quality;
	struct sdl3_monster_card_list attacks;
	struct sdl3_monster_card_list powers;
	struct sdl3_monster_card_list weaknesses;
	struct sdl3_monster_card_list resistances;
	struct sdl3_monster_card_list traits;
};

struct sdl3_monster_card_facts {
	char defense[SDL3_MONSTER_CARD_FACT_CAPACITY];
	char profile[SDL3_MONSTER_CARD_FACT_CAPACITY];
	char attacks[SDL3_MONSTER_CARD_FACT_CAPACITY];
	char powers[SDL3_MONSTER_CARD_FACT_CAPACITY];
	char elements[SDL3_MONSTER_CARD_FACT_CAPACITY];
	char traits[SDL3_MONSTER_CARD_FACT_CAPACITY];
};

void sdl3_monster_card_format_knowledge(
		const struct sdl3_monster_card_knowledge *knowledge,
		struct sdl3_monster_card_facts *facts);

#endif /* INCLUDED_SDL3_MONSTER_CARD_DATA_H */
