/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file game-world.c
 * \brief Game core management of the game world
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cmds.h"
#include "effects.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "mon-move.h"
#include "mon-util.h"
#include "obj-curse.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-resource.h"
#include "player-timed.h"
#include "player-util.h"
#include "source.h"
#include "store.h"
#include "target.h"
#include "trap.h"
#include "world-entry.h"
#include "world-location.h"
#include "world-story-data.h"
#include "world-overworld.h"
#include "world-spelunking-breath.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-combat.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-local.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-transition.h"
#include "world-turn.h"
#include "z-queue.h"

uint16_t daycount = 0;
uint32_t seed_randart;		/* Consistent random artifacts */
uint32_t seed_flavor;		/* Consistent object colors */
int32_t turn;			/* Current game turn */
bool character_generated;	/* The character exists */
bool character_dungeon;		/* The character has a dungeon */
struct level *world;
struct world_site *world_sites;
struct world_route *world_routes;
struct world_travel_node *world_travel_nodes;

/**
 * This table allows quick conversion from "speed" to "energy"
 * The basic function WAS ((S>=110) ? (S-110) : (100 / (120-S)))
 * Note that table access is *much* quicker than computation.
 *
 * Note that the table has been changed at high speeds.  From
 * "Slow (-40)" to "Fast (+30)" is pretty much unchanged, but
 * at speeds above "Fast (+30)", one approaches an asymptotic
 * effective limit of 50 energy per turn.  This means that it
 * is relatively easy to reach "Fast (+30)" and get about 40
 * energy per turn, but then speed becomes very "expensive",
 * and you must get all the way to "Fast (+50)" to reach the
 * point of getting 45 energy per turn.  After that point,
 * furthur increases in speed are more or less pointless,
 * except to balance out heavy inventory.
 *
 * Note that currently the fastest monster is "Fast (+30)".
 */
const uint8_t extract_energy[200] =
{
	/* Slow */     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	/* Slow */     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	/* Slow */     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	/* Slow */     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	/* Slow */     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	/* Slow */     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	/* S-50 */     1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
	/* S-40 */     2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
	/* S-30 */     2,  2,  2,  2,  2,  2,  2,  3,  3,  3,
	/* S-20 */     3,  3,  3,  3,  3,  4,  4,  4,  4,  4,
	/* S-10 */     5,  5,  5,  5,  6,  6,  7,  7,  8,  9,
	/* Norm */    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
	/* F+10 */    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
	/* F+20 */    30, 31, 32, 33, 34, 35, 36, 36, 37, 37,
	/* F+30 */    38, 38, 39, 39, 40, 40, 40, 41, 41, 41,
	/* F+40 */    42, 42, 42, 43, 43, 43, 44, 44, 44, 44,
	/* F+50 */    45, 45, 45, 45, 45, 46, 46, 46, 46, 46,
	/* F+60 */    47, 47, 47, 47, 47, 48, 48, 48, 48, 48,
	/* F+70 */    49, 49, 49, 49, 49, 49, 49, 49, 49, 49,
	/* Fast */    49, 49, 49, 49, 49, 49, 49, 49, 49, 49,
};

/**
 * Find a level by its name
 */
struct level *level_by_name(const char *name)
{
	struct level *lev = world;

	while (lev) {
		if (streq(lev->name, name)) {
			return lev;
		}
		lev = lev->next;
	}
	return NULL;
}

/**
 * Find a level by its depth
 */
struct level *level_by_depth(int depth)
{
	struct level *lev = world;
	while (lev) {
		if (lev->has_legacy_depth && lev->depth == depth) {
			break;
		}
		lev = lev->next;
	}
	return lev;
}

/**
 * Say whether it's daytime or not
 */
bool is_daytime(void)
{
	if ((turn % (10L * z_info->day_length)) < ((10L * z_info->day_length) / 2)) 
		return true;

	return false;
}

/**
 * The amount of energy gained in a turn by a player or monster
 */
int turn_energy(int speed)
{
	return extract_energy[speed] * z_info->move_energy / 100;
}

/**
 * If player has inscribed the object with "!!", let him know when it's
 * recharged. -LM-
 * Also inform player when first item of a stack has recharged. -HK-
 * Notify all recharges w/o inscription if notify_recharge option set -WP-
 */
static void recharged_notice(const struct object *obj, bool all)
{
	char o_name[120];

	const char *s;

	bool notify = false;

	if (OPT(player, notify_recharge)) {
		notify = true;
	} else if (obj->note) {
		/* Find a '!' */
		s = strchr(quark_str(obj->note), '!');

		/* Process notification request */
		while (s) {
			/* Find another '!' */
			if (s[1] == '!') {
				notify = true;
				break;
			}

			/* Keep looking for '!'s */
			s = strchr(s + 1, '!');
		}
	}

	if (!notify) return;

	/* Describe (briefly) */
	object_desc(o_name, sizeof(o_name), obj, ODESC_BASE, player);

	/* Disturb the player */
	disturb(player);

	/* Notify the player */
	if (obj->number > 1) {
		if (all) msg("Your %s have recharged.", o_name);
		else msg("One of your %s has recharged.", o_name);
	} else if (obj->artifact)
		msg("The %s has recharged.", o_name);
	else
		msg("Your %s has recharged.", o_name);
}


/** Recharge activatable equipment and carried rods. */
static void recharge_carried_objects(void)
{
	bool discharged_stack;
	struct object *obj;

	/* Recharge carried gear */
	for (obj = player->gear; obj; obj = obj->next) {
		/* Skip non-objects */
		assert(obj->kind);

		/* Recharge equipment */
		if (object_is_equipped(player->body, obj)) {
			/* Recharge activatable objects */
			if (recharge_timeout(obj)) {
				/* Message if an item recharged */
				recharged_notice(obj, true);

				/* Window stuff */
				player->upkeep->redraw |= (PR_EQUIP);
			}
		} else {
			/* Recharge the inventory */
			discharged_stack =
				(number_charging(obj) == obj->number) ? true : false;

			/* Recharge rods, and update if any rods are recharged */
			if (tval_can_have_timeout(obj) && recharge_timeout(obj)) {
				/* Entire stack is recharged */
				if (obj->timeout == 0)
					recharged_notice(obj, true);

				/* Previously exhausted stack has acquired a charge */
				else if (discharged_stack)
					recharged_notice(obj, false);

				/* Combine pack */
				player->upkeep->notice |= (PN_COMBINE);

				/* Redraw stuff */
				player->upkeep->redraw |= (PR_INVEN);
			}
		}
	}
}

