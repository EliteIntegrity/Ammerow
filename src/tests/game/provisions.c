/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* game/provisions.c */

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include <stdio.h>
#include "cave.h"
#include "cmd-core.h"
#include "cmd-fishing.h"
#include "cmd-larder.h"
#include "cmd-spelunking.h"
#include "datafile.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-timed.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-ignore.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "savefile.h"
#include "store.h"
#include "trap.h"
#include "player.h"
#include "player-birth.h"
#include "player-timed.h"
#include "player-util.h"
#include "ui-travel.h"
#include "world-entry.h"
#include "world-fishing-site.h"
#include "world-larder-data.h"
#include "world-map.h"
#include "world-overworld.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-layout.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-visibility.h"
#include "world-travel-action.h"
#include "world-travel-resources.h"
#include "world-turn.h"
#include "z-util.h"
#include "game-fixture.h"

static struct object *make_test_object(struct object_kind *kind, int number)
{
	struct object *obj;

	if (!kind || number <= 0 || number > UCHAR_MAX) return NULL;
	obj = object_new();
	object_prep(obj, kind, 1, AVERAGE);
	obj->number = (uint8_t)number;
	obj->known = object_new();
	object_copy(obj->known, obj);
	obj->known->known = NULL;
	return obj;
}

static int carried_kind_count(const struct player *p,
		const struct object_kind *kind)
{
	const struct object *obj;
	int count = 0;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->kind == kind && !object_is_equipped(p->body, obj)) {
			count += obj->number;
		}
	}
	return count;
}

static struct object *carried_kind_object(struct player *p,
		const struct object_kind *kind)
{
	struct object *obj;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->kind == kind && !object_is_equipped(p->body, obj)) return obj;
	}
	return NULL;
}

static bool store_has_kind(const struct store *store,
		const struct object_kind *kind)
{
	const struct object *obj;

	for (obj = store ? store->stock : NULL; obj; obj = obj->next) {
		if (obj->kind == kind) return true;
	}
	return false;
}

static bool enter_test_shop(int feat)
{
	struct loc grid;

	for (grid.y = 0; grid.y < cave->height; grid.y++) {
		for (grid.x = 0; grid.x < cave->width; grid.x++) {
			if (square(cave, grid)->feat == feat) {
				square_set_mon(cave, player->grid, 0);
				player_place(cave, player, grid);
				return true;
			}
		}
	}
	return false;
}

static int test_selling_default_and_saved_choice(void *state)
{
	struct player_options defaults = { 0 };
	int choice;
	(void)state;

	/* Test the shipped default, independent of a developer's preferences. */
	options_restore_maintainer(&defaults, OP_BIRTH);
	require(!defaults.opt[OPT_birth_no_selling]);
	for (choice = 0; choice < 2; choice++) {
		reset_before_load();
		require(savefile_load("Test1", false));
		OPT(player, birth_no_selling) = (choice != 0);
		player->au = 12345;
		require(savefile_save("TestSellingChoice"));
		reset_before_load();
		/* Start with the opposite value: the saved setting must win. */
		OPT(player, birth_no_selling) = (choice == 0);
		require(savefile_load("TestSellingChoice", false));
		eq(OPT(player, birth_no_selling), choice != 0);
		eq(player->au, 12345);
	}
	require(file_delete("TestSellingChoice"));
	ok;
}

static int test_catch_sale_transactions(void *state)
{
	const char *names[] = { "Rain Minnow", "Mudbelly", "Glassfin",
		"Fernscale", "Cistern Bream", "Blind Deepfish" };
	const int proceeds[] = { 1, 5, 20, 16, 60, 160 };
	const int buyers[] = { FEAT_STORE_GENERAL, FEAT_STORE_FISHING };
	size_t buyer, fish;
	(void)state;

	for (buyer = 0; buyer < N_ELEMENTS(buyers); buyer++) {
		for (fish = 0; fish < N_ELEMENTS(names); fish++) {
			struct object_kind *kind;
			struct object *obj;
			struct store *store;
			int before;
			uint32_t larder_before;

			reset_before_load();
			require(savefile_load("Test1", false));
			OPT(player, birth_no_selling) = false;
			require(enter_test_shop(buyers[buyer]));
			store = store_at(cave, player->grid);
			notnull(store);
			kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, names[fish]));
			notnull(kind);
			obj = make_test_object(kind, 3);
			notnull(obj);
			inven_carry(player, obj, true, false);
			obj = carried_kind_object(player, kind);
			notnull(obj);
			require(store_will_buy_tester(obj));
			/* Calibration of actual authored catches, including integer rounding.
			 * Buying and reselling a catch must lose money, not mint it. */
			eq(price_item(store, obj, true, 1), proceeds[fish]);
			require(price_item(store, obj, false, 1) > proceeds[fish]);
			before = player->au;
			larder_before = player->larder.food_points;
			eq(cmdq_push(CMD_SELL), 0);
			cmd_set_arg_item(cmdq_peek(), "item", obj);
			cmd_set_arg_number(cmdq_peek(), "quantity", 2);
			require(cmdq_pop(CTX_STORE));
			eq(player->au, before + 2 * proceeds[fish]);
			eq(carried_kind_count(player, kind), 1);
			eq(player->larder.food_points, larder_before);
			eq(player->village_requests.count, 0); /* Selling is not donating. */
			require(store_has_kind(store, kind));
		}
	}
	ok;
}

