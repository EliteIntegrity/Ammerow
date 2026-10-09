/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file load.c
 * \brief Individual loading functions
 *
 * Copyright (c) 1997 Ben Harrison, and others
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
#include "cave.h"
#include "effects.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-group.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "monster.h"
#include "obj-curse.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-init.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-randart.h"
#include "obj-slays.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player-calcs.h"
#include "player-resource.h"
#include "player-history.h"
#include "player-quest.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "savefile.h"
#include "store.h"
#include "trap.h"
#include "ui-term.h"
#include "world-overworld.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-system.h"
#include "world-spelunking-system-data.h"
#include "world-spelunking-layout.h"
#include "world-spelunking-layout-data.h"
#include "world-spelunking-recipe-data.h"

static const struct world_spelunk_perception *spelunking_perception_for_load(
		const struct world_spelunk_runtime *runtime, const char *recipe_id)
{
	const struct world_spelunk_recipe_definition *recipe = recipe_id &&
		recipe_id[0] ? world_spelunk_recipe_definition_by_id(recipe_id) : NULL;
	const struct world_spelunk_layout_definition *layout;

	if (recipe_id && recipe_id[0]) return recipe ? &recipe->perception : NULL;
	if (!runtime) return NULL;
	layout = world_spelunk_layout_definition_by_id(runtime->location_id);
	/* Empty recipe IDs are the legacy/synthetic one-runtime form. Production
	 * handcrafted IDs resolve above; an unknown test ID retains the codec's
	 * validated compatibility profile. */
	return layout ? &layout->perception : &runtime->perception;
}

/** Read player-owned physical resources.  Derived maxima are reconciled by
 * the ordinary bonus calculation after the complete save is loaded. */
int rd_player_resources(void)
{
	uint16_t stamina;
	uint16_t max_stamina;
	uint16_t air;
	uint16_t max_air;

	rd_u16b(&stamina);
	rd_u16b(&max_stamina);
	rd_u16b(&air);
	rd_u16b(&max_air);
	if (!max_stamina || max_stamina > INT16_MAX || stamina > max_stamina ||
			!max_air || max_air > INT16_MAX || air > max_air) {
		return -1;
	}
	player->resources.stamina.current = (int16_t)stamina;
	player->resources.stamina.maximum = (int16_t)max_stamina;
	player->resources.air.current = (int16_t)air;
	player->resources.air.maximum = (int16_t)max_air;
	player->resources.initialized = true;
	return 0;
}

/**
 * Setting this to 1 and recompiling gives a chance to recover a savefile 
 * where the object list has become corrupted.  Don't forget to reset to 0
 * and recompile again as soon as the savefile is viable again.
 */
#define OBJ_RECOVER 0

/**
 * Dungeon constants
 */
static uint8_t square_size = 0;

/**
 * Player constants
 */
static uint8_t hist_size = 0;

/**
 * Object constants
 */
static uint8_t obj_mod_max = 0;
static uint8_t of_size = 0;
static uint8_t elem_max = 0;
static uint8_t brand_max;
static uint8_t slay_max;
static uint8_t curse_max;

/**
 * Monster constants
 */
static uint8_t mflag_size = 0;

/**
 * Trap constants
 */
static uint8_t trf_size = 0;

/**
 * Shorthand function pointer for rd_item version
 */
typedef struct object *(*rd_item_t)(void);

/**
 * Read an object.
 */
static struct object *rd_item(void)
{
	struct object *obj = object_new();

	uint8_t tmp8u;
	uint16_t tmp16u;
	uint8_t effect;
	size_t i;
	char buf[128];
	uint8_t ver = 1;

	rd_u16b(&tmp16u);
	rd_byte(&ver);
	if (tmp16u != 0xffff)
		return NULL;

	rd_u16b(&obj->oidx);

	/* Location */
	rd_byte(&tmp8u);
	obj->grid.y = tmp8u;
	rd_byte(&tmp8u);
	obj->grid.x = tmp8u;

	/* Type/Subtype */
	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->tval = tval_find_idx(buf);
	}
	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->sval = lookup_sval(obj->tval, buf);
	}
	rd_s16b(&obj->pval);

	rd_byte(&obj->number);
	rd_s16b(&obj->weight);

	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->artifact = lookup_artifact_name(buf);
		if (!obj->artifact) {
			note(format("Couldn't find artifact %s!", buf));
			object_delete(NULL, NULL, &obj);
			return NULL;
		}
	}
	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->ego = lookup_ego_item(buf, obj->tval, obj->sval);
		if (!obj->ego) {
			note(format("Couldn't find ego item %s!", buf));
			object_delete(NULL, NULL, &obj);
			return NULL;
		}
	}
	rd_byte(&effect);

	rd_s16b(&obj->timeout);

	rd_s16b(&obj->to_h);
	rd_s16b(&obj->to_d);
	rd_s16b(&obj->to_a);

	rd_s16b(&obj->ac);

	rd_byte(&obj->dd);
	rd_byte(&obj->ds);

	rd_byte(&obj->origin);
	rd_byte(&obj->origin_depth);
	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		obj->origin_race = lookup_monster(buf);
	}
	rd_byte(&obj->notice);

	for (i = 0; i < of_size; i++)
		rd_byte(&obj->flags[i]);

	for (i = 0; i < obj_mod_max; i++) {
		rd_s16b(&obj->modifiers[i]);
	}

	/* Read brands */
	rd_byte(&tmp8u);
	if (tmp8u) {
		obj->brands = mem_zalloc(z_info->brand_max * sizeof(bool));
		for (i = 0; i < brand_max; i++) {
			rd_byte(&tmp8u);
			obj->brands[i] = tmp8u ? true : false;
		}
	}

	/* Read slays */
	rd_byte(&tmp8u);
	if (tmp8u) {
		obj->slays = mem_zalloc(z_info->slay_max * sizeof(bool));
		for (i = 0; i < slay_max; i++) {
			rd_byte(&tmp8u);
			obj->slays[i] = tmp8u ? true : false;
		}
	}

	/* Read curses */
	rd_byte(&tmp8u);
	if (tmp8u) {
		obj->curses = mem_zalloc(z_info->curse_max * sizeof(struct curse_data));
		for (i = 0; i < curse_max; i++) {
			rd_byte(&tmp8u);
			obj->curses[i].power = tmp8u;
			rd_u16b(&tmp16u);
			obj->curses[i].timeout = tmp16u;
		}
	}

	for (i = 0; i < elem_max; i++) {
		rd_s16b(&obj->el_info[i].res_level);
		rd_byte(&obj->el_info[i].flags);
	}

	/* Monster holding object */
	rd_s16b(&obj->held_m_idx);

	rd_s16b(&obj->mimicking_m_idx);

	/* Activation */
	rd_u16b(&tmp16u);
	if (tmp16u)
		obj->activation = &activations[tmp16u];
	rd_u16b(&tmp16u);
	obj->time.base = tmp16u;
	rd_u16b(&tmp16u);
	obj->time.dice = tmp16u;
	rd_u16b(&tmp16u);
	obj->time.sides = tmp16u;

	/* Save the inscription */
	rd_string(buf, sizeof(buf));
	if (buf[0]) obj->note = quark_add(buf);

	/* Lookup item kind */
	obj->kind = lookup_kind(obj->tval, obj->sval);

	/* Check we have a kind */
	if ((!obj->tval && !obj->sval) || !obj->kind) {
		object_delete(NULL, NULL, &obj);
		return NULL;
	}

	/* Set effect */
	if (effect)
		obj->effect = obj->kind->effect;

	/* Success */
	return obj;
}


/**
 * Read a monster
 */
