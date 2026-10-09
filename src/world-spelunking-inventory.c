/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-inventory.c
 * \brief Shared-inventory actions for the spelunking location mode.
 *
 *
 */

#include "world-spelunking-inventory.h"

#include "world-spelunking-adapter.h"
#include "game-event.h"
#include "init.h"
#include "message.h"
#include "obj-gear.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player.h"
#include "player-calcs.h"
#include "world-objective-data.h"
#include "world-spelunking-runtime.h"

#include <limits.h>
#include <string.h>

static void clear_local_report(struct world_spelunk_local_report *report,
		enum world_spelunk_local_action action)
{
	if (!report) return;
	memset(report, 0, sizeof(*report));
	report->action = action;
}

static enum world_spelunk_local_result finish_local(
		struct player *p, enum world_spelunk_local_action action,
		struct world_spelunk_local_report *report, int affected)
{
	unsigned int energy = p->spelunking->state.rules.action_energy;

	if (energy > INT_MAX) return WORLD_SPELUNK_LOCAL_INVALID;
	p->upkeep->energy_use = (int)energy;
	if (report) {
		report->action = action;
		report->result = WORLD_SPELUNK_LOCAL_TURN;
		report->energy_use = energy;
		report->affected = affected;
	}
	return WORLD_SPELUNK_LOCAL_TURN;
}

static enum world_spelunk_local_result local_reject(
		struct world_spelunk_local_report *report,
		enum world_spelunk_local_result result)
{
	if (report) report->result = result;
	return result;
}

static struct object *carried_tool(struct player *p,
		enum world_spelunk_local_action action)
{
	int flag = action == WORLD_SPELUNK_LOCAL_PITON ?
		KF_SPELUNK_PITON : KF_SPELUNK_ROPE;
	struct object *obj;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->kind && kf_has(obj->kind->kind_flags, flag) &&
				!object_is_equipped(p->body, obj)) {
			return obj;
		}
	}
	return NULL;
}

static void delete_detached_object(struct object **obj_address)
{
	struct object *obj = obj_address ? *obj_address : NULL;
	struct object *known;

	if (!obj) return;
	known = obj->known;
	obj->known = NULL;
	if (known) object_delete(NULL, NULL, &known);
	object_delete(NULL, NULL, obj_address);
}

static void consume_tool(struct player *p, struct object *obj, int amount)
{
	bool none_left = false;
	struct object *used = gear_object_for_use(p, obj, amount, false,
		&none_left);

	(void)none_left;
	delete_detached_object(&used);
	event_signal(EVENT_INVENTORY);
	event_signal(EVENT_EQUIPMENT);
}

static enum world_spelunk_local_result infrastructure_result(
		enum world_spelunk_infrastructure_result result,
		struct world_spelunk_local_report *report)
{
	switch (result) {
	case WORLD_SPELUNK_INFRASTRUCTURE_UNSTABLE:
		return local_reject(report, WORLD_SPELUNK_LOCAL_UNSTABLE);
	case WORLD_SPELUNK_INFRASTRUCTURE_ALREADY_PRESENT:
		return local_reject(report, WORLD_SPELUNK_LOCAL_ALREADY_PRESENT);
	case WORLD_SPELUNK_INFRASTRUCTURE_NO_BRACE:
		return local_reject(report, WORLD_SPELUNK_LOCAL_NO_BRACE);
	case WORLD_SPELUNK_INFRASTRUCTURE_NO_ANCHOR:
		return local_reject(report, WORLD_SPELUNK_LOCAL_NO_ANCHOR);
	case WORLD_SPELUNK_INFRASTRUCTURE_NO_SHAFT:
		return local_reject(report, WORLD_SPELUNK_LOCAL_NO_SHAFT);
	case WORLD_SPELUNK_INFRASTRUCTURE_INVALID:
	case WORLD_SPELUNK_INFRASTRUCTURE_OK:
	default:
		return local_reject(report, WORLD_SPELUNK_LOCAL_INVALID);
	}
}

static enum world_spelunk_local_result place_tool(
		struct player *p, enum world_spelunk_local_action action,
		struct world_spelunk_local_report *report)
{
	struct object *tool = carried_tool(p, action);
	enum world_spelunk_infrastructure_result result;
	int affected = 1;

	if (!tool) return local_reject(report, WORLD_SPELUNK_LOCAL_NO_TOOL);
	if (action == WORLD_SPELUNK_LOCAL_PITON) {
		result = world_spelunk_runtime_place_piton(p->spelunking);
	} else {
		result = world_spelunk_runtime_deploy_rope(p->spelunking,
			tool->number, &affected);
	}
	if (result != WORLD_SPELUNK_INFRASTRUCTURE_OK) {
		return infrastructure_result(result, report);
	}
	consume_tool(p, tool, affected);
	return finish_local(p, action, report, affected);
}