/** Recharge rods stored on an ordinary Angband level. */
static void recharge_native_ground_objects(struct chunk *c)
{
	int i;
	struct object *obj;

	for (i = 1; i < c->obj_max; i++) {
		obj = c->objects[i];
		if (!obj) continue;

		/* Recharge rods */
		if (tval_can_have_timeout(obj))
			recharge_timeout(obj);
	}
}

/** Recharge rods stored in the persistent side-view runtime. */
static void recharge_spelunking_ground_objects(void)
{
	struct world_spelunk_runtime *runtime = player->spelunking;
	struct object *obj;
	size_t count = 0;

	if (!world_spelunk_runtime_local_is_valid(runtime)) return;
	for (obj = runtime->ground_objects; obj && count < WORLD_SPELUNK_OBJECT_MAX;
			obj = obj->next, count++) {
		if (tval_can_have_timeout(obj)) recharge_timeout(obj);
	}
}


/**
 * Select ambience by authored location kind, then underground depth or time.
 */
void play_ambient_sound(void)
{
	const struct level *level = world_player_level(player);
	bool surface = level ? level->mode == WORLD_MODE_TOP_DOWN &&
		(level->kind == WORLD_LOCATION_HUB ||
			level->kind == WORLD_LOCATION_OUTDOORS) : player->depth == 0;

	/* Surface danger is stored in depth for the inherited simulation, but a
	 * dangerous forest is still outdoors, not a dripping dungeon. */
	if (surface) {
		if (is_daytime())
			sound(MSG_AMBIENT_DAY);
		else 
			sound(MSG_AMBIENT_NITE);
	} else if (player->depth <= 20) {
		sound(MSG_AMBIENT_DNG1);
	} else if (player->depth <= 40) {
		sound(MSG_AMBIENT_DNG2);
	} else if (player->depth <= 60) {
		sound(MSG_AMBIENT_DNG3);
	} else if (player->depth <= 80) {
		sound(MSG_AMBIENT_DNG4);
	} else {
		sound(MSG_AMBIENT_DNG5);
	}
}

/** Decrement map-independent player statuses for one periodic world tick. */
static void decrease_player_timeouts(enum world_turn_context context)
{
	int adjust = (adj_con_fix[player->state.stat_ind[STAT_CON]] + 1);
	int i;
	bool top_down = world_turn_context_has_capability(context,
		WORLD_MODE_CAP_NATIVE_CAVE);

	/* Most timed effects decrement by 1 */
	for (i = 0; i < TMD_MAX; i++) {
		int decr = 1;
		if (!player->timed[i])
			continue;

		/* Special cases */
		switch (i) {
            case TMD_FOOD:
            {
                /* Handled separately */
                decr = 0;
                break;
            }

			case TMD_CUT:
			{
				/* Check for truly "mortal" wound */
				if (player_timed_grade_eq(player, i, "Mortal Wound")) {
					decr = 0;
				} else {
					decr = adjust;
				}

				/* Rock players just maintain */
				if (player_has(player, PF_ROCK)) {
					decr = 0;
				}

				break;
			}

			case TMD_POISONED:
			case TMD_STUN:
			{
				decr = adjust;
				break;
			}

			case TMD_COMMAND:
			{
				struct monster *mon;

				/* Command belongs to a native-cave monster and is normally
				 * cleared by every cross-mode transition.  Fail closed if stale
				 * state nevertheless reaches a side-view periodic tick. */
				if (!top_down) {
					player_clear_timed(player, TMD_COMMAND, false, false);
					decr = 0;
					break;
				}
				mon = get_commanded_monster();
				if (!los(cave, player->grid, mon->grid)) {
					/* Out of sight is out of mind */
					mon_clear_timed(mon, MON_TMD_COMMAND, MON_TMD_FLG_NOTIFY);
					player_clear_timed(player, TMD_COMMAND,
						true, true);
				} else {
					/* Keep monster timer aligned */
					mon_dec_timed(mon, MON_TMD_COMMAND, decr, 0);
				}
				break;
			}
		}
		/* Decrement the effect */
		player_dec_timed(player, i, decr, false, true);
	}
	return;
}

/**
 * Decrement equipment curse clocks.  In side view, map-dependent effects are
 * held at their trigger boundary until a native map owns the turn again.
 */
static void decrease_curse_timeouts(enum world_turn_context context)
{
	int i;
	bool side_view = world_turn_context_has_capability(context,
		WORLD_MODE_CAP_SIDE_VIEW);

	/* Curse effects always decrement by 1. */
	for (i = 0; i < player->body.count; i++) {
		struct curse_data *curse = NULL;
		if (player->body.slots[i].obj == NULL) {
			continue;
		}
		curse = player->body.slots[i].obj->curses;
		if (curse) {
			int j;
			for (j = 0; j < z_info->curse_max; j++) {
				struct curse *c = &curses[j];

				if (!curse[j].power || curse[j].timeout <= 0 || !c->obj ||
						!c->obj->effect) continue;
				if (side_view && curse[j].timeout == 1 &&
						!world_spelunk_effect_chain_is_player_local(
							c->obj->effect)) {
					continue;
				}
				curse[j].timeout--;
				if (!curse[j].timeout) {
					if (do_curse_effect(j, player->body.slots[i].obj)) {
						player_learn_curse(player, c);
					}
					curse[j].timeout = randcalc(c->obj->time, 0, RANDOMISE);
				}
			}
		}
	}

	return;
}

/** Process clocks owned by carried equipment and location-local objects. */
static void process_inventory_periodic(enum world_turn_context context,
		struct chunk *native_cave)
{
	decrease_curse_timeouts(context);
	if (player->is_dead) return;

	/* Side-view sight is authored independently of Angband light radius.  Do
	 * not consume fuel until that model has an explicit equipped-light input. */
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		player_update_light(player);
	}

	if (player_of_has(player, OF_DRAIN_EXP)) {
		if ((player->exp > 0) && one_in_(10)) {
			int32_t d = damroll(10, 6) +
				(player->exp / 100) * z_info->life_drain_percent;
			player_exp_lose(player, d / 10, false);
		}
		equip_learn_flag(player, OF_DRAIN_EXP);
	}

	recharge_carried_objects();
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		if (native_cave) recharge_native_ground_objects(native_cave);
	} else if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_LOCAL_STORAGE)) {
		recharge_spelunking_ground_objects();
	}

	if (!(turn % 100)) equip_learn_after_time(player);
}