static bool rd_monster(struct chunk *c, struct monster *mon)
{
	uint8_t tmp8u;
	uint16_t tmp16u;
	char race_name[80];
	size_t j;
	bool delete = false;

	/* Read the monster race */
	rd_u16b(&tmp16u);
	mon->midx = tmp16u;
	rd_string(race_name, sizeof(race_name));
	mon->race = lookup_monster(race_name);
	if (!mon->race) {
		note(format("Monster race %s no longer exists!", race_name));
		return false;
	}
	rd_string(race_name, sizeof(race_name));
	if (streq(race_name, "none")) {
		mon->original_race = NULL;
	} else {
		mon->original_race = lookup_monster(race_name);
	}

	/* Read the other information */
	rd_byte(&tmp8u);
	mon->grid.y = tmp8u;
	rd_byte(&tmp8u);
	mon->grid.x = tmp8u;
	rd_s16b(&mon->hp);
	rd_s16b(&mon->maxhp);
	rd_byte(&mon->mspeed);
	rd_byte(&mon->energy);
	rd_byte(&tmp8u);

	for (j = 0; j < tmp8u; j++)
		rd_s16b(&mon->m_timed[j]);

	/* Read and extract the flag */
	for (j = 0; j < mflag_size; j++)
		rd_byte(&mon->mflag[j]);

	for (j = 0; j < of_size; j++)
		rd_byte(&mon->known_pstate.flags[j]);

	for (j = 0; j < elem_max; j++)
		rd_s16b(&mon->known_pstate.el_info[j].res_level);

	rd_u16b(&tmp16u);

	if (tmp16u) {
		/* Find and set the mimicked object */
		struct object *square_obj = square_object(c, mon->grid);

		/* Try and find the mimicked object; if we fail, delete the monster */
		while (square_obj) {
			if (square_obj->mimicking_m_idx == tmp16u) break;
			square_obj = square_obj->next;
		}
		if (square_obj) {
			mon->mimicked_obj = square_obj;
		} else {
			delete = true;
		}
	}

	/* Read all the held objects (order is unimportant) */
	while (true) {
		struct object *obj = rd_item();
		if (!obj)
			break;

		pile_insert(&mon->held_obj, obj);
		assert(obj->oidx);
		assert(c->objects[obj->oidx] == NULL);
		c->objects[obj->oidx] = obj;
	}

	/* Read group info */
	rd_u16b(&tmp16u);
	mon->group_info[PRIMARY_GROUP].index = tmp16u;
	rd_byte(&tmp8u);
	mon->group_info[PRIMARY_GROUP].role = tmp8u;
	rd_u16b(&tmp16u);
	mon->group_info[SUMMON_GROUP].index = tmp16u;
	rd_byte(&tmp8u);
	mon->group_info[SUMMON_GROUP].role = tmp8u;

	/* Now delete the monster if necessary */
	if (delete) {
		delete_monster(c, mon->grid);
	}

	return true;
}


/**
 * Read a trap record
 */
static void rd_trap(struct trap *trap)
{
	int i;
	uint8_t tmp8u;
	char buf[80];

	rd_string(buf, sizeof(buf));
	if (buf[0]) {
		trap->kind = lookup_trap(buf);
		trap->t_idx = trap->kind->tidx;
	}
	rd_byte(&tmp8u);
	trap->grid.y = tmp8u;
	rd_byte(&tmp8u);
	trap->grid.x = tmp8u;
	rd_byte(&trap->power);
	rd_byte(&trap->timeout);

	for (i = 0; i < trf_size; i++)
		rd_byte(&trap->flags[i]);
}

/** Read and validate a versioned gameplay RNG snapshot. */
int rd_randomizer(void)
{
	struct randomizer_state state;
	int i;
	uint32_t word_count;

	rd_u32b(&state.algorithm);
	rd_u32b(&word_count);
	if (word_count != RAND_STATE_WORDS) {
		note("Savefile uses an unsupported randomizer state size.");
		return -1;
	}
	for (i = 0; i < RAND_STATE_WORDS; i++) rd_u32b(&state.words[i]);
	if (!Rand_state_import(&state)) {
		note("Savefile uses an unsupported or invalid randomizer.");
		return -1;
	}

	return 0;
}


/**
 * Read options.
 */
int rd_options(void)
{
	uint8_t b;

	/*** Special info */

	/* Read "delay_factor" */
	rd_byte(&b);
	player->opts.delay_factor = b;

	/* Read "hitpoint_warn" */
	rd_byte(&b);
	player->opts.hitpoint_warn = b;

	/* Read lazy movement delay */
	rd_byte(&b);
	player->opts.lazymove_delay = b;

	/* Read sidebar mode (if it's an actual game) */
	if (angband_term[0]) {
		rd_byte(&b);
		if (b >= SIDEBAR_MAX) b = SIDEBAR_LEFT;
		SIDEBAR_MODE = b;
	} else {
		strip_bytes(1);
	}


	/* Read options */
	while (1) {
		uint8_t value;
		char name[40];
		rd_string(name, sizeof name);

		if (!name[0])
			break;

		rd_byte(&value);
		option_set(name, !!value);
	}

	return 0;
}

/**
 * Read the saved messages
 */
int rd_messages(void)
{
	int i;
	char buf[128];
	uint16_t tmp16u;

	int16_t num;

	/* Total */
	rd_s16b(&num);

	/* Read the messages */
	for (i = 0; i < num; i++) {
		/* Read the message */
		rd_string(buf, sizeof(buf));

		/* Read the message type */
		rd_u16b(&tmp16u);

		/* Save the message */
		message_add(buf, tmp16u);
	}

	return 0;
}

/**
 * Read monster memory.
 */
int rd_monster_memory(void)
{
	uint16_t nkill, ntheft;
	char buf[128];
	int i;

	/* Monster temporary flags */
	rd_byte(&mflag_size);

	/* Incompatible save files */
	if (mflag_size > MFLAG_SIZE) {
	        note(format("Too many (%u) monster temporary flags!", mflag_size));
		return (-1);
	}

	/* Reset maximum numbers per level */
	for (i = 1; z_info && i < z_info->r_max; i++) {
		struct monster_race *race = &r_info[i];
		race->max_num = 100;
		if (rf_has(race->flags, RF_UNIQUE))
			race->max_num = 1;
	}

	rd_string(buf, sizeof(buf));
	while (!streq(buf, "No more monsters")) {
		struct monster_race *race = lookup_monster(buf);

		/* Get the kill and theft counts, skip if monster invalid */
		rd_u16b(&nkill);
		rd_u16b(&ntheft);
		if (!race) continue;

		/* Store the kill count, ensure dead uniques stay dead */
		l_list[race->ridx].pkills = nkill;
		if (rf_has(race->flags, RF_UNIQUE) && nkill)
			race->max_num = 0;

		/* Store the theft count */
		l_list[race->ridx].thefts = ntheft;

		/* Look for the next monster */
		rd_string(buf, sizeof(buf));
	}

	return 0;
}


int rd_object_memory(void)
{
	size_t i;
	uint16_t tmp16u;

	/* Object Memory */
	rd_u16b(&tmp16u);
	if (tmp16u > z_info->k_max) {
		note(format("Too many (%u) object kinds!", tmp16u));
		return (-1);
	}

	/* Object flags */
	rd_byte(&of_size);
	if (of_size > OF_SIZE) {
	        note(format("Too many (%u) object flags!", of_size));
		return (-1);
	}

	/* Object modifiers */
	rd_byte(&obj_mod_max);
	if (obj_mod_max > OBJ_MOD_MAX) {
	        note(format("Too many (%u) object modifiers allowed!",
						obj_mod_max));
		return (-1);
	}

	/* Elements */
	rd_byte(&elem_max);
	if (elem_max > ELEM_MAX) {
	        note(format("Too many (%u) elements allowed!", elem_max));
		return (-1);
	}

	/* Brands */
	rd_byte(&brand_max);
	if (brand_max > z_info->brand_max) {
	        note(format("Too many (%u) brands allowed!", brand_max));
		return (-1);
	}

	/* Slays */
	rd_byte(&slay_max);
	if (slay_max > z_info->slay_max) {
	        note(format("Too many (%u) slays allowed!", slay_max));
		return (-1);
	}

	/* Curses */
	rd_byte(&curse_max);
	if (curse_max > z_info->curse_max) {
	        note(format("Too many (%u) curses allowed!", curse_max));
		return (-1);
	}

	/* Read the kind knowledge */
	for (i = 0; i < tmp16u; i++) {
		uint8_t tmp8u;
		struct object_kind *kind = &k_info[i];

		rd_byte(&tmp8u);

		kind->aware = (tmp8u & 0x01) ? true : false;
		kind->tried = (tmp8u & 0x02) ? true : false;
		kind->everseen = (tmp8u & 0x08) ? true : false;

		if (tmp8u & 0x04) kind_ignore_when_aware(kind);
		if (tmp8u & 0x10) kind_ignore_when_unaware(kind);
	}

	return 0;
}



