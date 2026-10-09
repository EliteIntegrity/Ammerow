/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file store.h
 * \brief Store stocking
 *
 * Copyright (c) 1997 Robert A. Koeneke, James E. Wilson, Ben Harrison
 * Copyright (c) 2007 Andi Sidwell
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

#ifndef INCLUDED_STORE_H
#define INCLUDED_STORE_H

#include "cave.h"
#include "cmd-core.h"
#include "datafile.h"
#include "object.h"

struct object_buy {
	struct object_buy *next;
	size_t tval;
	size_t flag;
};

struct world_larder_milestone_definition;
struct world_objective_definition;
struct player;

/** One player-state override for a store's authored introduction. */
struct store_welcome_condition {
	const struct world_objective_definition *objective;
	char *text;
	struct store_welcome_condition *next;
};

struct store_larder_stock {
	const struct world_larder_milestone_definition *milestone;
	struct object_kind *kind;
};

/** A guaranteed store line whose authored quantity is finite. */
struct store_limited_stock {
	struct object_kind *kind;
	unsigned int quantity;
};

struct owner {
	unsigned int oidx;
	struct owner *next;
	char *name;
	char *art_id;			/* Stable cross-media visual identity */
	int32_t max_cost;
};

struct store {
	struct owner *owners;
	struct owner *owner;
	char *art_id;			/* Stable shop-family visual identity */
	char *welcome;			/* Optional authored introduction */
	struct store_welcome_condition *welcome_conditions;
	char *victory_welcome;
	char *objective_briefing;	/* Optional authored quest-list subtitle */
	char *fishing_rig_hint;	/* Optional supplier hint used without a rig */
	int feat;			/* index for entrance's terrain */
	bool relic_broker;		/* Finite blind catalogue, not ordinary stock */

	uint8_t stock_num;		/* Stock -- Number of entries */
	int16_t stock_size;		/* Stock -- Total Size of Array */
	struct object *stock;		/* Stock -- Actual stock items */
	struct object *stock_k;		/* Stock -- Stock as known by the character */

	/* Always stock these items */
	size_t always_size;
	size_t always_num;
	struct object_kind **always_table;

	/* Guarantee these finite quantities when the store is maintained. */
	size_t limited_size;
	size_t limited_num;
	struct store_limited_stock *limited_table;

	/* Stock these items after an authored village-larder milestone. */
	size_t larder_size;
	size_t larder_num;
	struct store_larder_stock *larder_table;

	/* Select a number of these items to stock */
	size_t normal_size;
	size_t normal_num;
	struct object_kind **normal_table;

	/* Buy these items */
	struct object_buy *buy;

	int turnover;
	int normal_stock_min;
	int normal_stock_max;
};

extern struct store *stores;

struct store *store_at(struct chunk *c, struct loc grid);
/** Resolve authored store dialogue against current player progress. */
const char *store_welcome_for_player(const struct store *store,
		const struct player *p);
/** Return the single data-authored supplier hint for fishing rigs. */
const char *store_fishing_rig_hint(void);
/** Whether this data-authored store owns village-larder progression. */
bool store_accepts_larder_donations(const struct store *store);
void store_init(void);
void free_stores(void);
void store_stock_list(struct store *store, struct object **list, int n);
void home_carry(struct object *obj);
struct object *store_carry(struct store *store, struct object *obj,
		bool maintain);
void store_reset(void);
void store_reset_from(int first);
void store_reset_missing(void);
void store_shuffle(struct store *store);
void store_update(void);
/** Add newly unlocked larder staples without waiting for shop turnover. */
void store_sync_larder_unlocks(void);
/** Shared knowledge receipt for normal purchases and broker artefacts. */
void store_identify_purchase(struct player *p, struct object *obj);
int price_item(struct store *store, const struct object *obj,
			   bool store_buying, int qty);

bool store_will_buy_tester(const struct object *obj);
bool store_check_num(struct store *store, const struct object *obj);
int find_inven(const struct object *obj);

extern struct owner *store_ownerbyidx(struct store *s, unsigned int idx);

struct parser *init_parse_stores(void);
extern struct parser *store_parser_new(void);
extern struct parser *store_owner_parser_new(struct store *stores);

extern void do_cmd_sell(struct command *cmd);
extern void do_cmd_stash(struct command *cmd);
extern void do_cmd_buy(struct command *cmd);
extern void do_cmd_retrieve(struct command *cmd);

#endif /* INCLUDED_STORE_H */
