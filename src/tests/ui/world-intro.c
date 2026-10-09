/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "unit-test.h"
#include "datafile.h"
#include "player.h"
#include "ui-world-intro.h"
#include "world-story-data.h"

static struct player_race second = { .name = "Second", .lore = "Second origin lore.", .ridx = 1 };
static struct player_race first = { .name = "First", .lore = "First origin lore.", .next = &second };
static struct player_class first_class = { .name = "Test Ranger", .cidx = 0,
	.title = { "Runner", "Track Reader", "Scout", "Courser", "Tracker",
		"Guide", "Explorer", "Pathfinder", "Ranger", "Ranger Lord" } };
static struct player_class second_class = { .name = "Test Warrior", .cidx = 1,
	.title = { "Test Rookie" }, .next = &first_class };

int setup_tests(void **unused)
{
	struct parser *p = story_parser.init();
	(void)unused;
	races = &first;
	if (parser_parse(p, "scene:today:Today") ||
		parser_parse(p, "intro:1:prose") ||
		parser_parse(p, "body:Read this before creating a character.") ||
		parser_parse(p, "scene:people:People") ||
		parser_parse(p, "intro:2:origins") ||
		parser_parse(p, "body:Each origin has its own account.") ||
		parser_parse(p, "scene:classes:Classes & Ranks") ||
		parser_parse(p, "intro:3:classes") ||
		parser_parse(p, "body:Titles follow character levels, not dungeon depths.")) return 1;
	return story_parser.finish(p);
}

int teardown_tests(void *unused)
{
	(void)unused;
	races = NULL;
	classes = NULL;
	story_parser.cleanup();
	return 0;
}

static int test_tabs_and_origins(void *unused)
{
	struct ui_world_intro state = { 0 };
	(void)unused;
	null(player);
	eq(ui_world_intro_origin_count(), 2);
	null(ui_world_intro_origin_at(-1));
	null(ui_world_intro_origin_at(2));
	ui_world_intro_handle(&state, UI_INTRO_NEXT_TAB, 80, 24);
	eq(state.tab, 1);
	require(ui_world_intro_has_origins(&state));
	ui_world_intro_handle(&state, UI_INTRO_DOWN, 80, 24);
	eq(state.origin, 1);
	ui_world_intro_handle(&state, UI_INTRO_DOWN, 80, 24);
	eq(state.origin, 0);
	ui_world_intro_handle(&state, UI_INTRO_UP, 80, 24);
	eq(state.origin, 1);
	state.offset = 5;
	ui_world_intro_handle(&state, UI_INTRO_PREVIOUS_TAB, 80, 24);
	eq(state.tab, 0);
	eq(state.offset, 0);
	ui_world_intro_handle(&state, UI_INTRO_PREVIOUS_TAB, 80, 24);
	eq(state.tab, 2);
	ok;
}

static int test_geometry_and_pointer(void *unused)
{
	struct ui_world_intro state = { 0 };
	struct ui_world_intro_layout layout;
	(void)unused;
	for (int cols = 64; cols <= 160; cols += 17) {
		require(ui_world_intro_measure(&state, cols, 22, &layout));
		require(layout.text_col + layout.text_width <= cols);
		require(layout.top + layout.visible_rows < 22 - 2);
		require(ui_world_intro_select_at(&state, cols, 22,
			layout.left + layout.width / 2, layout.tab_row));
		eq(state.tab, 1);
		require(ui_world_intro_measure(&state, cols, 22, &layout));
		require(ui_world_intro_select_at(&state, cols, 22,
			layout.left + 1, layout.top + 1));
		eq(state.origin, 1);
		require(!ui_world_intro_select_at(&state, cols, 22,
			layout.left + 1, layout.top + 2));
		require(!ui_world_intro_select_at(&state, cols, 22, 0, 0));
		require(!ui_world_intro_select_at(&state, cols, 22,
			layout.text_col + 2, layout.top + 2));
		eq(state.tab, 1);
	}
	ok;
}