int rd_quests(void)
{
	int i;
	uint16_t tmp16u;

	/* Load the Quests */
	rd_u16b(&tmp16u);
	if (tmp16u > z_info->quest_max) {
		note(format("Too many (%u) quests!", tmp16u));
		return (-1);
	}

	/* Load the Quests */
	player_quests_reset(player);
	for (i = 0; i < tmp16u; i++) {
		uint16_t cur_num;
		rd_byte(&player->quests[i].level);
		rd_u16b(&cur_num);
		player->quests[i].cur_num = cur_num;
	}

	return 0;
}


/**
 * Read the player information
 */
int rd_player(void)
{
	int i;
	uint8_t tmp8u, num;
	uint8_t stat_max = 0;
	char buf[80];
	struct player_class *c;

	rd_string(player->full_name, sizeof(player->full_name));
	rd_string(player->died_from, 80);
	player->history = mem_zalloc(250);
	rd_string(player->history, 250);

	/* Player race */
	rd_string(buf, sizeof(buf));
	player->race = player_name2race(buf);

	/* Verify player race */
	if (!player->race) {
		note(format("Invalid player race (%s).", buf));
		return -1;
	}

	/* Player shape */
	rd_string(buf, sizeof(buf));
	player->shape = lookup_player_shape(buf);

	/* If no player shape recorded, set to normal and hope for the best */
	if (!player->shape) {
		note(format("Invalid player shape (%s).", buf));
		return -1;
	}

	/* Player class */
	rd_string(buf, sizeof(buf));
	for (c = classes; c; c = c->next) {
		if (streq(c->name, buf)) {
			player->class = c;
			break;
		}
	}

	if (!player->class) {
		note(format("Invalid player class (%s).", buf));
		return -1;
	}

	/* Numeric name suffix */
	rd_byte(&player->opts.name_suffix);

	/* Special Race/Class info */
	rd_byte(&player->hitdie);
	rd_byte(&player->expfact);

	/* Age/Height/Weight */
	rd_s16b(&player->age);
	rd_s16b(&player->ht);
	rd_s16b(&player->wt);

	/* Read the stat info */
	rd_byte(&stat_max);
	if (stat_max > STAT_MAX) {
		note(format("Too many stats (%d).", stat_max));
		return -1;
	}

	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_max[i]);
	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_cur[i]);
	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_map[i]);
	for (i = 0; i < stat_max; i++) rd_s16b(&player->stat_birth[i]);

	rd_s16b(&player->ht_birth);
	rd_s16b(&player->wt_birth);
	strip_bytes(2);
	rd_s32b(&player->au_birth);

	/* Player body */
	rd_string(buf, sizeof(buf));
	player->body.name = string_make(buf);
	rd_u16b(&player->body.count);
	if (player->body.count > z_info->equip_slots_max) {
		note(format("Too many (%u) body parts!", player->body.count));
		return (-1);
	}

	player->body.slots = mem_zalloc(player->body.count *
									sizeof(struct equip_slot));
	for (i = 0; i < player->body.count; i++) {
		rd_u16b(&player->body.slots[i].type);
		rd_string(buf, sizeof(buf));
		player->body.slots[i].name = string_make(buf);
	}

	strip_bytes(4);

	rd_s32b(&player->au);

	rd_s32b(&player->max_exp);
	rd_s32b(&player->exp);
	rd_u16b(&player->exp_frac);

	rd_s16b(&player->lev);

	/* Verify player level */
	if ((player->lev < 1) || (player->lev > PY_MAX_LEVEL)) {
		note(format("Invalid player level (%d).", player->lev));
		return (-1);
	}

	rd_s16b(&player->mhp);
	rd_s16b(&player->chp);
	rd_u16b(&player->chp_frac);

	rd_s16b(&player->msp);
	rd_s16b(&player->csp);
	rd_u16b(&player->csp_frac);

	rd_s16b(&player->max_lev);
	rd_s16b(&player->max_depth);
	rd_s16b(&player->recall_depth);

	/* Repair maximum player level */
	if (player->max_lev < player->lev) player->max_lev = player->lev;

	/* Repair maximum dungeon level */
	if (player->max_depth < 0) player->max_depth = 1;
	if (player->recall_depth <= 0) player->recall_depth = player->max_depth;

	/* Reset cause of death */
	if (player->chp >= 0)
		my_strcpy(player->died_from, "(alive and well)",
				  sizeof(player->died_from));

	/* More info */
	rd_byte(&tmp8u);
	player->old_grid.y = tmp8u;
	rd_byte(&tmp8u);
	player->old_grid.x = tmp8u;
	rd_u16b(&player->noise_falloff);
	rd_byte(&tmp8u);
	player->noise_grid.y = tmp8u;
	rd_byte(&tmp8u);
	player->noise_grid.x = tmp8u;
	rd_byte(&player->skip_cmd_coercion);
	rd_byte(&player->unignoring);
	rd_s16b(&player->deep_descent);

	/* Read the flags */
	rd_s16b(&player->energy);
	rd_s16b(&player->word_recall);

	/* Find the number of timed effects */
	rd_byte(&num);

	if (num <= TMD_MAX) {
		/* Read all the effects */
		for (i = 0; i < num; i++)
			rd_s16b(&player->timed[i]);

		/* Initialize any entries not read */
		if (num < TMD_MAX)
			memset(player->timed + num, 0, (TMD_MAX - num) * sizeof(int16_t));
	} else {
		/* Probably in trouble anyway */
		for (i = 0; i < TMD_MAX; i++)
			rd_s16b(&player->timed[i]);

		/* Discard unused entries */
		strip_bytes(2 * (num - TMD_MAX));
		note("Discarded unsupported timed effects");
	}

	/* Total energy used so far */
	rd_u32b(&player->total_energy);
	/* # of turns spent resting */
	rd_u32b(&player->resting_turn);

	/* Future use */
	strip_bytes(32);

	return 0;
}


/**
 * Read ignore and autoinscription submenu for all known objects
 */
int rd_ignore(void)
{
	size_t i, j;
	uint8_t tmp8u = 24;
	uint16_t file_e_max;
	uint16_t itype_size;
	uint16_t inscriptions;

	/* Read how many ignore bytes we have */
	rd_byte(&tmp8u);

	/* Check against current number */
	if (tmp8u != ignore_size) {
		strip_bytes(tmp8u);
	} else {
		for (i = 0; i < ignore_size; i++)
			rd_byte(&ignore_level[i]);
	}

	/* Read the number of saved ego-item */
	rd_u16b(&file_e_max);
	rd_u16b(&itype_size);
	if (itype_size > ITYPE_SIZE) {
		note(format("Too many (%u) ignore bytes!", itype_size));
		return (-1);
	}

	for (i = 0; i < file_e_max; i++) {
		if (i < z_info->e_max) {
			bitflag flags, itypes[ITYPE_SIZE];
			
			/* Read and extract the everseen flag */
			rd_byte(&flags);
			e_info[i].everseen = (flags & 0x02) ? true : false;

			/* Read and extract the ignore flags */
			for (j = 0; j < itype_size; j++)
				rd_byte(&itypes[j]);

			/* If number of ignore types has changed, don't set anything */
			if (itype_size == ITYPE_SIZE) {
				for (j = ITYPE_NONE; j < ITYPE_MAX; j++)
					if (itype_has(itypes, j))
						ego_ignore_toggle(i, j);
			}
		}
	}

	/* Read the current number of aware object auto-inscriptions */
	rd_u16b(&inscriptions);

	/* Read the aware object autoinscriptions array */
	for (i = 0; i < inscriptions; i++) {
		char tmp[80];
		uint8_t tval, sval;
		struct object_kind *k;

		rd_string(tmp, sizeof(tmp));
		tval = tval_find_idx(tmp);
		rd_string(tmp, sizeof(tmp));
		sval = lookup_sval(tval, tmp);
		k = lookup_kind(tval, sval);
		if (!k)
			quit_fmt("lookup_kind(%d, %d) failed", tval, sval);
		rd_string(tmp, sizeof(tmp));
		k->note_aware = quark_add(tmp);
	}

	/* Read the current number of unaware object auto-inscriptions */
	rd_u16b(&inscriptions);

	/* Read the unaware object autoinscriptions array */
	for (i = 0; i < inscriptions; i++) {
		char tmp[80];
		uint8_t tval, sval;
		struct object_kind *k;

		rd_string(tmp, sizeof(tmp));
		tval = tval_find_idx(tmp);
		rd_string(tmp, sizeof(tmp));
		sval = lookup_sval(tval, tmp);
		k = lookup_kind(tval, sval);
		if (!k)
			quit_fmt("lookup_kind(%d, %d) failed", tval, sval);
		rd_string(tmp, sizeof(tmp));
		k->note_unaware = quark_add(tmp);
	}

	/* Read the current number of rune auto-inscriptions */
	rd_u16b(&inscriptions);

	/* Read the rune autoinscriptions array */
	for (i = 0; i < inscriptions; i++) {
		char tmp[80];
		int16_t runeid;

		rd_s16b(&runeid);
		rd_string(tmp, sizeof(tmp));
		rune_set_note(runeid, tmp);
	}

	return 0;
}