static int test_partial_potion_sale_identifies_remaining_stack(void *state)
{
	const char *names[] = { "Cure Light Wounds", "Poison" };
	size_t i;
	(void)state;
	for (i = 0; i < N_ELEMENTS(names); i++) {
		struct object_kind *kind;
		struct object *obj;
		reset_before_load();
		require(savefile_load("Test1", false));
		OPT(player, birth_no_selling) = false;
		kind = lookup_kind(TV_POTION, lookup_sval(TV_POTION, names[i]));
		notnull(kind);
		obj = make_test_object(kind, 6);
		notnull(obj);
		inven_carry(player, obj, true, false);
		kind->aware = false;
		obj = carried_kind_object(player, kind);
		notnull(obj);
		eq(obj->number, 6);
		require(enter_test_shop(FEAT_STORE_ALCHEMY));
		require(store_will_buy_tester(obj));
		eq(cmdq_push(CMD_SELL), 0);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", 1);
		require(cmdq_pop(CTX_STORE));
		require(kind->aware);
		eq(carried_kind_count(player, kind), 5);
		obj = carried_kind_object(player, kind);
		notnull(obj);
		eq(store_will_buy_tester(obj), i == 0);
		/* Leaving the shop is not required to restore the remaining stack. */
		require(object_is_carried(player, obj));
	}
	ok;
}

static int test_no_selling_gold_compensation(void *state)
{
	int depth, seed;
	int saved_depth;
	bool saved_choice;
	(void)state;

	reset_before_load();
	require(savefile_load("Test1", false));
	saved_depth = player->depth;
	saved_choice = OPT(player, birth_no_selling);
	for (depth = 0; depth <= 1; depth++) {
		player->depth = depth;
		for (seed = 1; seed <= 16; seed++) {
			struct object *ordinary, *compensated;
			int ordinary_value, compensated_value;

			OPT(player, birth_no_selling) = false;
			Rand_begin_deterministic((uint32_t)seed);
			ordinary = make_gold(1, "any");
			Rand_end_deterministic();
			OPT(player, birth_no_selling) = true;
			Rand_begin_deterministic((uint32_t)seed);
			compensated = make_gold(1, "any");
			Rand_end_deterministic();
			notnull(ordinary);
			notnull(compensated);
			ordinary_value = ordinary->pval;
			compensated_value = compensated->pval;
			object_delete(NULL, NULL, &ordinary);
			object_delete(NULL, NULL, &compensated);
			/* Low-level fixtures avoid the gold-pile cap.  The multiplier
			 * is an existing tradeoff, not extra income added by selling. */
			require(ordinary_value > 0 && ordinary_value < SHRT_MAX / 5);
			eq(compensated_value, ordinary_value * (depth ? 5 : 1));
		}
	}
	player->depth = saved_depth;
	OPT(player, birth_no_selling) = saved_choice;
	ok;
}

