/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */

#include "unit-test.h"
#include "test-utils.h"
#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "cmds.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "monster.h"
#include "mon-make.h"
#include "mon-util.h"
#include "obj-ignore.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-birth.h"
#include "player-explore.h"
#include "player-timed.h"
#include "player-util.h"
#include "ui-prefs.h"
#include "trap.h"
#include "z-util.h"

static bool initialized_player;
static int floor_prompts;
static char last_message[256];

static void remember_message(game_event_type type, game_event_data *data, void *user)
{
	(void)type;
	(void)user;
	my_strcpy(last_message, data->message.msg, sizeof(last_message));
}

static void count_floor_prompt(game_event_type type, game_event_data *data,
		void *user)
{
	(void)type;
	(void)data;
	(void)user;
	floor_prompts++;
}

int setup_tests(void **state)
{
	char path[1024];
	(void)state;
	set_file_paths();
	path_build(path, sizeof(path), ANGBAND_DIR_USER, "explore-test");
	if (!dir_create(path)) return 1;
	string_free(ANGBAND_DIR_USER);
	ANGBAND_DIR_USER = string_make(path);
	init_angband();
	textui_prefs_init();
	event_add_handler(EVENT_MESSAGE, remember_message, NULL);
	return 0;
}

int teardown_tests(void *state)
{
	char path[1024];
	(void)state;
	path_build(path, sizeof(path), ANGBAND_DIR_USER,
		"customized_interface_options.txt");
	file_delete(path);
	path_build(path, sizeof(path), ANGBAND_DIR_USER,
		"customized_birth_options.txt");
	file_delete(path);
	player_explore_free(player);
	textui_prefs_free();
	cleanup_angband();
	return 0;
}

/* A narrow, unlit corridor longer than the lantern's reach. Actual movement
 * updates sight/knowledge, so this exercises successive frontier searches. */
static void corridor(void)
{
	if (initialized_player) {
		player_explore_free(player);
		wipe_mon_list(cave, player);
		cave_free(player->cave);
		player->cave = NULL;
		cave_free(cave);
		cave = NULL;
	} else {
		if (!player_make_simple("Quarryhand", "Warrior", "Explorer"))
			quit("Could not create exploration fixture player");
		initialized_player = true;
	}
	cmdq_flush();
	cave = cave_new(7, 34);
	player->cave = cave_new(7, 34);
	for (int y = 0; y < 7; y++) {
		for (int x = 0; x < 34; x++)
			square_set_feat(cave, loc(x, y), FEAT_PERM);
	}
	for (int x = 1; x < 33; x++)
		square_set_feat(cave, loc(x, 3), FEAT_FLOOR);
	player_place(cave, player, loc(1, 3));
	player->upkeep->generate_level = false;
	player->upkeep->running = 0;
	player->upkeep->update = 0;
	player->upkeep->notice = 0;
	player->upkeep->energy_use = 0;
	memset(player->timed, 0, TMD_MAX * sizeof(*player->timed));
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_FULL - 1);
	player->chp = player->mhp = 30;
	player->state.cur_light = 2;
	player->is_dead = false;
	player->unignoring = false;
	OPT(player, autoexplore_commands) = true;
	OPT(player, autoexplore_continuous) = true;
	OPT(player, auto_stairs) = false;
	OPT(player, pickup_always) = false;
	OPT(player, pickup_inven) = false;
	OPT(player, disturb_near) = false;
	OPT(player, birth_feelings) = false;
	update_view(cave, player);
}

static void start(void)
{
	struct command cmd = { .code = CMD_EXPLORE };
	do_cmd_explore(&cmd);
}

static struct object *loot_at(int x, bool known)
{
	struct object *obj = object_new();
	object_prep(obj, lookup_kind(TV_FOOD, lookup_sval(TV_FOOD,
		"Ration of Food")), 1, AVERAGE);
	obj->number = 6;
	if (!floor_carry(cave, loc(x, 3), obj, false))
		quit("Could not place exploration fixture loot");
	if (known) square_know_pile(cave, loc(x, 3), NULL);
	return obj;
}

