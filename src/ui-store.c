/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-store.c
 * \brief Store UI
 *
 * Copyright (c) 1997 Robert A. Koeneke, James E. Wilson, Ben Harrison
 * Copyright (c) 1998-2014 Angband developers
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
#include "cmd-larder.h"
#include "cmds.h"
#include "game-event.h"
#include "game-input.h"
#include "hint.h"
#include "init.h"
#include "monster.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-ignore.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-calcs.h"
#include "player-history.h"
#include "player-util.h"
#include "store.h"
#include "target.h"
#include "ui-display.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-player.h"
#include "ui-options.h"
#include "ui-knowledge.h"
#include "ui-object.h"
#include "ui-player.h"
#include "ui-spell.h"
#include "ui-command.h"
#include "ui-store.h"
#include "ui-relic-broker.h"
#include "ui-store-screen.h"
#include "world-larder.h"
#include "world-larder-data.h"
#include "world-objective-data.h"
#include "z-debug.h"

int (*textui_store_usable_width_hook)(int term_width) = NULL;


/**
 * Shopkeeper welcome messages.
 *
 * The shopkeeper's name must come first, then the character's name.
 */
static const char *comment_welcome[] =
{
	"",
	"%s nods to you.",
	"%s says hello.",
	"%s: \"See anything you need?\"",
	"%s: \"How may I help you, %s?\"",
	"%s: \"Welcome back, %s.\"",
	"%s: \"Good to see you again, %s.\"",
	"%s: \"What does the road require, %s?\"",
	"%s: \"I set aside the dry stock for you, %s.\"",
	"%s: \"Let us see what you can carry, %s.\""
};

static const char *comment_hint[] =
{
/*	"%s tells you soberly: \"%s\".",
	"(%s) There's a saying round here, \"%s\".",
	"%s offers to tell you a secret next time you're about."*/
	"\"%s\""
};


/**
 * Easy names for the elements of the 'scr_places' arrays.
 */
enum
{
	LOC_PRICE = 0,
	LOC_OWNER,
	LOC_HEADER,
	LOC_MORE,
	LOC_HELP_CLEAR,
	LOC_HELP_PROMPT,
	LOC_AU,
	LOC_WEIGHT,

	LOC_MAX
};

/* State flags */
#define STORE_GOLD_CHANGE      0x01
#define STORE_FRAME_CHANGE     0x02
#define STORE_SHOW_HELP        0x04

/* Compound flag for the initial display of a store */
#define STORE_INIT_CHANGE		(STORE_FRAME_CHANGE | STORE_GOLD_CHANGE)

struct store_context {
	struct menu menu;			/* Menu instance */
	struct store *store;	/* Pointer to store */
	struct object **list;	/* List of objects (unused) */
	int flags;				/* Display flags */
	bool inspect_only;		/* Only allow looking */
	int usable_width;		/* Text columns not occupied by SDL3 art */
	char notice[256];		/* Latest feedback, retained across stock redraws */

	/* Places for the various things displayed onscreen */
	unsigned int scr_places_x[LOC_MAX];
	unsigned int scr_places_y[LOC_MAX];
};

/* Return a random hint from the global hints list */
static const char *random_hint(void)
{
	struct hint *v, *r = hints;
	int n;
	for (v = hints->next, n = 2; v; v = v->next, n++)
		if (one_in_(n))
			r = v;
	return r->hint;
}

/**
 * The greeting a shopkeeper gives the character says a lot about their
 * general attitude.
 *
 * Taken and modified from Sangband 1.0.
 *
 * Note that each comment_hint should have exactly one %s
 */
static void prt_welcome(const struct owner *proprietor)
{
	char short_name[20];
	const char *owner_name = proprietor->name;

	int j;

	if (one_in_(2))
		return;

	/* Get the first name of the store owner (stop before the first space) */
	for (j = 0; owner_name[j] && owner_name[j] != ' '; j++)
		short_name[j] = owner_name[j];

	/* Truncate the name */
	short_name[j] = '\0';

	if (hints && one_in_(3)) {
		size_t i = randint0(N_ELEMENTS(comment_hint));
		msg(comment_hint[i], random_hint());
	} else if (player->lev > 5) {
		const char *player_name;

		/* We go from level 1 - 50  */
		size_t i = ((unsigned)player->lev - 1) / 5;
		i = MIN(i, N_ELEMENTS(comment_welcome) - 1);

		/* Get a title for the character */
		if ((i % 2) && randint0(2))
			player_name = player->class->title[(player->lev - 1) / PY_TITLE_LEVELS];
		else if (randint0(2))
			player_name = player->full_name;
		else
			player_name = "valued customer";

		/* Balthazar says "Welcome" */
		prt(format(comment_welcome[i], short_name, player_name), 0, 0);
	}
}


/*** Display code ***/


/**
 * This function sets up screen locations based on the current term size.
 *
 * Current screen layout:
 *  line 0: reserved for messages
 *  line 1: shopkeeper and their purse / item buying price
 *  line 2: empty
 *  line 3: table headers
 *
 *  line 4: Start of items
 *
 * If help is turned off, then the rest of the display goes as:
 *
 *  line (height - 4): end of items
 *  line (height - 3): "more" prompt
 *  line (height - 2): empty
 *  line (height - 1): Help prompt and remaining gold
 *
 * If help is turned on, then the rest of the display goes as:
 *
 *  line (height - 7): end of items
 *  line (height - 6): "more" prompt
 *  line (height - 4): gold remaining
 *  line (height - 3): command help 
 */
