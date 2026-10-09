/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home-model.c
 * \brief Platform-independent navigation state for the SDL3 home screens.
 */

#include "sdl3/home-model.h"

#include <string.h>

static bool has_suffix(const char *text, const char *ending)
{
	size_t text_length;
	size_t ending_length;

	if (!text || !ending) return false;
	text_length = strlen(text);
	ending_length = strlen(ending);
	return ending_length <= text_length &&
		strcmp(text + text_length - ending_length, ending) == 0;
}

bool sdl3_home_save_is_catalogue_entry(const char *filename,
		const char *description)
{
	if (!filename || !filename[0]) return false;
	/* These are interrupted-save working files, not player-facing slots. */
	if (has_suffix(filename, ".new") || has_suffix(filename, ".old") ||
			has_suffix(filename, ".lok")) {
		return false;
	}
	/* Stock Angband retains dead saves as templates for another birth.  They
	 * belong in a memorial, not in a list promising to load a live run. */
	return !description || strstr(description, ", dead (") == NULL;
}

bool sdl3_home_save_is_live(const char *filename, const char *description)
{
	if (!sdl3_home_save_is_catalogue_entry(filename, description) ||
			!description || !description[0]) {
		return false;
	}
	return strcmp(description, "Invalid savefile") != 0;
}

int sdl3_home_find_continue(const struct sdl3_home_save *saves, int count,
		const char *filename)
{
	int i;

	if (!saves || count <= 0 || !filename || !filename[0]) return -1;
	for (i = 0; i < count; i++) {
		if (!saves[i].damaged && saves[i].filename &&
				strcmp(saves[i].filename, filename) == 0) {
			return i;
		}
	}
	return -1;
}

void sdl3_home_init(struct sdl3_home_screen *home)
{
	if (!home) return;
	memset(home, 0, sizeof(*home));
	home->continue_save = -1;
}

bool sdl3_home_root_row_enabled(const struct sdl3_home_screen *home, int row)
{
	if (!home) return false;
	if (row == SDL3_HOME_CONTINUE) {
		return home->saves && home->continue_save >= 0 &&
			home->continue_save < home->save_count;
	}
	if (row == SDL3_HOME_LOAD_RUN) {
		return home->saves && home->save_count > 0;
	}
	return row >= 0 && row < SDL3_HOME_ROOT_ROW_COUNT;
}

void sdl3_home_open(struct sdl3_home_screen *home)
{
	if (!home) return;
	home->visible = true;
	home->page = SDL3_HOME_ROOT;
	home->parent_page = SDL3_HOME_ROOT;
	home->selected_row = sdl3_home_root_row_enabled(home,
		SDL3_HOME_CONTINUE) ?
		SDL3_HOME_CONTINUE : SDL3_HOME_NEW;
	home->selected_save = sdl3_home_root_row_enabled(home,
		SDL3_HOME_CONTINUE) ?
		home->continue_save : 0;
	home->action = SDL3_HOME_ACTION_NONE;
	home->notice = SDL3_HOME_NOTICE_NONE;
}

void sdl3_home_set_feature(struct sdl3_home_screen *home,
		const char *asset)
{
	size_t length;

	if (!home) return;
	if (!asset) asset = "";
	length = strlen(asset);
	if (length >= sizeof(home->feature_asset)) {
		length = sizeof(home->feature_asset) - 1;
	}
	memcpy(home->feature_asset, asset, length);
	home->feature_asset[length] = '\0';
}

void sdl3_home_move(struct sdl3_home_screen *home, int delta)
{
	int candidate;

	if (!home || !delta) return;
	home->notice = SDL3_HOME_NOTICE_NONE;
	if (home->page == SDL3_HOME_ROOT) {
		candidate = home->selected_row;
		do {
			candidate += delta < 0 ? -1 : 1;
			if (candidate < 0) candidate = SDL3_HOME_ROOT_ROW_COUNT - 1;
			if (candidate >= SDL3_HOME_ROOT_ROW_COUNT) candidate = 0;
		} while (!sdl3_home_root_row_enabled(home, candidate));
		home->selected_row = candidate;
	} else if (home->page == SDL3_HOME_ABOUT_MENU) {
		candidate = home->selected_about_row + (delta < 0 ? -1 : 1);
		if (candidate < 0) candidate = SDL3_HOME_ABOUT_ROW_COUNT - 1;
		if (candidate >= SDL3_HOME_ABOUT_ROW_COUNT) candidate = 0;
		home->selected_about_row = candidate;
	} else if (home->page == SDL3_HOME_LOAD && home->save_count > 0) {
		candidate = home->selected_save + (delta < 0 ? -1 : 1);
		if (candidate < 0) candidate = home->save_count - 1;
		if (candidate >= home->save_count) candidate = 0;
		home->selected_save = candidate;
	}
}

void sdl3_home_scroll_document(struct sdl3_home_screen *home, int delta,
		int maximum)
{
	if (!home || !delta || (home->page != SDL3_HOME_CREDITS_PAGE &&
			home->page != SDL3_HOME_LEGAL_PAGE)) {
		return;
	}
	home->document_offset += delta;
	if (home->document_offset < 0) home->document_offset = 0;
	if (home->document_offset > maximum) home->document_offset = maximum;
}