static int test_continues_and_finishes(void *state)
{
	int commands = 0, steps = 0;
	(void)state;
	corridor();
	start();
	require(player_explore_active(player));
	eq(player->grid.x, 2);
	eq(player->upkeep->energy_use, energy_per_move(player));
	while (player_explore_active(player) && commands++ < 100) {
		struct loc before = player->grid;
		player->upkeep->energy_use = 0;
		require(cmdq_pop(CTX_GAME));
		if (!loc_eq(before, player->grid)) {
			steps++;
			eq(player->upkeep->energy_use, energy_per_move(player));
		}
	}
	require(!player_explore_active(player));
	require(steps > 20);
	require(commands < 100);
	eq(player->upkeep->running, 0);
	ok;
}

static int test_existing_monster_prevents_start(void *state)
{
	struct monster *mon;
	(void)state;
	corridor();
	mon = t_add_monster(cave, loc(3, 3), "Kobold");
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	start();
	require(!player_explore_active(player));
	eq(player->grid.x, 1);
	eq(player->upkeep->energy_use, 0);
	ok;
}

static int test_hidden_monster_never_attacked(void *state)
{
	struct monster *mon;
	int hp;
	(void)state;
	corridor();
	start();
	require(player_explore_active(player));
	mon = t_add_monster(cave, loc(3, 3), "Kobold");
	mflag_off(mon->mflag, MFLAG_VISIBLE);
	hp = mon->hp;
	move_player(6, false);
	require(!player_explore_active(player));
	eq(player->grid.x, 2);
	eq(mon->hp, hp);
	ok;
}

static int test_new_monster_before_pickup(void *state)
{
	struct monster *mon;
	struct command cmd = { .code = CMD_AUTOPICKUP };
	(void)state;
	corridor();
	start();
	mon = t_add_monster(cave, loc(4, 3), "Kobold");
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	player->upkeep->energy_use = 0;
	do_cmd_autopickup(&cmd);
	require(!player_explore_active(player));
	eq(player->upkeep->energy_use, 0);
	ok;
}

static int test_detected_monster_message(void *state)
{
	struct monster *mon;
	(void)state;
	corridor();
	start();
	mon = t_add_monster(cave, loc(24, 3), "Kobold");
	mflag_on(mon->mflag, MFLAG_MARK);
	mflag_off(mon->mflag, MFLAG_VIEW);
	update_mon(mon, cave, true);
	player_explore_check(player);
	require(!player_explore_active(player));
	require(strstr(last_message, "detected beyond normal sight"));
	eq(player->grid.x, 2);
	ok;
}

static int test_generic_interrupt_does_not_guess(void *state)
{
	struct monster *mon;
	(void)state;
	corridor();
	start();
	mon = t_add_monster(cave, loc(24, 3), "Kobold");
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	/* Another event interrupted us before the next danger check. A generic
	 * disturbance must not invent which known condition caused that event. */
	disturb(player);
	require(!player_explore_active(player));
	require(strstr(last_message, "Explore stopped: interrupted."));
	ok;
}

static int test_perception_disturbance_and_camouflage(void *state)
{
	struct monster *mon;
	bool was_dungeon = character_dungeon;
	(void)state;
	corridor();
	start();
	OPT(player, disturb_near) = true;
	character_dungeon = true;
	sqinfo_on(square(cave, loc(3, 3))->info, SQUARE_GLOW);
	update_view(cave, player);
	mon = t_add_monster(cave, loc(3, 3), "Kobold");
	update_mon(mon, cave, true);
	character_dungeon = was_dungeon;
	require(!player_explore_active(player));
	require(strstr(last_message, "in view (seen or sensed"));
	corridor();
	start();
	mon = t_add_monster(cave, loc(24, 3), "Kobold");
	mflag_on(mon->mflag, MFLAG_MARK);
	mflag_on(mon->mflag, MFLAG_CAMOUFLAGE);
	update_mon(mon, cave, true);
	require(player_explore_active(player));
	require(player_explore_check(player));
	/* Deleting a perceived slot must not leave a phantom danger. */
	delete_monster_idx(cave, mon->midx);
	require(player_explore_check(player));
	ok;
}

