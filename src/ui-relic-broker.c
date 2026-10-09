/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
/** Native menu input and a knowledge-safe semantic store view. */
#include "angband.h"
#include "relic-broker-data.h"
#include "store.h"
#include "player-calcs.h"
#include "ui-relic-broker.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-output.h"
#include "ui-screen.h"
#include "ui-store.h"

static region broker_region(void)
{
	int width = Term->wid;
	region area;
	if (textui_store_usable_width_hook) width = textui_store_usable_width_hook(width);
	area.col = 4;
	area.row = 8;
	area.width = MAX(1, width - 8);
	area.page_rows = MAX(1, Term->hgt - area.row - 3);
	return area;
}

static void lot_label(int index, char *buffer, size_t size)
{
	const struct relic_broker_lot *lot = &player->broker.lots[index];
	const struct relic_broker_band *band = relic_broker_band(lot->band);
	strnfmt(buffer, size, "%s - %s", band->name, relic_broker_lot_category(lot));
}

void ui_relic_broker_present(struct store *store, int cursor, int top)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[RELIC_BROKER_MAX_LOTS] = { 0 };
	char labels[RELIC_BROKER_MAX_LOTS][96];
	char prices[RELIC_BROKER_MAX_LOTS][48];
	char subtitle[160];
	region area;
	int i;
	if (!Term || !store || !player || !player->broker.initialized) return;
	area = broker_region();
	strnfmt(subtitle, sizeof(subtitle), "%s   Your gold: %ld",
		store->owner ? store->owner->name : "", (long)player->au);
	Term_clear();
	Term_putstr(4, 1, area.width, COLOUR_YELLOW, f_info[store->feat].name);
	Term_putstr(4, 3, area.width, COLOUR_WHITE, subtitle);
	Term_putstr(4, 5, area.width, COLOUR_SLATE, "Sealed identity and powers; no guarantee of a useful fit. No restock.");
	Term_putstr(4, Term->hgt - 2, area.width, COLOUR_WHITE,
		"Arrows browse   Enter chooses   Escape leaves");
	for (i = 0; i < player->broker.count; i++) {
		const struct relic_broker_lot *lot = &player->broker.lots[i];
		bool available = relic_broker_lot_available(lot);
		lot_label(i, labels[i], sizeof(labels[i]));
		strnfmt(prices[i], sizeof(prices[i]), "%ld gold", (long)lot->price);
		rows[i].label = labels[i];
		rows[i].prefix = lot->sold ? "Sold" : !available ? "Unavailable" :
			player->au < lot->price ? "Too costly" : "Available";
		rows[i].detail = prices[i];
		rows[i].attr = available ? COLOUR_WHITE : COLOUR_SLATE;
		rows[i].enabled = true; /* Unaffordable/empty bands remain browsable. */
		if (i >= top && i < top + area.page_rows) {
			int y = area.row + i - top;
			int price_col = MAX(area.col, area.col + area.width - 28);
			Term_putstr(area.col, y, MAX(1, price_col - area.col - 1),
				i == cursor ? COLOUR_L_BLUE : rows[i].attr, labels[i]);
			Term_putstr(price_col, y, 28, rows[i].attr, prices[i]);
		}
	}
	screen.kind = UI_SCREEN_STORE;
	screen.title = f_info[store->feat].name;
	screen.subtitle = subtitle;
	screen.context = store->welcome;
	screen.help = "Arrows browse   Enter chooses   Escape leaves";
	screen.content_col = area.col;
	screen.content_row = area.row;
	screen.content_cols = area.width;
	screen.content_rows = area.page_rows;
	screen.cursor = cursor;
	screen.row_offset = top;
	screen.row_count = player->broker.count;
	screen.rows = rows;
	if (Term->screen_hook) Term->screen_hook(&screen);
}

static void present_menu(struct menu *menu)
{
	ui_relic_broker_present(menu_priv(menu), menu->cursor, menu->top);
}
static void resize_menu(struct menu *menu)
{
	region area = broker_region();
	menu_layout(menu, &area);
	present_menu(menu);
}
static void display_row(struct menu *m, int oid, bool selected, int row, int col, int width)
{
	/* The shared presentation hook paints both fallback rows and SDL rows. */
	(void)m; (void)oid; (void)selected; (void)row; (void)col; (void)width;
}