/** Accrue deferred economy work from the authoritative global clock. */
static void process_global_periodic(enum world_turn_context context)
{
	const struct level *level;

	if (!world_turn_context_is_valid(context) || z_info->store_turns <= 0) {
		return;
	}
	level = world_player_level(player);
	if (!level || level->has_store_services) return;

	/* Store contents are deliberately not changed while the player is away:
	 * that would leak stock through the knowledge UI.  The saved 16-bit debt
	 * saturates instead of wrapping on exceptionally long expeditions. */
	if (!(turn % (10L * z_info->store_turns)) && daycount < UINT16_MAX) {
		daycount++;
	}
}

/** Apply deferred services after arrival but before player-visible input. */
static void process_deferred_store_services(void)
{
	const struct level *level = world_player_level(player);

	if (!player->upkeep->arena_level && level && level->has_store_services &&
			daycount) {
		store_update();
	}
}

/**
 * Reconcile services belonging to a retained location.
 *
 * Save loading and ordinary level entry both pass through this boundary so
 * neither needs to recognize a particular location ID.  The world record is
 * the sole authority for whether the location provides stores.
 */
bool world_reconcile_location_services(struct chunk *actual,
		struct chunk *known)
{
	const struct level *level = world_chunk_level(actual);

	if (!level) return true;
	if (level->has_store_services &&
			(!town_ensure_main_stair(actual, known) ||
			!town_ensure_registered_stores(actual, known))) return false;
	return world_entry_materialize_landmarks(actual, known);
}

const char *world_player_opening_history(const struct player *p)
{
	const struct level *level = world_player_level(p);
	const struct world_site *site = level && level->site_id ?
		world_site_by_id(level->site_id) : NULL;

	return site && site->opening_scene ? site->opening_scene->history : NULL;
}


/**
 * Characters leave scent trails for perceptive monsters to track.
 *
 * Scent is rather more limited than sound.  Many creatures cannot use
 * it at all, it doesn't extend very far outwards from the character's
 * current position, and monsters can use it to home in the character,
 * but not to run away.
 *
 * Scent is valued according to age.  When a character takes his turn,
 * scent is aged by one, and new scent is laid down.  Monsters have a smell
 * value which indicates the oldest scent they can detect.  Grids where the
 * player has never been will have scent 0.  The player's grid will also have
 * scent 0, but this is OK as no monster will ever be smelling it.
 */
static void update_scent(void)
{
	int y, x;
	int scent_strength[5][5] = {
		{2, 2, 2, 2, 2},
		{2, 1, 1, 1, 2},
		{2, 1, 0, 1, 2},
		{2, 1, 1, 1, 2},
		{2, 2, 2, 2, 2},
	};

	/* Update scent for all grids */
	for (y = 1; y < cave->height - 1; y++) {
		for (x = 1; x < cave->width - 1; x++) {
			if (cave->scent.grids[y][x] > 0) {
				cave->scent.grids[y][x]++;
			}
		}
	}

	/* Scentless player */
	if (player->timed[TMD_COVERTRACKS]) return;

	/* Lay down new scent around the player */
	for (y = 0; y < 5; y++) {
		for (x = 0; x < 5; x++) {
			struct loc scent;
			int new_scent = scent_strength[y][x];
			int d;
			bool add_scent = false;

			/* Initialize */
			scent.y = y + player->grid.y - 2;
			scent.x = x + player->grid.x - 2;

			/* Ignore invalid or non-scent-carrying grids */
			if (!square_in_bounds(cave, scent)) continue;
			if (square_isnoscent(cave, scent)) continue;

			/* Check scent is spreading on floors, not going through walls */
			for (d = 0; d < 8; d++)	{
				struct loc adj = loc_sum(scent, ddgrid_ddd[d]);

				if (!square_in_bounds(cave, adj)) {
					continue;
				}

				/* Player grid is always valid */
				if (x == 2 && y == 2) {
					add_scent = true;
				}

				/* Adjacent to a closer grid, so valid */
				if (cave->scent.grids[adj.y][adj.x] == new_scent - 1) {
					add_scent = true;
				}
			}

			/* Not valid */
			if (!add_scent) {
				continue;
			}

			/* Mark the scent */
			cave->scent.grids[scent.y][scent.x] = new_scent;
		}
	}
}

/**
 * Advance physiology and statuses owned by the player rather than a map.
 *
 * This deliberately excludes inventory clocks, curse activations, light,
 * global time, and location actors.  Those systems have different owners and
 * must not acquire an implicit dependency on a compatibility chunk.
 */
static void process_player_periodic(enum world_turn_context context)
{
	int i;

	/* Take damage from poison. */
	if (player->timed[TMD_POISONED]) {
		take_hit(player, player_apply_damage_reduction(player, 1),
			"poison");
		if (player->is_dead) return;
	}

	/* Take damage from cuts, worse from serious cuts. */
	if (player->timed[TMD_CUT]) {
		if (player_has(player, PF_ROCK)) {
			i = 0;
		} else if (player_timed_grade_eq(player, TMD_CUT,
				"Mortal Wound") ||
				player_timed_grade_eq(player, TMD_CUT, "Deep Gash")) {
			i = 3;
		} else if (player_timed_grade_eq(player, TMD_CUT,
				"Severe Cut")) {
			i = 2;
		} else {
			i = 1;
		}

		take_hit(player, player_apply_damage_reduction(player, i),
			"a fatal wound");
		if (player->is_dead) return;
	}

	/* Side effects of diminishing bloodlust. */
	if (player->timed[TMD_BLOODLUST]) {
		player_over_exert(player, PY_EXERT_HP | PY_EXERT_CUT |
			PY_EXERT_SLOW, MAX(0, 10 - player->timed[TMD_BLOODLUST]),
			player->chp / 10);
		if (player->is_dead) return;
	}

	/* Timed healing. */
	if (player->timed[TMD_HEAL]) {
		bool ident = false;

		effect_simple(EF_HEAL_HP, source_player(), "30", 0, 0, 0, 0, 0,
			&ident);
	}

	/* Effects of dead-field sickness. */
	if (player->timed[TMD_DEADFIELD_SICKNESS]) {
		if (one_in_(2)) {
			msg("Dead-field pressure sickens you.");
			player_stat_dec(player, STAT_CON, false);
		}
		if (one_in_(2)) {
			msg("Dead-field pressure saps your strength.");
			player_stat_dec(player, STAT_STR, false);
		}
		if (one_in_(2)) {
			int drain = 100 + (player->exp / 100) *
				z_info->life_drain_percent;

			msg("Dead-field pressure dims your life force.");
			player_exp_lose(player, drain, false);
		}
	}

	/* Digest. */
	if (!player_timed_grade_eq(player, TMD_FOOD, "Full")) {
		if (!(turn % 100)) {
			i = turn_energy(player->state.speed);
			i = (i * 100) / z_info->food_value;
			if (player_of_has(player, OF_REGEN)) i *= 2;
			if (player_of_has(player, OF_SLOW_DIGEST)) i /= 2;
			if (i < 1) i = 1;
			player_dec_timed(player, TMD_FOOD, i, false, true);
		}

		/* Timed healing has a fast metabolism. */
		if (player->timed[TMD_HEAL]) {
			player_dec_timed(player, TMD_FOOD,
				8 * z_info->food_value, false, true);
			if (player->timed[TMD_FOOD] < PY_FOOD_HUNGRY) {
				player_set_timed(player, TMD_HEAL, 0, true, true);
			}
		}
	} else {
		/* Digest quickly when gorged. */
		player_dec_timed(player, TMD_FOOD,
			5000 / z_info->food_value, false, true);
		player->upkeep->update |= PU_BONUS;
	}

	/* Faint or starve. */
	if (player_timed_grade_eq(player, TMD_FOOD, "Faint")) {
		if (!player->timed[TMD_PARALYZED] && one_in_(10)) {
			msg("You faint from the lack of food.");
			disturb(player);
			(void)player_inc_timed(player, TMD_PARALYZED,
				1 + randint0(5), true, true, false);
		}
	} else if (player_timed_grade_eq(player, TMD_FOOD, "Starving")) {
		i = (PY_FOOD_STARVE - player->timed[TMD_FOOD]) / 10;
		take_hit(player, player_apply_damage_reduction(player, i),
			"starvation");
		if (player->is_dead) return;
	}

	if (player->chp < player->mhp) player_regen_hp(player);
	player_regen_mana(player);
	decrease_player_timeouts(context);
}

