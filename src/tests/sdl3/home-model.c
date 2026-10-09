/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/home-model.c */
/* Exercise platform-independent Home and Load Run navigation. */

#include "unit-test.h"

#include "sdl3/home-model.h"

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

static int test_empty_home(void *state)
{
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	sdl3_home_open(&home);
	require(home.visible);
	eq(home.page, SDL3_HOME_ROOT);
	eq(home.selected_row, SDL3_HOME_NEW);
	require(!sdl3_home_root_row_enabled(&home, SDL3_HOME_CONTINUE));
	require(!sdl3_home_root_row_enabled(&home, SDL3_HOME_LOAD_RUN));
	sdl3_home_move(&home, 1);
	eq(home.selected_row, SDL3_HOME_SETTINGS);
	sdl3_home_move(&home, -1);
	eq(home.selected_row, SDL3_HOME_NEW);
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_NEW);
	ok;
}

static int test_feature_survives_home_reopen(void *state)
{
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	sdl3_home_set_feature(&home, "the-iron-king");
	sdl3_home_open(&home);
	require(streq(home.feature_asset, "the-iron-king"));
	sdl3_home_open(&home);
	require(streq(home.feature_asset, "the-iron-king"));
	ok;
}

static int test_unambiguous_continue(void *state)
{
	struct sdl3_home_save save = {
		"only-save", "Only", "Level 10", false
	};
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	home.saves = &save;
	home.save_count = 1;
	home.continue_save = 0;
	sdl3_home_open(&home);
	eq(home.selected_row, SDL3_HOME_CONTINUE);
	require(sdl3_home_root_row_enabled(&home, SDL3_HOME_CONTINUE));
	require(sdl3_home_root_row_enabled(&home, SDL3_HOME_LOAD_RUN));
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_CONTINUE);
	require(streq(sdl3_home_selected_filename(&home), "only-save"));
	ok;
}

static int test_load_navigation(void *state)
{
	struct sdl3_home_save saves[] = {
		{ "alpha", "Alpha", "First", false },
		{ "beta", "Beta", "Second", false }
	};
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	home.saves = saves;
	home.save_count = 2;
	sdl3_home_open(&home);
	home.selected_row = SDL3_HOME_LOAD_RUN;
	sdl3_home_activate(&home);
	eq(home.page, SDL3_HOME_LOAD);
	eq(home.action, SDL3_HOME_ACTION_NONE);
	sdl3_home_move(&home, -1);
	eq(home.selected_save, 1);
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_LOAD);
	require(streq(sdl3_home_selected_filename(&home), "beta"));
	sdl3_home_back(&home);
	eq(home.page, SDL3_HOME_ROOT);
	eq(home.action, SDL3_HOME_ACTION_NONE);
	ok;
}