int rd_misc(void)
{
	size_t i;
	uint8_t tmp8u;
	
	/* Read the randart seed */
	rd_u32b(&seed_randart);

	/* Read the flavors seed */
	rd_u32b(&seed_flavor);
	flavor_init();

	/* Special stuff */
	rd_u16b(&player->total_winner);
	rd_u16b(&player->noscore);


	/* Read "death" */
	rd_byte(&tmp8u);
	player->is_dead = tmp8u;

	/* Current turn */
	rd_s32b(&turn);

	//if (player->is_dead)
	//	return 0;

	/* Reviewed generated identities are the only release-safe artifact mode. */
	player->opts.opt[OPT_birth_randarts] = true;
	if (randart_file_exists()) {
		cleanup_parser(&artifact_parser);
		activate_randart_file();
		if (run_parser(&randart_parser)) {
			quit("Could not parse random artifacts.");
		}
	} else {
		do_randart(seed_randart, true);
	}
	deactivate_randart_file();

	/* Property knowledge */
	/* Flags */
	for (i = 0; i < OF_SIZE; i++)
		rd_byte(&player->obj_k->flags[i]);

	/* Modifiers */
	for (i = 0; i < OBJ_MOD_MAX; i++) {
		rd_s16b(&player->obj_k->modifiers[i]);
	}

	/* Elements */
	for (i = 0; i < ELEM_MAX; i++) {
		rd_s16b(&player->obj_k->el_info[i].res_level);
		rd_byte(&player->obj_k->el_info[i].flags);
	}

	/* Read brands */
	for (i = 0; i < brand_max; i++) {
		rd_byte(&tmp8u);
		player->obj_k->brands[i] = tmp8u ? true : false;
	}

	/* Read slays */
	for (i = 0; i < slay_max; i++) {
		rd_byte(&tmp8u);
		player->obj_k->slays[i] = tmp8u ? true : false;
	}

	/* Read curses */
	for (i = 0; i < curse_max; i++) {
		rd_byte(&tmp8u);
		player->obj_k->curses[i].power = tmp8u;
	}

	/* Combat data */
	rd_s16b(&player->obj_k->ac);
	rd_s16b(&player->obj_k->to_a);
	rd_s16b(&player->obj_k->to_h);
	rd_s16b(&player->obj_k->to_d);
	rd_byte(&player->obj_k->dd);
	rd_byte(&player->obj_k->ds);
	return 0;
}

int rd_artifacts(void)
{
	int i;
	uint16_t tmp16u;

	/* Load the Artifacts */
	rd_u16b(&tmp16u);
	if (tmp16u > z_info->a_max) {
		note(format("Too many (%u) artifacts!", tmp16u));
		return (-1);
	}

	/* Read the artifact flags */
	for (i = 0; i < tmp16u; i++) {
		uint8_t tmp8u;

		rd_byte(&tmp8u);
		aup_info[i].created = tmp8u ? true : false;
		rd_byte(&tmp8u);
		aup_info[i].seen = tmp8u ? true : false;
		rd_byte(&tmp8u);
		aup_info[i].everseen = tmp8u ? true : false;
		rd_byte(&tmp8u);
	}

	return 0;
}



int rd_player_hp(void)
{
	int i;
	uint16_t tmp16u;

	/* Read the player_hp array */
	rd_u16b(&tmp16u);
	if (tmp16u > PY_MAX_LEVEL) {
		note(format("Too many (%u) hitpoint entries!", tmp16u));
		return (-1);
	}

	/* Read the player_hp array */
	for (i = 0; i < tmp16u; i++)
		rd_s16b(&player->player_hp[i]);

	return 0;
}


/**
 * Read the player spells
 */
int rd_player_spells(void)
{
	int i;
	uint16_t tmp16u;
	
	int cnt;
	
	/* Read the number of spells */
	rd_u16b(&tmp16u);
	if (tmp16u > player->class->magic.total_spells) {
		note(format("Too many player spells (%d).", tmp16u));
		return (-1);
	}

	/* Initialise */
	player_spells_init(player);
	
	/* Read the spell flags */
	for (i = 0; i < tmp16u; i++)
		rd_byte(&player->spell_flags[i]);
	
	/* Read the spell order */
	for (i = 0, cnt = 0; i < tmp16u; i++, cnt++)
		rd_byte(&player->spell_order[cnt]);
	
	/* Success */
	return (0);
}




/**
 * Read the player gear
 */
static int rd_gear_aux(rd_item_t rd_item_version, struct object **gear)
{
	uint8_t code;
	struct object *last_gear_obj = NULL;

	/* Get the first item code */
	rd_byte(&code);

	/* Read until done */
	while (code != FINISHED_CODE) {
		struct object *obj = (*rd_item_version)();

		/* Read the item */
		if (!obj) {
			note("Error reading item");
			return (-1);
		}

		/* Append the object */
		obj->prev = last_gear_obj;
		if (last_gear_obj)
			last_gear_obj->next = obj;
		else
			*gear = obj;
		last_gear_obj = obj;

		/* If it's equipment, wield it */
		if (code < player->body.count) {
			player->body.slots[code].obj = obj;
			player->upkeep->equip_cnt++;
		}

		/* Get the next item code */
		rd_byte(&code);
	}

	/* Success */
	return (0);
}

/**
 * Read the player gear - wrapper functions
 */
int rd_gear(void)
{
	struct object *obj, *known_obj;

	/* Get real gear */
	if (rd_gear_aux(rd_item, &player->gear))
		return -1;

	/* Get known gear */
	if (rd_gear_aux(rd_item, &player->gear_k))
		return -1;

	/* Align the two, add weight */
	for (obj = player->gear, known_obj = player->gear_k; obj;
		 obj = obj->next, known_obj = known_obj->next) {
		obj->known = known_obj;
		player->upkeep->total_weight +=
			obj->number * object_weight_one(obj);
	}

	calc_inventory(player);

	return 0;
}


/**
 * Read store contents
 */
