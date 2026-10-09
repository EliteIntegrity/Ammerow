/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home.c
 * \brief SDL3 home, load-run, and help screens.
 *
 *
 */

#include "angband.h"
#include "buildid.h"
#include "init.h"
#include "savefile.h"

#include "sdl3/home.h"
#include "sdl3/credits.h"
#include "sdl3/home-layout.h"
#include "sdl3/legal.h"
#include "sdl3/menu-layout.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"
#include "sdl3/world-intro.h"
#include "ui-game.h"

static const char *root_labels[SDL3_HOME_ROOT_ROW_COUNT] = {
	"CONTINUE",
	"NEW RUN",
	"LOAD RUN",
	"SETTINGS",
	"LORE",
	"MEMORIAL",
	"HELP / FIELD GUIDE",
	"ABOUT",
	"QUIT"
};

static const char *about_labels[SDL3_HOME_ABOUT_ROW_COUNT] = {
	"ABOUT AMMEROW",
	"CREDITS",
	"LEGAL / LICENCES"
};

static const char *about_details[SDL3_HOME_ABOUT_ROW_COUNT] = {
	"The game, its version, and its origins",
	"The many hands beneath Ammerow",
	"Code and asset licences, permissions, and notices"
};

static void clear_saves(struct sdl3_home_screen *home)
{
	int i;

	for (i = 0; i < home->save_count; i++) {
		string_free(home->saves[i].filename);
		string_free(home->saves[i].name);
		string_free(home->saves[i].description);
	}
	mem_free(home->saves);
	home->saves = NULL;
	home->save_count = 0;
	home->save_capacity = 0;
	home->continue_save = -1;
	home->selected_save = 0;
}

static int compare_saves(const void *left, const void *right)
{
	const struct sdl3_home_save *a = left;
	const struct sdl3_home_save *b = right;

	return my_stricmp(a->name, b->name);
}

void sdl3_home_cleanup(struct sdl3_home_screen *home)
{
	if (!home) return;
	clear_saves(home);
	memset(home, 0, sizeof(*home));
	home->continue_save = -1;
}

bool sdl3_home_refresh_saves(struct sdl3_home_screen *home)
{
	savefile_getter getter = NULL;
	bool readable;
	const char *requested_name = savefile[0] ?
		savefile + path_filename_index(savefile) : NULL;

	if (!home) return false;
	clear_saves(home);
	while (got_savefile(&getter)) {
		const struct savefile_details *details =
			get_savefile_details(getter);
		struct sdl3_home_save *entry;

		bool live;

		if (!details) continue;
		if (!sdl3_home_save_is_catalogue_entry(details->fnam,
				details->desc)) {
			continue;
		}
		live = sdl3_home_save_is_live(details->fnam, details->desc);
		if (home->save_count == home->save_capacity) {
			int capacity = home->save_capacity ?
				home->save_capacity * 2 : 8;

			home->saves = mem_realloc(home->saves,
				capacity * sizeof(*home->saves));
			memset(home->saves + home->save_capacity, 0,
				(capacity - home->save_capacity) *
				sizeof(*home->saves));
			home->save_capacity = capacity;
		}
		entry = &home->saves[home->save_count++];
		entry->filename = string_make(details->fnam);
		entry->name = string_make(details->fnam + details->foff);
		entry->description = string_make(live ? details->desc :
			"Damaged or incompatible save");
		entry->damaged = !live;
	}
	readable = got_savefile_dir(getter);
	cleanup_savefile_getter(getter);
	if (!readable) return false;

	if (home->save_count > 1) {
		qsort(home->saves, home->save_count, sizeof(*home->saves),
			compare_saves);
	}
	home->continue_save = sdl3_home_find_continue(home->saves,
		home->save_count, requested_name);
	return true;
}

bool sdl3_home_selected_save_is_live(const struct sdl3_home_screen *home)
{
	const char *filename = sdl3_home_selected_filename(home);
	const char *description;
	char path[1024];

	if (!filename) return false;
	path_build(path, sizeof(path), ANGBAND_DIR_SAVE, filename);
	if (!file_exists(path)) return false;
	description = savefile_get_description(path);
	return sdl3_home_save_is_live(filename, description);
}

void sdl3_home_mark_save_damaged(struct sdl3_home_screen *home,
		const char *filename)
{
	int i;

	if (!home || !filename || !filename[0]) return;
	for (i = 0; i < home->save_count; i++) {
		struct sdl3_home_save *entry = &home->saves[i];

		if (!entry->filename || !streq(entry->filename, filename)) continue;
		entry->damaged = true;
		string_free(entry->description);
		entry->description = string_make("Damaged or incompatible save");
		if (home->continue_save == i) home->continue_save = -1;
		return;
	}
}