/** Process delayed Recall under the active location-mode policy. */
static bool process_delayed_recall(enum world_turn_context context)
{
	if (!player->word_recall || player->upkeep->arena_level) return false;
	player->word_recall--;
	if (player->word_recall) return false;

	disturb(player);
	cmdq_flush();
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		if (world_spelunking_recall_execute(player, cave)) {
			msgt(MSG_TPLEVEL, "You feel yourself yanked back to safety!");
			return true;
		}
		/* Preserve the charged effect when authored routing cannot resolve so a
		 * data error cannot silently consume the player's scroll. */
		player->word_recall = 1;
		msg("The charged air strains, but cannot find a safe path.");
		return false;
	}

	if (player->depth) {
		msgt(MSG_TPLEVEL, "You feel yourself yanked upwards!");
		dungeon_change_level(player, 0);
	} else {
		msgt(MSG_TPLEVEL, "You feel yourself yanked downwards!");
		player_set_recall_depth(player);
		dungeon_change_level(player, player->recall_depth);
	}
	return false;
}

/**
 * Handle things that need updating once every 10 game turns
 */
void process_world(struct chunk *c)
{
	int y, x;

	/* Compact the monster list if we're approaching the limit */
	if (cave_monster_count(c) + 32 > z_info->level_monster_max)
		compact_monsters(c, 64);

	/* Too many holes in the monster list - compress */
	if (cave_monster_count(c) + 32 < cave_monster_max(c))
		compact_monsters(c, 0);

	/*** Check the Time ***/

	/* Play an ambient sound at regular intervals. */
	if (!(turn % ((10L * z_info->day_length) / 4)))
		play_ambient_sound();

	/* Handle native town sunshine.  Global economy time is processed by the
	 * mode-independent dispatcher before this native-map function is called. */
	if (!player->depth) {
		/* Daybreak/Nighfall in town */
		if (!(turn % ((10L * z_info->day_length) / 2))) {
			/* Check for dawn */
			bool dawn = (!(turn % (10L * z_info->day_length)));

			if (dawn) {
				/* Day breaks */
				msg("The sun has risen.");
			} else {
				/* Night falls */
				msg("The sun has fallen.");
			}

			/* Illuminate */
			cave_illuminate(c, dawn);
		}
	}

	/* Check for light change */
	if (player_has(player, PF_UNLIGHT)) {
		player->upkeep->update |= PU_BONUS;
	}

	/* Check for creature generation */
	if (one_in_(z_info->alloc_monster_chance)) {
		(void)pick_and_place_distant_monster(c, player->grid,
			z_info->max_sight + 5, true, player->depth);
	}

	/* Player physiology is map-independent and shared with side-view caves. */
	process_player_periodic(WORLD_TURN_CONTEXT_TOP_DOWN);
	if (player->is_dead) return;
	process_inventory_periodic(WORLD_TURN_CONTEXT_TOP_DOWN, c);
	if (player->is_dead) return;

	/* Update noise and scent (not if resting) */
	if (!player_is_resting(player)) {
		make_noise(player, NULL, NULL);
		update_scent();
	}

	/* Decrease trap timeouts */
	for (y = 0; y < c->height; y++) {
		for (x = 0; x < c->width; x++) {
			struct loc grid = loc(x, y);
			struct trap *trap = square(c, grid)->trap;
			bool changed = false;
			while (trap) {
				if (trap->timeout) {
					trap->timeout--;
					if (!trap->timeout) {
						changed = true;
					}
				}
				trap = trap->next;
			}
			if (changed && square_isseen(c, grid)) {
				square_memorize_traps(c, grid);
				square_light_spot(c, grid);
			}
		}
	}


	/*** Involuntary Movement ***/

	/* Delayed Word-of-Recall; suspended in arenas. */
	(void)process_delayed_recall(WORLD_TURN_CONTEXT_TOP_DOWN);

	/* Delayed Deep Descent */
	if (player->deep_descent) {
		/* Count down towards descent */
		player->deep_descent--;

		/* Activate the descent */
		if (player->deep_descent == 0) {
			/* Calculate target depth */
			int target_increment = (4 / z_info->stair_skip) + 1;
			int target_depth = dungeon_get_next_level(player,
				player->max_depth, target_increment);
			disturb(player);

			/* Determine the level */
			if (target_depth > player->depth) {
				msgt(MSG_TPLEVEL, "The floor opens beneath you!");
				dungeon_change_level(player, target_depth);
			} else {
				/* Otherwise do something disastrous */
				msgt(MSG_TPLEVEL, "You are thrown back in an explosion!");
				effect_simple(EF_DESTRUCTION, source_none(), "0", 0, 5, 0, 0, 0, NULL);
			}
		}
	}
}


/** Process deferred notices which are safe for the current location mode. */
static void process_location_notice(enum world_turn_context context)
{
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		notice_stuff(player);
	} else if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW) &&
			(player->upkeep->notice & PN_COMBINE)) {
		/* Pack combination owns no map and is safe in every location. */
		player->upkeep->notice &= ~PN_COMBINE;
		combine_pack(player);
	}
	/* Ignored-item floor drops and native monster-message batching still need
	 * location adapters.  Leave those flags pending in side view. */
}