static int rd_stores_aux(rd_item_t rd_item_version)
{
	int i;
	uint16_t tmp16u;

	/* Read the stores */
	rd_u16b(&tmp16u);
	if (tmp16u != z_info->store_max) {
		note(format("The number of stores in the savefile (%u) is "
			"different than expected (%u).", tmp16u,
			z_info->store_max));
	}
	for (i = 0; i < tmp16u; i++) {
		struct store *store = (i < z_info->store_max) ?
			 &stores[i] : NULL;
		uint8_t own, num;

		/* Read the basic info */
		rd_byte(&own);
		rd_byte(&num);

		/* XXX: refactor into store.c */
		if (store) {
			store->owner = store_ownerbyidx(store, own);
		}

		/* Read the items */
		for (; num; num--) {
			/* Read the known item */
			struct object *obj, *known_obj = (*rd_item_version)();
			if (!known_obj) {
				note("Error reading known item");
				return (-1);
			}

			/* Read the item */
			obj = (*rd_item_version)();
			if (!obj) {
				note("Error reading item");
				return (-1);
			}
			obj->known = known_obj;

			/* Accept any valid items */
			if (store && store->stock_num
					< z_info->store_inven_max
					&& obj->kind) {
				if (store->feat == FEAT_HOME) {
					home_carry(obj);
				} else if (!store_carry(store, obj, false)) {
					if (obj->known) {
						object_delete(NULL, NULL,
							&obj->known);
					}
					object_delete(NULL, NULL, &obj);
				}
			} else {
				if (obj->known) {
					object_delete(NULL, NULL, &obj->known);
				}
				object_delete(NULL, NULL, &obj);
			}
		}
	}
	/* Appended stores have no serialized owner or stock in older saves. */
	if (tmp16u < z_info->store_max && !player->is_dead)
		store_reset_from(tmp16u);

	return 0;
}

/**
 * Read the stores - wrapper functions
 */
int rd_stores(void) { return rd_stores_aux(rd_item); }


/**
 * Read the dungeon
 *
 * The monsters/objects must be loaded in the same order
 * that they were stored, since the actual indexes matter.
 *
 * Note that the size of the dungeon is now the currrent dimensions of the
 * cave global variable.
 *
 * Note that dungeon objects, including objects held by monsters, are
 * placed directly into the dungeon, using "object_copy()", which will
 * copy "iy", "ix", and "held_m_idx", leaving "next_o_idx" blank for
 * objects held by monsters, since it is not saved in the savefile.
 *
 * After loading the monsters, the objects being held by monsters are
 * linked directly into those monsters.
 */
static int rd_dungeon_aux(struct chunk **c)
{
	struct chunk *c1;
	int i, n, y, x;

	uint16_t height, width;

	uint8_t count;
	uint8_t tmp8u;
	uint16_t tmp16u;
	char name[100];

	/* Header info */
	rd_string(name, sizeof(name));
	if (streq(name, "arena") && (*c == cave)) {
		player->upkeep->arena_level = true;
	}
	rd_u16b(&height);
	rd_u16b(&width);

	/* We need a cave struct */
	c1 = cave_new(height, width);
	c1->name = string_make(name);

    /* Run length decoding of cave->squares[y][x].info */
	for (n = 0; n < square_size; n++) {
		/* Load the dungeon data */
		for (x = y = 0; y < c1->height; ) {
			/* Grab RLE info */
			rd_byte(&count);
			rd_byte(&tmp8u);

			/* Apply the RLE info */
			for (i = count; i > 0; i--) {
				/* Extract "info" */
				c1->squares[y][x].info[n] = tmp8u;

				/* Advance/Wrap */
				if (++x >= c1->width) {
					/* Wrap */
					x = 0;

					/* Advance/Wrap */
					if (++y >= c1->height) break;
				}
			}
		}
	}

	/* Run length decoding of dungeon data */
	for (x = y = 0; y < c1->height; ) {
		/* Grab RLE info */
		rd_byte(&count);
		rd_byte(&tmp8u);

		/* Apply the RLE info */
		for (i = count; i > 0; i--) {
			/* Extract "feat" */
			/* Older woodland used granite with a presentation marker. Migrate
			 * actual and remembered chunks alike, without revealing unknown
			 * cells or altering ordinary rock. Feature IDs remain unchanged. */
			int feat = tmp8u;
			if (feat == FEAT_GRANITE && square_iswood(c1, loc(x, y))) {
				feat = FEAT_TREE;
			}
			square_set_feat(c1, loc(x, y), feat);

			/* Advance/Wrap */
			if (++x >= c1->width) {
				/* Wrap */
				x = 0;

				/* Advance/Wrap */
				if (++y >= c1->height) break;
			}
		}
	}


	/* Read "feeling" */
	rd_byte(&tmp8u);
	c1->feeling = tmp8u;
	rd_u16b(&tmp16u);
	c1->feeling_squares = tmp16u;
	rd_s32b(&c1->turn);

	/* Read connector info */
	if (OPT(player, birth_levels_persist)) {
		rd_byte(&tmp8u);
		while (tmp8u != 0xff) {
			struct connector *current = mem_zalloc(sizeof *current);
			current->info = mem_zalloc(square_size * sizeof(bitflag));
			current->grid.x = tmp8u;
			rd_byte(&tmp8u);
			current->grid.y = tmp8u;
			rd_byte(&current->feat);
			for (n = 0; n < square_size; n++) {
				rd_byte(&current->info[n]);
			}
			current->next = c1->join;
			c1->join = current;
			rd_byte(&tmp8u);
		}
	}

	/* Assign */
	*c = c1;

	return 0;
}

/**
 * Read the floor object list
 */
