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
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-timed.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
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
	const char *names[] = { "Rain Minnow", "Mudbelly", "Glassfin" };
	const int proceeds[] = { 1, 5, 20 };
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
	receipt->committed = player->larder.food_points >= report->contributed_points &&
		player->exp >= report->experience;
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
	const char *ids[] = { "rain-minnow", "mudbelly", "glassfin" };
	const int rewards[] = { 1, 1, 2 };
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

const char *suite_name = "game/provisions";
struct test tests[] = {
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
	{ "invalid donation gives no experience", test_invalid_donation_gives_no_experience },
	{ "larder milestone unlocks expedition rations",
		test_larder_milestone_unlocks_expedition_rations },
	{ NULL, NULL }
};
