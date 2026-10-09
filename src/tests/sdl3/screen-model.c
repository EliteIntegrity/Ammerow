/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/screen-model.c */
/* Exercise owned snapshots for optional frontend-native screens. */

#include "unit-test.h"

#include "sdl3/screen-model.h"

int setup_tests(void **data)
{
	(void)data;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	return 0;
}

static int test_capture_is_owned(void *state)
{
	char label[] = "Potion of Speed";
	char context[] = "Selected item context";
	struct ui_screen_row row = {
		label, "on body      ", "0.4 lb", L'!', 14, 'a', true
	};
	struct ui_screen_tab tab = { "Inventory", '/', true };
	struct ui_screen source = {
		.kind = UI_SCREEN_ITEM_SELECTOR,
		.title = "Quaff which potion?",
		.subtitle = "Inventory",
		.help = "Escape cancels",
		.context_title = "Details",
		.context = context,
		.content_col = 6,
		.content_row = 8,
		.content_cols = 68,
		.content_rows = 12,
		.cursor = 0,
		.row_offset = 0,
		.row_count = 1,
		.rows = &row,
		.tab_count = 1,
		.tabs = &tab
	};
	struct sdl3_screen screen;
	(void)state;

	sdl3_screen_capture(&screen, &source);
	require(screen.active);
	eq(screen.kind, UI_SCREEN_ITEM_SELECTOR);
	require(streq(screen.rows[0].label, "Potion of Speed"));
	require(streq(screen.tabs[0].label, "Inventory"));
	require(streq(screen.context_title, "Details"));
	require(streq(screen.context, "Selected item context"));
	label[0] = 'X';
	context[0] = 'X';
	require(streq(screen.rows[0].label, "Potion of Speed"));
	require(streq(screen.context, "Selected item context"));
	ok;
}

static int test_clamps_counts_and_cursor(void *state)
{
	struct ui_screen_row rows[SDL3_SCREEN_ROW_CAPACITY + 2];
	struct ui_screen source = {
		.kind = UI_SCREEN_ITEM_SELECTOR,
		.title = "Items",
		.content_col = -5,
		.content_row = -3,
		.content_cols = -1,
		.content_rows = -1,
		.cursor = 999,
		.row_count = SDL3_SCREEN_ROW_CAPACITY + 2,
		.rows = rows
	};
	struct sdl3_screen screen;
	(void)state;

	memset(rows, 0, sizeof(rows));
	sdl3_screen_capture(&screen, &source);
	eq(screen.row_count, SDL3_SCREEN_ROW_CAPACITY);
	eq(screen.cursor, SDL3_SCREEN_ROW_CAPACITY - 1);
	eq(screen.content_col, 0);
	eq(screen.content_row, 0);
	eq(screen.content_cols, 0);
	eq(screen.content_rows, 0);
	source.cursor = -1;
	sdl3_screen_capture(&screen, &source);
	eq(screen.cursor, -1);
	ok;
}

static int test_clear_and_null_capture(void *state)
{
	struct sdl3_screen screen;
	(void)state;

	memset(&screen, 0xff, sizeof(screen));
	sdl3_screen_clear(&screen);
	require(!screen.active);
	sdl3_screen_capture(&screen, NULL);
	require(!screen.active);
	eq(screen.row_count, 0);
	ok;
}

static int test_row_hit_testing_uses_semantic_layout(void *state)
{
	struct ui_screen_row rows[] = {
		{ "First", NULL, NULL, 0, 1, 'a', true },
		{ "Second", NULL, NULL, 0, 1, 'b', true },
		{ "Third", NULL, NULL, 0, 1, 'c', true }
	};
	struct ui_screen source = {
		.kind = UI_SCREEN_CHARACTER_CREATION,
		.content_col = 4,
		.content_row = 8,
		.content_cols = 72,
		.content_rows = 3,
		.cursor = 1,
		.row_offset = 0,
		.row_count = N_ELEMENTS(rows),
		.rows = rows
	};
	struct sdl3_screen screen;
	int index = -1;
	(void)state;

	sdl3_screen_capture(&screen, &source);
	require(sdl3_screen_row_at(&screen, 120, 36, 10, 9, &index));
	eq(index, 1);
	require(!sdl3_screen_row_at(&screen, 120, 36, 80, 9, &index));
	require(!sdl3_screen_row_at(&screen, 120, 36, 10, 11, &index));
	ok;
}

