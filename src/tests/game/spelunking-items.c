/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Consumables, devices, and spell-command parity. */

#include "spelunking-integration-fixture.h"

static int test_spelunking_consumables(void *state)
{
	enum world_spelunk_tile cells[36];
	struct object_kind *ration_kind;
	struct object_kind *healing_kind;
	struct object_kind *speed_kind;
	struct object_kind *enlightenment_kind;
	struct object_kind *dragon_breath_kind;
	struct object_kind *seam_step_kind;
	struct object_kind *mapping_kind;
	struct object_kind *recall_kind;
	struct object_kind *summon_kind;
	struct object *obj;
	struct level *test_level;
	struct chunk *top_down_cave;
	int food_tval;
	int mushroom_tval;
	int potion_tval;
	int scroll_tval;
	int count_before;
	int food_before;
	int hp_before;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 36; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 6; i++) cells[5 * 6 + i] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 6, 6, 2, 4, 90));
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	require(world_spelunk_player_is_active(player));

	food_tval = tval_find_idx("food");
	mushroom_tval = tval_find_idx("mushroom");
	potion_tval = tval_find_idx("potion");
	scroll_tval = tval_find_idx("scroll");
	for (i = 0; i < z_info->k_max; i++) {
		struct object_kind *kind = &k_info[i];

		if ((kind->tval == food_tval || kind->tval == mushroom_tval) &&
				kind->effect) {
			require(world_spelunk_item_effect_is_safe(kind->effect));
		} else if (kind->tval == potion_tval && kind->effect) {
			require(world_spelunk_item_effect_is_safe(kind->effect));
		}
	}
	/* Timed-status transition effects are data too.  The shared cave clock
	 * may only expire statuses whose begin/end chains remain player-local. */
	for (i = 0; i < TMD_MAX; i++) {
		if (timed_effects[i].on_begin_effect) {
			require(world_spelunk_effect_chain_is_player_local(
				timed_effects[i].on_begin_effect));
		}
		if (timed_effects[i].on_end_effect) {
			require(world_spelunk_effect_chain_is_player_local(
				timed_effects[i].on_end_effect));
		}
	}

	ration_kind = lookup_kind(food_tval,
		lookup_sval(food_tval, "Ration of Food"));
	healing_kind = lookup_kind(potion_tval,
		lookup_sval(potion_tval, "Cure Light Wounds"));
	speed_kind = lookup_kind(potion_tval,
		lookup_sval(potion_tval, "Speed"));
	enlightenment_kind = lookup_kind(potion_tval,
		lookup_sval(potion_tval, "Enlightenment"));
	dragon_breath_kind = lookup_kind(potion_tval,
		lookup_sval(potion_tval, "Dragon Breath"));
	seam_step_kind = lookup_kind(scroll_tval,
		lookup_sval(scroll_tval, "Seam Step"));
	mapping_kind = lookup_kind(scroll_tval,
		lookup_sval(scroll_tval, "Magic Mapping"));
	recall_kind = lookup_kind(scroll_tval,
		lookup_sval(scroll_tval, "Homeward Tension"));
	summon_kind = lookup_kind(scroll_tval,
		lookup_sval(scroll_tval, "Summon Monster"));
	notnull(ration_kind);
	notnull(healing_kind);
	notnull(speed_kind);
	notnull(enlightenment_kind);
	notnull(dragon_breath_kind);
	notnull(seam_step_kind);
	notnull(mapping_kind);
	notnull(recall_kind);
	notnull(summon_kind);
	require(world_spelunk_item_effect_is_safe(seam_step_kind->effect));
	require(world_spelunk_item_effect_is_safe(mapping_kind->effect));
	require(world_spelunk_item_effect_is_safe(recall_kind->effect));
	require(!world_spelunk_item_effect_is_safe(summon_kind->effect));

	/* Eating is inventory-only in side view and applies player-local effects
	 * without the retained top-down chunk being present. */
	obj = make_test_object(ration_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, ration_kind);
	notnull(obj);
	count_before = carried_kind_count(player, ration_kind);
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_HUNGRY - 1);
	food_before = player->timed[TMD_FOOD];
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_EAT), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	top_down_cave = cave;
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	require(player->timed[TMD_FOOD] > food_before);
	eq(carried_kind_count(player, ration_kind), count_before - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* Quaffing uses the same guarded path and can heal the shared player. */
	obj = make_test_object(healing_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, healing_kind);
	notnull(obj);
	count_before = carried_kind_count(player, healing_kind);
	player->chp = MAX(1, player->mhp - 20);
	hp_before = player->chp;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_QUAFF), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	require(player->chp > hp_before);
	eq(carried_kind_count(player, healing_kind), count_before - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* Timed player effects take the same route without probing the top-down
	 * decoy or monster-target state. */
	obj = make_test_object(speed_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, speed_kind);
	notnull(obj);
	count_before = carried_kind_count(player, speed_kind);
	player->timed[TMD_FAST] = 0;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_QUAFF), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	require(player->timed[TMD_FAST] > 0);
	eq(carried_kind_count(player, speed_kind), count_before - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* Mapping is dispatched to side-view knowledge rather than touching the
	 * retained top-down compatibility chunk. */
	obj = make_test_object(enlightenment_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, enlightenment_kind);
	notnull(obj);
	count_before = carried_kind_count(player, enlightenment_kind);
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_QUAFF), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	eq(carried_kind_count(player, enlightenment_kind), count_before - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);
	require(world_spelunk_visibility_is_explored(player->spelunking, 0, 0));

	/* Aimed potion projections use the local actor owner. */
	player->spelunking->actor_roster_initialized = true;
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 3, 4));
	obj = make_test_object(dragon_breath_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, dragon_breath_kind);
	notnull(obj);
	count_before = carried_kind_count(player, dragon_breath_kind);
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_QUAFF), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cmd_set_arg_choice(cmdq_peek(), "list_index", 0);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	eq(carried_kind_count(player, dragon_breath_kind), count_before - 1);
	eq(player->spelunking->actor_count, 0);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* Reading is a normal inventory command.  Seam Step chooses by distance
	 * from real side-view cells and deliberately permits unsupported air. */
	obj = make_test_object(seam_step_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, seam_step_kind);
	notnull(obj);
	count_before = carried_kind_count(player, seam_step_kind);
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_READ_SCROLL), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	eq(carried_kind_count(player, seam_step_kind), count_before - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);
	require(player->spelunking->state.x != 2 ||
		player->spelunking->state.y != 4);
	eq(player->spelunking->state.movement, WORLD_SPELUNK_FALLING);
	require(world_spelunk_runtime_is_valid(player->spelunking));

	/* Recall is a deliberately standalone cross-mode exception to the local
	 * effect policy.  Reading it in side view consumes it and starts the normal
	 * delayed countdown without consulting inherited dungeon depth. */
	obj = make_test_object(recall_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, recall_kind);
	notnull(obj);
	count_before = carried_kind_count(player, recall_kind);
	player->word_recall = 0;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_READ_SCROLL), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	require(player->word_recall >= 15 && player->word_recall <= 34);
	eq(carried_kind_count(player, recall_kind), count_before - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);
	player->word_recall = 0;

	/* A scroll needing a top-down-only summon owner is rejected before it can
	 * consume the object or commit energy. */
	obj = make_test_object(summon_kind, 1);
	notnull(obj);
	inven_carry(player, obj, true, false);
	obj = carried_kind_object(player, summon_kind);
	notnull(obj);
	count_before = carried_kind_count(player, summon_kind);
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_READ_SCROLL), 0);
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	eq(carried_kind_count(player, summon_kind), count_before);
	eq(player->upkeep->energy_use, 0);

	reset_before_load();
	require(savefile_load("Test1", false));
	null(player->spelunking);
	ok;
}