/** Apply updates and redraws without invoking another mode's presentation. */
static void handle_location_stuff(enum world_turn_context context)
{
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE) ||
			world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		/* update_stuff() and redraw_stuff() own the final native-map guard so
		 * direct shared-inventory callers obey the same boundary. */
		handle_stuff(player);
	}
}

/** Run actors belonging to the current mode; fail closed on stale context. */
static bool process_location_actors(int minimum_energy)
{
	enum world_turn_context context = world_turn_context_for_player(player);

	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		process_monsters(minimum_energy);
		return true;
	}
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		struct world_spelunk_actor_report reports[WORLD_SPELUNK_ACTOR_MAX];
		size_t written;
		size_t i;
		bool changed = false;

		/* Fixed-speed actors act once at the ordinary end-of-cycle phase.  The
		 * higher-energy probes are reserved for a later actor-speed extension. */
		if (minimum_energy > 0) return true;
		if (!world_spelunk_process_actors(player, reports,
				N_ELEMENTS(reports), &written)) {
			return false;
		}
		for (i = 0; i < written; i++) {
			const char *name = world_spelunk_actor_name(reports[i].actor_id);

			changed = true;
			switch (reports[i].event) {
			case WORLD_SPELUNK_ACTOR_EVENT_MISSED:
				msg("The %s snaps at you and misses.", name);
				break;
			case WORLD_SPELUNK_ACTOR_EVENT_HIT:
				msg("The %s bites you for %d damage.", name,
					reports[i].damage);
				break;
			case WORLD_SPELUNK_ACTOR_EVENT_MOVED:
			case WORLD_SPELUNK_ACTOR_EVENT_NONE:
			default:
				break;
			}
		}
		if (changed) event_signal_point(EVENT_MAP, -1, -1);
		return true;
	}
	return false;
}

/** Reset per-cycle actor readiness for the current mode. */
static bool reset_location_actors(void)
{
	enum world_turn_context context = world_turn_context_for_player(player);

	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		reset_monsters();
		return true;
	}
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		return true;
	}
	return false;
}

/** Process periodic systems belonging to the current mode. */
static bool process_location_world(void)
{
	enum world_turn_context context = world_turn_context_for_player(player);

	if (!world_turn_context_is_valid(context)) return false;
	process_global_periodic(context);
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		process_world(cave);
		return true;
	}
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		/* Physiology and carried inventory follow the player across modes.
		 * Presentation weather and location hazards retain their own explicit
		 * ownership boundaries. */
		process_player_periodic(context);
		if (!player->is_dead) process_inventory_periodic(context, NULL);
		if (!player->is_dead) (void)process_delayed_recall(context);
		return true;
	}
	return false;
}

/** Apply hazards that belong to every completed side-view player turn. */
static void process_spelunking_environment(enum world_turn_context context)
{
	struct world_spelunk_environment_report report;
	enum world_spelunk_environment_result result;

	if (!world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		return;
	}
	result = world_spelunk_player_process_environment_turn(player, &report);
	if (result == WORLD_SPELUNK_ENVIRONMENT_INVALID) return;
	switch (report.event) {
	case WORLD_SPELUNK_BREATH_SCUBA_USED:
		if (report.air_supply_before > 0 &&
				report.air_supply_after == 0) {
			msg("Your scuba gear runs out of air.");
		}
		break;
	case WORLD_SPELUNK_BREATH_HELD:
		if (report.breath_after == 0) {
			msg("You have exhausted your held breath!");
		} else if (report.breath_after <= 2) {
			msg("You have only %d breath%s left.", report.breath_after,
				PLURAL(report.breath_after));
		}
		break;
	case WORLD_SPELUNK_BREATH_RESTORED:
		if (report.air_supply_after > report.air_supply_before) {
			msg("You reach open air and your scuba gear refills.");
		} else if (report.breath_after > report.breath_before) {
			msg("You reach open air and catch your breath.");
		}
		break;
	case WORLD_SPELUNK_BREATH_DROWNING:
		msg("You are drowning!");
		break;
	case WORLD_SPELUNK_BREATH_NONE:
	default:
		break;
	}
}

/** Housekeeping after the processing of a player command. */
static void process_player_cleanup(void)
{
	enum world_turn_context context = world_turn_context_for_player(player);
	bool top_down = world_turn_context_has_capability(context,
		WORLD_MODE_CAP_NATIVE_CAVE);
	int i;

	/* Significant */
	if (player->upkeep->energy_use) {
		process_spelunking_environment(context);
		player_stamina_process_turn(player);
		if (world_spelunk_player_is_active(player)) {
			(void)world_spelunk_player_sync_resources(player);
		}

		/* Use some energy */
		player->energy -= player->upkeep->energy_use;

		/* Increment the total energy counter */
		player->total_energy += player->upkeep->energy_use;

		/*
		 * Since the player used energy, the command wasn't
		 * canceled.  Therefore allow the bloodlust check on
		 * the player's next command unless this was a background
		 * command and the last player-issued command passed the
		 * bloodlust check but was canceled (skip_cmd_coercion is two
		 * in that case).
		 */
		if (player->skip_cmd_coercion) {
			--player->skip_cmd_coercion;
		}

		/* Player can be damaged by top-down terrain. */
		if (top_down) {
			player_take_terrain_damage(player, player->grid);
		}

		/* Do nothing else if player has auto-dropped stuff */
		if (top_down && !player->upkeep->dropping) {
			/* Constant hallucination */
			if (player->timed[TMD_IMAGE])
				player->upkeep->redraw |= (PR_MAP);

			/* Shimmer multi-hued monsters */
			for (i = 1; i < cave_monster_max(cave); i++) {
				struct monster *mon = cave_monster(cave, i);
				if (!mon->race)
					continue;
				if (!rf_has(mon->race->flags, RF_ATTR_MULTI))
					continue;
				square_light_spot(cave, mon->grid);
			}

			/* Clear NICE flag, and show marked monsters */
			for (i = 1; i < cave_monster_max(cave); i++) {
				struct monster *mon = cave_monster(cave, i);
				mflag_off(mon->mflag, MFLAG_NICE);
				if (mflag_has(mon->mflag, MFLAG_MARK)) {
					if (!mflag_has(mon->mflag, MFLAG_SHOW)) {
						mflag_off(mon->mflag, MFLAG_MARK);
						update_mon(mon, cave, false);
					}
				}
			}
		}
	} else if (player->skip_cmd_coercion > 1) {
		/*
		 * The last command was a backround command executing while
		 * skipping the bloodlust check on the player's next command.
		 * Set skip_cmd_coercion back to one in preparation for the
		 * player's next turn.
		 */
		player->skip_cmd_coercion = 1;
	}

	/* Clear SHOW flag and player drop status. */
	if (top_down) {
		for (i = 1; i < cave_monster_max(cave); i++) {
			struct monster *mon = cave_monster(cave, i);
			mflag_off(mon->mflag, MFLAG_SHOW);
		}
	}
	player->upkeep->dropping = false;

	/* Hack - update needed first because inventory may have changed */
	handle_location_stuff(context);
}