static int pickup_gold(struct player *p, struct world_spelunk_runtime *runtime,
		struct object *obj)
{
	int value = obj->pval;

	if (!world_spelunk_runtime_take_ground_object(runtime, obj)) return 0;
	p->au += value;
	p->upkeep->redraw |= PR_GOLD;
	delete_detached_object(&obj);
	return 1;
}

static enum world_spelunk_local_result pickup_local_objects(
		struct player *p, struct world_spelunk_local_report *report)
{
	struct world_spelunk_runtime *runtime = p->spelunking;
	struct object *obj = runtime->ground_objects;
	bool found = false;
	int picked = 0;

	while (obj) {
		struct object *next = obj->next;

		if (obj->grid.x != runtime->state.x ||
				obj->grid.y != runtime->state.y) {
			obj = next;
			continue;
		}
		found = true;
		if (tval_is_money(obj)) {
			picked += pickup_gold(p, runtime, obj);
		} else {
			int amount = inven_carry_num(p, obj);

			if (amount > 0) {
				struct object *carried;
				int completion_message =
					world_objective_item_completion_message(p, obj);

				if (amount >= obj->number) {
					if (!world_spelunk_runtime_take_ground_object(runtime,
							obj)) {
						return local_reject(report,
							WORLD_SPELUNK_LOCAL_INVALID);
					}
					carried = obj;
				} else {
					carried = object_split(obj, amount);
					carried->grid = loc(0, 0);
					carried->known->grid = loc(0, 0);
				}
				inven_carry(p, carried, true, true);
				if (report && !report->completion_message &&
						completion_message > MSG_GENERIC) {
					report->completion_message = completion_message;
				}
				picked++;
			}
		}
		obj = next;
	}
	if (!picked) {
		return local_reject(report, found ? WORLD_SPELUNK_LOCAL_NO_ROOM :
			WORLD_SPELUNK_LOCAL_NOTHING_HERE);
	}
	event_signal(EVENT_INVENTORY);
	return finish_local(p, WORLD_SPELUNK_LOCAL_PICKUP, report, picked);
}

enum world_spelunk_local_result world_spelunk_player_apply_local(
		struct player *p, enum world_spelunk_local_action action,
		struct world_spelunk_local_report *report)
{
	clear_local_report(report, action);
	if (!p || !p->upkeep || !p->spelunking || p->is_dead ||
			p->upkeep->energy_use != 0 ||
			!world_spelunk_player_is_active(p) ||
			p->spelunking->state.rules.action_energy > INT_MAX) {
		return local_reject(report, WORLD_SPELUNK_LOCAL_INVALID);
	}
	switch (action) {
	case WORLD_SPELUNK_LOCAL_PITON:
	case WORLD_SPELUNK_LOCAL_ROPE:
		return place_tool(p, action, report);
	case WORLD_SPELUNK_LOCAL_PICKUP:
		return pickup_local_objects(p, report);
	case WORLD_SPELUNK_LOCAL_DROP:
	default:
		return local_reject(report, WORLD_SPELUNK_LOCAL_INVALID);
	}
}

enum world_spelunk_local_result world_spelunk_player_drop(
		struct player *p, struct object *obj, int amount,
		struct world_spelunk_local_report *report)
{
	struct object *dropped;
	bool none_left = false;

	clear_local_report(report, WORLD_SPELUNK_LOCAL_DROP);
	if (!p || !p->upkeep || !p->spelunking || !obj || amount <= 0 ||
			p->is_dead || p->upkeep->energy_use != 0 ||
			!world_spelunk_player_is_active(p) ||
			!object_is_carried(p, obj) ||
			(object_is_equipped(p->body, obj) && !obj_can_takeoff(obj)) ||
			!world_spelunk_runtime_can_add_ground_object_at(p->spelunking,
				p->spelunking->state.x, p->spelunking->state.y) ||
			p->spelunking->state.rules.action_energy > INT_MAX) {
		return local_reject(report, WORLD_SPELUNK_LOCAL_INVALID);
	}
	if (amount > obj->number) amount = obj->number;
	if (object_is_equipped(p->body, obj)) inven_takeoff(obj);
	dropped = gear_object_for_use(p, obj, amount, false, &none_left);
	(void)none_left;
	if (!world_spelunk_runtime_add_ground_object(p->spelunking, dropped,
			p->spelunking->state.x, p->spelunking->state.y)) {
		inven_carry(p, dropped, true, false);
		return local_reject(report, WORLD_SPELUNK_LOCAL_INVALID);
	}
	event_signal(EVENT_INVENTORY);
	event_signal(EVENT_EQUIPMENT);
	return finish_local(p, WORLD_SPELUNK_LOCAL_DROP, report, amount);
}
