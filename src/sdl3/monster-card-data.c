/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-card-data.c
 * \brief Lore-gated text fields for the SDL3 monster examine card.
 *
 *
 */

#include "angband.h"

#include "sdl3/monster-card-data.h"

static const char *plural_suffix(int count)
{
	return count == 1 ? "" : "s";
}

static void append_text(char *output, size_t capacity, const char *text)
{
	if (!output || !capacity || !text) return;
	my_strcat(output, text, capacity);
}

static void format_list(const char *label,
		const struct sdl3_monster_card_list *list, char *output,
		size_t capacity)
{
	int i;

	if (!output || !capacity) return;
	output[0] = '\0';
	if (!label || !list || list->count <= 0) return;
	strnfmt(output, capacity, "%s: ", label);
	for (i = 0; i < list->count; i++) {
		if (!list->items[i] || !list->items[i][0]) continue;
		if (i) append_text(output, capacity, "; ");
		append_text(output, capacity, list->items[i]);
	}
	if (list->total > list->count) {
		char more[32];

		strnfmt(more, sizeof(more), "; +%d more",
			list->total - list->count);
		append_text(output, capacity, more);
	}
}

static void append_drop_profile(
		const struct sdl3_monster_card_knowledge *knowledge, char *profile,
		size_t capacity)
{
	char drop[128];
	const char *contents;
	const char *quality = "";

	if (!knowledge->drop_known) {
		append_text(profile, capacity, " | Drops: unknown");
		return;
	}
	if (knowledge->drop_kind == SDL3_MONSTER_DROP_OBJECTS) {
		contents = "object";
	} else if (knowledge->drop_kind == SDL3_MONSTER_DROP_TREASURE) {
		contents = "treasure";
	} else {
		contents = "object/treasure";
	}
	if (knowledge->drop_quality == SDL3_MONSTER_DROP_EXCEPTIONAL) {
		quality = "exceptional ";
	} else if (knowledge->drop_quality == SDL3_MONSTER_DROP_GOOD) {
		quality = "good ";
	}
	if (knowledge->maximum_drops <= 0 && knowledge->specific_drops <= 0) {
		my_strcpy(drop, " | Drops: none", sizeof(drop));
	} else if (knowledge->maximum_drops > 0 &&
			knowledge->specific_drops > 0) {
		strnfmt(drop, sizeof(drop),
			" | Drops: up to %d %s%s%s + %d specific",
			knowledge->maximum_drops, quality, contents,
			plural_suffix(knowledge->maximum_drops),
			knowledge->specific_drops);
	} else if (knowledge->maximum_drops > 0) {
		strnfmt(drop, sizeof(drop), " | Drops: up to %d %s%s%s",
			knowledge->maximum_drops, quality, contents,
			plural_suffix(knowledge->maximum_drops));
	} else {
		strnfmt(drop, sizeof(drop), " | Drops: %d specific",
			knowledge->specific_drops);
	}
	append_text(profile, capacity, drop);
}

void sdl3_monster_card_format_knowledge(
		const struct sdl3_monster_card_knowledge *knowledge,
		struct sdl3_monster_card_facts *facts)
{
	char resistance[SDL3_MONSTER_CARD_FACT_CAPACITY];
	char weakness[SDL3_MONSTER_CARD_FACT_CAPACITY];

	if (!facts) return;
	memset(facts, 0, sizeof(*facts));
	if (!knowledge) return;
	if (knowledge->armour_known) {
		strnfmt(facts->defense, sizeof(facts->defense),
			"Defense: AC %d | Life %d",
			knowledge->armour, knowledge->average_life);
	} else {
		my_strcpy(facts->defense, "Defense: not yet learned",
			sizeof(facts->defense));
	}
	/* Match recall's qualitative speed bands: do not expose the internal
	 * energy rating merely because a compact card is being drawn. */
	if (knowledge->relative_speed > 20) {
		my_strcpy(facts->profile, "Speed: incredibly fast",
			sizeof(facts->profile));
	} else if (knowledge->relative_speed > 10) {
		my_strcpy(facts->profile, "Speed: very fast", sizeof(facts->profile));
	} else if (knowledge->relative_speed > 5) {
		my_strcpy(facts->profile, "Speed: fast", sizeof(facts->profile));
	} else if (knowledge->relative_speed > 0) {
		my_strcpy(facts->profile, "Speed: quick", sizeof(facts->profile));
	} else if (knowledge->relative_speed < -20) {
		my_strcpy(facts->profile, "Speed: incredibly slow",
			sizeof(facts->profile));
	} else if (knowledge->relative_speed < -10) {
		my_strcpy(facts->profile, "Speed: very slow", sizeof(facts->profile));
	} else if (knowledge->relative_speed < 0) {
		my_strcpy(facts->profile, "Speed: slow", sizeof(facts->profile));
	} else {
		my_strcpy(facts->profile, "Speed: normal", sizeof(facts->profile));
	}
	if (knowledge->never_moves) {
		append_text(facts->profile, sizeof(facts->profile), " | stationary");
	}
	append_drop_profile(knowledge, facts->profile, sizeof(facts->profile));

	if (knowledge->no_physical_attacks) {
		my_strcpy(facts->attacks, "Attacks: none", sizeof(facts->attacks));
	} else if (knowledge->attacks.count > 0) {
		format_list("Attacks", &knowledge->attacks, facts->attacks,
			sizeof(facts->attacks));
	} else {
		my_strcpy(facts->attacks, "Attacks: not yet learned",
			sizeof(facts->attacks));
	}
	if (knowledge->powers.count > 0) {
		format_list("Powers", &knowledge->powers, facts->powers,
			sizeof(facts->powers));
	} else if (knowledge->all_known) {
		my_strcpy(facts->powers, "Powers: none", sizeof(facts->powers));
	}
	format_list("Weak", &knowledge->weaknesses, weakness,
		sizeof(weakness));
	format_list("Resists", &knowledge->resistances, resistance,
		sizeof(resistance));
	if (weakness[0]) append_text(facts->elements, sizeof(facts->elements),
		weakness);
	if (weakness[0] && resistance[0]) {
		append_text(facts->elements, sizeof(facts->elements), " | ");
	}
	if (resistance[0]) append_text(facts->elements, sizeof(facts->elements),
		resistance);
	format_list("Traits", &knowledge->traits, facts->traits,
		sizeof(facts->traits));
}