static int test_spelunking_magic_devices(void *state)
{
	enum world_spelunk_tile cells[96];
	struct object_kind *staff_kind;
	struct object_kind *rod_kind;
	struct object_kind *wand_kind;
	struct object_kind *ring_kind;
	struct object_kind *status_wand_kind;
	struct object *staff;
	struct object *rod;
	struct object *wand;
	struct object *ring;
	struct object *status_wand;
	struct level *test_level;
	struct chunk *top_down_cave;
	int staff_charges;
	int rod_timeout;
	int wand_charges;
	int status_charges;
	int hp_before;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (i = 0; i < 96; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 12; i++) cells[7 * 12 + i] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 12, 8, 2, 6, 90));
	world_spelunk_visibility_follow_player(player->spelunking);
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	player->spelunking->actor_roster_initialized = true;
	top_down_cave = cave;

	staff_kind = lookup_kind(TV_STAFF,
		lookup_sval(TV_STAFF, "Cure Light Wounds"));
	rod_kind = lookup_kind(TV_ROD, lookup_sval(TV_ROD, "Magic Mapping"));
	wand_kind = lookup_kind(TV_WAND, lookup_sval(TV_WAND, "Force Darts"));
	ring_kind = lookup_kind(TV_RING, lookup_sval(TV_RING, "Flames"));
	status_wand_kind = lookup_kind(TV_WAND,
		lookup_sval(TV_WAND, "Slow Monster"));
	notnull(staff_kind);
	notnull(rod_kind);
	notnull(wand_kind);
	notnull(ring_kind);
	notnull(status_wand_kind);
	require(world_spelunk_item_effect_is_safe(staff_kind->effect));
	require(world_spelunk_item_effect_is_safe(rod_kind->effect));
	require(world_spelunk_item_effect_is_safe(wand_kind->effect));
	require(world_spelunk_item_effect_is_safe(ring_kind->effect));
	require(!world_spelunk_item_effect_is_safe(status_wand_kind->effect));

	/* Shared device skill, charge, knowledge, energy, and healing rules run
	 * without a native cave.  Generic Use dispatches through the same path. */
	staff = make_test_object(staff_kind, 1);
	notnull(staff);
	staff->pval = 2;
	staff->known->pval = 2;
	inven_carry(player, staff, true, false);
	staff = carried_kind_object(player, staff_kind);
	notnull(staff);
	staff_charges = staff->pval;
	player->chp = MAX(1, player->mhp - 20);
	hp_before = player->chp;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_USE_STAFF), 0);
	cmd_set_arg_item(cmdq_peek(), "item", staff);
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	require(player->chp > hp_before);
	eq(staff->pval, staff_charges - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);
	player->chp = MAX(1, player->mhp - 20);
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_USE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", staff);
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	eq(staff->pval, staff_charges - 2);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* Rod recharge and cave-native mapping retain their normal shared rules. */
	rod = make_test_object(rod_kind, 1);
	notnull(rod);
	rod->timeout = 0;
	rod->known->timeout = 0;
	inven_carry(player, rod, true, false);
	rod = carried_kind_object(player, rod_kind);
	notnull(rod);
	rod_timeout = rod->timeout;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_USE_ROD), 0);
	cmd_set_arg_item(cmdq_peek(), "item", rod);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	require(rod->timeout > rod_timeout);
	eq(player->upkeep->energy_use, z_info->move_energy);
	require(world_spelunk_visibility_is_explored(player->spelunking, 11, 0));

	/* A damaging wand follows side-view rock and actor ownership while using
	 * the ordinary device chance, damage roll, charge, and identification. */
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 6));
	wand = make_test_object(wand_kind, 1);
	notnull(wand);
	wand->pval = 2;
	wand->known->pval = 2;
	inven_carry(player, wand, true, false);
	wand = carried_kind_object(player, wand_kind);
	notnull(wand);
	wand_charges = wand->pval;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_USE_WAND), 0);
	cmd_set_arg_item(cmdq_peek(), "item", wand);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	eq(player->spelunking->actor_count, 0);
	eq(wand->pval, wand_charges - 1);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* Equipped activations may combine a cave-owned ball with a shared player
	 * buff.  Both actors inside the unobstructed blast are affected. */
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 6));
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 5, 6));
	ring = make_test_object(ring_kind, 1);
	notnull(ring);
	inven_carry(player, ring, false, false);
	inven_wield(ring, wield_slot(ring));
	require(object_is_equipped(player->body, ring));
	player->timed[TMD_OPP_FIRE] = 0;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_ACTIVATE), 0);
	cmd_set_arg_item(cmdq_peek(), "item", ring);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cave = NULL;
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	cave = top_down_cave;
	eq(player->spelunking->actor_count, 0);
	require(ring->timeout > 0);
	require(player->timed[TMD_OPP_FIRE] > 0);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* Effects whose semantics need unauthored actor statuses remain
	 * transactional: no charge, sound, energy, or actor mutation. */
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 6));
	status_wand = make_test_object(status_wand_kind, 1);
	notnull(status_wand);
	status_wand->pval = 2;
	status_wand->known->pval = 2;
	inven_carry(player, status_wand, true, false);
	status_wand = carried_kind_object(player, status_wand_kind);
	notnull(status_wand);
	status_charges = status_wand->pval;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_USE_WAND), 0);
	cmd_set_arg_item(cmdq_peek(), "item", status_wand);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	cave = NULL;
	require(cmdq_pop(CTX_GAME));
	cave = top_down_cave;
	eq(status_wand->pval, status_charges);
	eq(player->upkeep->energy_use, 0);
	eq(player->spelunking->actor_count, 1);

	reset_before_load();
	require(savefile_load("Test1", false));
	null(player->spelunking);
	ok;
}

