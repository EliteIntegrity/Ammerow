/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "angband.h"
#include "player-advancement.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "store.h"

#if PY_MAX_LEVEL != ADVANCEMENT_MAX_LEVEL
#error Update the advancement offers and versioned save block for the new level cap.
#endif

/* Character-level milestones, not dungeon allocation depths. No permanent
 * boosts, experience, artifacts, Life or *Healing*. Contents never reroll. */
static const struct advancement_offer offers[] = {
	{ "Cure Serious Wounds", 1, "Seam Step", 3, 60 },
	{ "Cure Serious Wounds", 2, "Teleportation", 3, 120 },
	{ "Cure Critical Wounds", 1, "Homeward Tension", 2, 200 },
	{ "Cure Critical Wounds", 2, "Magic Mapping", 4, 300 },
	{ "Cure Critical Wounds", 3, "Teleport Level", 1, 450 },
	{ "Healing", 1, "Homeward Tension", 4, 600 },
	{ "Healing", 1, "Teleport Level", 1, 800 },
	{ "Healing", 2, "Teleport Level", 2, 1000 },
	{ "Healing", 2, "Teleport Level", 2, 1250 },
	{ "Healing", 3, "Teleport Level", 3, 1500 }
};

void player_advancement_reset(struct player *p, int highest)
{
	memset(&p->advancement, 0, sizeof(p->advancement));
	p->advancement.initialized = true;
	p->advancement.highest = (uint8_t)MAX(1, MIN(PY_MAX_LEVEL, highest));
}

void player_advancement_ensure(struct player *p)
{
	if (p && !p->advancement.initialized)
		player_advancement_reset(p, MAX(p->lev, p->max_lev));
}

bool player_advancement_valid(const struct player_advancement *s)
{
	uint16_t earned;
	if (!s || !s->initialized || s->highest < 1 || s->highest > PY_MAX_LEVEL)
		return false;
	earned = (uint16_t)((1U << (s->highest / 5)) - 1);
	if (s->rewards & ~earned) return false;
	for (int i = 0; i <= PY_MAX_LEVEL; i++) {
		if (s->cards[i] > 1 || (s->cards[i] && (i < 2 || i > s->highest)))
			return false;
	}
	return true;
}

void player_advancement_gain(struct player *p, int level)
{
	struct player_advancement *s = &p->advancement;
	player_advancement_ensure(p);
	if (level <= s->highest || level > PY_MAX_LEVEL) return;
	for (int i = s->highest + 1; i <= level; i++) {
		s->cards[i] = 1;
		if (i % 5 == 0) s->rewards |= (uint16_t)(1U << (i / 5 - 1));
	}
	s->highest = (uint8_t)level;
}

int player_advancement_next_card(const struct player *p)
{
	for (int i = 2; i <= PY_MAX_LEVEL; i++)
		if (p->advancement.cards[i]) return i;
	return 0;
}

int player_advancement_next_reward(const struct player *p)
{
	for (int i = 0; i < 10; i++)
		if (p->advancement.rewards & (1U << i)) return (i + 1) * 5;
	return 0;
}

void player_advancement_dismiss(struct player *p, int level)
{
	if (level >= 2 && level <= PY_MAX_LEVEL) p->advancement.cards[level] = 0;
}

const struct advancement_offer *player_advancement_offer(int level)
{
	return level >= 5 && level <= PY_MAX_LEVEL && level % 5 == 0 ?
		&offers[level / 5 - 1] : NULL;
}

enum advancement_result player_advancement_claim(struct player *p, int level,
	enum advancement_choice choice)
{
	const struct advancement_offer *offer = player_advancement_offer(level);
	struct object *obj = NULL;
	uint16_t bit;
	if (!p || p != player || p->is_dead || !offer ||
			!player_advancement_valid(&p->advancement) ||
			choice < ADVANCEMENT_POTION || choice > ADVANCEMENT_GOLD)
		return ADVANCEMENT_UNAVAILABLE;
	bit = (uint16_t)(1U << (level / 5 - 1));
	if (!(p->advancement.rewards & bit)) return ADVANCEMENT_UNAVAILABLE;
	if (choice == ADVANCEMENT_GOLD) {
		if (p->au > INT32_MAX - offer->gold) return ADVANCEMENT_NO_ROOM;
	} else {
		int tval = choice == ADVANCEMENT_POTION ? TV_POTION : TV_SCROLL;
		const char *name = choice == ADVANCEMENT_POTION ? offer->potion : offer->scroll;
		struct object_kind *kind = lookup_kind(tval, lookup_sval(tval, name));
		if (!kind) return ADVANCEMENT_UNAVAILABLE;
		obj = object_new();
		object_prep(obj, kind, 0, MINIMISE);
		obj->number = (uint8_t)(choice == ADVANCEMENT_POTION ? offer->potions : offer->scrolls);
		obj->origin = ORIGIN_ADVANCEMENT;
		obj->known = object_new();
		object_set_base_known(p, obj);
		/* All-or-nothing: do not drop overflow, partly claim or identify on failure. */
		if (inven_carry_num(p, obj) < obj->number) {
			object_delete(NULL, NULL, &obj->known);
			object_delete(NULL, NULL, &obj);
			return ADVANCEMENT_NO_ROOM;
		}
	}
	/* No fallible work after this point. Mark claimed before any notifications. */
	p->advancement.rewards = (uint16_t)(p->advancement.rewards & ~bit);
	player_advancement_dismiss(p, level);
	if (obj) {
		store_identify_purchase(p, obj);
		/* Capacity may be spread across several partial stacks, including
		 * differently inscribed ones. Carry single units so no intermediate
		 * overflow stack or deferred combine is needed to fulfil the bundle. */
		while (obj->number > 1) {
			struct object *one = object_split(obj, 1);
			inven_carry(p, one, true, false);
		}
		inven_carry(p, obj, true, true);
	} else {
		p->au += offer->gold;
		p->upkeep->redraw |= PR_GOLD;
		msg("Level %d reward: %d gold.", level, offer->gold);
	}
	return ADVANCEMENT_OK;
}