static int test_new_loot_stops_at_sight(void *state)
{
	struct object *obj;
	int commands = 0;
	(void)state;
	corridor();
	obj = loot_at(10, false);
	start();
	while (player_explore_active(player) && commands++ < 50)
		require(cmdq_pop(CTX_GAME));
	require(commands < 50);
	require(!player_explore_active(player));
	require(player->grid.x < 10);
	require(strstr(last_message, "new loot discovered"));
	require(obj->known && obj->known->kind == obj->kind);
	start(); /* Deliberate acknowledgement, not stuck at this same pile. */
	require(player_explore_active(player));
	ok;
}

static int test_old_loot_reduction_not_discovery(void *state)
{
	struct object *obj;
	(void)state;
	corridor();
	obj = loot_at(10, true);
	start();
	require(player_explore_active(player));
	require(!player_explore_new_loot(player));
	obj->number = obj->known->number = 5;
	require(!player_explore_new_loot(player));
	obj->number = obj->known->number = 7;
	require(player_explore_new_loot(player));
	ok;
}

static int test_ignored_loot_does_not_stop(void *state)
{
	struct object *obj;
	(void)state;
	corridor();
	start();
	obj = loot_at(10, true);
	obj->known->notice |= OBJ_NOTICE_IGNORE;
	require(!player_explore_new_loot(player));
	obj->known->notice &= ~OBJ_NOTICE_IGNORE;
	require(player_explore_new_loot(player));
	ok;
}

static int test_acknowledged_pickup_keeps_moving(void *state)
{
	(void)state;
	corridor();
	loot_at(2, true);
	/* Pickup recalculates equipment light. This fixture has no equipped
	 * lantern, so keep the two test squares permanently lit. */
	sqinfo_on(square(cave, loc(2, 3))->info, SQUARE_GLOW);
	sqinfo_on(square(cave, loc(3, 3))->info, SQUARE_GLOW);
	OPT(player, pickup_always) = true;
	floor_prompts = 0;
	event_add_handler(EVENT_SEEFLOOR, count_floor_prompt, NULL);
	start();
	require(player_explore_active(player));
	require(cmdq_pop(CTX_GAME)); /* Ordinary auto-pickup, at its normal cost. */
	event_remove_handler(EVENT_SEEFLOOR, count_floor_prompt, NULL);
	eq(floor_prompts, 0);
	require(!square_object(cave, loc(2, 3)));
	require(player_explore_active(player));
	require(player->upkeep->energy_use > 0);
	require(cmdq_pop(CTX_GAME));
	eq(player->grid.x, 3);
	ok;
}

static int test_barriers_stay_manual(void *state)
{
	const int features[] = { FEAT_CLOSED, FEAT_MORE, FEAT_LESS,
		FEAT_RUBBLE, FEAT_LAVA, FEAT_STORE_GENERAL };
	(void)state;
	for (size_t i = 0; i < N_ELEMENTS(features); i++) {
		int commands = 0;
		corridor();
		square_set_feat(cave, loc(4, 3), features[i]);
		update_view(cave, player);
		start();
		while (player_explore_active(player) && commands++ < 30)
			require(cmdq_pop(CTX_GAME));
		require(commands < 30);
		require(player->grid.x < 4);
		eq(square(cave, loc(4, 3))->feat, features[i]);
		require(!player->upkeep->generate_level);
	}
	ok;
}

static int test_closed_door_on_stale_route(void *state)
{
	(void)state;
	corridor();
	start();
	square_set_feat(cave, loc(3, 3), FEAT_CLOSED);
	move_player(6, false);
	require(!player_explore_active(player));
	eq(player->grid.x, 2);
	require(square_iscloseddoor(cave, loc(3, 3)));
	ok;
}

static int test_web_trap_water_and_darkness(void *state)
{
	struct trap_kind *pit = lookup_trap("pit");
	struct trap_kind *web = lookup_trap("web");
	(void)state;
	notnull(pit);
	notnull(web);
	for (int mode = 0; mode < 3; mode++) {
		int commands = 0;
		corridor();
		if (mode == 2) {
			sqinfo_on(square(cave, loc(4, 3))->info, SQUARE_WATER);
			square_memorize(cave, loc(4, 3));
		} else {
			place_trap(cave, loc(4, 3), mode ? web->tidx : pit->tidx, 1);
			square_reveal_trap(cave, loc(4, 3), true, false);
			square_memorize_traps(cave, loc(4, 3));
		}
		start();
		while (player_explore_active(player) && commands++ < 30)
			require(cmdq_pop(CTX_GAME));
		require(commands < 30);
		require(player->grid.x < 4);
	}
	corridor();
	place_trap(cave, player->grid, web->tidx, 1);
	start();
	require(!player_explore_active(player));
	require(square_iswebbed(cave, player->grid));
	corridor();
	player->state.cur_light = 0;
	update_view(cave, player);
	start();
	require(!player_explore_active(player));
	ok;
}

