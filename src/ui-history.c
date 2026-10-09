/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-history.c
 * \brief Character auto-history display UI
 *
 * Copyright (c) 2007 J.D. White
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
#include "player-history.h"
#include "ui-history.h"
#include "ui-history-screen.h"
#include "ui-input.h"
#include "ui-output.h"

/**
 * Print the header for the history display
 */
static void print_history_header(void)
{
	c_put_str(COLOUR_WHITE, "[Player history]", 0, 0);
	c_put_str(COLOUR_L_BLUE, "      Turn   Depth  Note", 1, 0);
}


/**
 * Handles all of the display functionality for the history list.
 */
void history_display(void)
{
	struct history_info *history_list_local = NULL;
	size_t max_item = history_get_list(player, &history_list_local);
	int row, wid, hgt, page_size;
	char buf[PLAYER_NAME_LEN + 106];
	static size_t first_item = 0;
	size_t i;
	bool active = true;

	Term_get_size(&wid, &hgt);

	screen_save();

	while (active)
	{
		struct keypress ch;

		Term_get_size(&wid, &hgt);
		page_size = Term->screen_hook ? MAX(1, hgt - 10) : MAX(1, hgt - 5);
		if (max_item > 0) {
			first_item = MIN(first_item, max_item - 1);
		} else {
			first_item = 0;
		}
		if (Term->screen_hook) {
			ui_history_screen_present_character(history_list_local, max_item,
				first_item, page_size, player);
		} else {
			Term_clear();

			/* Print everything to screen */
			print_history_header();

			row = 0;
			for (i = first_item; row < page_size && i < max_item; i++)
			{
				strnfmt(buf, sizeof(buf), "%10ld%7d\'  %s",
					(long)history_list_local[i].turn,
					history_list_local[i].dlev * 50,
					(!hist_has(history_list_local[i].type,
					HIST_USER_INPUT))
					? history_list_local[i].event
					: history_expand_user_input(
					history_list_local[i].event, player, NULL, 0,
					true));

				if (hist_has(history_list_local[i].type, HIST_ARTIFACT_LOST))
					my_strcat(buf, " (LOST)", sizeof(buf));

				/* Size of header = 3 lines */
				prt(buf, row + 2, 0);
				row++;
			}
			prt("[Arrow keys scroll, p/PgUp for previous page, n/PgDn for next page, ESC to exit.]", hgt - 1, 0);
		}

		ch = inkey();

		switch (ch.code) {
			case 'n':
			case ' ':
			case KC_PGDOWN: {
				size_t scroll_to = first_item + page_size;
				if (scroll_to < max_item) first_item = scroll_to;
				break;
			}

			case 'p':
			case KC_PGUP: {
				first_item = first_item >= (size_t)page_size ?
					first_item - (size_t)page_size : 0;
				break;
			}

			case 'j':
			case ARROW_DOWN: {
				size_t scroll_to = first_item + 1;
				if (scroll_to < max_item) first_item = scroll_to;
				break;
			}

			case 'k':
			case ARROW_UP: {
				if (first_item > 0) first_item--;
				break;
			}

			case ESCAPE:
				active = false;
				break;
		}
	}

	if (Term->screen_hook) Term->screen_hook(NULL);
	screen_load();

	return;
}


/**
 * Dump character history to a file, which we assume is already open.
 */
void dump_history(ang_file *file)
{
	struct history_info *history_list_local = NULL;
	size_t max_item = history_get_list(player, &history_list_local);
	size_t i;
	char buf[PLAYER_NAME_LEN + 106];

	file_putf(file, "[Player history]\n");
	file_putf(file, "      Turn   Depth  Note\n");

	for (i = 0; i < max_item; i++) {
		strnfmt(buf, sizeof(buf), "%10ld%7d\'  %s",
			(long)history_list_local[i].turn,
			history_list_local[i].dlev * 50,
			(!hist_has(history_list_local[i].type, HIST_USER_INPUT))
			? history_list_local[i].event
			: history_expand_user_input(history_list_local[i].event,
			player, NULL, 0, true));

		if (hist_has(history_list_local[i].type, HIST_ARTIFACT_LOST))
			my_strcat(buf, " (LOST)", sizeof(buf));

		file_putf(file, "%s", buf);
		file_put(file, "\n");
	}

	return;
}