static int test_spelunking_spell_commands(void *state)
{
	enum world_spelunk_tile cells[60];
	const struct class_spell *force_dart;
	const struct class_spell *survey_light;
	struct player_class *mage;
	struct object_kind *book_kind;
	struct object *book;
	struct object *uncarried_book;
	struct level *test_level;
	struct chunk *top_down_cave;
	int focus_before;
	int i;

	(void)state;
	reset_before_load();
	require(savefile_load("Test1", false));
	for (mage = classes; mage && !streq(mage->name, "Mage");
			mage = mage->next) {
		/* Find the data-authored class rather than relying on list order. */
	}
	notnull(mage);
	player_spells_free(player);
	player->class = mage;
	player_spells_init(player);
	pf_on(player->state.pflags, PF_CHOOSE_SPELLS);
	player->msp = 20;
	player->csp = player->msp;
	player->upkeep->new_spells = 2;
	book_kind = lookup_kind(TV_MAGIC_BOOK, 1);
	notnull(book_kind);
	book = make_test_object(book_kind, 1);
	notnull(book);
	inven_carry(player, book, true, false);
	book = carried_kind_object(player, book_kind);
	notnull(book);
	for (i = 0; i < 60; i++) cells[i] = WORLD_SPELUNK_AIR;
	for (i = 0; i < 10; i++) cells[5 * 10 + i] = WORLD_SPELUNK_ROCK;
	require(install_spelunking_runtime(cells, 10, 6, 2, 4, 90));
	world_spelunk_visibility_follow_player(player->spelunking);
	test_level = (struct level *)world_player_level(player);
	notnull(test_level);
	my_strcpy(player->world_location.entry, "stair.main",
		sizeof(player->world_location.entry));
	test_level->mode = WORLD_MODE_SPELUNKING;
	player->spelunking->actor_roster_initialized = true;
	require(world_spelunk_player_is_active(player));
	require(textui_mode_input_allows_command(player, CMD_BROWSE_SPELL,
		true, true));
	require(textui_mode_input_allows_command(player, CMD_STUDY, false, false));
	require(textui_mode_input_allows_command(player, CMD_CAST, false, false));

	force_dart = spell_by_index(player, 0);
	survey_light = spell_by_index(player, 1);
	notnull(force_dart);
	notnull(survey_light);
	require(world_spelunk_effect_chain_is_supported(force_dart->effect));
	require(!world_spelunk_effect_chain_is_supported(survey_light->effect));
	top_down_cave = cave;
	cave = NULL;

	/* Studying changes only the shared player and book knowledge state, so it
	 * remains valid even when that spell's spatial effect is not adapted yet. */
	player->upkeep->energy_use = 0;
	require(player->upkeep->new_spells > 0);
	eq(cmdq_push(CMD_STUDY), 0);
	cmd_set_arg_choice(cmdq_peek(), "spell", 0);
	cmd_set_arg_item(cmdq_peek(), "book", book);
	require(cmdq_pop(CTX_GAME));
	require(player->spell_flags[0] & PY_SPELL_LEARNED);
	eq(player->upkeep->energy_use, z_info->move_energy);
	player->upkeep->new_spells = 1;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_STUDY), 0);
	cmd_set_arg_choice(cmdq_peek(), "spell", 1);
	cmd_set_arg_item(cmdq_peek(), "book", book);
	require(cmdq_pop(CTX_GAME));
	require(player->spell_flags[1] & PY_SPELL_LEARNED);
	eq(player->upkeep->energy_use, z_info->move_energy);

	/* A supported directional spell uses the side-view actor and rock owners
	 * while retaining normal focus, failure, experience and action timing. */
	require(world_spelunk_runtime_add_actor(player->spelunking,
		WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 4));
	player->spelunking->actors[0].hp = 1;
	player->csp = player->msp;
	focus_before = player->csp;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_CAST), 0);
	cmd_set_arg_choice(cmdq_peek(), "spell", 0);
	cmd_set_arg_item(cmdq_peek(), "book", book);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	rand_fix(100);
	require(cmdq_pop(CTX_GAME));
	rand_unfix();
	eq(player->spelunking->actor_count, 0);
	eq(player->csp, focus_before - force_dart->smana);
	eq(player->upkeep->energy_use, z_info->move_energy);
	require(player->spell_flags[0] & PY_SPELL_WORKED);

	/* Unsupported cave geometry remains transactional even after the spell is
	 * learned: no focus, action energy, worked flag, or cave state is changed. */
	focus_before = player->csp;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_CAST), 0);
	cmd_set_arg_choice(cmdq_peek(), "spell", 1);
	cmd_set_arg_item(cmdq_peek(), "book", book);
	require(cmdq_pop(CTX_GAME));
	eq(player->csp, focus_before);
	eq(player->upkeep->energy_use, 0);
	require(!(player->spell_flags[1] & PY_SPELL_WORKED));
	eq(player->spelunking->actor_count, 0);

	/* UI filtering is not the authority boundary: a supplied native-floor
	 * book argument is rejected before even selecting a supported spell. */
	uncarried_book = make_test_object(book_kind, 1);
	notnull(uncarried_book);
	focus_before = player->csp;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_CAST), 0);
	cmd_set_arg_choice(cmdq_peek(), "spell", 0);
	cmd_set_arg_item(cmdq_peek(), "book", uncarried_book);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	require(cmdq_pop(CTX_GAME));
	eq(player->csp, focus_before);
	eq(player->upkeep->energy_use, 0);
	object_delete(NULL, NULL, &uncarried_book->known);
	object_delete(NULL, NULL, &uncarried_book);

	cave = top_down_cave;
	reset_before_load();
	require(savefile_load("Test1", false));
	null(player->spelunking);
	ok;
}

const char *suite_name = "game/spelunking-items";
struct test tests[] = {
	{ "consumables", test_spelunking_consumables },
	{ "magic devices", test_spelunking_magic_devices },
	{ "spell commands", test_spelunking_spell_commands },
	{ NULL, NULL }
};