static int test_interrupt_cannot_resume(void *state)
{
	struct command stale = { .code = CMD_EXPLORE, .background_command = 1 };
	(void)state;
	corridor();
	start();
	require(player_explore_active(player));
	disturb(player);
	require(!player_explore_active(player));
	eq(player->upkeep->running, 0);
	do_cmd_explore(&stale);
	require(!player_explore_active(player));
	eq(player->grid.x, 2);
	ok;
}

static int test_unsafe_conditions(void *state)
{
	const int timed[] = { TMD_CONFUSED, TMD_BLIND, TMD_IMAGE,
		TMD_PARALYZED, TMD_BLOODLUST, TMD_COMMAND, TMD_AFRAID,
		TMD_STUN, TMD_POISONED, TMD_CUT };
	(void)state;
	for (size_t i = 0; i < N_ELEMENTS(timed); i++) {
		corridor();
		player->timed[timed[i]] = 10;
		cmdq_push(CMD_EXPLORE);
		require(cmdq_pop(CTX_GAME));
		require(!player_explore_active(player));
		eq(player->grid.x, 1);
	}
	corridor();
	player->chp = 1;
	start();
	require(!player_explore_active(player));
	corridor();
	player->timed[TMD_FOOD] = (int16_t)(PY_FOOD_HUNGRY - 1);
	start();
	require(!player_explore_active(player));
	ok;
}

static int test_options_migration_and_defaults(void *state)
{
	struct player_options opts = { 0 }, restored = { 0 };
	(void)state;
	options_migrate_exploration(&opts, false, false);
	require(!opts.opt[OPT_auto_stairs]);
	opts.opt[OPT_autoexplore_commands] = true;
	opts.opt[OPT_autoexplore_continuous] = true;
	options_migrate_exploration(&opts, false, false);
	require(opts.opt[OPT_auto_stairs]);
	require(!opts.opt[OPT_autoexplore_continuous]);
	opts.opt[OPT_auto_stairs] = false;
	opts.opt[OPT_autoexplore_continuous] = true;
	options_migrate_exploration(&opts, true, true);
	require(!opts.opt[OPT_auto_stairs]);
	require(opts.opt[OPT_autoexplore_continuous]);
	require(options_save_custom(&opts, OP_INTERFACE));
	require(options_restore_custom(&restored, OP_INTERFACE));
	eq(restored.opt[OPT_autoexplore_commands], true);
	eq(restored.opt[OPT_auto_stairs], false);
	eq(restored.opt[OPT_autoexplore_continuous], true);
	ok;
}

static int test_traditional_and_off_remain_available(void *state)
{
	(void)state;
	corridor();
	OPT(player, autoexplore_commands) = false;
	start();
	require(!player_explore_active(player));
	eq(player->grid.x, 1);
	OPT(player, autoexplore_commands) = true;
	OPT(player, autoexplore_continuous) = false;
	start();
	require(!player_explore_active(player));
	eq(player->grid.x, 2);
	eq(player->upkeep->energy_use, energy_per_move(player));
	disturb(player);
	ok;
}

static int test_level_feeling_defaults(void *state)
{
	struct player_options opts = { 0 }, restored = { 0 };
	(void)state;
	opts.opt[OPT_birth_feelings] = true;
	options_restore_maintainer(&opts, OP_BIRTH);
	require(!opts.opt[OPT_birth_feelings]);
	/* Explicit saved preferences win over the shipped default in either direction. */
	for (int enabled = 0; enabled < 2; enabled++) {
		opts.opt[OPT_birth_feelings] = enabled != 0;
		restored.opt[OPT_birth_feelings] = enabled == 0;
		require(options_save_custom(&opts, OP_BIRTH));
		require(options_restore_custom(&restored, OP_BIRTH));
		eq(restored.opt[OPT_birth_feelings], enabled != 0);
	}
	ok;
}