bool sdl3_home_delete_selected_save(const struct sdl3_home_screen *home)
{
	const char *filename = sdl3_home_selected_filename(home);
	char path[1024];
	bool deleted;

	/* Only delete the exact basename returned by save enumeration.  In
	 * particular, do not infer or remove .old, .new, panic, or lock files. */
	if (!filename || !filename[0] || path_filename_index(filename) != 0 ||
			streq(filename, ".") || streq(filename, "..")) {
		return false;
	}
	path_build(path, sizeof(path), ANGBAND_DIR_SAVE, filename);
	safe_setuid_grab();
	deleted = file_exists(path) && file_delete(path);
	safe_setuid_drop();
	return deleted;
}

static const char *root_detail(const struct sdl3_home_screen *home, int row)
{
	if (home->notice == SDL3_HOME_NOTICE_SAVE_UNAVAILABLE) {
		return "That run is no longer available.";
	}
	if (home->notice == SDL3_HOME_NOTICE_SAVE_DELETED) {
		return "Saved run deleted.";
	}
	if (home->notice == SDL3_HOME_NOTICE_DELETE_FAILED) {
		return "The saved run could not be deleted.";
	}
	if (row == SDL3_HOME_CONTINUE) {
		if (sdl3_home_root_row_enabled(home, SDL3_HOME_CONTINUE)) {
			return "";
		}
		return "No living run to continue.";
	}
	if (row == SDL3_HOME_NEW) return "Begin at the rain-worn village.";
	if (row == SDL3_HOME_LOAD_RUN) {
		return home->save_count > 0 ? "Choose another living run." :
			"No living saved runs.";
	}
	if (row == SDL3_HOME_SETTINGS) return "Shape how Ammerow looks and sounds.";
	if (row == SDL3_HOME_WORLD) return "The lost roads, the people, and your expedition.";
	if (row == SDL3_HOME_MEMORIAL) return "Remember scored expeditions.";
	if (row == SDL3_HOME_HELP) return "Controls and practical guides to play.";
	if (row == SDL3_HOME_ABOUT) {
		return "About the game, its creators, and its licences.";
	}
	return "Leave Ammerow.";
}

static struct sdl3_menu_layout root_secondary_layout(
		const struct sdl3_home_layout *layout)
{
	return (struct sdl3_menu_layout) {
		layout->menu_col,
		layout->menu_row,
		layout->menu_width,
		SDL3_HOME_ROOT_ROW_COUNT - 1,
		layout->menu_spacing,
		1
	};
}

static void draw_feature(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme,
		const struct sdl3_home_layout *layout)
{
	SDL_FRect portrait;
	SDL_Color backdrop;
	SDL_FRect rule;

	if (!layout->hero_visible || !home->feature_asset[0]) return;
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	backdrop = theme->panel;
	backdrop.a = 72;
	sdl3_ui_fill_cells(renderer, visual, layout->hero_col,
		layout->hero_row, layout->hero_cols, layout->hero_rows, backdrop);
	rule = (SDL_FRect) {
		(float)(visual->origin_x + layout->hero_col * visual->cell_width),
		(float)(visual->origin_y + (layout->hero_row + 2) *
			visual->cell_height),
		2.0f,
		(float)((layout->hero_rows - 4) * visual->cell_height)
	};
	backdrop = theme->accent;
	backdrop.a = 70;
	sdl3_ui_set_color(renderer, backdrop);
	SDL_RenderFillRect(renderer, &rule);
	portrait = (SDL_FRect) {
		(float)(visual->origin_x + (layout->hero_col + 2) *
			visual->cell_width),
		(float)(visual->origin_y + (layout->hero_row + 1) *
			visual->cell_height),
		(float)((layout->hero_cols - 4) * visual->cell_width),
		(float)((layout->hero_rows - 2) * visual->cell_height)
	};
	sdl3_monster_art_draw(&visual->home_art, renderer, visual->font.path,
		home->feature_asset, &portrait);
}

static struct sdl3_menu_layout about_menu_layout(int cols, int rows)
{
	int width = SDL_min(88, cols - 8);

	(void)rows;
	return (struct sdl3_menu_layout) {
		SDL_max(4, (cols - width) / 2),
		9,
		width,
		SDL3_HOME_ABOUT_ROW_COUNT,
		3,
		2
	};
}