static int test_no_selling_gifts_and_unsuitable_buyer(void *state)
{
	struct object_kind *kind;
	struct object *obj;
	int before;
	uint32_t larder_before;
	(void)state;

	reset_before_load();
	require(savefile_load("Test1", false));
	kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Glassfin"));
	notnull(kind);
	obj = make_test_object(kind, 3);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, kind);
	notnull(obj);
	before = player->au;
	larder_before = player->larder.food_points;

	OPT(player, birth_no_selling) = false;
	require(enter_test_shop(FEAT_STORE_ARMOR));
	require(!store_will_buy_tester(obj));
	eq(cmdq_push(CMD_SELL), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_number(cmdq_peek(), "quantity", 2);
	require(cmdq_pop(CTX_STORE));
	eq(player->au, before);
	eq(carried_kind_count(player, kind), 3);

	OPT(player, birth_no_selling) = true;
	require(enter_test_shop(FEAT_STORE_FISHING));
	require(store_will_buy_tester(obj));
	eq(price_item(store_at(cave, player->grid), obj, true, 2), 0);
	eq(cmdq_push(CMD_SELL), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_number(cmdq_peek(), "quantity", 2);
	require(cmdq_pop(CTX_STORE));
	eq(player->au, before);
	eq(carried_kind_count(player, kind), 1);
	eq(player->larder.food_points, larder_before);
	ok;
}

static int test_fishing_catches_are_edible_objects(void *state)
{
	int food_tval = tval_find_idx("food");

	(void)state;
	require(food_tval > 0);
	require(lookup_sval(food_tval, "Rain Minnow") >= 0);
	require(lookup_sval(food_tval, "Mudbelly") >= 0);
	require(lookup_sval(food_tval, "Glassfin") >= 0);
	require(lookup_kind(food_tval,
		lookup_sval(food_tval, "Rain Minnow"))->effect != NULL);
	require(lookup_kind(food_tval,
		lookup_sval(food_tval, "Mudbelly"))->effect != NULL);
	require(lookup_kind(food_tval,
		lookup_sval(food_tval, "Glassfin"))->effect != NULL);
	ok;
}

struct receipt_observation {
	int count;
	uint32_t portions;
	int32_t experience;
	bool milestone;
	bool committed;
	int32_t gold;
};

static void observe_receipt(game_event_type type, game_event_data *data, void *user)
{
	struct receipt_observation *receipt = user;
	const struct world_larder_report *report = data->donation.report;
	(void)type;
	receipt->count++;
	receipt->portions = report->contributed_points;
	receipt->experience = report->experience;
	receipt->milestone = report->milestone_reached != NULL;
	receipt->gold = report->request_gold;
	receipt->committed = player->larder.food_points >= report->contributed_points &&
		player->exp >= report->experience;
	if (report->request_completed) receipt->committed &=
		world_request_is_complete(&player->village_requests, report->request_completed->id) &&
		player->au >= report->request_gold;
}

static int test_larder_donation_command(void *state)
{
	struct receipt_observation receipt = { 0 };
	struct object_kind *kind;
	struct object *obj;
	struct loc shop = loc(0, 0);
	struct loc scan;
	bool found = false;
	int food_tval;
	int32_t experience_before;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (scan.y = 0; scan.y < cave->height && !found; scan.y++) {
		for (scan.x = 0; scan.x < cave->width; scan.x++) {
			if (square(cave, scan)->feat == FEAT_STORE_GENERAL) {
				shop = scan;
				found = true;
				break;
			}
		}
	}
	require(found);
	food_tval = tval_find_idx("food");
	kind = lookup_kind(food_tval, lookup_sval(food_tval, "Rain Minnow"));
	notnull(kind);
	obj = make_test_object(kind, 4);
	notnull(obj);
	require(world_larder_object_is_catch(obj));
	eq(world_larder_object_value(obj), 1U);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, kind);
	notnull(obj);
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, shop);
	eq(player->larder.food_points, 6);
	experience_before = player->exp;
	eq(cmdq_push(CMD_LARDER_DONATE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_number(cmdq_peek(), "quantity", 4);
	event_add_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
	require(cmdq_pop(CTX_STORE));
	event_remove_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
	eq(receipt.count, 1);
	eq(receipt.portions, 4U);
	eq(receipt.experience, 4);
	require(receipt.committed);
	require(!receipt.milestone);
	eq(player->larder.food_points, 10);
	eq(carried_kind_count(player, kind), 0);
	eq(player->exp, experience_before + 4);
	ok;
}

static int test_larder_milestone_unlocks_expedition_rations(void *state)
{
	struct receipt_observation receipt = { 0 };
	const struct world_larder_milestone_definition *milestone;
	struct store *general;
	struct object_kind *glassfin;
	struct object_kind *ration;
	struct object *obj;
	struct loc shop = loc(0, 0);
	struct loc scan;
	bool found = false;
	int food_tval;
	int32_t experience_before;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	milestone = world_larder_milestone_by_id("emergency-reserve");
	notnull(milestone);
	food_tval = tval_find_idx("food");
	glassfin = lookup_kind(food_tval, lookup_sval(food_tval, "Glassfin"));
	ration = lookup_kind(food_tval,
		lookup_sval(food_tval, "Expedition Ration"));
	notnull(glassfin);
	notnull(ration);
	general = &stores[f_info[FEAT_STORE_GENERAL].shopnum - 1];
	require(!store_has_kind(general, ration));
	for (scan.y = 0; scan.y < cave->height && !found; scan.y++) {
		for (scan.x = 0; scan.x < cave->width; scan.x++) {
			if (square(cave, scan)->feat == FEAT_STORE_GENERAL) {
				shop = scan;
				found = true;
				break;
			}
		}
	}
	require(found);
	obj = make_test_object(glassfin, 3);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, glassfin);
	notnull(obj);
	square_set_mon(cave, player->grid, 0);
	player_place(cave, player, shop);
	experience_before = player->exp;
	eq(cmdq_push(CMD_LARDER_DONATE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_number(cmdq_peek(), "quantity", 3);
	event_add_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
	require(cmdq_pop(CTX_STORE));
	event_remove_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
	eq(receipt.count, 1);
	eq(receipt.experience, 6);
	require(receipt.committed);
	require(receipt.milestone);
	require(player->larder.food_points >= milestone->target);
	eq(player->larder.milestone, milestone->save_index);
	require(store_has_kind(general, ration));
	eq(player->exp, experience_before + 6);
	require(savefile_save("TestLarderUnlock"));

	reset_before_load();
	require(savefile_load("TestLarderUnlock", false));
	milestone = world_larder_milestone_by_id("emergency-reserve");
	notnull(milestone);
	food_tval = tval_find_idx("food");
	ration = lookup_kind(food_tval,
		lookup_sval(food_tval, "Expedition Ration"));
	notnull(ration);
	eq(player->larder.milestone, milestone->save_index);
	general = &stores[f_info[FEAT_STORE_GENERAL].shopnum - 1];
	require(store_has_kind(general, ration));
	eq(player->exp, experience_before + 6); /* Reload does not pay again. */
	require(file_delete("TestLarderUnlock"));
	ok;
}

static int test_catch_experience_and_origin(void *state)
{
	const char *ids[] = { "rain-minnow", "mudbelly", "glassfin",
		"fernscale", "cistern-bream", "blind-deepfish" };
	const int rewards[] = { 1, 1, 2, 1, 2, 3 };
	size_t i;
	(void)state;

	for (i = 0; i < N_ELEMENTS(ids); i++) {
		struct world_fishing_site site;
		struct world_fishing_runtime *runtime;
		struct world_fishing_fish *fish;
		const struct world_fishing_species *species;
		world_fishing_kind fish_kind;
		struct object_kind *kind;
		struct object *caught;
		textblock *info;
		int32_t experience_before;

		reset_before_load();
		require(savefile_load("Test1", false));
		sqinfo_on(square(cave, player->grid)->info, SQUARE_WATER);
		eq(world_fishing_site_query(player, cave, &site), WORLD_FISHING_SITE_OK);
		fish_kind = world_fishing_kind_by_id(ids[i]);
		species = world_fishing_species_by_kind(fish_kind);
		notnull(species);
		kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, species->name));
		notnull(kind);
		player->fishing = mem_zalloc(sizeof(*player->fishing));
		runtime = player->fishing;
		world_fishing_runtime_start(runtime, 17, site.habitat, site.danger,
			site.cave_depth,
			WORLD_FISHING_BASELINE_MAX_REACH, WORLD_FISHING_DEPTH_ROWS,
			"test-rig", "Test rig", site.origin);
		runtime->fish_count = 1;
		runtime->depth = species->depth_min;
		runtime->fish_move_chance_percent = 0;
		runtime->fish_depth_move_chance_percent = 0;
		fish = &runtime->fish[0];
		memset(fish, 0, sizeof(*fish));
		fish->kind = fish_kind;
		fish->glyph = species->glyph;
		fish->col = world_fishing_hook_col(runtime);
		fish->row = runtime->depth;
		fish->depth_min = species->depth_min;
		fish->depth_max = species->depth_max;
		fish->wind_needed = 1;
		fish->bite_timeout = 3;
		require(world_fishing_runtime_is_valid(runtime));
		experience_before = player->exp;
		eq(cmdq_push_fishing_action(WORLD_FISHING_WAIT), 0);
		require(cmdq_pop(CTX_GAME));
		eq(runtime->phase, WORLD_FISHING_BITE);
		eq(player->exp, experience_before);
		eq(cmdq_push_fishing_action(WORLD_FISHING_STRIKE), 0);
		require(cmdq_pop(CTX_GAME));
		eq(runtime->phase, WORLD_FISHING_WINDING);
		eq(player->exp, experience_before);
		eq(cmdq_push_fishing_action(WORLD_FISHING_REEL), 0);
		require(cmdq_pop(CTX_GAME));
		eq(player->exp, experience_before + rewards[i]);
		eq(carried_kind_count(player, kind), 1);
		caught = carried_kind_object(player, kind);
		notnull(caught);
		eq(caught->origin, ORIGIN_FISHING);
		info = object_info(caught, OINFO_NONE);
		notnull(info);
		notnull(wcsstr(textblock_text(info), L"Caught while fishing"));
		null(wcsstr(textblock_text(info), L"Found lying on the floor"));
		textblock_free(info);
		/* Empty-water actions and cancellation never pay the landing twice. */
		eq(cmdq_push_fishing_action(WORLD_FISHING_WAIT), 0);
		require(cmdq_pop(CTX_GAME));
		eq(player->exp, experience_before + rewards[i]);
		eq(cmdq_push_fishing_action(WORLD_FISHING_CANCEL), 0);
		require(cmdq_pop(CTX_GAME));
		null(player->fishing);
		require(savefile_save("TestFishingReward"));
		reset_before_load();
		require(savefile_load("TestFishingReward", false));
		eq(player->exp, experience_before + rewards[i]);
		species = world_fishing_species_by_kind(world_fishing_kind_by_id(ids[i]));
		notnull(species);
		kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, species->name));
		caught = carried_kind_object(player, kind);
		notnull(caught);
		eq(caught->origin, ORIGIN_FISHING);
		require(file_delete("TestFishingReward"));
	}
	ok;
}