static int test_level_feeling_during_exploration(void *state)
{
	(void)state;
	for (int enabled = 0; enabled < 2; enabled++) {
		int commands = 0;
		struct loc before;
		corridor();
		player->depth = 1;
		cave->depth = 1;
		player->upkeep->only_partial = false;
		OPT(player, birth_feelings) = enabled != 0;
		cave->feeling = 78;
		require(z_info->feeling_need > 0);
		cave->feeling_squares = z_info->feeling_need - 1;
		/* Initially beyond the lantern's reach. Walking reveals it and uses
		 * the real view-update path to announce the treasure feeling. */
		sqinfo_on(square(cave, loc(5, 3))->info, SQUARE_FEEL);
		start();
		require(player_explore_active(player));
		last_message[0] = '\0';
		while (cave->feeling_squares < z_info->feeling_need && commands++ < 8)
			require(cmdq_pop(CTX_GAME));
		eq(cave->feeling_squares, z_info->feeling_need);
		require(!square_isfeel(cave, loc(5, 3)));
		if (enabled) {
			require(strstr(last_message, "You feel that"));
		} else {
			require(!last_message[0]);
		}
		require(player_explore_active(player));
		require(player->upkeep->running > 0);
		before = player->grid;
		while (loc_eq(before, player->grid) && commands++ < 12)
			require(cmdq_pop(CTX_GAME));
		require(!loc_eq(before, player->grid));
		require(player_explore_active(player));
	}
	ok;
}

static int test_level_feeling_still_interrupts_traditional(void *state)
{
	(void)state;
	corridor();
	player->depth = 1;
	cave->depth = 1;
	cave->feeling = 78;
	OPT(player, birth_feelings) = true;
	OPT(player, autoexplore_continuous) = false;
	/* Model an ongoing traditional run; a nearest-frontier command can
	 * already finish after its first step in this narrow fixture. */
	player->upkeep->running = 10;
	require(!player_explore_active(player));
	require(player->upkeep->running > 0);
	display_feeling(true);
	eq(player->upkeep->running, 0);
	require(strstr(last_message, "You feel that"));
	ok;
}

const char *suite_name = "game/explore";
struct test tests[] = {
	{ "successive frontiers and paid movement", test_continues_and_finishes },
	{ "existing monster regardless of disturb option", test_existing_monster_prevents_start },
	{ "hidden occupant is never attacked", test_hidden_monster_never_attacked },
	{ "danger interrupts before queued pickup", test_new_monster_before_pickup },
	{ "detected monster message explains nonvisual danger", test_detected_monster_message },
	{ "generic interruption does not guess a monster cause", test_generic_interrupt_does_not_guess },
	{ "perception interruption is explicit and camouflage stays hidden", test_perception_disturbance_and_camouflage },
	{ "new loot stops at sight and restart acknowledges it", test_new_loot_stops_at_sight },
	{ "old stack reduction is not discovery", test_old_loot_reduction_not_discovery },
	{ "ignored loot respects existing rules", test_ignored_loot_does_not_stop },
	{ "acknowledged pickup continues without floor modal", test_acknowledged_pickup_keeps_moving },
	{ "doors hazards exits remain manual", test_barriers_stay_manual },
	{ "stale door route cannot auto-open", test_closed_door_on_stale_route },
	{ "webs traps water and darkness", test_web_trap_water_and_darkness },
	{ "interrupt cannot resume from a stale command", test_interrupt_cannot_resume },
	{ "unsafe conditions and coercion cannot explore", test_unsafe_conditions },
	{ "legacy options and new defaults", test_options_migration_and_defaults },
	{ "traditional and off modes retained", test_traditional_and_off_remain_available },
	{ "level feelings default off and custom choices persist", test_level_feeling_defaults },
	{ "level feelings announce without stopping continuous exploration", test_level_feeling_during_exploration },
	{ "level feelings retain traditional running interrupt", test_level_feeling_still_interrupts_traditional },
	{ NULL, NULL }
};