static void store_display_recalc(struct store_context *ctx)
{
	int wid, hgt;
	region loc;

	struct menu *m = &ctx->menu;
	struct store *store = ctx->store;
	const char *welcome = store_welcome_for_player(store, player);

	Term_get_size(&wid, &hgt);
	if (textui_store_usable_width_hook) {
		int hooked_width = textui_store_usable_width_hook(wid);

		if (hooked_width > 0 && hooked_width <= wid) wid = hooked_width;
	}

	/* Clip the width at a max of 104 (enough room for an 80-char item name) */
	if (wid > 104) wid = 104;

	/* Clip the text_out function at two smaller than the screen width */
	text_out_wrap = wid - 2;


	/* X co-ords first */
	ctx->scr_places_x[LOC_PRICE] = wid - 14;
	ctx->scr_places_x[LOC_AU] = wid - 26;
	ctx->scr_places_x[LOC_OWNER] = wid - 2;
	ctx->scr_places_x[LOC_WEIGHT] = wid - 14;

	/* Add space for prices */
	if (store->feat != FEAT_HOME)
		ctx->scr_places_x[LOC_WEIGHT] -= 10;

	/* Then Y */
	ctx->scr_places_y[LOC_OWNER] = 1;
	ctx->scr_places_y[LOC_HEADER] = welcome ? 4 : 3;
	ctx->usable_width = wid;

	/* If we are displaying help, make the height smaller */
	if (ctx->flags & (STORE_SHOW_HELP))
		hgt -= 3;

	ctx->scr_places_y[LOC_MORE] = hgt - 3;
	ctx->scr_places_y[LOC_AU] = hgt - 1;

	loc = m->boundary;
	loc.row = ctx->scr_places_y[LOC_HEADER] + 1;
	if (loc.col >= 0 && wid > loc.col) loc.width = wid - loc.col;

	/* If we're displaying the help, then put it with a line of padding */
	if (ctx->flags & (STORE_SHOW_HELP)) {
		ctx->scr_places_y[LOC_HELP_CLEAR] = hgt - 1;
		ctx->scr_places_y[LOC_HELP_PROMPT] = hgt;
		loc.page_rows = -5;
	} else {
		ctx->scr_places_y[LOC_HELP_CLEAR] = hgt - 2;
		ctx->scr_places_y[LOC_HELP_PROMPT] = hgt - 1;
		loc.page_rows = -2;
	}

	menu_layout(m, &loc);
}


/**
 * Redisplay a single store entry
 */
static void store_display_entry(struct menu *menu, int oid, bool cursor, int row,
								int col, int width)
{
	struct object *obj;
	int32_t x;
	uint32_t desc = ODESC_PREFIX;

	char o_name[80];
	char out_val[160];
	uint8_t colour;
	int16_t obj_weight;

	struct store_context *ctx = menu_priv(menu);
	struct store *store = ctx->store;
	assert(store);

	/* Get the object */
	obj = ctx->list[oid];

	/* Describe the object - preserving insriptions in the home */
	if (store->feat == FEAT_HOME) {
		desc |= ODESC_FULL;
	} else {
		desc |= ODESC_FULL | ODESC_STORE;
	}
	object_desc(o_name, sizeof(o_name), obj, desc, player);

	/* Display the object */
	c_put_str(obj->kind->base->attr, o_name, row, col);

	/* Show weights */
	colour = curs_attrs[CURS_KNOWN][(int)cursor];
	obj_weight = object_weight_one(obj);
	strnfmt(out_val, sizeof out_val, "%3d.%d lb", obj_weight / 10,
			obj_weight % 10);
	c_put_str(colour, out_val, row, ctx->scr_places_x[LOC_WEIGHT]);

	/* Describe an object (fully) in a store */
	if (store->feat != FEAT_HOME) {
		/* Extract the "minimum" price */
		x = price_item(store, obj, false, 1);

		/* Make sure the player can afford it */
		if (player->au < x)
			colour = curs_attrs[CURS_UNKNOWN][(int)cursor];

		/* Actually draw the price */
		if (tval_can_have_charges(obj) && (obj->number > 1))
			strnfmt(out_val, sizeof out_val, "%9ld avg", (long)x);
		else
			strnfmt(out_val, sizeof out_val, "%9ld    ", (long)x);

		c_put_str(colour, out_val, row, ctx->scr_places_x[LOC_PRICE]);
	}
}


/**
 * Display store (after clearing screen)
 */
static void store_display_frame(struct store_context *ctx)
{
	char buf[80];
	struct store *store = ctx->store;
	struct owner *proprietor = store->owner;
	const char *welcome = store_welcome_for_player(store, player);

	/* Clear screen */
	Term_clear();

	/* The "Home" is special */
	if (store->feat == FEAT_HOME) {
		/* Put the owner name */
		put_str("Your Home", ctx->scr_places_y[LOC_OWNER], 1);

		/* Label the object descriptions */
		put_str("Home Inventory", ctx->scr_places_y[LOC_HEADER], 1);

		/* Show weight header */
		put_str("Weight", ctx->scr_places_y[LOC_HEADER],
				ctx->scr_places_x[LOC_WEIGHT] + 2);
	} else {
		/* Normal stores */
		const char *store_name = f_info[store->feat].name;
		const char *owner_name = proprietor->name;

		/* Put the owner name */
		put_str(owner_name, ctx->scr_places_y[LOC_OWNER], 1);

		/* Show the max price in the store (above prices) */
		strnfmt(buf, sizeof(buf), "%s (%ld)", store_name,
				(long)proprietor->max_cost);
		prt(buf, ctx->scr_places_y[LOC_OWNER],
			ctx->scr_places_x[LOC_OWNER] - strlen(buf));

		/* Label the object descriptions */
		put_str("Store Inventory", ctx->scr_places_y[LOC_HEADER], 1);

		/* Showing weight label */
		put_str("Weight", ctx->scr_places_y[LOC_HEADER],
				ctx->scr_places_x[LOC_WEIGHT] + 2);

		/* Label the asking price (in stores) */
		put_str("Price", ctx->scr_places_y[LOC_HEADER], ctx->scr_places_x[LOC_PRICE] + 4);
		if (store_accepts_larder_donations(store) && !ctx->inspect_only) {
			const struct world_larder_milestone_definition *next =
				world_larder_next_milestone(&player->larder);
			const struct world_larder_milestone_definition *current =
				world_larder_current_milestone(&player->larder);

			if (next) {
				strnfmt(buf, sizeof(buf),
					"Village larder: %u/%u for %s   [D] Donate catch",
					player->larder.food_points, next->target, next->name);
			} else if (current) {
				strnfmt(buf, sizeof(buf),
					"Village larder: %s secured; %u total   [D] Donate catch",
					current->name, player->larder.food_points);
			} else {
				strnfmt(buf, sizeof(buf),
					"Village larder: no authored milestone   [D] Donate catch");
			}
			put_str(buf, 2, 1);
		} else if (welcome) {
			/* Authored dialogue gets two wrapped rows and must not run under
			 * the SDL3 proprietor portrait. */
			text_out_hook = text_out_to_screen;
			text_out_indent = 1;
			text_out_wrap = ctx->usable_width - 2;
			Term_gotoxy(1, 2);
			text_out(welcome);
			text_out_indent = 0;
		}
	}
}