static int test_null_collections_are_empty(void *state)
{
	struct ui_screen source = {
		.kind = UI_SCREEN_ITEM_SELECTOR,
		.title = "Empty",
		.cursor = 4,
		.row_count = 4,
		.rows = NULL,
		.tab_count = 2,
		.tabs = NULL
	};
	struct sdl3_screen screen;
	(void)state;

	sdl3_screen_capture(&screen, &source);
	require(screen.active);
	eq(screen.row_count, 0);
	eq(screen.cursor, -1);
	eq(screen.tab_count, 0);
	ok;
}

static int test_world_map_kind_is_preserved(void *state)
{
	struct ui_screen_row rows[] = {
		{ "Town", "Current", "Settlement | danger 0", L'@', 11, 0,
			true },
		{ "Eastern Approach", "Region", "Outdoors | danger 1", L'*',
			13, 0, true }
	};
	struct ui_screen_map_node nodes[] = {
		{
			.label = "Town", .detail = "Settlement | danger 0",
			.description = "Warm lamps and wet roads.",
			.stamp = { "..+..", ".H.H.", "..+.." },
			.x = 0, .y = 0, .glyph = L'@', .attr = 11, .current = true
		},
		{
			.label = "Eastern Approach",
			.detail = "Outdoors | danger 1",
			.description = "Cold stone beneath wet leaves.",
			.stamp = { "^^#^^", "^###^", "^^#^^" },
			.x = 1, .y = 0, .glyph = L'*', .attr = 13
		}
	};
	struct ui_screen_map_edge edges[] = {
		{ 0, 1, 11, true },
		{ 1, 8, 4, false }
	};
	struct ui_screen source = {
		.kind = UI_SCREEN_WORLD_MAP,
		.title = "Known World",
		/* The selected destination can differ from the current location. */
		.cursor = 1,
		.row_count = 2,
		.rows = rows,
		.map_node_count = 2,
		.map_nodes = nodes,
		.map_edge_count = 2,
		.map_edges = edges
	};
	struct sdl3_screen screen;
	(void)state;

	sdl3_screen_capture(&screen, &source);
	require(screen.active);
	eq(screen.kind, UI_SCREEN_WORLD_MAP);
	eq(screen.cursor, 1);
	require(streq(screen.rows[0].prefix, "Current"));
	require(screen.rows[0].glyph == L'@');
	eq(screen.map_node_count, 2);
	require(streq(screen.map_nodes[0].label, "Town"));
	require(streq(screen.map_nodes[1].detail, "Outdoors | danger 1"));
	require(streq(screen.map_nodes[0].description,
		"Warm lamps and wet roads."));
	require(streq(screen.map_nodes[1].stamp[1], "^###^"));
	eq(screen.map_nodes[1].x, 1);
	eq(screen.map_edge_count, 1);
	eq(screen.map_edges[0].from, 0);
	eq(screen.map_edges[0].to, 1);
	require(screen.map_edges[0].ready);
	nodes[0].label = "Changed";
	my_strcpy(nodes[1].stamp[1], ".....", sizeof(nodes[1].stamp[1]));
	require(streq(screen.map_nodes[0].label, "Town"));
	require(streq(screen.map_nodes[1].stamp[1], "^###^"));
	ok;
}