static int rd_objects_aux(rd_item_t rd_item_version, struct chunk *c)
{
	int i;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	/* Make the object list */
	rd_u16b(&c->obj_max);
	c->objects = mem_realloc(c->objects,
							 (c->obj_max + 1) * sizeof(struct object*));
	for (i = 0; i <= c->obj_max; i++)
		c->objects[i] = NULL;

	/* Read the dungeon items until one isn't returned */
	while (true) {
		struct object *obj = (*rd_item_version)();
		if (!obj)
			break;
#if OBJ_RECOVER
		if (square_in_bounds_fully(c, obj->grid) && c == cave) {
#else
		if (square_in_bounds_fully(c, obj->grid)) {
#endif
			pile_insert_end(&c->squares[obj->grid.y][obj->grid.x].obj, obj);
		}
		assert(obj->oidx);
		assert(c->objects[obj->oidx] == NULL);
		c->objects[obj->oidx] = obj;
	}

	return 0;
}

/**
 * Read monsters
 */
static int rd_monsters_aux(struct chunk *c)
{
	int i;
	uint16_t limit;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	/* Read the monster count */
	rd_u16b(&limit);
	if (limit > z_info->level_monster_max) {
		note(format("Too many (%d) monster entries!", limit));
		return (-1);
	}

	/* Read the monsters */
	for (i = 1; i < limit; i++) {
		struct monster *mon;
		struct monster monster_body;

		/* Get local monster */
		mon = &monster_body;
		memset(mon, 0, sizeof(*mon));

		/* Read the monster */
		if (!rd_monster(c, mon)) {
			note(format("Cannot read monster %d", i));
			return (-1);
		}

		/* Place monster in dungeon */
		if (place_monster(c, mon->grid, mon, 0) != i) {
			note(format("Cannot place monster %d", i));
			return (-1);
		}
	}

	return 0;
}

static int rd_traps_aux(struct chunk *c)
{
	struct loc grid;
	struct trap *trap;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	rd_byte(&trf_size);

	/* Read traps until one has no location */
	while (true) {
		trap = mem_zalloc(sizeof(*trap));
		rd_trap(trap);
		grid = trap->grid;
		if (loc_is_zero(grid))
			break;
		else {
			/* Put the trap at the front of the grid trap list */
			trap->next = square_trap(c, grid);
			square_set_trap(c, grid, trap);

			/* Set decoy if appropriate */
			if ((trap->kind == lookup_trap("decoy")) &&
			    (c == cave)) {
				c->decoy = grid;
			}
		}
	}

	mem_free(trap);
	return 0;
}

int rd_dungeon(void)
{
	uint16_t depth;
	uint16_t py, px;

	/* Header info */
	rd_u16b(&depth);
	rd_u16b(&daycount);
	rd_u16b(&py);
	rd_u16b(&px);
	rd_byte(&square_size);

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	/* Ignore illegal dungeons */
	if (depth >= z_info->max_depth) {
		note(format("Ignoring illegal dungeon depth (%d)", depth));
		return (0);
	}

	if (rd_dungeon_aux(&cave))
		return 1;

	/* Ignore illegal dungeons */
	if ((px >= cave->width) || (py >= cave->height)) {
		note(format("Ignoring illegal player location (%d,%d).", py, px));
		return (1);
	}

	/* Load player depth */
	player->depth = depth;
	cave->depth = depth;

	/* Place player in dungeon */
	player_place(cave, player, loc(px, py));

	/* The dungeon is ready */
	character_dungeon = true;

	/* Read known cave */
	if (rd_dungeon_aux(&player->cave)) {
		return 1;
	}
	player->cave->depth = depth;

	return 0;
}

static void rd_world_identity(void)
{
	int16_t danger;

	rd_string(player->world_location.id,
		sizeof(player->world_location.id));
	rd_string(player->world_location.entry,
		sizeof(player->world_location.entry));
	rd_s16b(&danger);
	if (player->is_dead) player->depth = danger;
}

/** Read the original stable identity block. */
int rd_world_state_1(void)
{
	world_player_clear_ledgers(player);
	world_larder_reset(&player->larder);
	rd_world_identity();
	if (!world_overworld_layout_create(player, true) ||
			!world_overworld_rebuild_routes(player)) {
		return 1;
	}
	(void)world_overworld_backfill_discovered_stops(player);
	return 0;
}

/** Read one bounded stable-ID ledger. */
enum world_ledger_kind {
	WORLD_LEDGER_LOCATIONS = 0,
	WORLD_LEDGER_ROUTES,
	WORLD_LEDGER_TRAVEL_NODES
};

static int rd_world_ledger(enum world_ledger_kind kind)
{
	uint16_t count;
	uint16_t i;
	char id[WORLD_ID_LEN];

	rd_u16b(&count);
	if (count > WORLD_LEDGER_LIMIT) return 1;
	for (i = 0; i < count; i++) {
		bool duplicate;
		bool added;

		rd_string(id, sizeof(id));
		if (kind == WORLD_LEDGER_LOCATIONS) {
			duplicate = world_player_has_discovered_location(player, id);
		} else if (kind == WORLD_LEDGER_ROUTES) {
			duplicate = world_player_route_is_unlocked(player, id);
		} else {
			duplicate = world_player_has_activated_travel_node(player, id);
		}
		if (duplicate) return 1;
		if (kind == WORLD_LEDGER_LOCATIONS) {
			added = world_player_discover_location(player, id);
		} else if (kind == WORLD_LEDGER_ROUTES) {
			added = world_player_unlock_route(player, id);
		} else {
			added = world_player_activate_travel_node(player, id);
		}
		if (!added) return 1;
	}
	return 0;
}

/** Read the data shared by versions two and three without migrating it. */
static int rd_world_state_2_data(void)
{
	world_player_clear_ledgers(player);
	world_larder_reset(&player->larder);
	rd_world_identity();
	if (rd_world_ledger(WORLD_LEDGER_LOCATIONS) ||
			rd_world_ledger(WORLD_LEDGER_ROUTES)) {
		return 1;
	}
	return 0;
}

/** Read version two and reconstruct the node ledger it predates. */
int rd_world_state_2(void)
{
	if (rd_world_state_2_data()) return 1;
	if (!world_overworld_layout_create(player, true) ||
			!world_overworld_rebuild_routes(player)) {
		return 1;
	}
	(void)world_player_backfill_travel_nodes(player);
	(void)world_overworld_backfill_discovered_stops(player);
	(void)world_player_unlock_activated_travel_routes(player);
	return 0;
}

/** Read version three and add its deterministic legacy surface layout. */
int rd_world_state_3(void)
{
	if (rd_world_state_2_data()) return 1;
	if (rd_world_ledger(WORLD_LEDGER_TRAVEL_NODES)) return 1;
	if (!world_overworld_layout_create(player, true) ||
			!world_overworld_rebuild_routes(player)) {
		return 1;
	}
	(void)world_overworld_backfill_discovered_stops(player);
	/* Aggregate route availability is derived from endpoint discovery.  This
	 * also makes newly authored trails available in an older compatible save
	 * once both of their endpoints had already been reached. */
	(void)world_player_unlock_activated_travel_routes(player);
	return 0;
}

/** Read stable identity, the shuffled layout, and all discovery ledgers. */
static int rd_world_state_4_data(void)
{
	uint8_t version, width, height, count;
	uint32_t seed;

	world_player_clear_ledgers(player);
	rd_world_identity();
	rd_byte(&version);
	rd_byte(&width);
	rd_byte(&height);
	rd_byte(&count);
	rd_u32b(&seed);
	if (!world_overworld_layout_set(player, version, width, height, count,
			seed) || !world_overworld_rebuild_routes(player)) {
		return 1;
	}
	if (rd_world_ledger(WORLD_LEDGER_LOCATIONS) ||
			rd_world_ledger(WORLD_LEDGER_ROUTES) ||
			rd_world_ledger(WORLD_LEDGER_TRAVEL_NODES)) {
		return 1;
	}
	(void)world_player_unlock_activated_travel_routes(player);
	return 0;
}

/** Read version four, which predates the persistent village larder. */
int rd_world_state_4(void)
{
	world_larder_reset(&player->larder);
	return rd_world_state_4_data();
}

/** Read current stable-world data including village-larder progress. */
int rd_world_state(void)
{
	uint32_t food_points;
	uint8_t milestone;

	if (rd_world_state_4_data()) return 1;
	rd_u32b(&food_points);
	rd_byte(&milestone);
	player->larder.food_points = food_points;
	player->larder.milestone = milestone;
	if (!world_larder_is_valid(&player->larder)) return 1;
	store_sync_larder_unlocks();
	return 0;
}

/** Read one bounded node runtime and its ordinary local objects. */
static int rd_spelunking_runtime_record(bool has_local_objects,
		bool allow_empty, bool upgrade_handcrafted_layout,
		struct world_spelunk_runtime **decoded)
{
	uint8_t *payload;
	uint32_t size;
	uint32_t i;
	uint16_t object_count = 0;

	if (!decoded) return -1;
	*decoded = NULL;
	rd_u32b(&size);
	if (!size) {
		if (has_local_objects) rd_u16b(&object_count);
		if (object_count) return -1;
		return allow_empty ? 0 : -1;
	}
	if (size > WORLD_SPELUNK_PAYLOAD_MAX) return -1;
	payload = mem_alloc(size);
	for (i = 0; i < size; i++) rd_byte(&payload[i]);
	if (world_spelunk_runtime_decode(payload, size, decoded) !=
			WORLD_SPELUNK_CODEC_OK) {
		mem_free(payload);
		return -1;
	}
	mem_free(payload);
	if (has_local_objects) {
		rd_u16b(&object_count);
		if (object_count > WORLD_SPELUNK_OBJECT_MAX) {
			world_spelunk_runtime_free(*decoded);
			*decoded = NULL;
			return -1;
		}
		for (i = 0; i < object_count; i++) {
			struct object *known = rd_item();
			struct object *obj = known ? rd_item() : NULL;

			if (!known || !obj) {
				if (known) object_delete(NULL, NULL, &known);
				if (obj) object_delete(NULL, NULL, &obj);
				world_spelunk_runtime_free(*decoded);
				*decoded = NULL;
				return -1;
			}
			obj->known = known;
			if (!world_spelunk_runtime_add_ground_object(*decoded, obj,
					obj->grid.x, obj->grid.y)) {
				obj->known = NULL;
				object_delete(NULL, NULL, &known);
				object_delete(NULL, NULL, &obj);
				world_spelunk_runtime_free(*decoded);
				*decoded = NULL;
				return -1;
			}
		}
	}
	if ((upgrade_handcrafted_layout &&
			(!world_spelunk_runtime_set_perception(*decoded,
				spelunking_perception_for_load(*decoded, NULL)) ||
			 !world_spelunk_layout_upgrade_runtime(*decoded))) ||
			!world_spelunk_runtime_is_valid(*decoded)) {
		world_spelunk_runtime_free(*decoded);
		*decoded = NULL;
		return -1;
	}
	return 0;
}

/** Read an old one-runtime block and adopt it as a one-node cave system. */
static int rd_spelunking_aux(bool has_local_objects)
{
	struct world_spelunk_runtime *decoded = NULL;
	struct world_spelunk_system *system;

	if (rd_spelunking_runtime_record(has_local_objects, true, true, &decoded)) {
		return -1;
	}
	if (!decoded) {
		player_spelunking_clear(player);
		return 0;
	}
	system = world_spelunk_system_create_legacy(decoded);
	if (!system) {
		world_spelunk_runtime_free(decoded);
		return -1;
	}
	if (!player_spelunking_set_system(player, system)) {
		world_spelunk_system_free(system);
		return -1;
	}
	return 0;
}

int rd_spelunking_1(void)
{
	return rd_spelunking_aux(false);
}

int rd_spelunking(void)
{
	return rd_spelunking_aux(true);
}

/** Read the multi-node cave-system block and its append-only extensions. */
static int rd_spelunking_system_aux(bool has_graph, bool has_spatial,
		bool has_portal_state)
{
	struct world_spelunk_system *system;
	char system_id[WORLD_ID_LEN];
	char location_id[WORLD_ID_LEN] = "";
	char active_node_id[WORLD_ID_LEN];
	uint8_t present;
	uint32_t system_seed;
	uint16_t graph_version;
	uint16_t node_count;
	uint16_t i;

	rd_byte(&present);
	if (present > 1) return -1;
	if (!present) {
		player_spelunking_clear(player);
		return 0;
	}
	rd_string(system_id, sizeof(system_id));
	if (has_spatial) rd_string(location_id, sizeof(location_id));
	rd_u32b(&system_seed);
	rd_u16b(&graph_version);
	if (!has_graph) {
		graph_version = WORLD_SPELUNK_SYSTEM_GRAPH_VERSION_LEGACY;
	}
	rd_string(active_node_id, sizeof(active_node_id));
	rd_u16b(&node_count);
	if (!node_count || node_count > WORLD_SPELUNK_SYSTEM_NODE_MAX) return -1;
	system = world_spelunk_system_create(system_id, system_seed,
		graph_version);
	if (!system) return -1;
	if (has_spatial &&
			!world_spelunk_system_set_location(system, location_id)) {
		world_spelunk_system_free(system);
		return -1;
	}
	for (i = 0; i < node_count; i++) {
		struct world_spelunk_runtime *runtime = NULL;
		char node_id[WORLD_ID_LEN];
		char recipe_id[WORLD_ID_LEN];
		uint32_t node_seed;
		uint16_t generator_version;
		uint16_t depth;
		uint8_t discovered;
		uint8_t has_runtime;

		rd_string(node_id, sizeof(node_id));
		rd_string(recipe_id, sizeof(recipe_id));
		rd_u32b(&node_seed);
		rd_u16b(&generator_version);
		rd_u16b(&depth);
		if (has_graph) {
			rd_byte(&discovered);
			if (discovered > 1) {
				world_spelunk_system_free(system);
				return -1;
			}
		} else {
			discovered = 0;
		}
		rd_byte(&has_runtime);
		if (has_runtime > 1 ||
				(has_runtime && rd_spelunking_runtime_record(true, false,
					graph_version ==
						WORLD_SPELUNK_SYSTEM_GRAPH_VERSION_LEGACY,
					&runtime))) {
			world_spelunk_system_free(system);
			return -1;
		}
		if (runtime) {
			const struct world_spelunk_perception *perception =
				spelunking_perception_for_load(runtime, recipe_id);

			if (!perception || !world_spelunk_runtime_set_perception(runtime,
					perception)) {
				world_spelunk_runtime_free(runtime);
				world_spelunk_system_free(system);
				return -1;
			}
		}
		if (!world_spelunk_system_add_node(system, node_id, recipe_id,
				node_seed, generator_version, depth, runtime)) {
			world_spelunk_runtime_free(runtime);
			world_spelunk_system_free(system);
			return -1;
		}
		if (has_graph) {
			struct world_spelunk_system_node *node =
				world_spelunk_system_node_by_id_mutable(system, node_id);

			node->discovered = discovered != 0;
		}
	}
	if (has_graph) {
		uint16_t edge_count;

		rd_u16b(&edge_count);
		if (edge_count > WORLD_SPELUNK_SYSTEM_EDGE_MAX) {
			world_spelunk_system_free(system);
			return -1;
		}
		for (i = 0; i < edge_count; i++) {
			char edge_id[WORLD_ID_LEN];
			char node_a_id[WORLD_ID_LEN];
			char endpoint_a_id[WORLD_ID_LEN];
			char node_b_id[WORLD_ID_LEN];
			char endpoint_b_id[WORLD_ID_LEN];
			uint8_t discovered;
			uint8_t endpoint_a_materialized = 0;
			uint8_t endpoint_b_materialized = 0;
			int16_t endpoint_a_x = 0;
			int16_t endpoint_a_y = 0;
			int16_t endpoint_b_x = 0;
			int16_t endpoint_b_y = 0;

			rd_string(edge_id, sizeof(edge_id));
			rd_string(node_a_id, sizeof(node_a_id));
			rd_string(endpoint_a_id, sizeof(endpoint_a_id));
			rd_string(node_b_id, sizeof(node_b_id));
			rd_string(endpoint_b_id, sizeof(endpoint_b_id));
			rd_byte(&discovered);
			if (has_spatial) {
				rd_byte(&endpoint_a_materialized);
				rd_s16b(&endpoint_a_x);
				rd_s16b(&endpoint_a_y);
				rd_byte(&endpoint_b_materialized);
				rd_s16b(&endpoint_b_x);
				rd_s16b(&endpoint_b_y);
			}
			if (discovered > 1 ||
					endpoint_a_materialized > 1 ||
					endpoint_b_materialized > 1 ||
					!world_spelunk_system_add_edge(system, edge_id,
						node_a_id, endpoint_a_id, node_b_id, endpoint_b_id,
						discovered != 0)) {
				world_spelunk_system_free(system);
				return -1;
			}
			if ((endpoint_a_materialized &&
					 !world_spelunk_system_set_endpoint_grid(system,
						 endpoint_a_id, endpoint_a_x, endpoint_a_y)) ||
					(endpoint_b_materialized &&
					 !world_spelunk_system_set_endpoint_grid(system,
						 endpoint_b_id, endpoint_b_x, endpoint_b_y))) {
				world_spelunk_system_free(system);
				return -1;
			}
		}
	}
	if (has_spatial) {
		uint16_t portal_count;

		rd_u16b(&portal_count);
		if (portal_count > WORLD_SPELUNK_SYSTEM_PORTAL_MAX) {
			world_spelunk_system_free(system);
			return -1;
		}
		for (i = 0; i < portal_count; i++) {
			char portal_id[WORLD_ID_LEN];
			char entry_id[WORLD_ENTRY_LEN];
			char node_id[WORLD_ID_LEN];
			uint8_t enabled = 1;
			uint8_t materialized;
			uint8_t discovered;
			int16_t x;
			int16_t y;

			rd_string(portal_id, sizeof(portal_id));
			rd_string(entry_id, sizeof(entry_id));
			rd_string(node_id, sizeof(node_id));
			if (has_portal_state) rd_byte(&enabled);
			rd_byte(&materialized);
			rd_s16b(&x);
			rd_s16b(&y);
			rd_byte(&discovered);
			if (enabled > 1 || materialized > 1 || discovered > 1 ||
					!world_spelunk_system_add_portal(system, portal_id,
						entry_id, node_id, enabled != 0) ||
					(materialized &&
					 !world_spelunk_system_set_portal_grid(system,
						 portal_id, x, y)) ||
					(discovered &&
					 !world_spelunk_system_discover_portal(system,
						 portal_id))) {
				world_spelunk_system_free(system);
				return -1;
			}
		}
	}
	if (has_graph) {
		const struct world_spelunk_system_node *active_node =
			world_spelunk_system_node_by_id(system, active_node_id);

		if (!active_node || !active_node->discovered) {
			world_spelunk_system_free(system);
			return -1;
		}
	}
	if (!has_spatial) {
		const struct world_spelunk_system_node *active_node =
			world_spelunk_system_node_by_id(system, active_node_id);

		if (!active_node || !active_node->runtime ||
				!world_spelunk_system_set_location(system,
					active_node->runtime->location_id)) {
			world_spelunk_system_free(system);
			return -1;
		}
	}
	if (!world_spelunk_system_reconcile_portals(system)) {
		world_spelunk_system_free(system);
		return -1;
	}
	if (!world_spelunk_system_set_active_node(system, active_node_id) ||
			!world_spelunk_system_is_valid(system) ||
			!player_spelunking_set_system(player, system)) {
		world_spelunk_system_free(system);
		return -1;
	}
	return 0;
}

int rd_spelunking_system_3(void)
{
	return rd_spelunking_system_aux(false, false, false);
}

int rd_spelunking_system_4(void)
{
	return rd_spelunking_system_aux(true, false, false);
}

int rd_spelunking_system_5(void)
{
	return rd_spelunking_system_aux(true, true, false);
}

int rd_spelunking_system(void)
{
	return rd_spelunking_system_aux(true, true, true);
}


/**
 * Read the objects - wrapper functions
 */
int rd_objects(void)
{
	if (rd_objects_aux(rd_item, cave))
		return -1;
	if (rd_objects_aux(rd_item, player->cave))
		return -1;

	return 0;
}

/**
 * Read the monster list - wrapper functions
 */
int rd_monsters(void)
{
	int i;

	/* Only if the player's alive */
	if (player->is_dead)
		return 0;

	if (rd_monsters_aux(cave))
		return -1;
	if (rd_monsters_aux(player->cave))
		return -1;

#if OBJ_RECOVER
	player->cave->objects = mem_zalloc((cave->obj_max + 1) * sizeof(struct object*));
	player->cave->obj_max = cave->obj_max;
	for (i = 0; i <= cave->obj_max; i++) {
		struct object *obj = cave->objects[i], *known_obj;
		if (!obj) continue;
		known_obj = object_new();
		obj->known = known_obj;
		object_copy(known_obj, obj);
		player->cave->objects[i] = known_obj;
	}
#else
	/* Associate known objects */
	for (i = 0; i < player->cave->obj_max; i++)
		if (cave->objects[i] && player->cave->objects[i])
			cave->objects[i]->known = player->cave->objects[i];
#endif
	return 0;
}

/**
 * Read the traps - wrapper functions
 */
int rd_traps(void)
{
	if (rd_traps_aux(cave))
		return -1;
	if (rd_traps_aux(player->cave))
		return -1;
	return 0;
}

/**
 * Read the chunk list
 */
static int rd_chunks_aux(bool has_feature_count, int legacy_feature_count)
{
	int j;
	uint16_t chunk_max, feature_count;

	if (player->is_dead)
		return 0;

	rd_u16b(&chunk_max);
	if (has_feature_count) {
		rd_u16b(&feature_count);
		if (feature_count > FEAT_MAX + 1) return -1;
	} else {
		feature_count = legacy_feature_count;
	}
	for (j = 0; j < chunk_max; j++) {
		struct chunk *c = NULL;

		/* Read the dungeon */
		if (rd_dungeon_aux(&c))
			return -1;

		/* Read the objects */
		if (rd_objects_aux(rd_item, c))
			return -1;

		/* Read the monsters */
		if (rd_monsters_aux(c))
			return -1;

		/* Read traps */
		if (rd_traps_aux(c))
			return -1;


		/* Read other chunk info */
		if (OPT(player, birth_levels_persist)) {
			char buf[80];
			int i;
			uint8_t tmp8u;
			uint16_t tmp16u;

			rd_string(buf, sizeof(buf));
			string_free(c->name);
			c->name = string_make(buf);
			rd_s32b(&c->turn);
			rd_u16b(&tmp16u);
			c->depth = tmp16u;
			rd_byte(&c->feeling);
			rd_u32b(&c->obj_rating);
			rd_u32b(&c->mon_rating);
			rd_byte(&tmp8u);
			c->good_item  = tmp8u ? true : false;
			rd_u16b(&tmp16u);
			c->height = tmp16u;
			rd_u16b(&tmp16u);
			c->width = tmp16u;
			rd_u16b(&c->feeling_squares);
			for (i = 0; i < feature_count; i++) {
				rd_u16b(&tmp16u);
				/* Terrain decoding already rebuilt these counts, including
				 * saved woodland migrated from granite to real trees. */
			}
		} else if (c->name) {
			struct level *lev = level_by_name(c->name);

			if (lev) {
				c->depth = lev->depth;
			} else if (suffix(c->name, " known")) {
				size_t offset = strlen(c->name) -
					strlen(" known");
				c->name[offset] = '\0';
				lev = level_by_name(c->name);
				if (lev) {
					c->depth = lev->depth;
				}
				c->name[offset] = ' ';
			}
		}

		chunk_list_add(c);
	}

#if OBJ_RECOVER
	for (j = 0; j < chunk_max; j++) {
		if (j == 0 && streq(chunk_list[j].name, "Town")) continue;
		chunk_list[j] = 0;
	}
	if (streq(chunk_list[0].name, "Town")) {
		chunk_list_max = 1;
	} else {
		chunk_list_max = 0;
	}
#endif

	return 0;
}

/** Read stable ownership for chunks loaded by earlier save blocks. */
int rd_world_chunks(void)
{
	char id[WORLD_ID_LEN];
	uint16_t count, i;
	uint8_t present, known;

	rd_byte(&present);
	if (present > 1) return -1;
	if (!present) return player->is_dead ? 0 : -1;
	if (player->is_dead || !cave || !player->cave) return -1;

	rd_string(id, sizeof(id));
	cave->is_known = false;
	if (id[0] && !world_chunk_set_location(cave, id, false)) return -1;
	rd_string(id, sizeof(id));
	player->cave->is_known = true;
	if (id[0] && !world_chunk_set_location(player->cave, id, true)) {
		return -1;
	}

	rd_u16b(&count);
	if (count != chunk_list_max) return -1;
	for (i = 0; i < count; i++) {
		struct level *lev;

		rd_string(id, sizeof(id));
		rd_byte(&known);
		lev = level_by_id(id);
		if (known > 1 || !lev) return -1;
		if (OPT(player, birth_levels_persist) &&
				chunk_list[i]->depth != lev->danger) {
			return -1;
		}
		chunk_list[i]->depth = lev->danger;
		if (!world_chunk_set_location(chunk_list[i], id, known != 0)) {
			return -1;
		}
	}
	return 0;
}

/* Version 1 was written before the Fishing Shop feature was appended. */
int rd_chunks_1(void)
{
	return rd_chunks_aux(false, FEAT_MAX);
}

int rd_chunks(void)
{
	return rd_chunks_aux(true, 0);
}


int rd_history(void)
{
	uint32_t tmp32u;
	size_t i, j;
	
	history_clear(player);

	/* History type flags */
	rd_byte(&hist_size);
	if (hist_size > HIST_SIZE) {
	        note(format("Too many (%u) history types!", hist_size));
		return (-1);
	}

	rd_u32b(&tmp32u);
	for (i = 0; i < tmp32u; i++) {
		int32_t turnno;
		int16_t dlev, clev;
		bitflag type[HIST_SIZE];
		const struct artifact *art = NULL;
		int aidx = 0;
		char name[80];
		char text[80];

		for (j = 0; j < hist_size; j++)		
			rd_byte(&type[j]);
		rd_s32b(&turnno);
		rd_s16b(&dlev);
		rd_s16b(&clev);
		rd_string(name, sizeof(name));
		if (name[0]) {
			art = lookup_artifact_name(name);
			if (art) {
				aidx = art->aidx;
			}
		}
		rd_string(text, sizeof(text));
		if (name[0] && !art) {
			note(format("Couldn't find artifact %s!", name));
			continue;
		}

		history_add_full(player, type, aidx, dlev, clev, turnno, text);
	}

	return 0;
}

/**
 * For blocks that don't need loading anymore.
 */
int rd_null(void) {
	return 0;
}