/**
 * Display help.
 */
static void store_display_help(struct store_context *ctx)
{
	struct store *store = ctx->store;
	int help_loc = ctx->scr_places_y[LOC_HELP_PROMPT];
	bool is_home = (store->feat == FEAT_HOME) ? true : false;

	/* Clear */
	clear_from(ctx->scr_places_y[LOC_HELP_CLEAR]);

	/* Prepare help hooks */
	text_out_hook = text_out_to_screen;
	text_out_indent = 1;
	Term_gotoxy(1, help_loc);

	if (OPT(player, rogue_like_commands))
		text_out_c(COLOUR_L_GREEN, "x");
	else
		text_out_c(COLOUR_L_GREEN, "l");

	text_out(" examines");
	if (!ctx->inspect_only) {
		text_out(" and ");
		text_out_c(COLOUR_L_GREEN, "p");
		text_out(" (or ");
		text_out_c(COLOUR_L_GREEN, "g");
		text_out(")");

		if (is_home) text_out(" picks up");
		else text_out(" purchases");
	}
	text_out(" an item. ");

	if (!ctx->inspect_only) {
		if (OPT(player, birth_no_selling) && !is_home) {
			text_out_c(COLOUR_L_GREEN, "d");
			text_out(" (or ");
			text_out_c(COLOUR_L_GREEN, "s");
			text_out(")");
			text_out(" gives an item to the store in return for its identification. Some wands and staves will also be recharged. ");
		} else {
			text_out_c(COLOUR_L_GREEN, "d");
			text_out(" (or ");
			text_out_c(COLOUR_L_GREEN, "s");
			text_out(")");
			if (is_home) text_out(" drops");
			else text_out(" sells");
			text_out(" an item from your inventory. ");
		}
		if (store_accepts_larder_donations(store)) {
			text_out_c(COLOUR_L_GREEN, "D");
			text_out(" donates a caught fish to the village larder. ");
		}
	}
	text_out_c(COLOUR_L_GREEN, "I");
	text_out(" inspects an item from your inventory. ");

	text_out_c(COLOUR_L_GREEN, "ESC");
	if (!ctx->inspect_only)
		text_out(" exits the building.");
	else
		text_out(" exits this screen.");

	text_out_indent = 0;
}

/**
 * Decides what parts of the store display to redraw.  Called on terminal
 * resizings and the redraw command.
 */
static void store_redraw(struct store_context *ctx)
{
	if (ctx->flags & (STORE_FRAME_CHANGE)) {
		store_display_frame(ctx);

		if (ctx->flags & STORE_SHOW_HELP)
			store_display_help(ctx);
		else
			prt(store_accepts_larder_donations(ctx->store) && !ctx->inspect_only ?
				"Press '?' for help; D donates a fishing catch." :
				"Press '?' for help.", ctx->scr_places_y[LOC_HELP_PROMPT], 1);

		ctx->flags &= ~(STORE_FRAME_CHANGE);
	}

	if (ctx->flags & (STORE_GOLD_CHANGE)) {
		prt(format("Gold Remaining: %9ld", (long)player->au),
				ctx->scr_places_y[LOC_AU], ctx->scr_places_x[LOC_AU]);
		ctx->flags &= ~(STORE_GOLD_CHANGE);
	}
}

struct store_quantity_context {
	struct store_context *store_context;
	const char *title;
	const char *prompt;
	int maximum;
};

static void store_quantity_refresh(const char *buffer, size_t cursor,
		bool first_time, void *user)
{
	struct store_quantity_context *context = user;

	(void)cursor;
	(void)first_time;
	if (!context || !context->store_context) return;
	ui_store_screen_present_quantity(context->store_context->store,
		context->title, context->prompt, buffer, context->maximum,
		context->store_context->usable_width);
}

static int store_get_quantity(struct store_context *ctx, const char *title,
		const char *prompt, int maximum)
{
	struct store_quantity_context context;
	char generated_prompt[96];
	int result;

	if (!Term->screen_hook) return get_quantity(prompt, maximum);
	if (!prompt) {
		strnfmt(generated_prompt, sizeof(generated_prompt),
			"Choose an amount from 0 to %d.", maximum);
		prompt = generated_prompt;
	}
	context.store_context = ctx;
	context.title = title;
	context.prompt = prompt;
	context.maximum = maximum;
	result = textui_get_quantity_with_refresh(prompt, maximum,
		store_quantity_refresh, &context);
	Term->screen_hook(NULL);
	return result;
}

static bool store_get_check(struct store_context *ctx, const char *title,
		const char *prompt, int32_t price)
{
	struct keypress ch;

	if (Term->screen_hook) {
		ui_store_screen_present_confirmation(ctx->store, title, prompt, price,
			ctx->usable_width);
	} else {
		prt(prompt, 0, 0);
	}

	/* Get an answer */
	ch = inkey();

	if (Term->screen_hook) Term->screen_hook(NULL);
	prt("", 0, 0);

	if (ch.code == ESCAPE) return (false);
	if (strchr("Nn", ch.code)) return (false);

	/* Success */
	return (true);
}

/*
 * Sell an object, or drop if it we're in the home.
 */
