/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-store-screen.c
 * \brief Semantic presentation adapter shared by ordinary stores and Home.
 */

#include "angband.h"

#include "obj-desc.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "store.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-screen.h"
#include "ui-store-screen.h"
#include "ui-story.h"
#include "world-larder.h"
#include "world-larder-data.h"

#define UI_STORE_SCREEN_CAPACITY 64

void ui_store_screen_present_donation(const struct store *store,
		const char *name, const struct world_larder_state *larder,
		const struct world_larder_report *report, bool wait_for_ack)
{
	const struct world_larder_milestone_definition *next;
	char body[2048], progress[256];
	if (!name || !larder || !report) return;
	next = world_larder_next_milestone(larder);
	strnfmt(body, sizeof(body), "Thank you for %s.\n\n"
		"Added to the larder: %u meal portion%s\nExperience earned: %d XP\n\n",
		name, report->contributed_points,
		report->contributed_points == 1 ? "" : "s", report->experience);
	if (report->milestone_reached) {
		my_strcat(body, report->milestone_reached->message, sizeof(body));
		my_strcat(body, "\n\n", sizeof(body));
		my_strcat(body, report->milestone_reached->reward, sizeof(body));
	} else {
		if (next) {
			strnfmt(progress, sizeof(progress), "Village reserve: %u / %u "
				"meal portions toward %s.", larder->food_points,
				next->target, next->name);
		} else {
			strnfmt(progress, sizeof(progress), "Village reserve secured. "
				"Total donated: %u meal portions.", larder->food_points);
		}
		my_strcat(body, progress, sizeof(body));
	}
	ui_story_show(report->milestone_reached ? "The larder is ready" :
		"A gift for the village", store && store->owner ?
		store->owner->name : "General Store", body,
		"Shift+C > Quests records your objectives and progress.", wait_for_ack);
}

void ui_store_screen_present(struct menu *menu, struct store *store,
		struct object *const *stock, bool inspect_only, int usable_width,
		const char *notice)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[UI_STORE_SCREEN_CAPACITY];
	char labels[UI_STORE_SCREEN_CAPACITY][160];
	char weights[UI_STORE_SCREEN_CAPACITY][32];
	char prices[UI_STORE_SCREEN_CAPACITY][32];
	char subtitle[256];
	const char *title;
	const char *help;
	const char *welcome;
	bool home;
	int count;
	int i;

	if (!Term || !Term->screen_hook || !menu || !store || !stock) return;
	home = store->feat == FEAT_HOME;
	welcome = store_welcome_for_player(store, player);
	count = MIN(MIN((int)store->stock_num, menu->count),
		UI_STORE_SCREEN_CAPACITY);
	memset(rows, 0, sizeof(rows));
	memset(labels, 0, sizeof(labels));
	memset(weights, 0, sizeof(weights));
	memset(prices, 0, sizeof(prices));
	for (i = 0; i < count; i++) {
		struct object *obj = stock[i];
		int16_t weight;

		if (!obj || !obj->kind) continue;
		object_desc(labels[i], sizeof(labels[i]), obj,
			ODESC_PREFIX | ODESC_FULL | (home ? 0 : ODESC_STORE), player);
		weight = object_weight_one(obj);
		strnfmt(weights[i], sizeof(weights[i]), "%d.%d lb",
			weight / 10, ABS(weight % 10));
		if (!home) {
			int32_t price = price_item(store, obj, false, 1);

			if (tval_can_have_charges(obj) && obj->number > 1) {
				strnfmt(prices[i], sizeof(prices[i]), "%ld avg",
					(long)price);
			} else {
				strnfmt(prices[i], sizeof(prices[i]), "%ld gold",
					(long)price);
			}
		}
		rows[i].label = labels[i];
		rows[i].prefix = weights[i];
		rows[i].detail = prices[i];
		rows[i].glyph = object_char(obj);
		rows[i].attr = object_attr(obj);
		/* A genuinely black flavour still needs a visible menu swatch. */
		if (rows[i].attr == COLOUR_DARK) rows[i].attr = COLOUR_SLATE;
		rows[i].tag = menu->selections && menu->selections[i] ?
			menu->selections[i] : 0;
		rows[i].enabled = true;
	}
	if (home) {
		title = "Your Home";
		strnfmt(subtitle, sizeof(subtitle),
			"Stored possessions   Carried gold: %ld", (long)player->au);
		help = inspect_only ?
			"Enter examines   Escape returns" :
			"Enter actions   D stashes   L examines   ? help   Escape leaves";
	} else {
		title = f_info[store->feat].name;
		strnfmt(subtitle, sizeof(subtitle), "%s   Your gold: %ld",
			store->owner ? store->owner->name : "Unattended",
			(long)player->au);
		help = inspect_only ?
			"Enter examines   Escape returns" :
			(store_accepts_larder_donations(store) ?
			"Enter actions   d sells   Shift+D donates   L examines   Escape leaves" :
			"Enter actions   D sells   L examines   ? help   Escape leaves");
	}
	screen.kind = UI_SCREEN_STORE;
	screen.title = title;
	screen.subtitle = subtitle;
	screen.help = help;
	screen.context_title = notice && notice[0] ? "Shop message" : NULL;
	screen.context = notice && notice[0] ? notice : (welcome ? welcome : "");
	screen.content_col = 4;
	screen.content_row = welcome && !home ? 8 : 7;
	screen.content_cols = MAX(40, MIN(Term->wid - 8, usable_width - 8));
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = count > 0 ? MIN(MAX(0, menu->cursor), count - 1) : -1;
	screen.row_offset = MAX(0, menu->top);
	screen.row_count = count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}

