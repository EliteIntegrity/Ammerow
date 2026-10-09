/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-player-properties.c 
 * \brief UI for class and race abilities
 *
 * Copyright (c) 1997-2020 Ben Harrison, James E. Wilson, Robert A. Koeneke,
 * Leon Marrick, Bahman Rabii, Nick McConnell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "player-properties.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-player-properties.h"
#include "ui-screen.h"

/**
 * ------------------------------------------------------------------------
 * Code for viewing race and class abilities
 * ------------------------------------------------------------------------ */

static char view_ability_tag(struct menu *menu, int oid)
{
	return all_letters_nohjkl[oid];
}

static const char *view_ability_group_name(int group)
{
	switch (group) {
	case PLAYER_FLAG_SPECIAL: return "Specialty";
	case PLAYER_FLAG_CLASS: return "Calling";
	case PLAYER_FLAG_RACE: return "Heritage";
	default: return "Unknown";
	}
}

static uint8_t view_ability_group_attr(int group)
{
	switch (group) {
	case PLAYER_FLAG_SPECIAL: return COLOUR_GREEN;
	case PLAYER_FLAG_CLASS: return COLOUR_UMBER;
	case PLAYER_FLAG_RACE: return COLOUR_ORANGE;
	default: return COLOUR_PURPLE;
	}
}

/** Present the existing ability browser as a responsive semantic screen. */
static void present_ability_menu(struct menu *menu)
{
	struct player_ability *choices;
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[PLAYER_ABILITY_LIST_CAPACITY] = { 0 };
	char subtitle[160];
	char context[2048];
	int count;
	int cursor;
	int i;

	if (!Term || !Term->screen_hook || !menu || !player) return;
	choices = menu_priv(menu);
	count = MIN(MAX(0, menu->count), (int)N_ELEMENTS(rows));
	cursor = count > 0 ? MIN(MAX(0, menu->cursor), count - 1) : -1;
	for (i = 0; i < count; i++) {
		rows[i].label = choices[i].name;
		rows[i].prefix = view_ability_group_name(choices[i].group);
		rows[i].tag = view_ability_tag(menu, i);
		rows[i].attr = view_ability_group_attr(choices[i].group);
		rows[i].enabled = true;
	}
	strnfmt(subtitle, sizeof(subtitle), "%s %s   Level %d",
		player->race->name, player->class->name, player->lev);
	screen.kind = UI_SCREEN_ABILITIES;
	screen.title = "Abilities";
	screen.subtitle = subtitle;
	screen.help =
		"Up/Down or 8/2 browses   Letter shortcuts work   Escape returns";
	screen.context_title = cursor >= 0 ? choices[cursor].name : "Abilities";
	strnfmt(context, sizeof(context),
		"Origin and class traits, not learned book magic. "
		"This screen explains them; it does not activate them.\n\n%s",
		cursor >= 0 ? choices[cursor].desc :
		"This character has no inherent abilities.");
	screen.context = context;
	screen.content_col = 4;
	screen.content_row = 8;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = cursor;
	screen.row_offset = MIN(MAX(0, menu->top), MAX(0, count - 1));
	screen.row_count = count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}

/**
 * Display an entry on the gain ability menu
 */
static void view_ability_display(struct menu *menu, int oid, bool cursor,
	int row, int col, int width)
{
	char buf[80];
	uint8_t color;
	struct player_ability *choices = menu->menu_data;

	switch (choices[oid].group) {
	case PLAYER_FLAG_SPECIAL:
		{
			strnfmt(buf, sizeof(buf), "Specialty Ability: %s",
				choices[oid].name);
			color = COLOUR_GREEN;
			break;
		}
	case PLAYER_FLAG_CLASS:
		{
			strnfmt(buf, sizeof(buf), "Class: %s",
				choices[oid].name);
			color = COLOUR_UMBER;
			break;
		}
	case PLAYER_FLAG_RACE:
		{
			strnfmt(buf, sizeof(buf), "Racial: %s",
				choices[oid].name);
			color = COLOUR_ORANGE;
			break;
		}
	default:
		{
			my_strcpy(buf, "Mysterious", sizeof(buf));
			color = COLOUR_PURPLE;
		}
	}

	/* Print it */
	c_put_str(cursor ? COLOUR_WHITE : color, buf, row, col);

}


/**
 * Show ability long description when browsing
 */
static void view_ability_menu_browser(int oid, void *data, const region *loc)
{
	struct player_ability *choices = data;
	if (Term->screen_hook) return;

	/* Redirect output to the screen */
	text_out_hook = text_out_to_screen;
	text_out_wrap = 60;
	text_out_indent = loc->col - 1;
	text_out_pad = 1;

	clear_from(loc->row + loc->page_rows);
	Term_gotoxy(loc->col, loc->row + loc->page_rows);
	text_out("\nOrigin and class traits, not learned book magic. "
		"This screen explains them; it does not activate them.\n");
	text_out_c(COLOUR_L_BLUE, "\n%s\n", (char *) choices[oid].desc);

	/* XXX */
	text_out_pad = 0;
	text_out_indent = 0;
	text_out_wrap = 0;
}

/**
 * Display list available specialties.
 */
void textui_view_ability_menu(struct player_ability *ability_list,
							  int num_abilities)
{
	struct menu menu;
	menu_iter menu_f = { view_ability_tag, 0, view_ability_display, 0, 0 };
	region loc = { 0, 0, 70, -99 };
	char buf[80];

	/* Save the screen and clear it */
	screen_save();

	/* Prompt choices */
	if (num_abilities > 0) {
		strnfmt(buf, sizeof(buf),
			"Origin and class abilities (%c-%c, ESC=exit): ",
			all_letters_nohjkl[0],
			all_letters_nohjkl[num_abilities - 1]);
	} else {
		my_strcpy(buf, "No origin or class abilities (ESC=exit).", sizeof(buf));
	}

	/* Set up the menu */
	menu_init(&menu, MN_SKIN_SCROLL, &menu_f);
	menu.header = buf;
	menu_setpriv(&menu, num_abilities, ability_list);
	loc.page_rows = num_abilities + 1;
	menu.flags = MN_DBL_TAP;
	menu.browse_hook = view_ability_menu_browser;
	menu.present_hook = present_ability_menu;
	region_erase_bordered(&loc);
	menu_layout(&menu, &loc);

	menu_select(&menu, 0, false);
	if (Term->screen_hook) Term->screen_hook(NULL);

	/* Load screen */
	screen_load();

	return;
}