static bool store_sell(struct store_context *ctx)
{
	int amt;
	int get_mode = USE_EQUIP | USE_INVEN | USE_FLOOR | USE_QUIVER;

	struct store *store = ctx->store;

	struct object *obj;
	struct object object_type_body = OBJECT_NULL;
	struct object *temp_obj = &object_type_body;

	char o_name[120];

	item_tester tester = NULL;

	const char *reject = "You have nothing the shopkeeper wants.";
	const char *prompt = OPT(player, birth_no_selling) ? "Give which item? " : "Sell which item? ";

	assert(store);

	/* Clear all current messages */
	msg_flag = false;
	prt("", 0, 0);

	if (store->feat == FEAT_HOME) {
		prompt = "Drop which item? ";
		reject = "You have nothing to stash.";
	} else {
		tester = store_will_buy_tester;
		get_mode |= SHOW_PRICES;
	}

	/* Get an item */
	player->upkeep->command_wrk = USE_INVEN;

	if (!get_item(&obj, prompt, reject, CMD_DROP, tester, get_mode))
		return false;

	/* Cannot remove stickied objects */
	if (object_is_equipped(player->body, obj) && !obj_can_takeoff(obj)) {
		/* Oops */
		msg("Hmmm, it seems to be stuck.");

		/* Nope */
		return false;
	}

	/* Get a quantity */
	amt = store_get_quantity(ctx,
		store->feat == FEAT_HOME ? "Choose stash quantity" :
			(OPT(player, birth_no_selling) ? "Choose gift quantity" :
				"Choose sale quantity"),
		NULL, obj->number);

	/* Allow user abort */
	if (amt <= 0) return false;

	/* Get a copy of the object representing the number being sold */
	object_copy_amt(temp_obj, obj, amt);

	if (!store_check_num(store, temp_obj)) {
		object_wipe(temp_obj);
		if (store->feat == FEAT_HOME)
			msg("Your home is full.");
		else
			msg("I have not the room in my store to keep it.");

		return false;
	}

	/* Get a full description */
	object_desc(o_name, sizeof(o_name), temp_obj,
		ODESC_PREFIX | ODESC_FULL, player);

	/* Real store */
	if (store->feat != FEAT_HOME) {
		/* Extract the value of the items */
		int32_t price = price_item(store, temp_obj, true, amt);

		object_wipe(temp_obj);
		screen_save();

		/* Show price */
		if (!OPT(player, birth_no_selling))
			prt(format("Price: %ld", (long)price), 1, 0);

		/* Confirm sale */
		if (!store_get_check(ctx,
				OPT(player, birth_no_selling) ? "Confirm gift" : "Confirm sale",
				format("%s %s?", OPT(player, birth_no_selling) ? "Give" :
					"Sell", o_name),
				OPT(player, birth_no_selling) ? -1 : price)) {
			screen_load();
			return false;
		}

		screen_load();

		cmdq_push(CMD_SELL);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	} else { /* Player is at home */
		object_wipe(temp_obj);
		cmdq_push(CMD_STASH);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	}

	/* Update the display */
	ctx->flags |= STORE_GOLD_CHANGE;

	return true;
}

/** Queue a selected fishing catch for donation to the village larder. */
static bool store_donate_catch(struct store_context *ctx)
{
	struct object *obj;
	int quantity;

	if (!ctx || ctx->inspect_only ||
			!store_accepts_larder_donations(ctx->store)) {
		return false;
	}
	msg_flag = false;
	prt("", 0, 0);
	player->upkeep->command_wrk = USE_INVEN;
	if (!get_item(&obj, "Donate which fishing catch? ",
			"You are not carrying a fishing catch.", CMD_LARDER_DONATE,
			world_larder_object_is_catch, USE_INVEN | IS_HARMLESS)) {
		return false;
	}
	quantity = store_get_quantity(ctx, "Choose donation quantity", NULL,
		obj->number);
	if (quantity <= 0) return false;
	if (cmdq_push(CMD_LARDER_DONATE)) return false;
	cmd_set_arg_item(cmdq_peek(), "item", obj);
	cmd_set_arg_number(cmdq_peek(), "quantity", quantity);
	ctx->flags |= STORE_FRAME_CHANGE;
	return true;
}



/**
 * Buy an object from a store
 */
static bool store_purchase(struct store_context *ctx, int item, bool single)
{
	struct store *store = ctx->store;

	struct object *obj = ctx->list[item];
	struct object *dummy = NULL;

	char o_name[80];

	int amt, num;

	int32_t price;

	/* Clear all current messages */
	msg_flag = false;
	prt("", 0, 0);


	/*** Check the player can get any at all ***/

	/* Get an amount if we weren't given one */
	if (single) {
		amt = 1;

		/* Check if the player can afford any at all */
		if (store->feat != FEAT_HOME &&
				player->au < price_item(store, obj, false, 1)) {
			msg("You do not have enough gold for this item.");
			return false;
		}
	} else {
		bool flavor_aware;

		if (store->feat == FEAT_HOME) {
			amt = obj->number;
		} else {
			/* Price of one */
			price = price_item(store, obj, false, 1);

			/* Check if the player can afford any at all */
			if ((uint32_t)player->au < (uint32_t)price) {
				msg("You do not have enough gold for this item.");
				return false;
			}

			/* Work out how many the player can afford */
			if (price == 0)
				amt = obj->number; /* Prevent division by zero */
			else
				amt = player->au / price;

			if (amt > obj->number) amt = obj->number;

			/* Double check for wands/staves */
			if ((player->au >= price_item(store, obj, false, amt+1)) &&
				(amt < obj->number))
				amt++;
		}

		/* Limit to the number that can be carried */
		amt = MIN(amt, inven_carry_num(player, obj));

		/* Fail if there is no room.  Don't leak information about
		 * unknown flavors for a purchase (getting it from home doesn't
		 * leak information since it doesn't show the true flavor). */
		flavor_aware = object_flavor_is_aware(obj);
		if (amt <= 0 || (!flavor_aware && store->feat != FEAT_HOME &&
				pack_is_full())) {
			msg("You cannot carry that many items.");
			return false;
		}

		/* Find the number of this item in the inventory.  As above,
		 * avoid leaking information about unknown flavors. */
		if (!flavor_aware && store->feat != FEAT_HOME)
			num = 0;
		else
			num = find_inven(obj);

		strnfmt(o_name, sizeof o_name, "%s how many%s? (max %d) ",
				(store->feat == FEAT_HOME) ? "Take" : "Buy",
				num ? format(" (you have %d)", num) : "", amt);

		/* Get a quantity */
		amt = store_get_quantity(ctx,
			store->feat == FEAT_HOME ? "Choose retrieval quantity" :
				"Choose purchase quantity",
			o_name, amt);

		/* Allow user abort */
		if (amt <= 0) return false;
	}

	/* Get desired object */
	dummy = object_new();
	object_copy_amt(dummy, obj, amt);

	/* Ensure we have room */
	if (!inven_carry_okay(dummy)) {
		msg("You cannot carry that many items.");
		object_delete(NULL, NULL, &dummy);
		return false;
	}

	/* Attempt to buy it */
	if (store->feat != FEAT_HOME) {
		bool response;

		bool obj_is_book = tval_is_book_k(obj->kind);
		bool obj_can_use = !obj_is_book || obj_can_browse(obj);

		/* Describe the object (fully) */
		object_desc(o_name, sizeof(o_name), dummy,
			ODESC_PREFIX | ODESC_FULL | ODESC_STORE, player);

		/* Extract the price for the entire stack */
		price = price_item(store, dummy, false, dummy->number);

		screen_save();

		/* Show price */
		prt(format("Price: %ld", (long)price), 1, 0);

		/* Confirm purchase */
		response = store_get_check(ctx, "Confirm purchase",
				format("Buy %s?%s",
					o_name,
					obj_can_use ? "" : " (Can't use!)"), price);

		screen_load();

		/* Negative response, so give up */
		if (!response) return false;

		cmdq_push(CMD_BUY);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	} else {
		/* Home is much easier */
		cmdq_push(CMD_RETRIEVE);
		cmd_set_arg_item(cmdq_peek(), "item", obj);
		cmd_set_arg_number(cmdq_peek(), "quantity", amt);
	}

	/* Update the display */
	ctx->flags |= STORE_GOLD_CHANGE;

	object_delete(NULL, NULL, &dummy);

	/* Not kicked out */
	return true;
}


