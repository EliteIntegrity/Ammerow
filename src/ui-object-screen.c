/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-object-screen.c
 * \brief Semantic presentation adapter for item-selection menus.
 */

#include "angband.h"

#include "effects.h"
#include "game-input.h"
#include "obj-desc.h"
#include "obj-knowledge.h"
#include "obj-util.h"
#include "store.h"
#include "ui-menu.h"
#include "ui-object-screen.h"
#include "ui-screen.h"

#define UI_OBJECT_SCREEN_CAPACITY 50

struct item_tab_definition {
	int work;
	const char *label;
	char key;
};

static const struct item_tab_definition item_tabs[] = {
	{ USE_INVEN, "Inventory", '/' },
	{ USE_EQUIP, "Equipment", '/' },
	{ USE_QUIVER, "Quiver", '|' },
	{ USE_FLOOR, "Floor", '-' },
	{ SHOW_THROWING, "Throwing", 0 }
};

int ui_object_screen_step_tab(int current_work, int direction,
		int available_work)
{
	int current = -1;
	int i;

	for (i = 0; i < (int)N_ELEMENTS(item_tabs); i++) {
		if (item_tabs[i].work == current_work) current = i;
	}
	if (current < 0) {
		for (i = 0; i < (int)N_ELEMENTS(item_tabs); i++) {
			if (available_work & item_tabs[i].work) return item_tabs[i].work;
		}
		return current_work;
	}
	for (i = 1; i <= (int)N_ELEMENTS(item_tabs); i++) {
		int offset = direction < 0 ? -i : i;
		int candidate = (current + offset +
			(int)N_ELEMENTS(item_tabs)) % (int)N_ELEMENTS(item_tabs);

		if (available_work & item_tabs[candidate].work) {
			return item_tabs[candidate].work;
		}
	}
	return current_work;
}

static void append_detail(char *detail, size_t capacity, const char *part)
{
	if (!detail || !capacity || !part || !part[0]) return;
	if (detail[0]) my_strcat(detail, "  ", capacity);
	my_strcat(detail, part, capacity);
}

static void format_detail(const struct object *obj,
		olist_detail_t detail_mode, char *detail, size_t capacity)
{
	char part[32];

	if (!detail || !capacity) return;
	detail[0] = '\0';
	if (!obj) return;
	if (detail_mode & OLIST_PRICE) {
		struct store *store = store_at(cave, player->grid);

		if (store) {
			strnfmt(part, sizeof(part), "%d au",
				price_item(store, obj, true, obj->number));
			append_detail(detail, capacity, part);
		}
	}
	if (detail_mode & OLIST_FAIL && obj_can_fail(obj)) {
		if (object_effect_is_known(obj)) {
			strnfmt(part, sizeof(part), "%d%% fail",
				(9 + get_use_device_chance(obj)) / 10);
		} else {
			my_strcpy(part, "? fail", sizeof(part));
		}
		append_detail(detail, capacity, part);
	}
	if (detail_mode & OLIST_RECHARGE) {
		if (object_effect_is_known(obj)) {
			int fail = 1000 / recharge_failure_chance(obj,
				player->upkeep->recharge_pow);

			strnfmt(part, sizeof(part), "%d.%d%% fail", fail / 10,
				fail % 10);
		} else {
			my_strcpy(part, "? fail", sizeof(part));
		}
		append_detail(detail, capacity, part);
	}
	if (detail_mode & OLIST_WEIGHT) {
		int weight = obj->number * object_weight_one(obj);

		strnfmt(part, sizeof(part), "%d.%d lb", weight / 10,
			weight % 10);
		append_detail(detail, capacity, part);
	}
}

void ui_object_screen_present(struct menu *menu,
		const struct ui_object_screen_item *items, int item_count,
		olist_detail_t detail_mode, int item_mode, bool allow_all,
		const char *prompt, const char *header)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[UI_OBJECT_SCREEN_CAPACITY];
	struct ui_screen_tab tabs[5];
	char names[UI_OBJECT_SCREEN_CAPACITY][160];
	char details[UI_OBJECT_SCREEN_CAPACITY][80];
	int source_count;
	int count = 0;
	int cursor_before = 0;
	int top_before = 0;
	int cursor = -1;
	int row_offset;
	int i;
	int tab_count = 0;

	if (!Term || !Term->screen_hook || !menu || !items || item_count < 0) {
		return;
	}
	source_count = menu->filter_list ? menu->filter_count : menu->count;
	source_count = MAX(0, source_count);
	memset(rows, 0, sizeof(rows));
	memset(tabs, 0, sizeof(tabs));
	for (i = 0; i < source_count && count < UI_OBJECT_SCREEN_CAPACITY; i++) {
		int oid = menu->filter_list ? menu->filter_list[i] : i;
		const struct ui_object_screen_item *item;
		const struct object *obj;

		if (oid < 0 || oid >= item_count) continue;
		if (i < menu->cursor) cursor_before++;
		if (i < menu->top) top_before++;
		if (i == menu->cursor) cursor = count;
		item = &items[oid];
		obj = item->object;
		if (obj) {
			object_desc(names[count], sizeof(names[count]), obj,
				ODESC_PREFIX | ODESC_FULL, player);
		} else if (item->fallback_name && item->fallback_name[0]) {
			my_strcpy(names[count], item->fallback_name,
				sizeof(names[count]));
		} else {
			my_strcpy(names[count], "(nothing)", sizeof(names[count]));
		}
		format_detail(obj, detail_mode, details[count], sizeof(details[count]));
		rows[count].label = names[count];
		rows[count].prefix = item->equipment_label;
		rows[count].detail = details[count];
		rows[count].glyph = obj ? object_char(obj) : L' ';
		rows[count].attr = obj ? object_attr(obj) : COLOUR_SLATE;
		rows[count].tag = item->tag;
		rows[count].enabled = obj != NULL;
		count++;
	}
	if (cursor < 0 && count > 0) cursor = MIN(cursor_before, count - 1);
	row_offset = MIN(top_before, count);
	for (i = 0; i < (int)N_ELEMENTS(item_tabs); i++) {
		const struct item_tab_definition *definition = &item_tabs[i];
		bool include = definition->work == SHOW_THROWING ?
			player->upkeep->command_wrk == SHOW_THROWING :
			(item_mode & definition->work) || allow_all;

		if (!include) continue;
		tabs[tab_count].label = definition->label;
		tabs[tab_count].key = definition->key;
		tabs[tab_count].active =
			player->upkeep->command_wrk == definition->work;
		tab_count++;
	}

	screen.kind = UI_SCREEN_ITEM_SELECTOR;
	screen.title = prompt && prompt[0] ? prompt : "Choose an item";
	screen.subtitle = header;
	screen.help =
		"Up/Down moves   Enter selects   Left/Right changes list   Escape cancels";
	screen.content_col = menu->active.col;
	screen.content_row = menu->active.row;
	screen.content_cols = menu->active.width;
	screen.content_rows = menu->active.page_rows;
	screen.cursor = cursor;
	screen.row_offset = row_offset;
	screen.row_count = count;
	screen.rows = rows;
	screen.tab_count = tab_count;
	screen.tabs = tabs;
	Term->screen_hook(&screen);
}