static void draw_root(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	struct sdl3_home_layout layout;
	struct sdl3_menu_layout menu;
	const struct sdl3_home_save *save = NULL;
	int i;

	if (!sdl3_home_layout_init(visual->cols, visual->rows, &layout)) {
		return;
	}
	sdl3_ui_fill_output(renderer, visual, theme->canvas);
	draw_feature(home, renderer, visual, theme, &layout);
	sdl3_ui_draw_text(visual, VERSION_TRADEMARK_HOME, layout.title_col,
		layout.title_row, layout.menu_width, theme->title);
	sdl3_ui_draw_text(visual, "L A N D S   B E Y O N D", layout.title_col,
		layout.title_row + 2, layout.menu_width, theme->accent);
	sdl3_ui_draw_text(visual, "THE WORLD IS DEEPER THAN IT REMEMBERS",
		layout.title_col, layout.title_row + 4,
		layout.menu_width, theme->muted);
	sdl3_ui_draw_text(visual, format("VERSION %s", buildver),
		layout.title_col, layout.title_row + 5,
		layout.menu_width, theme->muted);

	if (sdl3_home_root_row_enabled(home, SDL3_HOME_CONTINUE)) {
		save = &home->saves[home->continue_save];
	}
	sdl3_ui_draw_panel(renderer, visual, theme, layout.menu_col,
		layout.run_card_row, layout.menu_width, layout.run_card_rows, false);
	if (home->selected_row == SDL3_HOME_CONTINUE) {
		sdl3_ui_draw_selection(renderer, visual, theme, layout.menu_col + 1,
			layout.run_card_row + 1, layout.menu_width - 2);
		sdl3_ui_draw_text(visual, ">", layout.menu_col + 2,
			layout.run_card_row + 1, 1, theme->accent);
	}
	sdl3_ui_draw_text(visual, save ? "CONTINUE RUN" : "NO RUN TO CONTINUE",
		layout.menu_col + 5, layout.run_card_row + 1,
		layout.menu_width - 7, save ? theme->text : theme->muted);
	sdl3_ui_draw_text(visual, save ?
		(save->description[0] ? save->description : save->name) :
		"Begin a new expedition below.", layout.menu_col + 5,
		layout.run_card_row + 2, layout.menu_width - 7, theme->muted);

	menu = root_secondary_layout(&layout);
	for (i = 1; i < SDL3_HOME_ROOT_ROW_COUNT; i++) {
		int row = sdl3_menu_item_row(&menu, i - 1);
		bool enabled = sdl3_home_root_row_enabled(home, i);
		SDL_Color color = enabled ? theme->text : theme->muted;

		if (home->selected_row == i) {
			sdl3_ui_draw_selection(renderer, visual, theme, layout.menu_col,
				row, layout.menu_width);
			sdl3_ui_draw_text(visual, ">", layout.menu_col + 1, row, 1,
				theme->accent);
		}
		sdl3_ui_draw_text(visual, root_labels[i], layout.menu_col + 4, row,
			layout.menu_width - 5, color);
	}
	sdl3_ui_draw_text(visual, root_detail(home, home->selected_row),
		layout.menu_col, layout.detail_row, layout.menu_width,
		home->notice == SDL3_HOME_NOTICE_NONE ? theme->muted : theme->accent);
	sdl3_ui_fill_cells(renderer, visual, 0, layout.footer_row - 1,
		layout.cols, 3, theme->canvas);
	sdl3_ui_draw_text(visual, "MOUSE OR ARROWS SELECT   ENTER CONFIRMS   F1 HELP",
		layout.title_col, layout.footer_row,
		layout.cols - layout.title_col * 2, theme->muted);
}

static void draw_about_menu(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	struct sdl3_menu_layout menu = about_menu_layout(visual->cols,
		visual->rows);
	int i;

	sdl3_ui_draw_text(visual, "ABOUT", menu.col, 3, menu.width,
		theme->title);
	sdl3_ui_draw_text(visual,
		"The game and the people behind it.",
		menu.col, 5, menu.width, theme->muted);
	for (i = 0; i < SDL3_HOME_ABOUT_ROW_COUNT; i++) {
		int row = sdl3_menu_item_row(&menu, i);

		if (home->selected_about_row == i) {
			sdl3_ui_draw_selection(renderer, visual, theme, menu.col, row,
				menu.width);
			sdl3_ui_draw_text(visual, ">", menu.col + 1, row, 1,
				theme->accent);
		}
		sdl3_ui_draw_text(visual, about_labels[i], menu.col + 4, row, 24,
			theme->text);
		sdl3_ui_draw_text(visual, about_details[i], menu.col + 4, row + 1,
			menu.width - 6, theme->muted);
	}
	sdl3_ui_draw_text(visual, "Click or Enter opens   Escape returns home",
		menu.col, visual->rows - 3, menu.width, theme->muted);
}