static int test_invalid_donation_gives_no_experience(void *state)
{
	struct receipt_observation receipt = { 0 };
	struct object_kind *kind;
	struct object *obj;
	int32_t experience_before;
	uint32_t portions_before;
	(void)state;

	reset_before_load();
	require(savefile_load("Test1", false));
	kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Glassfin"));
	notnull(kind);
	obj = make_test_object(kind, 2);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, kind);
	notnull(obj);
	experience_before = player->exp;
	portions_before = player->larder.food_points;
	require(enter_test_shop(FEAT_STORE_GENERAL));
	eq(cmdq_push(CMD_LARDER_DONATE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_number(cmdq_peek(), "quantity", 3);
	event_add_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
	require(cmdq_pop(CTX_STORE));
	event_remove_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
	eq(receipt.count, 0);
	eq(player->exp, experience_before);
	eq(player->larder.food_points, portions_before);
	eq(carried_kind_count(player, kind), 2);
	require(enter_test_shop(FEAT_STORE_FISHING));
	eq(cmdq_push(CMD_LARDER_DONATE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_number(cmdq_peek(), "quantity", 1);
	require(cmdq_pop(CTX_STORE));
	eq(player->exp, experience_before);
	eq(player->larder.food_points, portions_before);
	eq(carried_kind_count(player, kind), 2);
	ok;
}

static int test_new_catches_eat_donate_and_reload(void *state)
{
	const char *ids[] = { "fernscale", "cistern-bream", "blind-deepfish" };
	int i;
	(void)state;

	for (i = 0; i < (int)N_ELEMENTS(ids); i++) {
		const struct world_fishing_species *species;
		struct object_kind *kind;
		struct object *obj;
		int food_before;
		int32_t exp_before;
		uint32_t portions_before;

		reset_before_load();
		require(savefile_load("Test1", false));
		species = world_fishing_species_by_kind(world_fishing_kind_by_id(ids[i]));
		notnull(species);
		kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, species->name));
		notnull(kind);
		obj = make_test_object(kind, 2);
		inven_carry(player, obj, true, false);
		obj = carried_kind_object(player, kind);
		notnull(obj);
		player_set_timed(player, TMD_FOOD, PY_FOOD_HUNGRY, false, false);
		food_before = player->timed[TMD_FOOD];
		exp_before = player->exp;
		eq(cmdq_push(CMD_EAT), 0);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		require(cmdq_pop(CTX_GAME));
		require(player->timed[TMD_FOOD] > food_before);
		eq(carried_kind_count(player, kind), 1);
		eq(player->exp, exp_before); /* Eating does not also pay donation XP. */
		eq(player->village_requests.count, 0);
		require(enter_test_shop(FEAT_STORE_GENERAL));
		portions_before = player->larder.food_points;
		obj = carried_kind_object(player, kind);
		require(world_larder_object_is_catch(obj));
		eq(cmdq_push(CMD_LARDER_DONATE), 0);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", 1);
		require(cmdq_pop(CTX_STORE));
		eq(player->larder.food_points, portions_before + species->larder_value);
		eq(player->exp, exp_before + species->donation_experience);
		eq(carried_kind_count(player, kind), 0);
		exp_before = player->exp;
		portions_before = player->larder.food_points;
		require(savefile_save("TestRareDonation"));
		reset_before_load();
		require(savefile_load("TestRareDonation", false));
		eq(player->exp, exp_before);
		eq(player->larder.food_points, portions_before);
		species = world_fishing_species_by_kind(world_fishing_kind_by_id(ids[i]));
		kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, species->name));
		eq(carried_kind_count(player, kind), 0);
		require(file_delete("TestRareDonation"));
	}
	ok;
}

