/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/pause-menu.c
 * \brief Renderer-owned in-game Escape menu for the SDL3 frontend.
 */

#include "angband.h"

#include "sdl3/pause-menu.h"
#include "sdl3/menu-layout.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

static const char *pause_labels[SDL3_PAUSE_ROOT_ROW_COUNT] = {
	"Back to Game",
	"Settings",
	"Help / Controls",
	"Save and Return Home",
	"Abandon Run"
};

static void draw_root(const struct sdl3_pause_menu *menu,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	int cols = SDL_min(SDL3_PAUSE_PANEL_COLS, visual->cols - 4);
	int rows = SDL_min(SDL3_PAUSE_PANEL_ROWS, visual->rows - 2);
	int col = (visual->cols - cols) / 2;
	int row = (visual->rows - rows) / 2;
	struct sdl3_menu_layout layout = sdl3_pause_root_layout(visual->cols,
		visual->rows);
	int i;

	if (layout.count == 0) return;
	sdl3_ui_draw_panel(renderer, visual, theme, col, row, cols, rows, true);
	sdl3_ui_draw_text(visual, "GAME MENU", col + 4, row + 1, cols - 8,
		theme->title);
	sdl3_ui_draw_text(visual, "The world waits between turns.", col + 4,
		row + 2, cols - 8, theme->muted);
	for (i = 0; i < SDL3_PAUSE_ROOT_ROW_COUNT; i++) {
		int item_row = sdl3_menu_item_row(&layout, i);
		SDL_Color color = i == SDL3_PAUSE_ABANDON ?
			sdl3_theme_color(theme, COLOUR_L_RED) : theme->text;

		if (menu->selected_row == i) {
			sdl3_ui_draw_selection(renderer, visual, theme, col + 2, item_row,
				cols - 4);
			sdl3_ui_draw_text(visual, ">", col + 3, item_row, 1, theme->accent);
		}
		sdl3_ui_draw_text(visual, pause_labels[i], col + 6, item_row, cols - 10,
			color);
	}
	sdl3_ui_draw_text(visual, "Click or Enter selects   Escape returns", col + 4,
		row + rows - 1, cols - 8, theme->muted);
}

static void draw_confirm(const struct sdl3_pause_menu *menu,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	int cols = SDL_min(SDL3_PAUSE_CONFIRM_COLS, visual->cols - 4);
	int rows = SDL_min(SDL3_PAUSE_CONFIRM_ROWS, visual->rows - 2);
	int col = (visual->cols - cols) / 2;
	int row = (visual->rows - rows) / 2;
	struct sdl3_menu_layout back = sdl3_pause_confirm_choice_layout(
		visual->cols, visual->rows, false);
	struct sdl3_menu_layout abandon = sdl3_pause_confirm_choice_layout(
		visual->cols, visual->rows, true);
	const struct sdl3_menu_layout *selected = menu->abandon_confirmed ?
		&abandon : &back;
	SDL_Color danger = sdl3_theme_color(theme, COLOUR_L_RED);

	if (back.count == 0) return;
	sdl3_ui_draw_panel(renderer, visual, theme, col, row, cols, rows, true);
	sdl3_ui_draw_text(visual, "ABANDON RUN?", col + 4, row + 1, cols - 8,
		danger);
	sdl3_ui_draw_text(visual, "This permanently retires the current character.",
		col + 4, row + 3, cols - 8, theme->text);
	sdl3_ui_draw_text(visual, "The run will end through the normal death flow.",
		col + 4, row + 4, cols - 8, theme->muted);
	sdl3_ui_draw_selection(renderer, visual, theme,
		selected->col, selected->row, selected->width);
	sdl3_ui_draw_text(visual, menu->abandon_confirmed ? "  Go back" : "> Go back",
		back.col + 1, back.row, back.width - 1, theme->text);
	sdl3_ui_draw_text(visual, menu->abandon_confirmed ? "> Abandon run" :
		"  Abandon run", abandon.col + 1, abandon.row,
		abandon.width - 1, danger);
	sdl3_ui_draw_text(visual, "Click or Left/Right + Enter   Escape cancels",
		col + 4, row + rows - 2, cols - 8, theme->muted);
}

static void draw_help(SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	int width = SDL_min(88, visual->cols - 8);
	int left = SDL_max(4, (visual->cols - width) / 2);

	sdl3_ui_fill_output(renderer, visual,
		(SDL_Color){ 0, 0, 0, SDL_ALPHA_OPAQUE });
	sdl3_ui_draw_text(visual, "HELP / CONTROLS", left, 3, width, theme->title);
	sdl3_ui_draw_text(visual, "Movement", left, 7, 18, theme->accent);
	sdl3_ui_draw_text(visual, "Arrow keys, keypad, or classic roguelike keys", left + 20,
		7, width - 20, theme->text);
	sdl3_ui_draw_text(visual, "Look", left, 9, 18, theme->accent);
	sdl3_ui_draw_text(visual, "L, then movement keys; Escape cancels Look", left + 20,
		9, width - 20, theme->text);
	sdl3_ui_draw_text(visual, "Map zoom", left, 11, 18, theme->accent);
	sdl3_ui_draw_text(visual, "Mouse wheel or Ctrl + Plus/Minus; Ctrl + 0 resets",
		left + 20, 11,
		width - 20, theme->text);
	sdl3_ui_draw_text(visual, "Settings", left, 13, 18, theme->accent);
	sdl3_ui_draw_text(visual, "Ctrl + Alt + S", left + 20, 13, width - 20,
		theme->text);
	sdl3_ui_draw_text(visual, "Save", left, 15, 18, theme->accent);
	sdl3_ui_draw_text(visual, "Ctrl + S saves; Ctrl + X saves and returns Home", left + 20,
		15, width - 20, theme->text);
	sdl3_ui_draw_text(visual, "Field guide", left, 17, 18, theme->accent);
	sdl3_ui_draw_text(visual,
		"F1 or ? opens controls, world, fishing, and cave help", left + 20,
		17, width - 20, theme->text);
	sdl3_ui_draw_text(visual,
		"Ctrl+1/2/3 toggles top messages, stats, bottom messages", left,
		19, width, theme->muted);
	sdl3_ui_draw_text(visual, "Escape returns to the game menu", left,
		visual->rows - 3, width, theme->muted);
}

void sdl3_pause_menu_draw(const struct sdl3_pause_menu *menu,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	if (!menu || !menu->visible || !renderer || !visual || !theme) return;
	if (menu->page == SDL3_PAUSE_HELP) {
		draw_help(renderer, visual, theme);
	} else if (menu->page == SDL3_PAUSE_CONFIRM_ABANDON) {
		draw_confirm(menu, renderer, visual, theme);
	} else {
		draw_root(menu, renderer, visual, theme);
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}