/**
 * Examine an item in a store
 */
static void store_examine(struct store_context *ctx, int item)
{
	struct object *obj;
	char header[120];
	textblock *tb;
	region area = { 0, 0, 0, 0 };
	uint32_t odesc_flags = ODESC_PREFIX | ODESC_FULL;

	if (item < 0) return;

	/* Get the actual object */
	obj = ctx->list[item];

	/* Items in the home get less description */
	if (ctx->store->feat == FEAT_HOME) {
		odesc_flags |= ODESC_CAPITAL;
	} else {
		odesc_flags |= ODESC_STORE;
	}

	/* No flush needed */
	msg_flag = false;

	/* Show full info in most stores, but normal info in player home */
	tb = object_info(obj, OINFO_NONE);
	object_desc(header, sizeof(header), obj, odesc_flags, player);

	textui_textblock_show(tb, area, header);
	textblock_free(tb);

	/* Browse book, then prompt for a command */
	if (obj_can_browse(obj))
		textui_book_browse(obj);
}


static void store_menu_set_selections(struct menu *menu, bool knowledge_menu)
{
	if (knowledge_menu) {
		if (OPT(player, rogue_like_commands)) {
			/* These two can't intersect! */
			menu->cmd_keys = "?|IeilxLX";
			menu->selections = "abcdfghmnopqrstuvwyzABCDEFGHJKMNOPQRSTUVWYZ";
		} else {
			/* These two can't intersect! */
			menu->cmd_keys = "?|IeilLX";
			menu->selections = "abcdfghjkmnopqrstuvwxyzABCDEFGHJKMNOPQRSTUVWYZ";
		}
	} else {
		if (OPT(player, rogue_like_commands)) {
			/* These two can't intersect! */
			menu->cmd_keys = "\x04\x05\x10?={|}~CDEILPTXdegilpswx"; /* \x10 = ^p , \x04 = ^D, \x05 = ^E */
			menu->selections = "abcfmnoqrtuvyzABFGHJKMNOQRSUVWYZ";
		} else {
			/* These two can't intersect! */
			menu->cmd_keys = "\x05\x010?={|}~CDEILXbdegiklpstwx"; /* \x05 = ^E, \x10 = ^p */
			menu->selections = "acfhjmnoqruvyzABFGHJKMNOPQRSTUVWYZ";
		}
	}
}

static void store_menu_recalc(struct menu *m)
{
	struct store_context *ctx = menu_priv(m);
	menu_setpriv(m, ctx->store->stock_num, ctx);
}

/**
 * Process a command in a store
 *
 * Note that we must allow the use of a few "special" commands in the stores
 * which are not allowed in the dungeon, and we must disable some commands
 * which are allowed in the dungeon but not in the stores, to prevent chaos.
 */
static bool store_process_command_key(struct keypress kp)
{
	int cmd = 0;

	/* No flush needed */
	prt("", 0, 0);
	msg_flag = false;

	/* Process the keycode */
	switch (kp.code) {
		case 'T': /* roguelike */
		case 't': cmd = CMD_TAKEOFF; break;

		case KTRL('D'): /* roguelike */
		case 'k': textui_cmd_ignore(); break;

		case 'P': /* roguelike */
		case 'b': textui_spell_browse(); break;

		case '~': textui_browse_knowledge(); break;
		case 'I': textui_obj_examine(); break;
		case 'w': cmd = CMD_WIELD; break;
		case '{': cmd = CMD_INSCRIBE; break;
		case '}': cmd = CMD_UNINSCRIBE; break;

		case 'e': do_cmd_equip(); break;
		case 'i': do_cmd_inven(); break;
		case '|': do_cmd_quiver(); break;
		case KTRL('E'): toggle_inven_equip(); break;
		case 'C': do_cmd_change_name(); break;
		case KTRL('P'): do_cmd_messages(); break;
		case ')': do_cmd_save_screen(); break;

		default: return false;
	}

	if (cmd)
		cmdq_push_repeat(cmd, 0);

	return true;
}

/**
 * Select an item from the store's stock, and return the stock index
 */
static int store_get_stock(struct menu *m, int oid)
{
	ui_event e;
	int no_act = m->flags & MN_NO_ACTION;

	/* Set a flag to make sure that we get the selection or escape
	 * without running the menu handler */
	m->flags |= MN_NO_ACTION;
	e = menu_select(m, 0, true);
	if (!no_act) {
		m->flags &= ~MN_NO_ACTION;
	}

	if (e.type == EVT_SELECT) {
		return m->cursor;
	} else if (e.type == EVT_ESCAPE) {
		return -1;
	}

	/* if we do not have a new selection, just return the original item */
	return oid;
}