static int test_secondary_actions(void *state)
{
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	sdl3_home_open(&home);
	home.selected_row = SDL3_HOME_ABOUT;
	sdl3_home_activate(&home);
	eq(home.page, SDL3_HOME_ABOUT_MENU);
	home.selected_about_row = SDL3_HOME_ABOUT_OVERVIEW;
	sdl3_home_activate(&home);
	eq(home.page, SDL3_HOME_ABOUT_PAGE);
	sdl3_home_back(&home);
	eq(home.page, SDL3_HOME_ABOUT_MENU);
	home.selected_about_row = SDL3_HOME_ABOUT_CREDITS;
	sdl3_home_activate(&home);
	eq(home.page, SDL3_HOME_CREDITS_PAGE);
	sdl3_home_scroll_document(&home, 12, 30);
	eq(home.document_offset, 12);
	sdl3_home_scroll_document(&home, 40, 30);
	eq(home.document_offset, 30);
	sdl3_home_scroll_document(&home, -40, 30);
	eq(home.document_offset, 0);
	sdl3_home_back(&home);
	eq(home.page, SDL3_HOME_ABOUT_MENU);
	home.selected_about_row = SDL3_HOME_ABOUT_LEGAL;
	sdl3_home_activate(&home);
	eq(home.page, SDL3_HOME_LEGAL_PAGE);
	sdl3_home_scroll_document(&home, 5, 30);
	eq(home.document_offset, 5);
	sdl3_home_back(&home);
	eq(home.page, SDL3_HOME_ABOUT_MENU);
	sdl3_home_back(&home);
	eq(home.page, SDL3_HOME_ROOT);
	home.selected_row = SDL3_HOME_WORLD;
	sdl3_home_move(&home, 1);
	eq(home.selected_row, SDL3_HOME_MEMORIAL);
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_MEMORIAL);
	eq(home.page, SDL3_HOME_ROOT);
	home.action = SDL3_HOME_ACTION_NONE;
	sdl3_home_move(&home, 1);
	eq(home.selected_row, SDL3_HOME_HELP);
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_FIELD_GUIDE);
	home.action = SDL3_HOME_ACTION_NONE;
	sdl3_home_move(&home, 1);
	eq(home.selected_row, SDL3_HOME_ABOUT);
	home.selected_row = SDL3_HOME_SETTINGS;
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_SETTINGS);
	home.action = SDL3_HOME_ACTION_NONE;
	home.selected_row = SDL3_HOME_QUIT;
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_QUIT);
	ok;
}

static int test_live_save_filter(void *state)
{
	(void)state;

	require(sdl3_home_save_is_live("Ranger",
		"Ranger, L12 Rainfolk Ranger, at DL18"));
	require(!sdl3_home_save_is_live("Ranger",
		"Ranger, dead (a cave spider)"));
	require(!sdl3_home_save_is_live("Ranger.new",
		"Ranger, L12 Rainfolk Ranger, at DL18"));
	require(!sdl3_home_save_is_live("Ranger.old",
		"Ranger, L12 Rainfolk Ranger, at DL18"));
	require(!sdl3_home_save_is_live("Ranger", "Invalid savefile"));
	require(!sdl3_home_save_is_live("Ranger", ""));
	require(sdl3_home_save_is_catalogue_entry("Ranger",
		"Invalid savefile"));
	require(sdl3_home_save_is_catalogue_entry("Ranger", NULL));
	require(!sdl3_home_save_is_catalogue_entry("Ranger.old",
		"Invalid savefile"));
	require(!sdl3_home_save_is_catalogue_entry("Ranger",
		"Ranger, dead (a cave spider)"));
	ok;
}

static int test_stale_continue_is_not_actionable(void *state)
{
	struct sdl3_home_save save = { "gone", "Gone", "Level 10", false };
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	home.saves = &save;
	home.save_count = 0;
	home.continue_save = 0;
	sdl3_home_open(&home);
	eq(home.selected_row, SDL3_HOME_NEW);
	require(!sdl3_home_root_row_enabled(&home, SDL3_HOME_CONTINUE));
	home.selected_row = SDL3_HOME_CONTINUE;
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_NONE);
	require(!sdl3_home_selected_filename(&home));
	home.notice = SDL3_HOME_NOTICE_SAVE_UNAVAILABLE;
	sdl3_home_move(&home, 1);
	eq(home.notice, SDL3_HOME_NOTICE_NONE);

	home.saves = NULL;
	home.save_count = 1;
	require(!sdl3_home_root_row_enabled(&home, SDL3_HOME_CONTINUE));
	require(!sdl3_home_selected_filename(&home));
	ok;
}