/**
 * Process player commands from the command queue, finishing when there is a
 * command using energy (any regular game command), or we run out of commands
 * and need another from the user, or the character changes level or dies, or
 * the game is stopped.
 *
 * Notice the annoying code to handle "pack overflow", which
 * must come first just in case somebody manages to corrupt
 * the savefiles by clever use of menu commands or something. (Can go? NRM)
 *
 * Notice the annoying code to handle "monster memory" changes,
 * which allows us to avoid having to update the window flags
 * every time we change any internal monster memory field, and
 * also reduces the number of times that the recall window must
 * be redrawn.
 */
void process_player(void)
{
	enum world_turn_context context;

	/* Check for interrupts */
	player_resting_complete_special(player);
	event_signal(EVENT_CHECK_INTERRUPT);

	/* Repeat until energy is reduced */
	do {
		context = world_turn_context_for_player(player);
		if (!world_turn_context_is_valid(context)) break;

		/* Refresh */
		process_location_notice(context);
		handle_location_stuff(context);
		event_signal(EVENT_REFRESH);

		/* Pack overflow currently drops an item onto a top-down grid. */
		if (world_turn_context_has_capability(context,
				WORLD_MODE_CAP_NATIVE_CAVE)) {
			pack_overflow(NULL);
		}

		/* Assume free turn */
		player->upkeep->energy_use = 0;

		/* Dwarves detect treasure */
		if (world_turn_context_has_capability(context,
				WORLD_MODE_CAP_NATIVE_CAVE) &&
				player_has(player, PF_SEE_ORE)) {
			/* Only if they are in good shape */
			if (!player->timed[TMD_IMAGE] &&
				!player->timed[TMD_CONFUSED] &&
				!player->timed[TMD_AMNESIA] &&
				!player->timed[TMD_STUN] &&
				!player->timed[TMD_PARALYZED] &&
				!player->timed[TMD_TERROR] &&
				!player->timed[TMD_AFRAID])
				effect_simple(EF_DETECT_ORE, source_none(), "0", 0, 0, 0, 3, 3, NULL);
		}

		/* Paralyzed or Knocked Out player gets no turn */
		if (player->timed[TMD_PARALYZED] ||
			player_timed_grade_eq(player, TMD_STUN, "Knocked Out")) {
			cmdq_push(CMD_SLEEP);
		}

		/* Prepare for the next command */
		if (cmd_get_nrepeats() > 0) {
			event_signal(EVENT_COMMAND_REPEAT);
		} else {
			/* Check monster recall */
			if (world_turn_context_has_capability(context,
					WORLD_MODE_CAP_NATIVE_CAVE) &&
					player->upkeep->monster_race)
				player->upkeep->redraw |= (PR_MONSTER);

			/* Place cursor on player/target */
			event_signal(EVENT_REFRESH);
		}

		/* Get a command from the queue if there is one */
		if (!cmdq_pop(CTX_GAME))
			break;

		if (!player->upkeep->playing)
			break;

		process_player_cleanup();
	} while (!player->upkeep->energy_use &&
			 !player->is_dead &&
			 !player->upkeep->generate_level);

	/* Notice stuff (if needed). */
	process_location_notice(world_turn_context_for_player(player));
}

/**
 * Housekeeping on arriving on a new level
 */
void on_new_level(void)
{
	const struct level *level = world_player_level(player);
	const struct world_site *site = level ?
		world_site_by_id(level->site_id) : NULL;
	bool first_descent = level && world_level_tracks_dungeon_depth(level) &&
		player->depth > player->max_depth;

	/* Materialize authored shafts for newly generated levels and retrofit the
	 * same stable cells in development saves made before the feature existed. */
	/* Synthetic caves used by focused tests and arenas do not represent an
	 * authored world location.  Only enforce authored-entry materialization
	 * when the chunk identifies the level whose lifecycle is being entered. */
	if (level && world_player_mode_has_capability(player,
			WORLD_MODE_CAP_NATIVE_CAVE) &&
			world_id_is_valid(cave->location_id)) {
		bool matching_location = streq(cave->location_id, level->id);

		assert(matching_location);
		if (matching_location) {
			bool materialized =
				world_entry_materialize_cross_mode_sources(cave, level);
			world_entry_retire_cross_mode_sources(player->cave, level);

			if (materialized && level->is_overworld_cell) {
				materialized = world_entry_materialize_overworld_gates(cave,
					level);
			}

			assert(materialized);
			(void)materialized;
		}
	}

	/* Arena levels are not really a level change */
	if (!player->upkeep->arena_level) {
		/* An ordinary journey teaches its declared directed route only after
		 * arrival at the matching endpoint. */
		(void)world_player_finish_physical_route(player, cave);

		/* Named travel endpoints are discovered by physically reaching one of
		 * their cells, including arrival through ordinary stairs. */
		world_player_activate_travel_nodes_here(player, cave);
		/* A surface region becomes a convenient stop once reached; the fixed
		 * cell is an arrival landmark, not a square the player must touch. */
		(void)world_overworld_activate_current_stop(player);

		/* Play ambient sound on change of level. */
		play_ambient_sound();

		/* Cancel the target */
		target_set_monster(0);

		/* Cancel the health bar */
		health_track(player->upkeep, NULL);
	} else {
		world_player_cancel_physical_route(player);
	}

	/* Disturb */
	disturb(player);

	/* Track maximum player level */
	if (player->max_lev < player->lev)
		player->max_lev = player->lev;

	/* Track maximum dungeon level */
	if (world_level_tracks_dungeon_depth(level) &&
			player->max_depth < player->depth)
		player->max_depth = player->recall_depth = player->depth;

	/* Flush messages */
	event_signal(EVENT_MESSAGE_FLUSH);

	/* Update display */
	event_signal(EVENT_NEW_LEVEL_DISPLAY);

	/* Update player */
	player->upkeep->update |= (PU_BONUS | PU_HP | PU_SPELLS | PU_INVEN);
	player->upkeep->notice |= (PN_COMBINE);
	notice_stuff(player);
	update_stuff(player);
	redraw_stuff(player);

	/* Refresh */
	event_signal(EVENT_REFRESH);

	if (player->upkeep->arena_level) {
		return;
	}

	if (player->upkeep->opening_story_pending && site &&
			site->opening_scene) {
		player->upkeep->opening_story_pending = false;
		event_signal_story(site->opening_scene);
	}
	if (first_descent && level->arrival_message) {
		msg("%s", level->arrival_message);
	}
	if (first_descent && level->arrival_scene) {
		event_signal_story(level->arrival_scene);
	}
	if (player->total_winner && site && site->victory_message) {
		msg("%s", site->victory_message);
	}

	/* Announce (or repeat) the feeling */
	if (player->depth)
		display_feeling(false);

	/* Check the surroundings */
	search(player);

	/* Give player minimum energy to start a new level, but do not reduce
	 * higher value from savefile for level in progress */
	if (player->energy < z_info->move_energy)
		player->energy = z_info->move_energy;
}