/** Enum for context menu entries */
enum {
	ACT_INSPECT_INVEN,
	ACT_SELL,
	ACT_DONATE,
	ACT_EXAMINE,
	ACT_BUY,
	ACT_BUY_ONE,
	ACT_EXIT
};

struct store_action_entry {
	const char *label;
	char tag;
	int value;
};

struct store_action_menu_context {
	struct store_context *store_context;
	const char *title;
	const char *context;
	const struct store_action_entry *entries;
	int count;
};

static char store_action_tag(struct menu *menu, int oid)
{
	struct store_action_menu_context *context = menu_priv(menu);

	return context->entries[oid].tag;
}

static void store_action_display(struct menu *menu, int oid, bool cursor,
		int row, int col, int width)
{
	struct store_action_menu_context *context = menu_priv(menu);

	Term_putstr(col, row, width, cursor ? COLOUR_L_BLUE : COLOUR_WHITE,
		context->entries[oid].label);
}

static const menu_iter store_action_menu_iter = {
	store_action_tag,
	NULL,
	store_action_display,
	NULL,
	NULL
};

static void present_store_action_menu(struct menu *menu)
{
	struct store_action_menu_context *context = menu_priv(menu);
	const char *labels[8];
	char tags[8];
	int i;

	if (!context || !context->store_context) return;
	for (i = 0; i < context->count; i++) {
		labels[i] = context->entries[i].label;
		tags[i] = context->entries[i].tag;
	}
	ui_store_screen_present_actions(context->store_context->store,
		context->title, context->context, labels, tags, context->count,
		menu->cursor, context->store_context->usable_width);
}

static int store_select_action(struct store_context *ctx, const char *title,
		const char *context, const struct store_action_entry *entries, int count,
		int mx, int my, const char *prompt)
{
	struct store_action_menu_context menu_context;
	struct menu menu;
	region loc = { 4, 8, 40, 8 };
	ui_event event;
	bool buy_g_alias = false;
	bool semantic = Term->screen_hook != NULL;
	size_t longest = 0;
	int i;

	assert(count > 0 && count <= 8);
	menu_context.store_context = ctx;
	menu_context.title = title;
	menu_context.context = context;
	menu_context.entries = entries;
	menu_context.count = count;
	menu_init(&menu, MN_SKIN_SCROLL, &store_action_menu_iter);
	menu_setpriv(&menu, count, &menu_context);
	for (i = 0; i < count; i++) {
		if (entries[i].value == ACT_BUY) buy_g_alias = true;
		longest = MAX(longest, strlen(entries[i].label));
	}
	if (buy_g_alias) menu.switch_keys = "g";
	if (semantic) {
		menu.present_hook = present_store_action_menu;
		loc.width = MAX(1, MIN(ctx->usable_width - 8, 40));
	} else {
		loc.width = (int)longest + 5;
		loc.col = mx > Term->wid - loc.width - 1 ?
			Term->wid - loc.width - 1 : mx + 1;
		if (my > Term->hgt - count - 1) {
			if (my - count - 1 <= 0) {
				loc.row = 1;
				loc.col = Term->wid - loc.width - 1;
			} else {
				loc.row = Term->hgt - count - 1;
			}
		} else {
			loc.row = my + 1;
		}
	}
	loc.page_rows = count;
	menu_layout(&menu, &loc);
	msg_flag = false;
	if (!semantic) {
		screen_save();
		region_erase_bordered(&menu.boundary);
		prt(prompt, 0, 0);
	}
	event = menu_select(&menu, 0, true);
	if (semantic) {
		Term->screen_hook(NULL);
	} else {
		screen_load();
	}
	if (event.type == EVT_KBRD && event.key.code == 'g') return ACT_BUY;
	return event.type == EVT_SELECT ? entries[menu.cursor].value : -1;
}

/* pick the context menu options appropiate for a store */
static int context_menu_store(struct store_context *ctx, const int oid, int mx, int my)
{
	struct store *store = ctx->store;
	bool home = (store->feat == FEAT_HOME) ? true : false;
	struct store_action_entry entries[4];
	int count = 0;
	int selected;
	(void)oid;

	entries[count++] = (struct store_action_entry) {
		"Inspect inventory", 'I', ACT_INSPECT_INVEN };
	if (!ctx->inspect_only) {
		entries[count++] = (struct store_action_entry) {
			home ? "Stash" : "Sell", 'd', ACT_SELL };
		if (store_accepts_larder_donations(store)) {
			entries[count++] = (struct store_action_entry) {
				"Donate fishing catch", 'D', ACT_DONATE };
		}
	}
	entries[count++] = (struct store_action_entry) {
		"Exit", '`', ACT_EXIT };
	selected = store_select_action(ctx, "Store actions",
		"Choose what to do here.", entries, count, mx, my,
		"(Enter to select, ESC) Command:");

	switch (selected) {
		case ACT_SELL:
			store_sell(ctx);
			break;
		case ACT_DONATE:
			store_donate_catch(ctx);
			break;
		case ACT_INSPECT_INVEN:
			textui_obj_examine();
			break;
		case ACT_EXIT:
			return false;
	}

	return true;
}
/* pick the context menu options appropiate for an item available in a store */
static bool context_menu_store_item(struct store_context *ctx, const int oid, int mx, int my)
{
	struct store *store = ctx->store;
	bool home = (store->feat == FEAT_HOME) ? true : false;
	struct object *obj = ctx->list[oid];
	struct store_action_entry entries[3];
	int count = 0;
	int selected;
	char header[120];
	char prompt[240];

	object_desc(header, sizeof(header), obj,
		ODESC_PREFIX | ODESC_FULL | ((home) ? 0 : ODESC_STORE), player);

	entries[count++] = (struct store_action_entry) {
		"Examine", OPT(player, rogue_like_commands) ? 'x' : 'l',
		ACT_EXAMINE };
	if (!ctx->inspect_only) {
		entries[count++] = (struct store_action_entry) {
			home ? "Take" : "Buy", 'p', ACT_BUY };
		if (obj->number > 1) {
			entries[count++] = (struct store_action_entry) {
				home ? "Take one" : "Buy one", 'o', ACT_BUY_ONE };
		}
	}
	strnfmt(prompt, sizeof(prompt), "(Enter to select, ESC) Command for %s:",
		header);
	selected = store_select_action(ctx, home ? "Stored item" : "Shop item",
		header, entries, count, mx, my, prompt);

	switch (selected) {
		case ACT_EXAMINE:
			store_examine(ctx, oid);
			return false;
		case ACT_BUY:
			return store_purchase(ctx, oid, false);
		case ACT_BUY_ONE:
			return store_purchase(ctx, oid, true);
	}
	return false;
}