/* Optional private pre-update fixture; the regular round trips above run on
 * every checkout. Never write to the supplied original save. */
static bool write_old_memory_fixture(unsigned old_count)
{
	ang_file *in = file_open("TestRareCurrent", MODE_READ, FTYPE_SAVE);
	ang_file *out = file_open("TestRareOld", MODE_WRITE, FTYPE_SAVE);
	char header[28];
	bool good = in && out, found = false;

	if (good) good = file_read(in, header, 8) == 8 && file_write(out, header, 8);
	while (good) {
		uint32_t size, padded;
		char *payload;
		int got = file_read(in, header, sizeof(header));
		if (!got) break;
		if (got != sizeof(header)) { good = false; break; }
		size = (uint8_t)header[20] | ((uint32_t)(uint8_t)header[21] << 8) |
			((uint32_t)(uint8_t)header[22] << 16) | ((uint32_t)(uint8_t)header[23] << 24);
		if (size > 16 * 1024 * 1024) { good = false; break; }
		padded = (size + 3) & ~3U;
		payload = mem_alloc(MAX(padded, 1));
		good = file_read(in, payload, padded) == (int)padded;
		if (good && !strncmp(header, "object memory", 16)) {
			uint32_t check = 0, j;
			if (size < 8u + z_info->k_max) { mem_free(payload); good = false; break; }
			/* Preserve authored kinds, move the sentinel, generated class books
			 * and artefact bases back. Mark jewellery distinctly as a canary. */
			memmove(payload + 8 + old_count - 23, payload + 8 + z_info->ordinary_kind_max - 21,
				23);
			payload[0] = (char)(uint8_t)(old_count & 255);
			payload[1] = (char)(old_count >> 8);
			payload[8 + old_count - 23] = 0;
			payload[8 + old_count - 22] = 0x18; /* first generated book */
			payload[8 + old_count - 2] = 0x1d;
			payload[8 + old_count - 1] = 0x02;
			size = 8 + old_count;
			padded = (size + 3) & ~3U;
			memset(payload + size, 0, padded - size);
			for (j = 0; j < size; j++) check += (uint8_t)payload[j];
			for (j = 0; j < 4; j++) {
				header[20 + j] = (char)(size >> (8 * j));
				header[24 + j] = (char)(check >> (8 * j));
			}
			found = true;
		}
		if (good) good = file_write(out, header, sizeof(header)) &&
			file_write(out, payload, padded);
		mem_free(payload);
	}
	if (in) file_close(in);
	if (out) file_close(out);
	return good && found;
}