static int test_shared_lore_and_scrolling(void *unused)
{
	struct ui_world_intro state = { .tab = 1, .origin = 1 };
	struct ui_world_intro_layout layout;
	textblock *tb = ui_world_intro_document(&state);
	(void)unused;
	require(wcsstr(textblock_text(tb), L"Second origin lore.") != NULL);
	require(wcsstr(textblock_text(tb), L"First origin lore.") == NULL);
	require(wcsstr(textblock_text(tb), L"Stat modifiers") != NULL);
	textblock_free(tb);
	require(ui_world_intro_measure(&state, 64, 22, &layout));
	require(ui_world_intro_max_offset(&state, &layout) > 0);
	ui_world_intro_handle(&state, UI_INTRO_LAST_LINE, 64, 22);
	eq(state.offset, ui_world_intro_max_offset(&state, &layout));
	ui_world_intro_handle(&state, UI_INTRO_FIRST_LINE, 64, 22);
	eq(state.offset, 0);
	ui_world_intro_scroll(&state, -10, &layout);
	eq(state.offset, 0);
	ui_world_intro_scroll(&state, 10000, &layout);
	eq(state.offset, ui_world_intro_max_offset(&state, &layout));
	ui_world_intro_handle(&state, UI_INTRO_UP, 64, 22);
	eq(state.offset, 0);
	ok;
}

static int test_class_title_navigation(void *unused)
{
	struct ui_world_intro state = { .tab = 1, .origin = 1 };
	struct ui_world_intro_layout layout;
	(void)unused;
	eq(ui_world_intro_people_count(), 2);
	null(ui_world_intro_people_label(2));
	classes = &second_class;
	eq(ui_world_intro_people_count(), 2);
	require(streq(ui_world_intro_people_label(0), "First"));
	null(ui_world_intro_people_label(2));
	null(ui_world_intro_people_label(-1));
	null(ui_world_intro_people_label(3));
	ui_world_intro_handle(&state, UI_INTRO_DOWN, 64, 22);
	eq(state.origin, 0);
	ui_world_intro_handle(&state, UI_INTRO_NEXT_TAB, 64, 22);
	eq(state.tab, 2);
	require(!ui_world_intro_has_origins(&state));
	require(ui_world_intro_measure(&state, 64, 22, &layout));
	eq(layout.list_width, 0);
	require(ui_world_intro_max_offset(&state, &layout) > 0);
	ui_world_intro_handle(&state, UI_INTRO_LAST_LINE, 64, 22);
	eq(state.offset, ui_world_intro_max_offset(&state, &layout));
	ui_world_intro_handle(&state, UI_INTRO_DOWN, 64, 22);
	eq(state.origin, 0);
	eq(state.offset, ui_world_intro_max_offset(&state, &layout));
	require(!ui_world_intro_select_at(&state, 64, 22, layout.left + 1,
		layout.top + 3));
	classes = NULL;
	ok;
}

static int test_class_titles_use_live_data(void *unused)
{
	struct ui_world_intro state = { .tab = 2 };
	textblock *tb;
	const wchar_t *text;
	(void)unused;
	null(player); /* The guide also works before any character is created. */
	classes = &second_class;
	tb = ui_world_intro_document(&state);
	text = textblock_text(tb);
	require(wcsstr(text, L"Levels 1-5: Runner") != NULL);
	require(wcsstr(text, L"Levels 6-10: Track Reader") != NULL);
	require(wcsstr(text, L"Levels 46-50: Ranger Lord") != NULL);
	require(wcsstr(text, L"Levels 1-5: Test Rookie") != NULL);
	require(wcsstr(text, L"not dungeon depths") != NULL);
	require(wcsstr(text, L"First origin lore.") == NULL);
	notnull(wcsstr(text, L"Test Ranger"));
	notnull(wcsstr(text, L"Test Warrior"));
	require(wcsstr(text, L"Test Ranger") < wcsstr(text, L"Test Warrior"));
	textblock_free(tb);
	/* No separate copied title table to drift when content changes. */
	first_class.title[1] = "Changed title";
	tb = ui_world_intro_document(&state);
	require(wcsstr(textblock_text(tb), L"Levels 6-10: Changed title") != NULL);
	require(wcsstr(textblock_text(tb), L"Track Reader") == NULL);
	textblock_free(tb);
	first_class.title[1] = "Track Reader";
	state.tab = 1;
	state.origin = 0;
	tb = ui_world_intro_document(&state);
	require(wcsstr(textblock_text(tb), L"Track Reader") == NULL);
	require(wcsstr(textblock_text(tb), L"First origin lore.") != NULL);
	textblock_free(tb);
	classes = NULL;
	ok;
}

const char *suite_name = "ui/world-intro";
struct test tests[] = {
	{ "tabs and origins without a character", test_tabs_and_origins },
	{ "shared geometry and pointer misses", test_geometry_and_pointer },
	{ "shared origin text and bounded scrolling", test_shared_lore_and_scrolling },
	{ "class title list navigation", test_class_title_navigation },
	{ "class title guide uses live data", test_class_titles_use_live_data },
	{ NULL, NULL }
};
