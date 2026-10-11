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
#include "obj-info.h"
#include "obj-util.h"
#include "store.h"
#include "ui-menu.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-object-screen.h"
#include "ui-screen.h"
#include "z-textblock.h"

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

void ui_object_actions_present(struct menu *menu)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[32] = { 0 };
	int count = MIN(menu->count, (int)N_ELEMENTS(rows));
	for (int i = 0; i < count; i++) {
		rows[i].label = menu_dynamic_text(menu, i);
		rows[i].tag = menu->selections[i];
		rows[i].enabled = menu->row_funcs->valid_row(menu, i) == MN_ROW_VALID;
		rows[i].attr = COLOUR_WHITE;
	}
	screen.kind = UI_SCREEN_ITEM_ACTIONS;
	screen.modal = true;
	screen.title = "Item actions";
	screen.subtitle = menu->title;
	screen.help = "Arrows select   Enter chooses   Escape returns";
	screen.content_col = menu->active.col;
	screen.content_row = menu->active.row;
	screen.content_cols = menu->active.width;
	screen.content_rows = menu->active.page_rows;
	screen.rows = rows;
	screen.row_count = count;
	screen.cursor = menu->cursor;
	screen.row_offset = menu->top;
	Term->screen_hook(&screen);
}

void ui_item_details_measure(int cols, int rows,
		struct ui_item_details_layout *layout)
{
	memset(layout, 0, sizeof(*layout));
	layout->panel_col = 4;
	layout->panel_row = 3;
	layout->panel_cols = MAX(12, cols - 8);
	layout->panel_rows = MAX(10, rows - 6);
	layout->text_col = 6;
	layout->text_row = 8;
	layout->text_cols = MAX(8, cols - 12);
	layout->art_cols = cols >= 100 ? MIN(32, cols / 4) : MIN(18, cols / 4);
	layout->art_col = cols - 6 - layout->art_cols;
	layout->art_row = cols >= 100 ? 8 : 5;
	layout->art_rows = cols >= 100 ? MAX(4, rows - 14) : 5;
	if (cols >= 100) {
		layout->text_cols -= layout->art_cols + 3;
		layout->heading_cols = cols - 15;
	} else {
		layout->text_row = 11;
		layout->heading_cols = MAX(8, layout->art_col - 10);
	}
	layout->text_rows = MAX(1, rows - layout->text_row - 5);
}

static int present_item_details(const struct object *obj, textblock *tb,
		int offset)
{
	struct ui_item_details_layout layout;
	struct ui_screen screen = { 0 };
	size_t *starts = NULL, *lengths = NULL;
	size_t count;
	char name[160], subtitle[160], help[192];
	bool unidentified = obj->kind->flavor && !object_flavor_is_aware(obj);
	int maximum;

	ui_item_details_measure(Term->wid, Term->hgt, &layout);
	count = textblock_calculate_lines(tb, &starts, &lengths, layout.text_cols);
	maximum = MAX(0, (int)count - layout.text_rows);
	object_desc(name, sizeof(name), obj,
		ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL, player);
	strnfmt(subtitle, sizeof(subtitle), "%s | %d.%d lb each",
		unidentified ? "Identity unknown" : object_fully_known(obj) ?
		"Fully known" : "Partly known", obj->weight / 10, ABS(obj->weight % 10));
	strnfmt(help, sizeof(help), maximum ?
		"Up/Down scroll  PgUp/PgDn page  Enter/Esc/Click returns  [%d-%d/%d]" :
		"Enter / Escape / Click to return", MIN(MAX(offset, 0), maximum) + 1,
		MIN((int)count, MIN(MAX(offset, 0), maximum) + layout.text_rows), (int)count);
	screen.kind = UI_SCREEN_ITEM_DETAILS;
	screen.modal = true;
	screen.title = name;
	screen.subtitle = subtitle;
	screen.help = help;
	screen.content_col = layout.text_col;
	screen.content_row = layout.text_row;
	screen.content_cols = layout.text_cols;
	screen.content_rows = layout.text_rows;
	screen.document_text = textblock_text(tb);
	screen.document_length = wcslen(screen.document_text);
	screen.document_attrs = textblock_attrs(tb);
	screen.document_cols = layout.text_cols;
	screen.document_line_count = (int)count;
	screen.document_line_starts = starts;
	screen.document_line_lengths = lengths;
	screen.document_line_offset = MIN(MAX(offset, 0), maximum);
	screen.subject_glyph = object_char(obj);
	screen.subject_attr = object_attr(obj);
	screen.subject_unidentified = unidentified;
	/* Never send the true kind's art ID across the presentation boundary
	 * until its flavor is known, including to headless snapshots. */
	screen.subject_art_id = unidentified ?
		(obj->kind->base ? obj->kind->base->unaware_art_id : NULL) :
		obj->kind->art_id;
	Term->screen_hook(&screen);
	mem_free(starts);
	mem_free(lengths);
	return maximum;
}

int ui_item_details_present(const struct object *obj, int offset)
{
	textblock *tb;
	int maximum;
	if (!obj || !obj->kind || !Term || !Term->screen_hook) return 0;
	tb = object_info(obj, OINFO_NONE);
	maximum = present_item_details(obj, tb, offset);
	textblock_free(tb);
	return maximum;
}

void ui_item_details_show(struct object *obj)
{
	textblock *tb = object_info(obj, OINFO_NONE);
	int offset = 0;
	bool done = false;
	if (!Term->screen_hook) {
		char name[160];
		object_desc(name, sizeof(name), obj, ODESC_PREFIX | ODESC_FULL, player);
		textui_textblock_show(tb, SCREEN_REGION, name);
		textblock_free(tb);
		return;
	}
	screen_save();
	while (!done) {
		struct ui_item_details_layout layout;
		int maximum = present_item_details(obj, tb, offset);
		ui_event event = inkey_ex();
		ui_item_details_measure(Term->wid, Term->hgt, &layout);
		if (event.type == EVT_MOUSE || event.type == EVT_DISCONNECT) break;
		if (event.type != EVT_KBRD) continue;
		switch (event.key.code) {
		case ESCAPE: case KC_ENTER: case 'q': done = true; break;
		case ARROW_UP: offset--; break;
		case ARROW_DOWN: offset++; break;
		case KC_PGUP: offset -= layout.text_rows; break;
		case KC_PGDOWN: case ' ': offset += layout.text_rows; break;
		case KC_HOME: offset = 0; break;
		case KC_END: offset = maximum; break;
		default: break;
		}
		offset = MIN(MAX(0, offset), maximum);
	}
	Term->screen_hook(NULL);
	screen_load();
	textblock_free(tb);
}
