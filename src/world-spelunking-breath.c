/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-breath.c
 * \brief Bind side-view breath rules to player HP and charged equipment.
 *
 *
 */

#include "world-spelunking-breath.h"

#include "game-event.h"
#include "obj-properties.h"
#include "object.h"
#include "player.h"
#include "player-calcs.h"
#include "player-resource.h"
#include "player-util.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-runtime.h"
#include "z-rand.h"
#include "z-util.h"

#include <limits.h>
#include <string.h>

static int air_supply_capacity(const struct object *obj)
{
	int capacity;

	if (!obj || !obj->kind ||
			!kf_has(obj->kind->kind_flags, KF_AIR_SUPPLY)) {
		return 0;
	}
	capacity = randcalc(obj->kind->pval, 0, MAXIMISE);
	return capacity > 0 && capacity <= INT16_MAX ? capacity : 0;
}

static int bounded_add(int total, int value)
{
	return value > INT_MAX - total ? INT_MAX : total + value;
}

static void carried_air_status(const struct player *p, int *remaining,
		int *capacity)
{
	const struct object *obj;
	int current = 0;
	int maximum = 0;

	if (p) {
		for (obj = p->gear; obj; obj = obj->next) {
			int item_capacity = air_supply_capacity(obj);
			int item_current;

			if (!item_capacity) continue;
			item_current = obj->pval;
			if (item_current < 0) item_current = 0;
			if (item_current > item_capacity) item_current = item_capacity;
			current = bounded_add(current, item_current);
			maximum = bounded_add(maximum, item_capacity);
		}
	}
	if (remaining) *remaining = current;
	if (capacity) *capacity = maximum;
}

static void set_supply_charge(struct object *obj, int charge)
{
	obj->pval = (int16_t)charge;
	if (obj->known) obj->known->pval = obj->pval;
}

static bool consume_one_air(struct player *p)
{
	struct object *obj;

	for (obj = p->gear; obj; obj = obj->next) {
		int capacity = air_supply_capacity(obj);

		if (capacity && obj->pval > 0) {
			set_supply_charge(obj, MIN(obj->pval, capacity) - 1);
			return true;
		}
	}
	return false;
}

static bool recharge_air_supplies(struct player *p)
{
	struct object *obj;
	bool changed = false;

	for (obj = p->gear; obj; obj = obj->next) {
		int capacity = air_supply_capacity(obj);

		if (capacity && obj->pval != capacity) {
			set_supply_charge(obj, capacity);
			changed = true;
		}
	}
	return changed;
}

bool world_spelunk_player_air_status(const struct player *p, int *remaining,
		int *capacity)
{
	int supply_remaining;
	int supply_capacity;

	if (remaining) *remaining = 0;
	if (capacity) *capacity = 0;
	if (!p) return false;
	carried_air_status(p, &supply_remaining, &supply_capacity);
	if (remaining) {
		*remaining = bounded_add(player_air_current(p),
			supply_remaining);
	}
	if (capacity) {
		*capacity = bounded_add(player_air_maximum(p),
			supply_capacity);
	}
	return true;
}

enum world_spelunk_environment_result
world_spelunk_player_process_environment_turn(struct player *p,
		struct world_spelunk_environment_report *report)
{
	struct world_spelunk_environment_report local = { 0 };
	struct world_spelunk_breath_report breath_report;
	struct world_spelunk_state before;
	int air_before;
	int supply_before;
	int supply_capacity;
	int supply_after;
	bool inventory_changed = false;

	if (!report) report = &local;
	memset(report, 0, sizeof(*report));
	if (!p || !p->upkeep || p->is_dead || p->upkeep->energy_use <= 0 ||
			!world_spelunk_player_is_active(p)) {
		return WORLD_SPELUNK_ENVIRONMENT_INVALID;
	}
	player_resources_ensure(p);
	if (!world_spelunk_player_sync_resources(p)) {
		return WORLD_SPELUNK_ENVIRONMENT_INVALID;
	}
	before = p->spelunking->state;
	air_before = player_air_current(p);
	carried_air_status(p, &supply_before, &supply_capacity);
	if (!world_spelunk_apply_breath_turn(&p->spelunking->state,
			supply_before > 0, &breath_report)) {
		p->spelunking->state = before;
		player_air_set(p, air_before);
		return WORLD_SPELUNK_ENVIRONMENT_INVALID;
	}
	if (breath_report.air_supply_used) {
		if (!consume_one_air(p)) {
			p->spelunking->state = before;
			player_air_set(p, air_before);
			return WORLD_SPELUNK_ENVIRONMENT_INVALID;
		}
		inventory_changed = true;
	} else if (breath_report.event == WORLD_SPELUNK_BREATH_NONE ||
			breath_report.event == WORLD_SPELUNK_BREATH_RESTORED) {
		inventory_changed = recharge_air_supplies(p);
		if (inventory_changed &&
				breath_report.event == WORLD_SPELUNK_BREATH_NONE) {
			breath_report.event = WORLD_SPELUNK_BREATH_RESTORED;
		}
	}
	carried_air_status(p, &supply_after, NULL);
	player_air_set(p, p->spelunking->state.breath);
	report->event = breath_report.event;
	report->breath_before = air_before;
	report->breath_after = player_air_current(p);
	report->air_supply_before = supply_before;
	report->air_supply_after = supply_after;
	report->air_supply_capacity = supply_capacity;
	report->damage = breath_report.damage;
	p->upkeep->redraw |= PR_STATUS;
	if (inventory_changed) event_signal(EVENT_INVENTORY);
	if (breath_report.damage > 0) {
		int committed_damage = breath_report.damage;

		if (p->chp >= 0 && committed_damage > p->chp) {
			committed_damage = p->chp + 1;
		}
		take_hit(p, committed_damage, "drowning");
		return p->is_dead ? WORLD_SPELUNK_ENVIRONMENT_DEAD :
			WORLD_SPELUNK_ENVIRONMENT_DAMAGED;
	}
	return WORLD_SPELUNK_ENVIRONMENT_SAFE;
}