/**
 * Housekeeping on leaving a level
 */
void on_leave_level(void)
{
	/* Cancel any command */
	player_clear_timed(player, TMD_COMMAND, false, false);

	/* Don't allow command repeat if moved away from item used. */
	cmd_disable_repeat_floor_item();

	/* Any pending processing */
	notice_stuff(player);
	update_stuff(player);
	redraw_stuff(player);

	/* Flush messages */
	event_signal(EVENT_MESSAGE_FLUSH);
}

/** Housekeeping for a side-view location without touching cave simulation. */
static void on_new_spelunking_location(void)
{
	world_player_cancel_physical_route(player);
	play_ambient_sound();
	target_set_monster(0);
	health_track(player->upkeep, NULL);
	disturb(player);
	if (player->max_lev < player->lev) player->max_lev = player->lev;
	/* Derived player state is not serialized.  A normal top-down arrival
	 * rebuilds it in on_new_level(); side-view resume must do the same or a
	 * loaded player retains zero speed and appears as Slow (-110). */
	player->upkeep->update |= (PU_BONUS | PU_HP | PU_SPELLS | PU_INVEN);
	player->upkeep->notice |= PN_COMBINE;
	notice_stuff(player);
	update_stuff(player);
	redraw_stuff(player);
	if (player->energy < z_info->move_energy) {
		player->energy = z_info->move_energy;
	}
	event_signal(EVENT_MESSAGE_FLUSH);
	event_signal(EVENT_NEW_LEVEL_DISPLAY);
	event_signal(EVENT_REFRESH);
}

/** Route initial load and committed arrival through the active location mode. */
bool on_enter_world_location(void)
{
	enum world_turn_context context = world_turn_context_for_player(player);

	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		on_new_level();
		return true;
	}
	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_SIDE_VIEW)) {
		on_new_spelunking_location();
		return true;
	}
	return false;
}

/** Run inherited top-down source housekeeping after a route commits. */
static void top_down_leave_source(
		const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		void *user)
{
	(void)transition;
	(void)user;
	(void)actual;
	(void)known;
	(void)p;
	assert(p == player);
	assert(actual == cave);
	assert(known == p->cave);
	on_leave_level();
}

/** Run inherited top-down arrival housekeeping after publication. */
void world_top_down_enter_destination(
		const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		bool fast_travel)
{
	(void)transition;
	(void)known;
	assert(p == player);
	assert(actual == cave);
	assert(known == p->cave);
	if (fast_travel) {
		/* Fast travel must never masquerade as first physical discovery. */
		world_player_cancel_physical_route(player);
	}
	if (actual->turn < turn) {
		/* Restore only bounded inactive-level state; do not replay turns. */
		restore_monsters();
		age_scent();
		forget_noise();
	}
	p->upkeep->autosave = true;
	on_new_level();
}

static void top_down_enter_destination(
		const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		void *user)
{
	(void)user;
	world_top_down_enter_destination(transition, actual, known, p, true);
}

static void top_down_enter_physical_destination(
		const struct world_transition *transition,
		struct chunk *actual, struct chunk *known, struct player *p,
		void *user)
{
	(void)user;
	world_top_down_enter_destination(transition, actual, known, p, false);
}

/** Lifecycle provider for inherited top-down route transitions. */
const struct world_destination_lifecycle *world_top_down_route_lifecycle(void)
{
	static const struct world_destination_lifecycle lifecycle = {
		.leave_source = top_down_leave_source,
		.enter_destination = top_down_enter_destination
	};

	return &lifecycle;
}

/** Lifecycle for an ordinary edge crossing which teaches its directed route. */
const struct world_destination_lifecycle *
	world_top_down_physical_route_lifecycle(void)
{
	static const struct world_destination_lifecycle lifecycle = {
		.leave_source = top_down_leave_source,
		.enter_destination = top_down_enter_physical_destination
	};

	return &lifecycle;
}


/**
 * The main game loop.
 *
 * This function will run until the player needs to enter a command, or closes
 * the game, or the character dies.
 */