static int check_old_kind_memory(unsigned old_count)
{
	struct object_kind *ring, *amulet;
	int i;
	reset_before_load();
	require(savefile_load("Test1", false));
	/* Appending records preserves the original 385 ordinary-kind indices. */
	eq(lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Fernscale"))->kidx, 385);
	eq(z_info->k_max - z_info->ordinary_kind_max, 2);
	eq(z_info->ordinary_kind_max, 412);
	require(savefile_save("TestRareCurrent"));
	require(write_old_memory_fixture(old_count));
	reset_before_load();
	require(savefile_load("TestRareOld", false));
	ring = lookup_kind(TV_RING, lookup_sval(TV_RING, "Ring"));
	amulet = lookup_kind(TV_AMULET, lookup_sval(TV_AMULET, "Amulet"));
	notnull(ring); notnull(amulet);
	require(ring->aware && ring->everseen && !ring->tried);
	require(kind_is_ignored_aware(ring) && kind_is_ignored_unaware(ring));
	require(amulet->tried);
	require(k_info[z_info->ordinary_kind_max - 20].everseen);
	require(kind_is_ignored_unaware(&k_info[z_info->ordinary_kind_max - 20]));
	for (i = old_count - 23; i < z_info->ordinary_kind_max - 21; i++) {
		struct object_kind *new_kind = &k_info[i];
		require(!new_kind->everseen && !new_kind->tried);
		require(!kind_is_ignored_aware(new_kind) && !kind_is_ignored_unaware(new_kind));
	}
	require(file_delete("TestRareCurrent"));
	require(file_delete("TestRareOld"));
	return 0;
}

static int test_old_kind_memory_does_not_become_fish(void *state)
{
	(void)state;
	eq(check_old_kind_memory(408), 0);
	eq(check_old_kind_memory(411), 0);
	ok;
}

static int test_survey_curio_sales(void *state)
{
	const char *names[] = { "Brass Survey Counter", "Clouded Survey Lens", "Knotted Stone Sample" };
	const int prices[] = { 50, 80, 120 };
	int i;
	(void)state;
	for (i = 0; i < 3; i++) {
		struct object_kind *kind;
		struct object *obj;
		struct store *store;
		int gold;
		reset_before_load(); require(savefile_load("Test1", false));
		OPT(player, birth_no_selling) = false;
		require(enter_test_shop(FEAT_STORE_FISHING));
		store = store_at(cave, player->grid);
		kind = lookup_kind(TV_TOOL, lookup_sval(TV_TOOL, names[i])); notnull(kind);
		obj = make_test_object(kind, 1); notnull(obj);
		inven_carry(player, obj, true, false);
		obj = carried_kind_object(player, kind); notnull(obj);
		require(store_will_buy_tester(obj));
		eq(price_item(store, obj, true, 1), prices[i]);
		require(price_item(store, obj, false, 1) > prices[i]);
		gold = player->au;
		eq(cmdq_push(CMD_SELL), 0);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", 1);
		require(cmdq_pop(CTX_STORE)); eq(player->au, gold + prices[i]);
		eq(carried_kind_count(player, kind), 0);
	}
	ok;
}

static int test_request_transactions(void *state)
{
	const char *ids[] = { "forest-supper", "cistern-supper", "deepwater-supper" };
	const int rewards[] = { 40, 120, 240 };
	(void)state;
	for (int i = 0; i < 3; i++) {
		const struct world_objective_definition *d;
		struct world_objective_progress progress;
		struct object_kind *kind;
		struct object *obj;
		int32_t gold;
		reset_before_load(); require(savefile_load("Test1", false));
		d = world_objective_by_id(ids[i]); notnull(d);
		eq(d->donation_gold, rewards[i]); eq(d->kind, WORLD_OBJECTIVE_DONATION);
		kind = lookup_kind(d->tval, d->sval); notnull(kind);
		obj = make_test_object(kind, 6); notnull(obj);
		inven_carry(player, obj, true, false);
		require(enter_test_shop(FEAT_STORE_GENERAL));
		gold = player->au;
		/* First two together, then one individually, then the remainder after load. */
		for (int part = 0; part < 3; part++) {
			struct receipt_observation receipt = { 0 };
			int quantity = part == 0 ? 2 : part == 1 ? 1 : 3;
			int32_t xp = player->exp;
			uint32_t portions = player->larder.food_points;
			const struct world_fishing_species *species;
			d = world_objective_by_id(ids[i]); notnull(d);
			kind = lookup_kind(d->tval, d->sval);
			species = world_fishing_species_by_kind(world_fishing_kind_by_id(
				i == 0 ? "fernscale" : i == 1 ? "cistern-bream" : "blind-deepfish"));
			notnull(species);
			obj = carried_kind_object(player, kind); notnull(obj);
			eq(cmdq_push(CMD_LARDER_DONATE), 0);
			cmd_set_arg_item(cmdq_peek(), "item", obj);
			cmd_set_arg_number(cmdq_peek(), "quantity", quantity);
			event_add_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
			require(cmdq_pop(CTX_STORE));
			event_remove_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
			eq(receipt.count, 1); require(receipt.committed);
			eq(receipt.gold, part == 0 ? rewards[i] : 0);
			eq(player->au, gold + rewards[i]);
			eq(player->exp, xp + quantity * species->donation_experience);
			eq(player->larder.food_points, portions + quantity * species->larder_value);
			eq(player->village_requests.count, 1);
			require(world_objective_progress(player, d, &progress));
			require(progress.complete);
			if (part == 1) {
				require(savefile_save("TestVillageRequest"));
				reset_before_load(); require(savefile_load("TestVillageRequest", false));
				eq(player->au, gold + rewards[i]);
				require(world_request_is_complete(&player->village_requests, ids[i]));
				require(enter_test_shop(FEAT_STORE_GENERAL));
				require(file_delete("TestVillageRequest"));
			}
		}
		eq(carried_kind_count(player, kind), 0);
		/* No stale completion carries into another character. */
		reset_before_load(); require(savefile_load("Test1", false));
		eq(player->village_requests.count, 0);
	}
	ok;
}

static int test_request_rejections_are_atomic(void *state)
{
	(void)state;
	for (int failure = 0; failure < 8; failure++) {
		struct command cmd = { 0 };
		struct object_kind *kind;
		struct object *obj;
		struct receipt_observation receipt = { 0 };
		int32_t gold, xp;
		int count;
		uint32_t portions;
		reset_before_load(); require(savefile_load("Test1", false));
		kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD,
			failure == 4 ? "Ration of Food" : "Fernscale")); notnull(kind);
		obj = make_test_object(kind, 2); notnull(obj);
		inven_carry(player, obj, true, false);
		obj = carried_kind_object(player, kind); notnull(obj);
		require(enter_test_shop(failure == 3 ? FEAT_STORE_FISHING : FEAT_STORE_GENERAL));
		if (failure == 5) player->au = INT32_MAX - 39;
		if (failure == 6) {
			for (int n = 0; n < WORLD_OBJECTIVE_MAX; n++) {
				char id[32]; strnfmt(id, sizeof(id), "retired-request-%d", n);
				require(world_request_record(&player->village_requests, id));
			}
		}
		gold = player->au; xp = player->exp; portions = player->larder.food_points;
		count = carried_kind_count(player, kind);
		/* Missing args (cancel), zero/too many, wrong shop/food, overflow,
		 * exhausted claim capacity, and an object no longer in the pack. */
		if (failure != 0) {
			cmd_set_arg_item(&cmd, "item", failure == 7 ? NULL : obj);
			cmd_set_arg_number(&cmd, "quantity", failure == 1 ? 0 : failure == 2 ? 3 : 1);
		}
		event_add_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
		do_cmd_larder_donate(&cmd);
		event_remove_handler(EVENT_LARDER_DONATED, observe_receipt, &receipt);
		eq(receipt.count, 0); eq(player->au, gold); eq(player->exp, xp);
		eq(player->larder.food_points, portions); eq(carried_kind_count(player, kind), count);
		require(!world_request_is_complete(&player->village_requests, "forest-supper"));
	}
	ok;
}