static int test_dungeon_map_snapshot_is_owned(void *state)
{
	struct ui_screen_map_cell cells[] = {
		{ L'#', 2 }, { L'.', 1 }, { L'@', 11 },
		{ L' ', 1 }, { L'+', 3 }, { L'~', 6 }
	};
	struct ui_screen source = {
		.kind = UI_SCREEN_DUNGEON_MAP,
		.title = "Level Map",
		.dungeon_map_cols = 3,
		.dungeon_map_rows = 2,
		.dungeon_player_col = 2,
		.dungeon_player_row = 0,
		.dungeon_map_cells = cells
	};
	struct sdl3_screen screen;
	(void)state;

	sdl3_screen_capture(&screen, &source);
	eq(screen.kind, UI_SCREEN_DUNGEON_MAP);
	eq(screen.dungeon_map_cols, 3);
	eq(screen.dungeon_map_rows, 2);
	eq(screen.dungeon_player_col, 2);
	eq(screen.dungeon_player_row, 0);
	require(screen.dungeon_map_cells[4].glyph == L'+');
	eq(screen.dungeon_map_cells[5].attr, 6);
	cells[4].glyph = L'?';
	require(screen.dungeon_map_cells[4].glyph == L'+');
	ok;
}

static int test_dungeon_map_snapshot_clamps_width(void *state)
{
	struct ui_screen_map_cell cells[SDL3_SCREEN_DUNGEON_MAP_COLS + 2];
	struct ui_screen source = {
		.kind = UI_SCREEN_DUNGEON_MAP,
		.dungeon_map_cols = N_ELEMENTS(cells),
		.dungeon_map_rows = 1,
		.dungeon_player_col = N_ELEMENTS(cells) - 1,
		.dungeon_player_row = 0,
		.dungeon_map_cells = cells
	};
	struct sdl3_screen screen;
	int i;
	(void)state;

	for (i = 0; i < (int)N_ELEMENTS(cells); i++) {
		cells[i].glyph = L'a' + i % 26;
		cells[i].attr = i % 16;
	}
	sdl3_screen_capture(&screen, &source);
	eq(screen.dungeon_map_cols, SDL3_SCREEN_DUNGEON_MAP_COLS);
	eq(screen.dungeon_map_rows, 1);
	eq(screen.dungeon_player_col, -1);
	eq(screen.dungeon_player_row, -1);
	require(screen.dungeon_map_cells[SDL3_SCREEN_DUNGEON_MAP_COLS - 1].glyph ==
		cells[SDL3_SCREEN_DUNGEON_MAP_COLS - 1].glyph);
	ok;
}

static int test_memorial_copy_preserves_record_lines(void *state)
{
	struct ui_screen_row row = {
		"1. Mira - Nearlander Explorer, level 12",
		"Score 4100   Gold 260   Turns 8300   2026-08-27",
		"Ended by a cave lurker at danger 8; deepest 11",
		0, 1, 0, true
	};
	struct ui_screen source = {
		.kind = UI_SCREEN_MEMORIAL,
		.title = "Memorial",
		.content_col = 4,
		.content_row = 7,
		.content_cols = 88,
		.content_rows = 1,
		.cursor = -1,
		.row_count = 1,
		.rows = &row
	};
	struct sdl3_screen screen;
	(void)state;

	sdl3_screen_capture(&screen, &source);
	eq(screen.kind, UI_SCREEN_MEMORIAL);
	require(streq(screen.rows[0].label,
		"1. Mira - Nearlander Explorer, level 12"));
	require(streq(screen.rows[0].prefix,
		"Score 4100   Gold 260   Turns 8300   2026-08-27"));
	require(streq(screen.rows[0].detail,
		"Ended by a cave lurker at danger 8; deepest 11"));
	ok;
}