/**
 * Handle store menu input
 */
static bool store_menu_handle(struct menu *m, const ui_event *event, int oid)
{
	bool processed = true;
	struct store_context *ctx = menu_priv(m);
	struct store *store = ctx->store;

	/* Command handlers may open quantity, confirmation, item, or information
	 * interfaces.  Remove the stock snapshot first so those nested interfaces
	 * can own the screen; the next menu refresh restores it. */
	if (Term->screen_hook) Term->screen_hook(NULL);
	
	if (event->type == EVT_SELECT) {
		ctx->notice[0] = '\0';
		/* HACK there's no mouse event coordinates to use for */
		/* menu_store_item, so fake one as if mouse clicked on letter */
		bool purchased = context_menu_store_item(ctx, oid, 1, m->active.row + oid);
		ctx->flags |= (STORE_FRAME_CHANGE | STORE_GOLD_CHANGE);

		/* Let the game handle any core commands (equipping, etc) */
		cmdq_pop(CTX_STORE);

		/* Notice and handle stuff */
		notice_stuff(player);
		handle_stuff(player);

		if (purchased) {
			/* Display the store */
			store_display_recalc(ctx);
			store_menu_recalc(m);
			store_redraw(ctx);
		}

		return true;
	} else if (event->type == EVT_MOUSE) {
		if (event->mouse.button == 2) {
			/* exit the store? what already does this? menu_handle_mouse
			 * so exit this so that menu_handle_mouse will be called */
			return false;
		} else if (event->mouse.button == 1) {
			bool action = false;
			if ((event->mouse.y == 0) || (event->mouse.y == 1)) {
				/* show the store context menu */
				if (context_menu_store(ctx, oid, event->mouse.x, event->mouse.y) == false)
					return false;

				action = true;
			} else if ((oid >= 0) && (event->mouse.y == m->active.row + oid)) {
				/* if press is on a list item, so store item context */
				context_menu_store_item(ctx, oid, event->mouse.x,
										event->mouse.y);
				action = true;
			}

			if (action) {
				ctx->flags |= (STORE_FRAME_CHANGE | STORE_GOLD_CHANGE);

				/* Let the game handle any core commands (equipping, etc) */
				cmdq_pop(CTX_STORE);

				/* Notice and handle stuff */
				notice_stuff(player);
				handle_stuff(player);

				/* Display the store */
				store_display_recalc(ctx);
				store_menu_recalc(m);
				store_redraw(ctx);

				return true;
			}
		}
	} else if (event->type == EVT_KBRD) {
		ctx->notice[0] = '\0';
		switch (event->key.code) {
			case 's':
			case 'd': store_sell(ctx); break;
			case 'D':
				if (store_accepts_larder_donations(store))
					(void)store_donate_catch(ctx);
				else
					store_sell(ctx);
				break;

			case 'p':
			case 'g':
				/* use the old way of purchasing items */
				msg_flag = false;
				if (store->feat != FEAT_HOME) {
					prt("Purchase which item? (ESC to cancel, Enter to select)",
						0, 0);
				} else {
					prt("Get which item? (Esc to cancel, Enter to select)",
						0, 0);
				}
				oid = store_get_stock(m, oid);
				prt("", 0, 0);
				if (oid >= 0) {
					store_purchase(ctx, oid, false);
				}
				break;
			case 'l':
			case 'L':
			case 'x':
			case 'X':
				/* Examine the highlighted stock directly; Enter also offers it. */
				if (oid >= 0 && oid < store->stock_num) store_examine(ctx, oid);
				break;

			case '?': {
				/* Toggle help */
				if (ctx->flags & STORE_SHOW_HELP)
					ctx->flags &= ~(STORE_SHOW_HELP);
				else
					ctx->flags |= STORE_SHOW_HELP;

				/* Redisplay */
				ctx->flags |= STORE_INIT_CHANGE;

				store_display_recalc(ctx);
				store_redraw(ctx);

				break;
			}

			case '=': {
				do_cmd_options();
				store_menu_set_selections(m, false);
				break;
			}

			default:
				processed = store_process_command_key(event->key);
		}

		/* Let the game handle any core commands (equipping, etc) */
		cmdq_pop(CTX_STORE);

		if (processed) {
			event_signal(EVENT_INVENTORY);
			event_signal(EVENT_EQUIPMENT);
		}

		/* Notice and handle stuff */
		notice_stuff(player);
		handle_stuff(player);
		if (ctx->flags & (STORE_FRAME_CHANGE | STORE_GOLD_CHANGE)) {
			store_redraw(ctx);
		}

		return processed;
	}

	return false;
}

static region store_menu_region = { 1, 4, -1, -2 };
static const menu_iter store_menu =
{
	NULL,
	NULL,
	store_display_entry,
	store_menu_handle,
	NULL
};

static void present_store_menu(struct menu *menu)
{
	struct store_context *ctx = menu_priv(menu);

	if (!ctx) return;
	ui_store_screen_present(menu, ctx->store, ctx->list, ctx->inspect_only,
		ctx->usable_width, ctx->notice);
}

/**
 * Init the store menu
 */