void sdl3_home_activate(struct sdl3_home_screen *home)
{
	if (!home) return;
	if (home->page == SDL3_HOME_LOAD_ERROR) {
		home->action = SDL3_HOME_ACTION_ACKNOWLEDGE;
		return;
	}
	if (home->page == SDL3_HOME_LOAD) {
		if (home->save_count > 0) home->action = SDL3_HOME_ACTION_LOAD;
		return;
	}
	if (home->page == SDL3_HOME_ABOUT_MENU) {
		switch (home->selected_about_row) {
		case SDL3_HOME_ABOUT_OVERVIEW:
			home->parent_page = SDL3_HOME_ABOUT_MENU;
			home->page = SDL3_HOME_ABOUT_PAGE;
			break;
		case SDL3_HOME_ABOUT_CREDITS:
			home->parent_page = SDL3_HOME_ABOUT_MENU;
			home->page = SDL3_HOME_CREDITS_PAGE;
			home->document_offset = 0;
			break;
		case SDL3_HOME_ABOUT_LEGAL:
			home->parent_page = SDL3_HOME_ABOUT_MENU;
			home->page = SDL3_HOME_LEGAL_PAGE;
			home->document_offset = 0;
			break;
		default:
			break;
		}
		return;
	}
	if (home->page != SDL3_HOME_ROOT ||
			!sdl3_home_root_row_enabled(home, home->selected_row)) {
		return;
	}
	switch (home->selected_row) {
	case SDL3_HOME_CONTINUE:
		home->selected_save = home->continue_save;
		home->action = SDL3_HOME_ACTION_CONTINUE;
		break;
	case SDL3_HOME_NEW:
		home->action = SDL3_HOME_ACTION_NEW;
		break;
	case SDL3_HOME_LOAD_RUN:
		home->page = SDL3_HOME_LOAD;
		if (home->selected_save < 0 ||
				home->selected_save >= home->save_count) {
			home->selected_save = 0;
		}
		break;
	case SDL3_HOME_SETTINGS:
		home->action = SDL3_HOME_ACTION_SETTINGS;
		break;
	case SDL3_HOME_HELP:
		home->action = SDL3_HOME_ACTION_FIELD_GUIDE;
		break;
	case SDL3_HOME_ABOUT:
		home->page = SDL3_HOME_ABOUT_MENU;
		home->selected_about_row = SDL3_HOME_ABOUT_OVERVIEW;
		break;
	case SDL3_HOME_WORLD:
		home->page = SDL3_HOME_WORLD_PAGE;
		memset(&home->introduction, 0, sizeof(home->introduction));
		break;
	case SDL3_HOME_QUIT:
		home->action = SDL3_HOME_ACTION_QUIT;
		break;
	case SDL3_HOME_MEMORIAL:
		home->action = SDL3_HOME_ACTION_MEMORIAL;
		break;
	default:
		break;
	}
}

void sdl3_home_back(struct sdl3_home_screen *home)
{
	if (!home || home->page == SDL3_HOME_ROOT) return;
	if (home->page == SDL3_HOME_LOAD_ERROR) {
		home->action = SDL3_HOME_ACTION_ACKNOWLEDGE;
		return;
	}
	if (home->page == SDL3_HOME_DELETE_CONFIRM) {
		home->page = SDL3_HOME_LOAD;
		home->action = SDL3_HOME_ACTION_NONE;
		return;
	}
	if ((home->page == SDL3_HOME_ABOUT_PAGE ||
			home->page == SDL3_HOME_CREDITS_PAGE ||
			home->page == SDL3_HOME_LEGAL_PAGE) &&
			home->parent_page == SDL3_HOME_ABOUT_MENU) {
		home->page = SDL3_HOME_ABOUT_MENU;
		home->parent_page = SDL3_HOME_ROOT;
		home->action = SDL3_HOME_ACTION_NONE;
		home->document_offset = 0;
		return;
	}
	home->page = SDL3_HOME_ROOT;
	home->parent_page = SDL3_HOME_ROOT;
	home->action = SDL3_HOME_ACTION_NONE;
	home->notice = SDL3_HOME_NOTICE_NONE;
	home->document_offset = 0;
}

void sdl3_home_request_delete(struct sdl3_home_screen *home)
{
	if (!home || home->page != SDL3_HOME_LOAD || !home->saves ||
			home->selected_save < 0 || home->selected_save >= home->save_count) {
		return;
	}
	home->page = SDL3_HOME_DELETE_CONFIRM;
	home->action = SDL3_HOME_ACTION_NONE;
	home->notice = SDL3_HOME_NOTICE_NONE;
}

void sdl3_home_confirm_delete(struct sdl3_home_screen *home)
{
	if (!home || home->page != SDL3_HOME_DELETE_CONFIRM) return;
	home->action = SDL3_HOME_ACTION_DELETE;
}

void sdl3_home_show_load_error(struct sdl3_home_screen *home,
		const char *name)
{
	size_t length;

	if (!home) return;
	home->visible = true;
	home->page = SDL3_HOME_LOAD_ERROR;
	home->action = SDL3_HOME_ACTION_NONE;
	home->notice = SDL3_HOME_NOTICE_NONE;
	if (!name) name = "Unknown run";
	length = strlen(name);
	if (length >= sizeof(home->load_error_name)) {
		length = sizeof(home->load_error_name) - 1;
	}
	memcpy(home->load_error_name, name, length);
	home->load_error_name[length] = '\0';
}

const char *sdl3_home_selected_filename(const struct sdl3_home_screen *home)
{
	if (!home || !home->saves || home->selected_save < 0 ||
			home->selected_save >= home->save_count) {
		return NULL;
	}
	return home->saves[home->selected_save].filename;
}