static void draw_load(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	int first_row = 7;
	int available = SDL_max(1, visual->rows - first_row - 5);
	int first = 0;
	int i;

	if (home->selected_save >= available) {
		first = home->selected_save - available + 1;
	}
	sdl3_ui_draw_text(visual, "LOAD RUN", left, 3, width, theme->title);
	sdl3_ui_draw_text(visual, "Choose a saved run.", left, 5, width,
		theme->muted);
	for (i = 0; i < available && first + i < home->save_count; i++) {
		int index = first + i;
		int row = first_row + i;
		const struct sdl3_home_save *save = &home->saves[index];

		if (home->selected_save == index) {
			sdl3_ui_draw_selection(renderer, visual, theme, left, row, width);
			sdl3_ui_draw_text(visual, ">", left + 1, row, 1, theme->accent);
		}
		sdl3_ui_draw_text(visual, save->name, left + 4, row, 22,
			save->damaged ? theme->accent : theme->text);
		sdl3_ui_draw_text(visual, save->description, left + 28, row, width - 30,
			save->damaged ? theme->accent : theme->muted);
	}
	if (home->notice == SDL3_HOME_NOTICE_SAVE_DELETED) {
		sdl3_ui_draw_text(visual, "Saved run deleted.", left,
			visual->rows - 5, width, theme->accent);
	} else if (home->notice == SDL3_HOME_NOTICE_DELETE_FAILED) {
		sdl3_ui_draw_text(visual, "The saved run could not be deleted.", left,
			visual->rows - 5, width, theme->accent);
	}
	sdl3_ui_draw_text(visual,
		"Enter loads   Delete / D removes   Escape returns home", left,
		visual->rows - 3, width, theme->muted);
}

static void draw_delete_confirm(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	const struct sdl3_home_save *save = NULL;
	int row = SDL_max(5, visual->rows / 2 - 5);

	(void)renderer;
	if (home->saves && home->selected_save >= 0 &&
			home->selected_save < home->save_count) {
		save = &home->saves[home->selected_save];
	}
	sdl3_ui_draw_text(visual, "DELETE SAVED RUN?", left, row, width,
		theme->title);
	if (save) {
		sdl3_ui_draw_text(visual, save->name, left, row + 2, width,
			theme->accent);
		sdl3_ui_draw_text(visual, save->description, left, row + 3, width,
			theme->muted);
	}
	sdl3_ui_draw_text(visual,
		"This permanently removes this save file. It cannot be undone.",
		left, row + 5, width, theme->text);
	sdl3_ui_draw_text(visual, "Y deletes   N or Escape cancels", left,
		row + 8, width, theme->accent);
}

static void draw_load_error(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	int row = SDL_max(5, visual->rows / 2 - 5);

	(void)renderer;
	sdl3_ui_draw_text(visual, "THIS RUN COULD NOT BE LOADED", left, row,
		width, theme->title);
	sdl3_ui_draw_text(visual, home->load_error_name, left, row + 2, width,
		theme->accent);
	sdl3_ui_draw_text(visual,
		"The save appears damaged or incompatible. It has not been changed.",
		left, row + 5, width, theme->text);
	sdl3_ui_draw_text(visual, "Enter or Escape returns Home", left,
		row + 8, width, theme->muted);
}

static void draw_about(SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	(void)renderer;
	sdl3_ui_draw_text(visual, "ABOUT AMMEROW", left, 3, width, theme->title);
	sdl3_ui_draw_text(visual, format("VERSION %s", buildver), left, 4, width,
		theme->muted);
	sdl3_ui_draw_text(visual, VERSION_TRADEMARK_NAME, left, 5, width,
		theme->accent);
	sdl3_ui_draw_text(visual,
		"An original roguelike built on the engine of The Scottish Game.", left,
		6, width, theme->text);
	sdl3_ui_draw_text(visual, "Created by John Horton", left, 8, width, theme->accent);
	sdl3_ui_draw_text(visual, "A turn-based expedition through the Meridian watershed:",
		left, 10, width, theme->text);
	sdl3_ui_draw_text(visual, "hostile lands, deep dungeons, climbing, and cave fishing.",
		left, 11, width, theme->text);
	sdl3_ui_draw_text(visual, "The program is free software under the GNU GPL v2.",
		left, 14, width, theme->text);
	sdl3_ui_draw_text(visual, "Artwork and audio have separate licences; see Legal / Licences.",
		left, 15, width, theme->text);
	sdl3_ui_draw_text(visual, "Credits acknowledges the contributors to this game and its engine.",
		left, 17, width, theme->muted);
	sdl3_ui_draw_text(visual, "Escape returns to About   F1 / ? opens the Field Guide",
		left, visual->rows - 3, width, theme->muted);
}

