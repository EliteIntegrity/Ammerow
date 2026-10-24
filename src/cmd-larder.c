/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-larder.c
 * \brief Authoritative command boundary for village-larder donations.
 *
 */

#include "angband.h"
#include "cave.h"
#include "cmd-larder.h"

#include "game-world.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "object.h"
#include "player.h"
#include "player-calcs.h"
#include "store.h"
#include "world-larder.h"
#include "world-larder-data.h"

static bool catch_kind(const struct object *obj,
		world_fishing_kind *kind)
{
	int fish_kind;

	if (!obj || !kind || obj->tval != TV_FOOD) return false;
	for (fish_kind = 0; fish_kind < world_fishing_species_count(); fish_kind++) {
		int sval = lookup_sval(TV_FOOD,
			world_fishing_kind_name((world_fishing_kind)fish_kind));

		if (sval >= 0 && obj->sval == sval) {
			*kind = (world_fishing_kind)fish_kind;
			return true;
		}
	}
	return false;
}

bool world_larder_object_is_catch(const struct object *obj)
{
	world_fishing_kind kind;

	return catch_kind(obj, &kind);
}

uint32_t world_larder_object_value(const struct object *obj)
{
	world_fishing_kind kind;

	return catch_kind(obj, &kind) ? world_larder_fish_value(kind) : 0;
}

void do_cmd_larder_donate(struct command *cmd)
{
	struct store *store;
	struct object *obj;
	struct object *used;
	struct world_larder_state updated;
	struct world_larder_report report;
	struct world_request_state requests;
	world_fishing_kind kind;
	char name[120];
	bool none_left = false;
	int quantity;

	cmd_disable_repeat();
	if (!player || !player->upkeep || !cave || !cmd ||
			cmd_get_arg_item(cmd, "item", &obj) != CMD_OK ||
			cmd_get_arg_number(cmd, "quantity", &quantity) != CMD_OK) {
		return;
	}
	store = store_at(cave, player->grid);
	if (!store_accepts_larder_donations(store)) {
		msg("The village larder only accepts donations at the General Store.");
		return;
	}
	if (!object_is_carried(player, obj) || !catch_kind(obj, &kind) ||
			quantity <= 0 || quantity > obj->number) {
		msg("Only a carried fishing catch can be donated.");
		return;
	}
	updated = player->larder;
	if (!world_larder_donate(&updated, kind, quantity, &report)) return;
	requests = player->village_requests;
	report.request_completed = world_objective_donation_request(player, obj);
	if (report.request_completed) {
		report.request_gold = report.request_completed->donation_gold;
		if (player->au > INT32_MAX - report.request_gold ||
				!world_request_record(&requests, report.request_completed->id)) {
			msg("The request reward cannot be recorded. You keep your fish.");
			return;
		}
	}
	object_desc(name, sizeof(name), obj,
		ODESC_PREFIX | ODESC_BASE | ODESC_ALTNUM | (quantity << 16), player);
	used = gear_object_for_use(player, obj, quantity, false, &none_left);
	if (!used) return;
	player->larder = updated;
	player->village_requests = requests;
	player->au += report.request_gold;
	if (report.request_gold) player->upkeep->redraw |= PR_GOLD;
	if (used->known) object_delete(player->cave, NULL, &used->known);
	object_delete(cave, player->cave, &used);
	if (report.experience) player_exp_gain(player, report.experience);
	msgt(MSG_LARDER_DONATE,
		"You donate %s, adding %u meal portion%s to the village larder.",
		name, report.contributed_points,
		report.contributed_points == 1 ? "" : "s");
	if (report.experience) {
		msg("You gain %d XP for the donation.", report.experience);
	}
	if (report.request_completed) {
		msgt(report.request_completed->completion_message,
			"Village request complete: %s. You receive %d gold (once only).",
			report.request_completed->title, report.request_gold);
	}
	if (report.milestone_reached) {
		store_sync_larder_unlocks();
		msgt(MSG_LARDER_MILESTONE,
			"%s", report.milestone_reached->message);
		msg("%s", report.milestone_reached->reward);
	} else {
		const struct world_larder_milestone_definition *next =
			world_larder_next_milestone(&player->larder);
		const struct world_larder_milestone_definition *current =
			world_larder_current_milestone(&player->larder);

		if (next) {
			msg("Village larder: %u of %u meal portions toward the %s.",
				player->larder.food_points, next->target, next->name);
		} else if (current) {
			msg("Village larder: %s secured; %u meal portions donated.",
				current->name, player->larder.food_points);
		}
	}
	event_signal_larder_donated(name, &report);
}