void ui_store_screen_present_confirmation(struct store *store,
		const char *title, const char *prompt, int32_t price, int usable_width)
{
	struct ui_screen screen = { 0 };
	char subtitle[256];
	const char *owner;

	if (!Term || !Term->screen_hook || !store || !title || !prompt) return;
	owner = store->owner ? store->owner->name : "Unattended";
	if (price >= 0) {
		strnfmt(subtitle, sizeof(subtitle), "%s   Total: %ld gold", owner,
			(long)price);
	} else {
		strnfmt(subtitle, sizeof(subtitle), "%s", owner);
	}
	screen.kind = UI_SCREEN_STORE;
	screen.title = title;
	screen.subtitle = subtitle;
	screen.help = "Any key accepts   N or Escape cancels";
	screen.context = prompt;
	screen.content_col = 4;
	screen.content_row = 8;
	screen.content_cols = MAX(40, MIN(Term->wid - 8, usable_width - 8));
	screen.content_rows = 1;
	screen.cursor = -1;
	Term->screen_hook(&screen);
}

void ui_store_screen_present_quantity(struct store *store, const char *title,
		const char *prompt, const char *value, int maximum, int usable_width)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row row = { 0 };
	char subtitle[256];
	char label[80];
	const char *owner;

	if (!Term || !Term->screen_hook || !store || !title || !prompt ||
			!value) {
		return;
	}
	owner = store->owner ? store->owner->name :
		(store->feat == FEAT_HOME ? "Your Home" : "Unattended");
	strnfmt(subtitle, sizeof(subtitle), "%s   Maximum: %d", owner, maximum);
	if (value[0] == '*' || isalpha((unsigned char)value[0])) {
		strnfmt(label, sizeof(label), "Quantity: all (%d)", maximum);
	} else {
		strnfmt(label, sizeof(label), "Quantity: %s", value[0] ? value : "0");
	}
	row.label = label;
	row.attr = COLOUR_WHITE;
	row.enabled = true;
	screen.kind = UI_SCREEN_STORE;
	screen.title = title;
	screen.subtitle = subtitle;
	screen.help =
		"Type an amount   * takes all   Enter accepts   Escape cancels";
	screen.context = prompt;
	screen.content_col = 4;
	screen.content_row = 8;
	screen.content_cols = MAX(40, MIN(Term->wid - 8, usable_width - 8));
	screen.content_rows = 1;
	screen.cursor = 0;
	screen.row_count = 1;
	screen.rows = &row;
	Term->screen_hook(&screen);
}

void ui_store_screen_present_actions(struct store *store, const char *title,
		const char *context, const char *const *labels, const char *tags,
		int count, int cursor, int usable_width)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[UI_STORE_SCREEN_CAPACITY];
	char subtitle[256];
	const char *owner;
	int i;

	if (!Term || !Term->screen_hook || !store || !title || !labels ||
			count <= 0) {
		return;
	}
	count = MIN(count, UI_STORE_SCREEN_CAPACITY);
	owner = store->owner ? store->owner->name :
		(store->feat == FEAT_HOME ? "Your Home" : "Unattended");
	strnfmt(subtitle, sizeof(subtitle), "%s   Your gold: %ld", owner,
		(long)player->au);
	memset(rows, 0, sizeof(rows));
	for (i = 0; i < count; i++) {
		rows[i].label = labels[i];
		rows[i].tag = tags ? tags[i] : 0;
		rows[i].attr = COLOUR_WHITE;
		rows[i].enabled = true;
	}
	screen.kind = UI_SCREEN_STORE;
	screen.title = title;
	screen.subtitle = subtitle;
	screen.help = "Arrows choose   Enter accepts   Escape returns";
	screen.context_title = "Selected";
	screen.context = context ? context : "Choose an action.";
	screen.content_col = 4;
	screen.content_row = 8;
	screen.content_cols = MAX(40, MIN(Term->wid - 8, usable_width - 8));
	screen.content_rows = count;
	screen.cursor = MIN(MAX(0, cursor), count - 1);
	screen.row_count = count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}