bool sdl3_home_select_at(struct sdl3_home_screen *home, int cols, int rows,
		int col, int row)
{
	if (!home) return false;
	if (home->page == SDL3_HOME_ROOT) {
		struct sdl3_home_layout layout;
		struct sdl3_menu_layout menu;
		int item;

		if (!sdl3_home_layout_init(cols, rows, &layout)) return false;
		if (col >= layout.menu_col &&
				col < layout.menu_col + layout.menu_width &&
				row >= layout.run_card_row &&
				row < layout.run_card_row + layout.run_card_rows &&
				sdl3_home_root_row_enabled(home, SDL3_HOME_CONTINUE)) {
			home->selected_row = SDL3_HOME_CONTINUE;
			home->notice = SDL3_HOME_NOTICE_NONE;
			return true;
		}
		menu = root_secondary_layout(&layout);
		item = sdl3_menu_item_at(&menu, col, row);
		if (item < 0 || !sdl3_home_root_row_enabled(home, item + 1)) {
			return false;
		}
		home->selected_row = item + 1;
		home->notice = SDL3_HOME_NOTICE_NONE;
		return true;
	}
	if (home->page == SDL3_HOME_ABOUT_MENU) {
		struct sdl3_menu_layout menu = about_menu_layout(cols, rows);
		int item = sdl3_menu_item_at(&menu, col, row);

		if (item < 0) return false;
		home->selected_about_row = item;
		return true;
	}
	if (home->page == SDL3_HOME_LOAD && home->save_count > 0) {
		int first_row = 7;
		int available = SDL_max(1, rows - first_row - 5);
		int first = home->selected_save >= available ?
			home->selected_save - available + 1 : 0;
		struct sdl3_menu_layout menu = {
			SDL_max(4, (cols - SDL_min(88, cols - 8)) / 2),
			first_row,
			SDL_min(88, cols - 8),
			SDL_min(available, home->save_count - first),
			1,
			1
		};
		int item = sdl3_menu_item_at(&menu, col, row);

		if (item < 0) return false;
		home->selected_save = first + item;
		home->notice = SDL3_HOME_NOTICE_NONE;
		return true;
	}
	return false;
}

void sdl3_home_draw(const struct sdl3_home_screen *home,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	int width;
	int left;

	if (!home || !home->visible || !renderer || !visual || !theme) return;
	sdl3_ui_fill_output(renderer, visual,
		(SDL_Color){ 0, 0, 0, SDL_ALPHA_OPAQUE });
	width = SDL_min(88, visual->cols - 8);
	left = SDL_max(4, (visual->cols - width) / 2);
	if (home->page == SDL3_HOME_ABOUT_MENU) {
		draw_about_menu(home, renderer, visual, theme);
	} else if (home->page == SDL3_HOME_LOAD) {
		draw_load(home, renderer, visual, theme, left, width);
	} else if (home->page == SDL3_HOME_DELETE_CONFIRM) {
		draw_delete_confirm(home, renderer, visual, theme, left, width);
	} else if (home->page == SDL3_HOME_LOAD_ERROR) {
		draw_load_error(home, renderer, visual, theme, left, width);
	} else if (home->page == SDL3_HOME_ABOUT_PAGE) {
		draw_about(renderer, visual, theme, left, width);
	} else if (home->page == SDL3_HOME_WORLD_PAGE) {
		sdl3_world_intro_draw(&home->introduction, renderer, visual, theme);
	} else if (home->page == SDL3_HOME_CREDITS_PAGE) {
		sdl3_credits_draw(renderer, visual, theme, left, width,
			home->document_offset);
	} else if (home->page == SDL3_HOME_LEGAL_PAGE) {
		sdl3_legal_draw(renderer, visual, theme, left, width,
			home->document_offset);
	} else {
		draw_root(home, renderer, visual, theme);
	}
}