static int test_creature_recall_document_is_owned(void *state)
{
	wchar_t text[] = L"The cave watcher ('c')\nIt keeps to the dark.";
	uint8_t attrs[N_ELEMENTS(text) - 1];
	size_t starts[] = { 0, 23 };
	size_t lengths[] = { 22, 21 };
	char name[] = "cave watcher";
	struct ui_screen source = {
		.kind = UI_SCREEN_MONSTER_LORE,
		.document_length = N_ELEMENTS(text) - 1,
		.document_text = text,
		.document_attrs = attrs,
		.document_line_count = 2,
		.document_line_starts = starts,
		.document_line_lengths = lengths,
		.document_line_offset = 1,
		.document_cols = 52,
		.subject_name = name,
		.subject_base = "watcher",
		.subject_glyph = L'c',
		.subject_attr = 12,
		.subject_unique = true
	};
	struct sdl3_screen screen;
	int i;
	(void)state;

	for (i = 0; i < (int)N_ELEMENTS(attrs); i++) attrs[i] = i % 16;
	sdl3_screen_capture(&screen, &source);
	eq(screen.kind, UI_SCREEN_MONSTER_LORE);
	eq(screen.document_length, N_ELEMENTS(text) - 1);
	require(screen.document_text[0] == L'T');
	eq(screen.document_attrs[7], attrs[7]);
	eq(screen.document_line_count, 2);
	eq(screen.document_line_starts[1], 23);
	eq(screen.document_line_lengths[1], 21);
	eq(screen.document_line_offset, 1);
	eq(screen.document_cols, 52);
	require(streq(screen.subject_name, "cave watcher"));
	require(streq(screen.subject_base, "watcher"));
	require(screen.subject_glyph == L'c');
	eq(screen.subject_attr, 12);
	require(screen.subject_unique);
	text[0] = L'X';
	attrs[7] = 0;
	name[0] = 'X';
	require(screen.document_text[0] == L'T');
	require(screen.document_attrs[7] != 0);
	require(streq(screen.subject_name, "cave watcher"));
	ok;
}

static int test_creature_recall_document_clamps_ranges(void *state)
{
	wchar_t text[SDL3_SCREEN_DOCUMENT_CAPACITY + 3];
	size_t starts[] = { 0, SDL3_SCREEN_DOCUMENT_CAPACITY - 2,
		SDL3_SCREEN_DOCUMENT_CAPACITY + 1 };
	size_t lengths[] = { 20, 20, 1 };
	struct ui_screen source = {
		.kind = UI_SCREEN_MONSTER_LORE,
		.document_length = N_ELEMENTS(text),
		.document_text = text,
		.document_line_count = 3,
		.document_line_starts = starts,
		.document_line_lengths = lengths,
		.document_line_offset = 99
	};
	struct sdl3_screen screen;
	(void)state;

	wmemset(text, L'x', N_ELEMENTS(text));
	sdl3_screen_capture(&screen, &source);
	eq(screen.document_length, SDL3_SCREEN_DOCUMENT_CAPACITY);
	require(screen.document_text[SDL3_SCREEN_DOCUMENT_CAPACITY] == L'\0');
	eq(screen.document_line_count, 2);
	eq(screen.document_line_lengths[1], 2);
	eq(screen.document_line_offset, 1);
	eq(screen.document_attrs[0], 1);
	ok;
}

static int test_measure_responsive_density(void *state)
{
	struct sdl3_screen screen = {
		.active = true,
		.content_col = 12,
		.content_row = 8,
		.content_cols = 100,
		.content_rows = 20,
		.row_count = 40
	};
	struct sdl3_screen_layout layout;
	(void)state;

	require(sdl3_screen_measure(&screen, 80, 24, &layout));
	eq(layout.left, 4);
	eq(layout.width, 72);
	eq(layout.visible_rows, 13);
	eq(layout.detail_width, 18);
	eq(layout.label_width, 46);
	require(sdl3_screen_measure(&screen, 100, 30, &layout));
	eq(layout.left, 4);
	eq(layout.width, 92);
	eq(layout.visible_rows, 19);
	eq(layout.label_width, 66);
	require(sdl3_screen_measure(&screen, 120, 36, &layout));
	eq(layout.left, 12);
	eq(layout.width, 100);
	eq(layout.visible_rows, 20);
	eq(layout.label_width, 74);
	ok;
}

static int test_measure_scroll_and_narrow_details(void *state)
{
	struct sdl3_screen screen = {
		.active = true,
		.content_col = 2,
		.content_row = 7,
		.content_cols = 52,
		.content_rows = 18,
		.row_offset = 12,
		.row_count = 20
	};
	struct sdl3_screen_layout layout;
	(void)state;

	require(sdl3_screen_measure(&screen, 80, 40, &layout));
	eq(layout.left, 4);
	eq(layout.width, 52);
	eq(layout.first_row, 12);
	eq(layout.visible_rows, 8);
	eq(layout.detail_width, 0);
	eq(layout.label_width, 44);
	screen.row_offset = 200;
	require(sdl3_screen_measure(&screen, 80, 40, &layout));
	eq(layout.first_row, 20);
	eq(layout.visible_rows, 0);
	ok;
}