void run_game_loop(void)
{
	/* Tidy up after the player's command */
	process_player_cleanup();
	if (!world_player_mode_is_valid(player)) {
		return;
	}

	/* Keep processing the player until they use some energy or
	 * another command is needed */
	while (player->upkeep->playing) {
		process_deferred_store_services();
		process_player();
		if (player->upkeep->energy_use)
			break;
		else
			return;
	}

	/* The player may still have enough energy to move, so we run another
	 * player turn before processing the rest of the world */
	while (player->energy >= z_info->move_energy) {
		/* Do any necessary animations */
		event_signal(EVENT_ANIMATE);
		
		/* Process actors with even more energy first. */
		if (!process_location_actors(player->energy + 1)) return;
		if (player->is_dead || !player->upkeep->playing ||
			player->upkeep->generate_level)
			break;

		/* Process the player until they use some energy */
		while (player->upkeep->playing) {
			process_deferred_store_services();
			process_player();
			if (player->upkeep->energy_use)
				break;
			else
				return;
		}
	}

	/* Now that the player's turn is fully complete, we run the main loop 
	 * until player input is needed again */
	while (true) {
		enum world_turn_context context =
			world_turn_context_for_player(player);

		if (!world_turn_context_is_valid(context)) return;
		process_location_notice(context);
		handle_location_stuff(context);
		event_signal(EVENT_REFRESH);

		/* Process the rest of the world, give the player energy and 
		 * increment the turn counter unless we need to stop playing or
		 * generate a new level */
		if (player->is_dead || !player->upkeep->playing)
			return;
		else if (!player->upkeep->generate_level) {
			/* Process the rest of the location's actors. */
			if (!process_location_actors(0)) return;

			/* Mark local actors as ready for the next energy cycle. */
			if (!reset_location_actors()) return;

			/* Refresh */
			context = world_turn_context_for_player(player);
			if (!world_turn_context_is_valid(context)) return;
			process_location_notice(context);
			handle_location_stuff(context);
			event_signal(EVENT_REFRESH);
			if (player->is_dead || !player->upkeep->playing)
				return;

			/* Process the world every ten turns */
			if (!(turn % 10) && !player->upkeep->generate_level) {
				if (!process_location_world()) return;

				/* Refresh */
				context = world_turn_context_for_player(player);
				if (!world_turn_context_is_valid(context)) return;
				process_location_notice(context);
				handle_location_stuff(context);
				event_signal(EVENT_REFRESH);
				if (player->is_dead || !player->upkeep->playing)
					return;
			}

			/* Give the player some energy */
			player->energy += turn_energy(player->state.speed);

			/* Count game turns */
			turn++;
		}

		/* Make a new level if requested */
		if (player->upkeep->generate_level) {
			bool arena = false;

			/* Legacy depth changes cannot cross or replace another location
			 * mode.  Cross-mode movement has its own transaction boundary. */
			if (!world_turn_context_has_capability(context,
					WORLD_MODE_CAP_LEGACY_LEVEL_CHANGE)) {
				player->upkeep->generate_level = false;
				return;
			}
			if (character_dungeon) {
				on_leave_level();
				if (cave->name && streq(cave->name, "arena")) {
					arena = true;
				}
			}

			prepare_next_level(player);
			if (!on_enter_world_location()) return;

			player->upkeep->generate_level = false;

			/* Kill arena monster */
			if (arena) {
				player->upkeep->arena_level = false;
				if (player->upkeep->health_who) {
					kill_arena_monster(player->upkeep->health_who);
				}
			}
		}

		/* If the player has enough energy to move they now do so, after
		 * any monsters with more energy take their turns */
		while (player->energy >= z_info->move_energy) {
			/* Do any necessary animations */
			event_signal(EVENT_ANIMATE);

			/* Process actors with even more energy first. */
			if (!process_location_actors(player->energy + 1)) return;
			if (player->is_dead || !player->upkeep->playing ||
				player->upkeep->generate_level)
				break;

			/* Process the player until they use some energy */
			while (player->upkeep->playing) {
				process_deferred_store_services();
				process_player();
				if (player->upkeep->energy_use)
					break;
				else
					return;
			}
		}
	}
}


/**
 * Recompute the noise, from the player, that monsters can use to track the
 * player.
 *
 * \param p is the player making the noise.
 * \param origin will, if not NULL, cause the position in *origin to be
 * used as the source of the noise rather than p->grid.
 * \param falloff will, if not NULL, cause the falloff with an additional
 * grid of distance to be *falloff rather than a value dependent on the current
 * state in p.
 *
 * Every turn, the character makes enough noise that nearby monsters can use
 * it to home in.
 *
 * This function actually just computes distance from the player; this is
 * used in combination with the player's stealth value to determine what
 * monsters can hear.  We mark the player's grid with 0, then fill in the noise
 * field of every grid that the player can reach with that "noise"
 * (actally distance) plus the number of steps needed to reach that grid
 * - so higher values mean further from the player.
 *
 * Monsters use this information by moving to adjacent grids with lower noise
 * values, thereby homing in on the player even though twisty tunnels and
 * mazes.  Monsters have a hearing value, which is the largest sound value
 * they can detect.
 */
void make_noise(struct player *p, const struct loc *origin,
		const uint16_t *falloff)
{
	struct loc next = (origin) ? *origin: p->grid;
	int d;
	uint16_t noise = 0;
	uint16_t noise_increment = (falloff)
		? *falloff : (p->timed[TMD_COVERTRACKS] ? 4 : 1);
	struct queue *queue = q_new(cave->height * cave->width);

	/*
	 * Remember the source of the noise and falloff so it can be
	 * recomputed if the game is saved and reloaded.
	 */
	p->noise_grid = next;
	p->noise_falloff = noise_increment;

	/* Set all the grids to silence */
	forget_noise();

	/* Player makes noise */
	cave->noise.grids[next.y][next.x] = noise;
	q_push_int(queue, grid_to_i(next, cave->width));
	noise += noise_increment;

	/* Propagate noise */
	while (q_len(queue) > 0) {
		/* Get the next grid */
		i_to_grid(q_pop_int(queue), cave->width, &next);

		/* If we've reached the current noise level, put it back and step */
		if (cave->noise.grids[next.y][next.x] == noise) {
			q_push_int(queue, grid_to_i(next, cave->width));
			noise += noise_increment;
			continue;
		}

		/* Assign noise to the children and enqueue them */
		for (d = 0; d < 8; d++)	{
			/* Child location */
			struct loc grid = loc_sum(next, ddgrid_ddd[d]);

			if (!square_in_bounds(cave, grid)) continue;

			/* Ignore features that don't transmit sound */
			if (square_isnoflow(cave, grid)) continue;

			/* Skip grids that already have noise */
			if (cave->noise.grids[grid.y][grid.x] != 0) continue;

			/* Skip the player grid */
			if (loc_eq(p->grid, grid)) continue;

			/* Save the noise */
			cave->noise.grids[grid.y][grid.x] = noise;

			/* Enqueue that entry */
			q_push_int(queue, grid_to_i(grid, cave->width));
		}
	}

	q_free(queue);
}


/**
 * Remove all noise caused by the player.
 */
void forget_noise(void)
{
	int y, x;

	for (y = 1; y < cave->height - 1; y++) {
		for (x = 1; x < cave->width - 1; x++) {
			cave->noise.grids[y][x] = 0;
		}
	}
}


/**
 * Fast-forward player caused scent for a cave restored from the chunk list.
 */
void age_scent(void)
{
	/*
	 * Match how run_game_loop() (by calling process_world() and then
	 * update_scent()) increments the scent by one whenever the turn
	 * counter is a multiple of 10.
	 */
	int inc = (turn / 10) - (cave->turn / 10);
	int y, x;

	/* Not prepared to handle if the turn counter wrapped. */
	assert(inc >= 0);

	for (y = 1; y < cave->height - 1; y++) {
		for (x = 1; x < cave->width - 1; x++) {
			if (cave->scent.grids[y][x] > 0) {
				if (inc <= 65535 && cave->scent.grids[y][x] <=
						(uint16_t)(65535 - inc)) {
					cave->scent.grids[y][x] += inc;
				} else {
					/*
					 * The scent has faded to the point
					 * that it is as if it was never there.
					 */
					cave->scent.grids[y][x] = 0;
				}
			}
		}
	}
}