static void store_menu_init(struct store_context *ctx, struct store *store, bool inspect_only)
{
	struct menu *menu = &ctx->menu;

	ctx->store = store;
	ctx->flags = STORE_INIT_CHANGE;
	ctx->inspect_only = inspect_only;
	ctx->notice[0] = '\0';
	ctx->list = mem_zalloc(sizeof(struct object *) * z_info->store_inven_max);

	store_stock_list(ctx->store, ctx->list, z_info->store_inven_max);

	/* Init the menu structure */
	menu_init(menu, MN_SKIN_SCROLL, &store_menu);
	menu_setpriv(menu, 0, ctx);
	menu->present_hook = present_store_menu;

	/* Calculate the positions of things and draw */
	menu_layout(menu, &store_menu_region);
	store_menu_set_selections(menu, inspect_only);
	store_display_recalc(ctx);
	store_menu_recalc(menu);
	store_redraw(ctx);
}

/**
 * Display contents of a store from knowledge menu
 *
 * The only allowed actions are 'I' to inspect an item
 */
void textui_store_knowledge(int n)
{
	struct store_context ctx;

	screen_save();
	clear_from(0);

	store_menu_init(&ctx, &stores[n], true);
	menu_select(&ctx.menu, 0, false);
	if (Term->screen_hook) Term->screen_hook(NULL);

	/* Flush messages XXX XXX XXX */
	event_signal(EVENT_MESSAGE_FLUSH);

	screen_load();

	mem_free(ctx.list);
}


/**
 * Handle stock change.
 */
static void refresh_stock(game_event_type type, game_event_data *unused, void *user)
{
	struct store_context *ctx = user;
	struct menu *menu = &ctx->menu;

	store_stock_list(ctx->store, ctx->list, z_info->store_inven_max);

	/* Display the store */
	store_display_recalc(ctx);
	store_menu_recalc(menu);
	store_redraw(ctx);
}

/**
 * Enter a store.
 */
void enter_store(game_event_type type, game_event_data *data, void *user)
{
	struct store *store = store_at(cave, player->grid);

	/* Check that we're on a store */
	if (!store) {
		msg("You see no store here.");
		return;
	}

	sound((store->feat == FEAT_HOME) ? MSG_STORE_HOME : MSG_STORE_ENTER);

	/* Shut down the normal game view */
	event_signal(EVENT_LEAVE_WORLD);
}

/** Retain feedback while the semantic shop screen covers terminal messages. */
static void store_message(game_event_type type, game_event_data *data, void *user)
{
	struct store_context *ctx = user;
	(void)type;
	my_strcpy(ctx->notice, data->message.msg, sizeof(ctx->notice));
}

/** Present one receipt after the command has committed items, XP and unlocks. */
static void store_donation_receipt(game_event_type type, game_event_data *data,
		void *user)
{
	struct store_context *ctx = user;
	const struct world_larder_report *report = data->donation.report;
	(void)type;
	ui_store_screen_present_donation(ctx->store, data->donation.name,
		&player->larder, report, true);
	/* The receipt replaces the cramped, last-line-only shop announcement. */
	ctx->notice[0] = '\0';
	ctx->flags |= STORE_FRAME_CHANGE;
}

/**
 * Interact with a store.
 */
void use_store(game_event_type type, game_event_data *data, void *user)
{
	struct store *store = store_at(cave, player->grid);
	struct store_context ctx;

	/* Check that we're on a store */
	if (!store) return;

	/*** Display ***/

	/* Save current screen (ie. dungeon) */
	screen_save();
	msg_flag = false;
	if (store->relic_broker) {
		ui_relic_broker(store);
		if (Term->screen_hook) Term->screen_hook(NULL);
		player->upkeep->energy_use = z_info->move_energy;
		event_signal(EVENT_MESSAGE_FLUSH);
		screen_load();
		return;
	}
	if (store->objective_briefing && player->upkeep &&
			!player->upkeep->fishing_quests_announced) {
		ui_player_present_fishing_quest_briefing(
			store->objective_briefing, true);
		player->upkeep->fishing_quests_announced = true;
		msg("%d optional quests are now listed under Shift+C > Quests.",
			world_objective_count());
	}

	/* Get a array version of the store stock, register handler for changes */
	event_add_handler(EVENT_STORECHANGED, refresh_stock, &ctx);
	store_menu_init(&ctx, store, false);

	/* Generic Angband hints are inappropriate for stores with authored
	 * dialogue; their fixed introduction is already displayed in-frame. */
	if (store->feat != FEAT_HOME &&
			!store_welcome_for_player(store, player))
		prt_welcome(store->owner);

	/* Shopping */
	/* Semantic shops own their message area and donation cards.  The legacy
	 * top-line handler would otherwise insert hidden "-more-" input waits
	 * behind those screens, before the receipt can even be displayed. */
	if (Term->screen_hook) {
		event_remove_handler(EVENT_MESSAGE, display_message, NULL);
	}
	event_add_handler(EVENT_MESSAGE, store_message, &ctx);
	event_add_handler(EVENT_LARDER_DONATED, store_donation_receipt, &ctx);
	menu_select(&ctx.menu, 0, false);
	event_remove_handler(EVENT_LARDER_DONATED, store_donation_receipt, &ctx);
	event_remove_handler(EVENT_MESSAGE, store_message, &ctx);
	if (Term->screen_hook) {
		event_add_handler(EVENT_MESSAGE, display_message, NULL);
	}
	if (Term->screen_hook) Term->screen_hook(NULL);

	/* Shopping's done */
	event_remove_handler(EVENT_STORECHANGED, refresh_stock, &ctx);
	msg_flag = false;
	mem_free(ctx.list);

	/* Take a turn */
	player->upkeep->energy_use = z_info->move_energy;

	/* Flush messages */
	event_signal(EVENT_MESSAGE_FLUSH);

	/* Load the screen */
	screen_load();
}

void leave_store(game_event_type type, game_event_data *data, void *user)
{
	/* Disable repeats */
	cmd_disable_repeat();

	sound(MSG_STORE_LEAVE);

	/* Switch back to the normal game view. */
	event_signal(EVENT_ENTER_WORLD);

	/* Update the visuals */
	player->upkeep->update |= (PU_UPDATE_VIEW | PU_MONSTERS);

	/* Redraw entire screen */
	player->upkeep->redraw |= (PR_BASIC | PR_EXTRA);

	/* Redraw map */
	player->upkeep->redraw |= (PR_MAP);
}
