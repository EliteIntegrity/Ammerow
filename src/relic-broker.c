/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
/** Finite brokerage over the existing run artefacts; never a generator. */
#include "angband.h"
#include "relic-broker-data.h"
#include "game-event.h"
#include "game-world.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-power.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-history.h"
#include "player-calcs.h"
#include "store.h"

bool relic_broker_artifact_eligible(const struct artifact *art)
{
	struct object_kind *kind;
	struct object base = { 0 };
	if (!art || !art->tval) return false;
	kind = lookup_kind(art->tval, art->sval);
	if (!kind || kf_has(kind->kind_flags, KF_QUEST_ART)) return false;
	base.tval = (uint8_t)art->tval;
	return tval_is_wearable(&base);
}

static bool make_broker_object(struct object *obj, const struct artifact *art)
{
	bool made;
	/* MAXIMISE still randomizes inherited curse timeouts. Keep that work
	 * off the gameplay stream, including previews and failed purchases. */
	Rand_begin_deterministic(seed_randart ^ (uint32_t)art->aidx);
	made = make_fake_artifact(obj, art);
	Rand_end_deterministic();
	return made;
}

int32_t relic_broker_artifact_power(const struct artifact *art)
{
	struct object *obj;
	int32_t power;
	if (!relic_broker_artifact_eligible(art)) return 0;
	obj = object_new();
	if (!make_broker_object(obj, art)) {
		object_delete(NULL, NULL, &obj);
		return 0;
	}
	obj->known = object_new();
	object_copy(obj->known, obj);
	obj->known->known = NULL;
	power = object_power(obj, false, NULL);
	object_delete(NULL, NULL, &obj->known);
	object_delete(NULL, NULL, &obj);
	return power;
}

/* Stable catalogue ordering; deliberately consumes no gameplay RNG. */
static uint32_t catalogue_rank(uint32_t seed, uint32_t slot)
{
	uint32_t x = seed ^ (slot * UINT32_C(0x9e3779b9));
	x ^= x >> 16;
	x *= UINT32_C(0x85ebca6b);
	x ^= x >> 13;
	x *= UINT32_C(0xc2b2ae35);
	return x ^ (x >> 16);
}

bool relic_broker_ensure(struct player *p)
{
	struct relic_broker_state staged = { 0 };
	int32_t *powers;
	bool *selected;
	int i, b;
	if (!p || !z_info || !a_info || !relic_broker_band_count()) return false;
	if (p->broker.initialized) return relic_broker_state_valid(&p->broker);
	if (z_info->a_max > UINT16_MAX) return false;
	powers = mem_zalloc(z_info->a_max * sizeof(*powers));
	selected = mem_zalloc(z_info->a_max * sizeof(*selected));
	for (i = 1; i < z_info->a_max; i++)
		powers[i] = relic_broker_artifact_power(&a_info[i]);
	for (b = 1; b <= relic_broker_band_count(); b++) {
		const struct relic_broker_band *band = relic_broker_band(b);
		unsigned int n;
		for (n = 0; n < band->quantity; n++) {
			int best = 0;
			uint32_t rank = UINT32_MAX;
			struct relic_broker_lot *lot = &staged.lots[staged.count++];
			for (i = 1; i < z_info->a_max; i++) {
				uint32_t candidate = catalogue_rank(seed_randart, (uint32_t)i);
				if (!selected[i] && powers[i] >= band->minimum &&
					powers[i] < band->maximum && (!best || candidate < rank)) {
					best = i;
					rank = candidate;
				}
			}
			lot->artifact = (uint16_t)best;
			lot->band = band->index;
			lot->price = band->price;
			if (best) selected[best] = true;
		}
	}
	mem_free(selected);
	mem_free(powers);
	staged.initialized = true;
	p->broker = staged;
	return true;
}