static int test_measure_rejects_invalid_inputs(void *state)
{
	struct sdl3_screen screen = { .active = true };
	struct sdl3_screen_layout layout;
	(void)state;

	require(!sdl3_screen_measure(NULL, 80, 24, &layout));
	require(!sdl3_screen_measure(&screen, 80, 24, NULL));
	screen.active = false;
	require(!sdl3_screen_measure(&screen, 80, 24, &layout));
	screen.active = true;
	require(!sdl3_screen_measure(&screen, 8, 24, &layout));
	require(!sdl3_screen_measure(&screen, 80, 3, &layout));
	ok;
}

static int test_measure_context_panel(void *state)
{
	struct sdl3_screen screen = {
		.active = true,
		.content_col = 4,
		.content_row = 8,
		.content_cols = 100,
		.content_rows = 20,
		.row_count = 12
	};
	struct sdl3_screen_layout layout;
	(void)state;

	my_strcpy(screen.context, "Stat modifiers\nSTR +1", sizeof(screen.context));
	require(sdl3_screen_measure(&screen, 100, 30, &layout));
	eq(layout.width, 92);
	eq(layout.list_width, 30);
	eq(layout.context_col, 37);
	eq(layout.context_width, 59);
	eq(layout.detail_width, 0);
	eq(layout.label_width, 22);
	require(sdl3_screen_measure(&screen, 70, 24, &layout));
	eq(layout.width, 62);
	eq(layout.list_width, 62);
	eq(layout.context_width, 0);
	eq(layout.label_width, 54);
	screen.kind = UI_SCREEN_STORE;
	require(sdl3_screen_measure(&screen, 100, 30, &layout));
	eq(layout.list_width, 92);
	eq(layout.context_width, 0);
	ok;
}

static int test_measure_store_columns_keep_price_gutter(void *state)
{
	struct sdl3_screen screen = {
		.active = true,
		.kind = UI_SCREEN_STORE,
		.content_col = 4,
		.content_row = 8,
		.content_cols = 71,
		.content_rows = 1,
		.row_count = 3
	};
	struct sdl3_screen_layout layout;
	(void)state;

	my_strcpy(screen.rows[0].prefix, "3.0 lb",
		sizeof(screen.rows[0].prefix));
	my_strcpy(screen.rows[0].detail, "125 gold",
		sizeof(screen.rows[0].detail));
	my_strcpy(screen.rows[1].prefix, "120.0 lb",
		sizeof(screen.rows[1].prefix));
	my_strcpy(screen.rows[1].detail, "8 gold",
		sizeof(screen.rows[1].detail));
	require(sdl3_screen_measure(&screen, 120, 36, &layout));
	eq(layout.width, 71);
	eq(layout.prefix_width, 8);
	eq(layout.detail_width, 10);
	eq(layout.label_col, 17);
	eq(layout.detail_col, 61);
	eq(layout.label_width, 42);
	eq(layout.detail_col - (layout.label_col + layout.label_width), 2);
	ok;
}

static int test_story_body_uses_full_width_at_small_sizes(void *state)
{
	struct sdl3_screen screen = { .active = true, .kind = UI_SCREEN_STORY,
		.content_col = 8, .content_row = 6, .content_cols = 100 };
	struct sdl3_screen_layout layout;
	(void)state;
	my_strcpy(screen.context, "Quest and donation text", sizeof(screen.context));
	for (int cols = 40; cols <= 160; cols += 20) {
		require(sdl3_screen_measure(&screen, cols, 30, &layout));
		eq(layout.context_width, layout.width);
		eq(layout.context_col, layout.left);
		eq(layout.list_width, 0);
	}
	ok;
}