static int test_continue_requires_an_exact_remembered_run(void *state)
{
	struct sdl3_home_save saves[] = {
		{ "alpha", "Alpha", "Alpha, L1 Nearlander Warrior, at DL0", false },
		{ "broken", "Broken", "Damaged or incompatible save", true },
		{ "beta", "Beta", "Beta, L2 Nearlander Warrior, at DL1", false }
	};
	(void)state;

	eq(sdl3_home_find_continue(saves, N_ELEMENTS(saves), NULL), -1);
	eq(sdl3_home_find_continue(saves, N_ELEMENTS(saves), ""), -1);
	eq(sdl3_home_find_continue(saves, N_ELEMENTS(saves), "missing"), -1);
	eq(sdl3_home_find_continue(saves, N_ELEMENTS(saves), "alpha"), 0);
	eq(sdl3_home_find_continue(saves, N_ELEMENTS(saves), "broken"), -1);
	eq(sdl3_home_find_continue(saves, N_ELEMENTS(saves), "beta"), 2);
	ok;
}

static int test_delete_confirmation(void *state)
{
	struct sdl3_home_save save = { "run", "Run", "Level 10", false };
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	home.saves = &save;
	home.save_count = 1;
	sdl3_home_open(&home);
	home.selected_row = SDL3_HOME_LOAD_RUN;
	sdl3_home_activate(&home);
	sdl3_home_request_delete(&home);
	eq(home.page, SDL3_HOME_DELETE_CONFIRM);
	eq(home.action, SDL3_HOME_ACTION_NONE);
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_NONE);
	sdl3_home_back(&home);
	eq(home.page, SDL3_HOME_LOAD);
	sdl3_home_request_delete(&home);
	sdl3_home_confirm_delete(&home);
	eq(home.action, SDL3_HOME_ACTION_DELETE);
	ok;
}

static int test_load_error_acknowledgement(void *state)
{
	struct sdl3_home_screen home;
	(void)state;

	sdl3_home_init(&home);
	sdl3_home_show_load_error(&home, "Fractured");
	require(home.visible);
	eq(home.page, SDL3_HOME_LOAD_ERROR);
	require(streq(home.load_error_name, "Fractured"));
	sdl3_home_activate(&home);
	eq(home.action, SDL3_HOME_ACTION_ACKNOWLEDGE);
	home.action = SDL3_HOME_ACTION_NONE;
	sdl3_home_back(&home);
	eq(home.action, SDL3_HOME_ACTION_ACKNOWLEDGE);
	ok;
}

static int test_world_intro_returns_to_same_home_row(void *state)
{
	struct sdl3_home_screen home;
	(void)state;
	sdl3_home_init(&home);
	sdl3_home_open(&home);
	home.selected_row = SDL3_HOME_WORLD;
	require(sdl3_home_root_row_enabled(&home, home.selected_row));
	sdl3_home_activate(&home);
	eq(home.page, SDL3_HOME_WORLD_PAGE);
	eq(home.action, SDL3_HOME_ACTION_NONE);
	eq(home.introduction.tab, 0);
	home.introduction.tab = 2;
	home.introduction.offset = 4;
	sdl3_home_back(&home);
	eq(home.page, SDL3_HOME_ROOT);
	eq(home.selected_row, SDL3_HOME_WORLD);
	sdl3_home_activate(&home);
	eq(home.introduction.tab, 0);
	eq(home.introduction.offset, 0);
	ok;
}

const char *suite_name = "sdl3/home-model";
struct test tests[] = {
	{ "World introduction returns to the same Home row", test_world_intro_returns_to_same_home_row },
	{ "empty home", test_empty_home },
	{ "feature survives Home reopen", test_feature_survives_home_reopen },
	{ "unambiguous continue", test_unambiguous_continue },
	{ "load navigation", test_load_navigation },
	{ "secondary actions", test_secondary_actions },
	{ "live save filtering", test_live_save_filter },
	{ "stale continue is not actionable", test_stale_continue_is_not_actionable },
	{ "continue requires an exact remembered run",
		test_continue_requires_an_exact_remembered_run },
	{ "delete confirmation", test_delete_confirmation },
	{ "load error acknowledgement", test_load_error_acknowledgement },
	{ NULL, NULL },
};