struct broker_confirmation {
	struct store *store;
	char question[240];
};

static void present_confirmation(struct menu *menu)
{
	struct broker_confirmation *ctx = menu_priv(menu);
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[2] = {
		{ .label = "Cancel purchase", .attr = COLOUR_WHITE, .enabled = true },
		{ .label = "Buy sealed lot", .attr = COLOUR_WHITE, .enabled = true }
	};
	region area = broker_region();
	int i;
	menu_layout(menu, &area);
	Term_clear();
	Term_putstr(4, 2, area.width, COLOUR_YELLOW, "Confirm sealed purchase");
	Term_putstr(4, 4, area.width, COLOUR_WHITE, ctx->question);
	for (i = 0; i < 2; i++) {
		Term_putstr(area.col, area.row + i, area.width,
			i == menu->cursor ? COLOUR_L_BLUE : COLOUR_WHITE, rows[i].label);
	}
	screen.kind = UI_SCREEN_STORE;
	screen.title = "Confirm sealed purchase";
	screen.subtitle = ctx->store->owner->name;
	screen.context = ctx->question;
	screen.help = "Arrows choose   Enter confirms   Escape cancels";
	screen.content_col = area.col;
	screen.content_row = area.row;
	screen.content_cols = area.width;
	screen.content_rows = 2;
	screen.cursor = menu->cursor;
	screen.rows = rows;
	screen.row_count = 2;
	if (Term->screen_hook) Term->screen_hook(&screen);
}

static bool confirm_lot(struct store *store, int index)
{
	const menu_iter iter = { NULL, NULL, display_row, NULL, present_confirmation };
	struct broker_confirmation ctx = { .store = store };
	struct menu *menu = menu_new(MN_SKIN_SCROLL, &iter);
	region area = broker_region();
	char label[96];
	ui_event event;
	bool confirmed;
	lot_label(index, label, sizeof(label));
	strnfmt(ctx.question, sizeof(ctx.question), "Buy %s for %ld gold? Identity and powers remain sealed until bought.",
		label, (long)player->broker.lots[index].price);
	menu_setpriv(menu, 2, &ctx);
	menu->flags = MN_NO_TAGS | MN_DBL_TAP;
	menu->present_hook = present_confirmation;
	menu_layout(menu, &area);
	event_signal(EVENT_INPUT_FLUSH);
	event = menu_select(menu, 0, false);
	confirmed = event.type == EVT_SELECT && menu->cursor == 1;
	menu_free(menu);
	return confirmed;
}

void ui_relic_broker(struct store *store)
{
	const menu_iter iter = { NULL, NULL, display_row, NULL, resize_menu };
	struct menu *menu;
	region area = broker_region();
	if (!relic_broker_ensure(player)) {
		msg("The relic catalogue is unavailable.");
		return;
	}
	menu = menu_new(MN_SKIN_SCROLL, &iter);
	menu_setpriv(menu, player->broker.count, store);
	menu->flags = MN_NO_TAGS | MN_DBL_TAP;
	menu->present_hook = present_menu;
	menu_layout(menu, &area);
	for (;;) {
		ui_event event = menu_select(menu, 0, false);
		int index = menu->cursor;
		if (event.type == EVT_ESCAPE) break;
		if (event.type != EVT_SELECT || index < 0) continue;
		if (relic_broker_lot_available(&player->broker.lots[index]) &&
			player->au >= player->broker.lots[index].price && confirm_lot(store, index)) {
			struct object *obj;
			int artifact = player->broker.lots[index].artifact;
			cmdq_push(CMD_BUY_RELIC);
			cmd_set_arg_number(cmdq_peek(), "lot", index);
			cmdq_pop(CTX_STORE);
			notice_stuff(player);
			handle_stuff(player);
			if (player->broker.lots[index].sold) {
				if (Term->screen_hook) Term->screen_hook(NULL);
				for (obj = player->gear; obj; obj = obj->next) {
					if (obj->artifact == &a_info[artifact]) {
						display_object_recall_interactive(obj);
						break;
					}
				}
			}
		}
	}
	menu_free(menu);
}