static int test_measure_character_dossier(void *state)
{
	struct sdl3_screen screen = {
		.active = true,
		.kind = UI_SCREEN_CHARACTER_DOSSIER,
		.content_col = 4,
		.content_row = 8,
		.content_cols = 100,
		.content_rows = 30,
		.row_count = 40
	};
	struct sdl3_screen_layout layout;
	(void)state;

	my_strcpy(screen.context, "Born beyond the Meridian.",
		sizeof(screen.context));
	require(sdl3_screen_measure(&screen, 100, 30, &layout));
	eq(layout.width, 92);
	eq(layout.list_width, 44);
	eq(layout.context_col, 51);
	eq(layout.context_width, 45);
	eq(layout.detail_width, 14);
	eq(layout.label_width, 22);
	eq(layout.visible_rows, 19);
	require(sdl3_screen_measure(&screen, 80, 24, &layout));
	eq(layout.width, 72);
	eq(layout.list_width, 36);
	eq(layout.context_width, 33);
	eq(layout.detail_width, 12);
	eq(layout.label_width, 16);
	eq(layout.visible_rows, 13);
	ok;
}

static int test_action_menu_pointer_keys(void *state)
{
	struct sdl3_screen screen = { 0 };
	keycode_t keys[8];
	(void)state;
	screen.active = true;
	screen.kind = UI_SCREEN_ACTION_MENU;
	screen.row_count = 6;
	screen.cursor = 3;
	for (int i = 0; i < screen.row_count; i++) screen.rows[i].enabled = true;
	eq(sdl3_screen_action_menu_keys(&screen, 1, true, keys, 8), 3);
	eq(keys[0], ARROW_UP);
	eq(keys[1], ARROW_UP);
	eq(keys[2], KC_ENTER);
	eq(sdl3_screen_action_menu_keys(&screen, 5, false, keys, 8), 2);
	eq(keys[0], ARROW_DOWN);
	eq(keys[1], ARROW_DOWN);
	eq(sdl3_screen_action_menu_keys(&screen, 3, true, keys, 8), 1);
	eq(keys[0], KC_ENTER);
	eq(sdl3_screen_action_menu_keys(&screen, 3, false, keys, 8), 0);
	eq(sdl3_screen_action_menu_keys(&screen, -1, true, keys, 8), 0);
	eq(sdl3_screen_action_menu_keys(&screen, 6, true, keys, 8), 0);
	keys[0] = 'X';
	eq(sdl3_screen_action_menu_keys(&screen, 1, true, keys, 2), 0);
	eq(keys[0], 'X');
	screen.rows[1].enabled = false;
	eq(sdl3_screen_action_menu_keys(&screen, 1, true, keys, 8), 0);
	screen.active = false;
	eq(sdl3_screen_action_menu_keys(&screen, 3, true, keys, 8), 0);
	ok;
}

const char *suite_name = "sdl3/screen-model";
struct test tests[] = {
	{ "capture is owned", test_capture_is_owned },
	{ "clamps counts and cursor", test_clamps_counts_and_cursor },
	{ "clear and null capture", test_clear_and_null_capture },
	{ "row hit testing uses semantic layout",
		test_row_hit_testing_uses_semantic_layout },
	{ "null collections are empty", test_null_collections_are_empty },
	{ "world map kind is preserved", test_world_map_kind_is_preserved },
	{ "dungeon map snapshot is owned",
		test_dungeon_map_snapshot_is_owned },
	{ "dungeon map snapshot clamps width",
		test_dungeon_map_snapshot_clamps_width },
	{ "memorial copy preserves record lines",
		test_memorial_copy_preserves_record_lines },
	{ "creature recall document is owned",
		test_creature_recall_document_is_owned },
	{ "creature recall document clamps ranges",
		test_creature_recall_document_clamps_ranges },
	{ "measure responsive density", test_measure_responsive_density },
	{ "measure scroll and narrow details", test_measure_scroll_and_narrow_details },
	{ "measure rejects invalid inputs", test_measure_rejects_invalid_inputs },
	{ "measure context panel", test_measure_context_panel },
	{ "story body uses full width at small sizes",
		test_story_body_uses_full_width_at_small_sizes },
	{ "measure store columns keep price gutter",
		test_measure_store_columns_keep_price_gutter },
	{ "measure character dossier", test_measure_character_dossier },
	{ "action menu pointer keys", test_action_menu_pointer_keys },
	{ NULL, NULL },
};