static int test_request_state_validation(void *state)
{
	struct world_request_state requests = { 0 };
	(void)state;
	require(world_request_state_valid(&requests));
	require(world_request_record(&requests, "retired-request"));
	require(world_request_record(&requests, "deepwater-supper"));
	require(world_request_is_complete(&requests, "deepwater-supper"));
	require(!world_request_record(&requests, "deepwater-supper"));
	require(!world_request_record(&requests, "Invalid ID"));
	my_strcpy(requests.completed[1], requests.completed[0], WORLD_ID_LEN);
	require(!world_request_state_valid(&requests));
	memset(requests.completed[1], 'a', WORLD_ID_LEN);
	require(!world_request_state_valid(&requests));
	requests.count = WORLD_OBJECTIVE_MAX + 1;
	require(!world_request_state_valid(&requests));
	ok;
}

static int test_request_parser_validation(void *state)
{
	(void)state;
	for (int failure = 0; failure < 6; failure++) {
		struct parser *p = objective_parser.init();
		eq(parser_parse(p, "objective:test-request:donation:Test"), PARSE_ERROR_NONE);
		eq(parser_parse(p, "description:Donate one fish."), PARSE_ERROR_NONE);
		eq(parser_parse(p, failure == 0 ? "item:food:Ration of Food" :
			"item:food:Fernscale"), PARSE_ERROR_NONE);
		eq(parser_parse(p, "reward:One-time gold."), PARSE_ERROR_NONE);
		if (failure == 2 || failure == 3) {
			eq(parser_parse(p, failure == 2 ? "donation-gold:0" : "donation-gold:10001"),
				PARSE_ERROR_INVALID_VALUE);
		} else if (failure != 1) {
			eq(parser_parse(p, "donation-gold:40"), PARSE_ERROR_NONE);
		}
		if (failure == 4) {
			eq(parser_parse(p, "objective:another-request:donation:Other"), PARSE_ERROR_NONE);
			eq(parser_parse(p, "description:Another fish."), PARSE_ERROR_NONE);
			eq(parser_parse(p, "item:food:Fernscale"), PARSE_ERROR_NONE);
			eq(parser_parse(p, "reward:More gold."), PARSE_ERROR_NONE);
			eq(parser_parse(p, "donation-gold:120"), PARSE_ERROR_NONE);
		}
		if (failure == 5) {
			eq(parser_parse(p, "donation-gold:120"), PARSE_ERROR_REPEATED_DIRECTIVE);
			eq(parser_parse(p, "route:core.route.underflow.out"), PARSE_ERROR_INVALID_VALUE);
			/* Force a missing target in a following record so finish frees the parser. */
			eq(parser_parse(p, "objective:unfinished:donation:Missing"), PARSE_ERROR_NONE);
		}
		require(objective_parser.finish(p) != PARSE_ERROR_NONE);
		eq(world_objective_count(), 6); /* Failed parse never replaces valid definitions. */
	}
	ok;
}

/* Edit only the new block. Other payloads and the original fixture stay intact. */
static bool rewrite_request_block(const char *replacement, size_t length, int copies)
{
	ang_file *in = file_open("TestRequestSave", MODE_READ, FTYPE_SAVE);
	ang_file *out;
	char header[28];
	bool good, found = false;
	file_delete("TestRequestEdited");
	out = file_open("TestRequestEdited", MODE_WRITE, FTYPE_SAVE);
	good = in && out;
	if (good) good = file_read(in, header, 8) == 8 && file_write(out, header, 8);
	while (good) {
		uint32_t size, padded;
		char *payload;
		bool target;
		int got = file_read(in, header, sizeof(header));
		if (!got) break;
		if (got != sizeof(header)) { good = false; break; }
		size = (uint8_t)header[20] | ((uint32_t)(uint8_t)header[21] << 8) |
			((uint32_t)(uint8_t)header[22] << 16) | ((uint32_t)(uint8_t)header[23] << 24);
		if (size > 16 * 1024 * 1024) { good = false; break; }
		padded = (size + 3) & ~3U;
		payload = mem_alloc(MAX(padded, 1));
		good = file_read(in, payload, padded) == (int)padded;
		target = !strncmp(header, "village claims", 16);
		if (target) found = true;
		if (good && target && replacement) {
			uint32_t check = 0;
			mem_free(payload);
			size = (uint32_t)length; padded = (size + 3) & ~3U;
			payload = mem_zalloc(MAX(padded, 1));
			memcpy(payload, replacement, length);
			for (uint32_t i = 0; i < size; i++) check += (uint8_t)payload[i];
			for (int i = 0; i < 4; i++) {
				header[20 + i] = (char)(size >> (8 * i));
				header[24 + i] = (char)(check >> (8 * i));
			}
		}
		for (int copy = 0; good && copy < (target ? copies : 1); copy++)
			good = file_write(out, header, sizeof(header)) && file_write(out, payload, padded);
		mem_free(payload);
	}
	if (in) file_close(in);
	if (out) file_close(out);
	return good && found;
}