bool relic_broker_state_valid(const struct relic_broker_state *state)
{
	int i, j;
	if (!state || state->count > RELIC_BROKER_MAX_LOTS) return false;
	if (!state->initialized) return state->count == 0;
	if (!state->count) return false;
	for (i = 0; i < state->count; i++) {
		const struct relic_broker_lot *lot = &state->lots[i];
		if (!relic_broker_band(lot->band) || lot->price <= 0 ||
			lot->artifact >= z_info->a_max || (!lot->artifact && lot->sold))
			return false;
		if (!lot->artifact) continue; /* An honestly empty appraisal band. */
		if (!relic_broker_artifact_eligible(&a_info[lot->artifact])) return false;
		for (j = 0; j < i; j++)
			if (state->lots[j].artifact == lot->artifact) return false;
	}
	return true;
}

bool relic_broker_lot_available(const struct relic_broker_lot *lot)
{
	const struct artifact *art;
	if (!lot || lot->sold || !lot->artifact || lot->artifact >= z_info->a_max)
		return false;
	art = &a_info[lot->artifact];
	return relic_broker_artifact_eligible(art) && !is_artifact_created(art) &&
		!is_artifact_seen(art) && !is_artifact_everseen(art);
}

const char *relic_broker_lot_category(const struct relic_broker_lot *lot)
{
	if (!lot || !lot->artifact || lot->artifact >= z_info->a_max) return "No lot";
	return tval_find_name(a_info[lot->artifact].tval);
}

enum relic_broker_result relic_broker_buy(struct player *p, int index,
	struct object **received)
{
	struct relic_broker_lot *lot;
	struct object *obj;
	struct store *store;
	if (received) *received = NULL;
	if (!p || p != player || !cave || p->spelunking) return RELIC_BROKER_WRONG_STORE;
	store = store_at(cave, p->grid);
	if (!store || !store->relic_broker) return RELIC_BROKER_WRONG_STORE;
	if (!relic_broker_ensure(p) || index < 0 || index >= p->broker.count)
		return RELIC_BROKER_UNAVAILABLE;
	lot = &p->broker.lots[index];
	if (!relic_broker_lot_available(lot)) return RELIC_BROKER_UNAVAILABLE;
	if (p->au < lot->price) return RELIC_BROKER_NO_GOLD;
	obj = object_new();
	if (!make_broker_object(obj, &a_info[lot->artifact])) {
		object_delete(NULL, NULL, &obj);
		return RELIC_BROKER_UNAVAILABLE;
	}
	if (inven_carry_num(p, obj) < 1) {
		object_delete(NULL, NULL, &obj);
		return RELIC_BROKER_NO_ROOM;
	}
	/* All fallible checks precede gold, stock, knowledge and uniqueness writes. */
	p->au -= lot->price;
	lot->sold = true;
	mark_artifact_created(obj->artifact, true);
	obj->origin = ORIGIN_STORE;
	obj->known = object_new();
	object_set_base_known(p, obj);
	obj->known->artifact = obj->artifact;
	store_identify_purchase(p, obj);
	history_find_artifact(p, obj->artifact);
	mark_artifact_seen(obj->artifact, true);
	mark_artifact_everseen(obj->artifact, true);
	inven_carry(p, obj, false, true);
	p->upkeep->update |= PU_INVEN;
	p->upkeep->notice |= PN_COMBINE | PN_IGNORE;
	p->upkeep->redraw |= PR_GOLD | PR_INVEN;
	if (received) *received = obj;
	event_signal(EVENT_STORECHANGED);
	event_signal(EVENT_INVENTORY);
	return RELIC_BROKER_OK;
}

void do_cmd_buy_relic(struct command *cmd)
{
	int lot;
	struct object *received;
	if (cmd_get_arg_number(cmd, "lot", &lot) != CMD_OK) return;
	switch (relic_broker_buy(player, lot, &received)) {
	case RELIC_BROKER_OK: {
		char name[160];
		object_desc(name, sizeof(name), received, ODESC_PREFIX | ODESC_FULL, player);
		msg("The broker reveals %s. Its history can now be examined.", name);
		break;
	}
	case RELIC_BROKER_NO_GOLD: msg("You cannot afford that appraisal."); break;
	case RELIC_BROKER_NO_ROOM: msg("Make room in your pack before buying."); break;
	case RELIC_BROKER_WRONG_STORE: msg("You must be at the relic broker."); break;
	default: msg("That lot is unavailable. The catalogue does not restock."); break;
	}
}
