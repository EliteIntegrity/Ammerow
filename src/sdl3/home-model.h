/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home-model.h
 * \brief Platform-independent navigation state for the SDL3 home screens.
 */

#ifndef INCLUDED_SDL3_HOME_MODEL_H
#define INCLUDED_SDL3_HOME_MODEL_H

#include <stdbool.h>
#include "ui-world-intro.h"

#define SDL3_HOME_FEATURE_ASSET_CAPACITY 160

enum sdl3_home_page {
	SDL3_HOME_ROOT = 0,
	SDL3_HOME_ABOUT_MENU,
	SDL3_HOME_LOAD,
	SDL3_HOME_DELETE_CONFIRM,
	SDL3_HOME_LOAD_ERROR,
	SDL3_HOME_ABOUT_PAGE,
	SDL3_HOME_CREDITS_PAGE,
	SDL3_HOME_LEGAL_PAGE,
	SDL3_HOME_WORLD_PAGE
};

enum sdl3_home_action {
	SDL3_HOME_ACTION_NONE = 0,
	SDL3_HOME_ACTION_CONTINUE,
	SDL3_HOME_ACTION_NEW,
	SDL3_HOME_ACTION_LOAD,
	SDL3_HOME_ACTION_DELETE,
	SDL3_HOME_ACTION_ACKNOWLEDGE,
	SDL3_HOME_ACTION_SETTINGS,
	SDL3_HOME_ACTION_FIELD_GUIDE,
	SDL3_HOME_ACTION_MEMORIAL,
	SDL3_HOME_ACTION_QUIT
};

enum sdl3_home_notice {
	SDL3_HOME_NOTICE_NONE = 0,
	SDL3_HOME_NOTICE_SAVE_UNAVAILABLE,
	SDL3_HOME_NOTICE_SAVE_DELETED,
	SDL3_HOME_NOTICE_DELETE_FAILED
};

enum sdl3_home_root_row {
	SDL3_HOME_CONTINUE = 0,
	SDL3_HOME_NEW,
	SDL3_HOME_LOAD_RUN,
	SDL3_HOME_SETTINGS,
	SDL3_HOME_WORLD,
	SDL3_HOME_MEMORIAL,
	SDL3_HOME_HELP,
	SDL3_HOME_ABOUT,
	SDL3_HOME_QUIT,
	SDL3_HOME_ROOT_ROW_COUNT
};

enum sdl3_home_about_row {
	SDL3_HOME_ABOUT_OVERVIEW = 0,
	SDL3_HOME_ABOUT_CREDITS,
	SDL3_HOME_ABOUT_LEGAL,
	SDL3_HOME_ABOUT_ROW_COUNT
};

struct sdl3_home_save {
	char *filename;
	char *name;
	char *description;
	bool damaged;
};

struct sdl3_home_screen {
	bool visible;
	enum sdl3_home_page page;
	enum sdl3_home_page parent_page;
	int selected_row;
	int selected_about_row;
	int selected_save;
	int document_offset;
	struct ui_world_intro introduction;
	int continue_save;
	int save_count;
	int save_capacity;
	struct sdl3_home_save *saves;
	enum sdl3_home_action action;
	enum sdl3_home_notice notice;
	char load_error_name[128];
	char feature_asset[SDL3_HOME_FEATURE_ASSET_CAPACITY];
};

void sdl3_home_init(struct sdl3_home_screen *home);
void sdl3_home_open(struct sdl3_home_screen *home);
void sdl3_home_set_feature(struct sdl3_home_screen *home,
		const char *asset);
bool sdl3_home_root_row_enabled(const struct sdl3_home_screen *home, int row);
void sdl3_home_move(struct sdl3_home_screen *home, int delta);
void sdl3_home_scroll_document(struct sdl3_home_screen *home, int delta,
		int maximum);
void sdl3_home_activate(struct sdl3_home_screen *home);
void sdl3_home_back(struct sdl3_home_screen *home);
void sdl3_home_request_delete(struct sdl3_home_screen *home);
void sdl3_home_confirm_delete(struct sdl3_home_screen *home);
void sdl3_home_show_load_error(struct sdl3_home_screen *home,
		const char *name);
const char *sdl3_home_selected_filename(const struct sdl3_home_screen *home);
bool sdl3_home_save_is_catalogue_entry(const char *filename,
		const char *description);
bool sdl3_home_save_is_live(const char *filename, const char *description);
int sdl3_home_find_continue(const struct sdl3_home_save *saves, int count,
		const char *filename);

#endif /* INCLUDED_SDL3_HOME_MODEL_H */