static int test_request_save_block(void *state)
{
	const char bad_count[] = { WORLD_OBJECTIVE_MAX + 1, (char)0xa7 };
	const char duplicate[] = "\002forest-supper\0forest-supper\0\247";
	const char invalid_id[] = "\001BAD ID\0\247";
	const char truncated[] = "\001forest-supper\0";
	char overlength[WORLD_ID_LEN + 3];
	const char *bad[] = { bad_count, duplicate, invalid_id, truncated, overlength, "" };
	size_t lengths[] = { sizeof(bad_count), sizeof(duplicate) - 1,
		sizeof(invalid_id) - 1, sizeof(truncated) - 1, sizeof(overlength), 0 };
	int32_t gold, xp;
	(void)state;
	reset_before_load(); require(savefile_load("Test1", false));
	require(world_request_record(&player->village_requests, "retired-request"));
	require(world_request_record(&player->village_requests, "forest-supper"));
	gold = player->au; xp = player->exp;
	require(savefile_save("TestRequestSave"));
	reset_before_load(); require(savefile_load("TestRequestSave", false));
	eq(player->village_requests.count, 2); eq(player->au, gold); eq(player->exp, xp);
	require(world_request_is_complete(&player->village_requests, "retired-request"));
	require(world_request_is_complete(&player->village_requests, "forest-supper"));
	/* Simulate a pre-feature file; an in-memory claim must not leak into it. */
	require(rewrite_request_block(NULL, 0, 0));
	reset_before_load();
	require(world_request_record(&player->village_requests, "forest-supper"));
	require(savefile_load("TestRequestEdited", false));
	eq(player->village_requests.count, 0); eq(player->au, gold); eq(player->exp, xp);
	require(!player->village_requests.initialized);
	require(savefile_save("TestRequestEdited"));
	reset_before_load(); require(savefile_load("TestRequestEdited", false));
	eq(player->village_requests.count, 0); eq(player->au, gold);
	/* Duplicate blocks and malformed payloads are rejected, never paid. */
	require(rewrite_request_block(NULL, 0, 2));
	reset_before_load(); require(savefile_load("Test1", false));
	require(!savefile_load("TestRequestEdited", false));
	memset(overlength, 'a', sizeof(overlength));
	overlength[0] = 1; overlength[WORLD_ID_LEN + 1] = 0;
	overlength[WORLD_ID_LEN + 2] = (char)0xa7;
	for (int i = 0; i < (int)N_ELEMENTS(bad); i++) {
		require(rewrite_request_block(bad[i], lengths[i], 1));
		reset_before_load(); require(savefile_load("Test1", false));
		require(!savefile_load("TestRequestEdited", false));
	}
	reset_before_load(); require(savefile_load("TestRequestSave", false));
	player->village_requests.count = WORLD_OBJECTIVE_MAX + 1;
	require(!savefile_save("TestRequestSave"));
	reset_before_load(); require(savefile_load("TestRequestSave", false));
	eq(player->village_requests.count, 2); /* Failed save preserved original. */
	require(file_delete("TestRequestSave")); require(file_delete("TestRequestEdited"));
	ok;
}

static int test_external_legacy_save(void *state)
{
	const char *path = getenv("AMMEROW_TEST_LEGACY_SAVE");
	int32_t experience, gold;
	struct loc grid;
	char location[WORLD_ID_LEN];
	(void)state;
	if (!path || !*path) ok;
	reset_before_load();
	require(savefile_load(path, false));
	eq(player->village_requests.count, 0);
	experience = player->exp;
	gold = player->au;
	grid = player->grid;
	my_strcpy(location, player->world_location.id, sizeof(location));
	require(savefile_save("TestRareLegacyRoundtrip"));
	reset_before_load();
	require(savefile_load("TestRareLegacyRoundtrip", false));
	eq(player->exp, experience);
	eq(player->au, gold);
	eq(player->village_requests.count, 0);
	require(loc_eq(player->grid, grid));
	require(streq(player->world_location.id, location));
	require(file_delete("TestRareLegacyRoundtrip"));
	ok;
}

const char *suite_name = "game/provisions";
struct test tests[] = {
	{ "request transactions stack split and reload", test_request_transactions },
	{ "request rejections are atomic", test_request_rejections_are_atomic },
	{ "request stable ID validation", test_request_state_validation },
	{ "request parser validation", test_request_parser_validation },
	{ "request save block and pre-feature migration", test_request_save_block },
	{ "selling default and saved choice", test_selling_default_and_saved_choice },
	{ "catch sale transactions", test_catch_sale_transactions },
	{ "partial potion sale identifies remaining stack",
		test_partial_potion_sale_identifies_remaining_stack },
	{ "no-selling gold compensation", test_no_selling_gold_compensation },
	{ "no-selling gifts and unsuitable buyer",
		test_no_selling_gifts_and_unsuitable_buyer },
	{ "fishing catches are edible objects",
		test_fishing_catches_are_edible_objects },
	{ "larder donation command", test_larder_donation_command },
	{ "catch experience and origin", test_catch_experience_and_origin },
	{ "new catches eat donate and reload", test_new_catches_eat_donate_and_reload },
	{ "external legacy save", test_external_legacy_save },
	{ "old kind memory does not become fish", test_old_kind_memory_does_not_become_fish },
	{ "survey curios sell once at conservative prices", test_survey_curio_sales },
	{ "invalid donation gives no experience", test_invalid_donation_gives_no_experience },
	{ "larder milestone unlocks expedition rations",
		test_larder_milestone_unlocks_expedition_rations },
	{ NULL, NULL }
};
