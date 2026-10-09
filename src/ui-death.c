/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-death.c
 * \brief Handle the UI bits that happen after the character dies.
 *
 * Copyright (c) 1987 - 2007 Angband contributors
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
#include "cmds.h"
#include "game-input.h"
#include "game-world.h"
#include "init.h"
#include "obj-desc.h"
#include "obj-info.h"
#include "savefile.h"
#include "ui-death.h"
#include "ui-game.h"
#include "ui-history.h"
#include "ui-input.h"
#include "ui-knowledge.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-player.h"
#include "ui-score.h"
#include "ui-screen.h"
#include "ui-spoil.h"

/** Let a frontend-owned run summary yield to a nested terminal screen. */
static void death_screen_clear_semantic(void)
{
	if (Term && Term->screen_hook) Term->screen_hook(NULL);
}

/**
 * Write formatted string `fmt` on line `y`, centred between points x1 and x2.
 */
static void put_str_centred(int y, int x1, int x2, const char *fmt, ...)
{
	va_list vp;
	char *tmp;
	size_t len;
	int x;

	/* Format into the (growable) tmp */
	va_start(vp, fmt);
	tmp = vformat(fmt, vp);
	va_end(vp);

	/* Centre now; account for possible multibyte characters */
	len = utf8_strlen(tmp);
	x = x1 + ((x2-x1)/2 - len/2);

	put_str(tmp, y, x);
}

/** Describe the exit location without confusing danger depth with identity. */
static void exit_location(char *buf, size_t len, bool retired)
{
	const struct level *lev = world_player_level(player);
	const struct world_site *site = lev && lev->site_id ?
		world_site_by_id(lev->site_id) : NULL;
	const char *verb = retired ? "Journey ended" : "Fell";

	if (lev && site && site->floor_term && lev->local_floor > 0) {
		strnfmt(buf, len, "%s on %s %d", verb, site->floor_term,
			lev->local_floor);
	} else if (lev && lev->name && lev->name[0]) {
		strnfmt(buf, len, "%s at %-.20s", verb, lev->name);
	} else {
		strnfmt(buf, len, "%s at recorded depth %d", verb,
			player->depth);
	}
}


/**
 * Display the tombstone/retirement screen
 */
static void display_exit_screen(void)
{
	ang_file *fp;
	char buf[1024];
	int line = 0;
	time_t death_time = (time_t)0;
	bool completed = player->total_winner != 0;
	bool retired = completed || streq(player->died_from, "Retiring");

	Term_clear();
	(void)time(&death_time);

	/* Open the background picture */
	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS,
		(retired) ? "retire.txt" : "dead.txt");
	fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	if (fp) {
		while (file_getl(fp, buf, sizeof(buf)))
			put_str(buf, line++, 0);

		file_close(fp);
	}

	line = 7;

	put_str_centred(line++, 8, 8+31, "%s", player->full_name);
	put_str_centred(line++, 8, 8+31, "the");
	if (completed)
		put_str_centred(line++, 8, 8+31, "Watershed Keeper");
	else
		put_str_centred(line++, 8, 8+31, "%s",
			player->class->title[(player->lev - 1) / PY_TITLE_LEVELS]);

	line++;

	put_str_centred(line++, 8, 8+31, "%s", player->class->name);
	put_str_centred(line++, 8, 8+31, "Level: %d", (int)player->lev);
	put_str_centred(line++, 8, 8+31, "Exp: %d", (int)player->exp);
	put_str_centred(line++, 8, 8+31, "AU: %d", (int)player->au);
	if (completed) {
		put_str_centred(line++, 8, 8+31, "First sounding complete");
	} else {
		exit_location(buf, sizeof(buf), retired);
		put_str_centred(line++, 8, 8+31, "%s", buf);
		if (!retired) {
			put_str_centred(line++, 8, 8+31, "by %s.",
				player->died_from);
		}
	}

	line++;

	put_str_centred(line, 8, 8+31, "on %-.24s", ctime(&death_time));
}


/**
 * Display the winner crown
 */
static void display_winner(void)
{
	char buf[1024];
	ang_file *fp;

	int wid, hgt;
	int i = 2;

	path_build(buf, sizeof(buf), ANGBAND_DIR_SCREENS, "crown.txt");
	fp = file_open(buf, MODE_READ, FTYPE_TEXT);

	Term_clear();
	Term_get_size(&wid, &hgt);

	if (fp) {
		char *pe;
		long lw;
		int width;

		/* Get us the first line of file, which tells us how long the */
		/* longest line is */
		file_getl(fp, buf, sizeof(buf));
		lw = strtol(buf, &pe, 10);
		width = (pe != buf && lw > 0 && lw < INT_MAX) ? (int)lw : 25;

		/* Dump the file to the screen */
		while (file_getl(fp, buf, sizeof(buf))) {
			put_str(buf, i++, (wid / 2) - (width / 2));
		}

		file_close(fp);
	}

	put_str_centred(i, 0, wid, "The watershed remembers what you did.");

	event_signal(EVENT_INPUT_FLUSH);
	pause_line(Term);
}


/**
 * Menu command: dump character dump to file.
 */
static void death_file(const char *title, int row)
{
	char buf[1024];
	char ftmp[80];

	death_screen_clear_semantic();

	/* Get the filesystem-safe name and append .txt */
	player_safe_name(ftmp, sizeof(ftmp), player->full_name, false);
	my_strcat(ftmp, ".txt", sizeof(ftmp));

	if (get_file(ftmp, buf, sizeof buf)) {
		bool success;

		/* Dump a character file */
		screen_save();
		success = dump_save(buf);
		screen_load();

		/* Check result */
		if (success)
			msg("Character dump successful.");
		else
			msg("Character dump failed!");

		/* Flush messages */
		event_signal(EVENT_MESSAGE_FLUSH);
	}
}

/**
 * Menu command: view character dump and inventory.
 */
static void death_info(const char *title, int row)
{
	death_screen_clear_semantic();
	screen_save();
	display_player(0);
	prt("Escape or any key returns to the final record.", 0, 0);
	(void)anykey();
	screen_load();
}

/**
 * Menu command: peruse pre-death messages.
 */
static void death_messages(const char *title, int row)
{
	death_screen_clear_semantic();
	screen_save();
	do_cmd_messages();
	screen_load();
}

/**
 * Menu command: see top twenty scores.
 */
static void death_scores(const char *title, int row)
{
	death_screen_clear_semantic();
	screen_save();
	show_scores();
	screen_load();
}

/**
 * Menu command: examine items in the inventory.
 */
static void death_examine(const char *title, int row)
{
	struct object *obj;
	const char *q, *s;

	death_screen_clear_semantic();

	/* Get an item */
	q = "Examine which item? ";
	s = "You have nothing to examine.";

	while (get_item(&obj, q, s, 0, NULL, (USE_INVEN | USE_QUIVER | USE_EQUIP | IS_HARMLESS))) {
		char header[120];

		textblock *tb;
		region area = { 0, 0, 0, 0 };

		tb = object_info(obj, OINFO_NONE);
		object_desc(header, sizeof(header), obj,
			ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL, player);

		textui_textblock_show(tb, area, header);
		textblock_free(tb);
	}
}


/**
 * Menu command: view character history.
 */
static void death_history(const char *title, int row)
{
	death_screen_clear_semantic();
	history_display();
}

/**
 * Menu command: allow spoiler generation (mainly for randarts).
 */
static void death_spoilers(const char *title, int row)
{
	death_screen_clear_semantic();
	do_cmd_spoilers();
}

#define DEATH_SCREEN_ART_CAPACITY 2048
#define DEATH_SCREEN_ART_LINES 32
#define DEATH_SCREEN_RECENT_MESSAGES 5

/* The only choices that leave the final record have no menu callbacks.  They
 * must reach death_screen() as EVT_SELECT so the outer lifecycle changes once,
 * in one place. */
static menu_action death_actions[DEATH_SCREEN_PRIMARY_COUNT] = {
	[DEATH_SCREEN_HOME] = { 0, 'r', "Return to Home Screen", NULL },
	[DEATH_SCREEN_NEW_RUN] = { 0, 'n', "Begin a New Run", NULL },
	[DEATH_SCREEN_QUIT] = { 0, 'q', "Quit Ammerow", NULL }
};

enum death_screen_result death_screen_primary_result(
		enum death_screen_primary_action action, bool home_available)
{
	switch (action) {
	case DEATH_SCREEN_HOME:
		return home_available ? DEATH_SCREEN_RETURN_HOME : DEATH_SCREEN_STAY;
	case DEATH_SCREEN_NEW_RUN:
		return DEATH_SCREEN_START_NEW_RUN;
	case DEATH_SCREEN_QUIT:
		return DEATH_SCREEN_EXIT_GAME;
	default:
		return DEATH_SCREEN_STAY;
	}
}

void death_screen_recent_messages(char *buffer, size_t length,
		unsigned int maximum)
{
	unsigned int count;
	unsigned int i;
	size_t end = 0;

	if (!buffer || !length) return;
	buffer[0] = '\0';
	count = MIN(maximum, (unsigned int)messages_num());
	for (i = count; i > 0; i--) {
		uint16_t age = (uint16_t)(i - 1);
		uint16_t repeat = message_count(age);
		const char *text = message_str(age);

		if (!text || !text[0]) continue;
		if (repeat > 1) {
			strnfcat(buffer, length, &end, "  %-.112s <%ux>\n", text,
				(unsigned int)repeat);
		} else {
			strnfcat(buffer, length, &end, "  %-.120s\n", text);
		}
	}
}

/** Load the compact, data-owned art used by a native run-summary screen. */
static int load_exit_art(bool retired, wchar_t *text, uint8_t *attrs,
		size_t *starts, size_t *lengths, size_t *text_length)
{
	char path[1024];
	char line[160];
	ang_file *file;
	size_t used = 0;
	int line_count = 0;
	uint8_t attr = retired ? COLOUR_L_UMBER : COLOUR_SLATE;

	path_build(path, sizeof(path), ANGBAND_DIR_SCREENS,
		retired ? "retire-summary.txt" : "death-summary.txt");
	file = file_open(path, MODE_READ, FTYPE_TEXT);
	if (!file) {
		*text_length = 0;
		return 0;
	}
	while (line_count < DEATH_SCREEN_ART_LINES &&
			file_getl(file, line, sizeof(line))) {
		size_t source_length = strlen(line);
		size_t copy_length = MIN(source_length,
			(size_t)DEATH_SCREEN_ART_CAPACITY - used);
		size_t i;

		starts[line_count] = used;
		lengths[line_count] = copy_length;
		for (i = 0; i < copy_length; i++) {
			text[used] = (unsigned char)line[i];
			attrs[used] = attr;
			used++;
		}
		line_count++;
		if (used >= DEATH_SCREEN_ART_CAPACITY) break;
	}
	file_close(file);
	*text_length = used;
	return line_count;
}

/** Footer-only record tools always return to the same final summary. */
static bool death_record_key(struct menu *menu, const ui_event *event, int oid)
{
	(void)menu;
	(void)oid;
	if (!event || event->type != EVT_KBRD) return false;
	switch (event->key.code) {
	case 'i': case 'I': death_info("Character record", 0); return true;
	case 'm': case 'M': death_messages("Messages", 0); return true;
	case 'f': case 'F': death_file("File dump", 0); return true;
	case 'v': case 'V': death_scores("Scores", 0); return true;
	case 'x': case 'X': death_examine("Examine items", 0); return true;
	case 'h': case 'H': death_history("History", 0); return true;
	case 's': case 'S': death_spoilers("Spoilers", 0); return true;
	default: return false;
	}
}

/** Present the gravestone, final record, last moments, and primary actions. */
static void present_death_menu(struct menu *menu)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[DEATH_SCREEN_PRIMARY_COUNT] = { 0 };
	wchar_t art_text[DEATH_SCREEN_ART_CAPACITY] = { 0 };
	uint8_t art_attrs[DEATH_SCREEN_ART_CAPACITY] = { 0 };
	size_t art_starts[DEATH_SCREEN_ART_LINES] = { 0 };
	size_t art_lengths[DEATH_SCREEN_ART_LINES] = { 0 };
	size_t art_length = 0;
	char subtitle[192];
	char context[1024];
	char recent[640];
	char location[160];
	size_t context_end;
	bool completed;
	bool retired;
	int row_count = 0;
	int cursor = -1;
	int i;

	if (!Term || !Term->screen_hook || !menu || !player) return;
	completed = player->total_winner != 0;
	retired = completed || streq(player->died_from, "Retiring");
	exit_location(location, sizeof(location), retired);
	strnfmt(subtitle, sizeof(subtitle), "%s   %s %s   Level %d",
		player->full_name, player->race->name, player->class->name,
		player->lev);
	if (completed) {
		strnfmt(context, sizeof(context),
			"The first sounding is complete.\n%s.\n\n"
			"Experience  %ld\nGold  %ld\nDeepest danger  %d\nTurns  %ld",
			location, (long)player->exp, (long)player->au,
			player->max_depth, (long)turn);
	} else if (retired) {
		strnfmt(context, sizeof(context),
			"%s.\n\nExperience  %ld\nGold  %ld\n"
			"Deepest danger  %d\nTurns  %ld",
			location, (long)player->exp, (long)player->au,
			player->max_depth, (long)turn);
	} else {
		strnfmt(context, sizeof(context),
			"%s.\nCause: %s\n\nExperience  %ld\nGold  %ld\n"
			"Deepest danger  %d\nTurns  %ld",
			location, player->died_from, (long)player->exp,
			(long)player->au, player->max_depth, (long)turn);
	}
	death_screen_recent_messages(recent, sizeof(recent),
		DEATH_SCREEN_RECENT_MESSAGES);
	context_end = strlen(context);
	strnfcat(context, sizeof(context), &context_end, "\n\nLast moments\n%s",
		recent[0] ? recent : "  No messages were recorded.\n");
	for (i = 0; i < DEATH_SCREEN_PRIMARY_COUNT; i++) {
		if (death_actions[i].flags & MN_ACT_HIDDEN) continue;
		if (i == menu->cursor) cursor = row_count;
		rows[row_count].label = death_actions[i].name;
		rows[row_count].tag = death_actions[i].tag;
		rows[row_count].attr = i == DEATH_SCREEN_QUIT ?
			COLOUR_L_RED : COLOUR_WHITE;
		rows[row_count].enabled = true;
		row_count++;
	}
	screen.kind = UI_SCREEN_RUN_SUMMARY;
	screen.title = completed ? "The watershed remembers" :
		(retired ? "Run abandoned" : "A life in Ammerow ends");
	screen.subtitle = subtitle;
	screen.help =
		"Enter confirms | Esc returns Home | I character | M messages | X items | H history | F dump | V scores | S spoilers";
	screen.context_title = "Final record";
	screen.context = context;
	screen.content_col = 4;
	screen.content_row = 8;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = row_count;
	screen.cursor = cursor;
	screen.row_count = row_count;
	screen.rows = rows;
	screen.document_line_count = load_exit_art(retired, art_text, art_attrs,
		art_starts, art_lengths, &art_length);
	screen.document_length = art_length;
	screen.document_text = art_text;
	screen.document_attrs = art_attrs;
	screen.document_line_starts = art_starts;
	screen.document_line_lengths = art_lengths;
	screen.document_cols = 34;
	Term->screen_hook(&screen);
}

/** Apply one primary result at the sole lifecycle boundary. */
static void apply_death_result(enum death_screen_result result, bool *done)
{
	switch (result) {
	case DEATH_SCREEN_RETURN_HOME:
		if (textui_request_return_home()) *done = true;
		break;
	case DEATH_SCREEN_START_NEW_RUN:
		play_again = true;
		break;
	case DEATH_SCREEN_EXIT_GAME:
		*done = true;
		break;
	case DEATH_SCREEN_STAY:
	default:
		break;
	}
}

/** Handle death, victory, or deliberate abandonment. */
void death_screen(void)
{
	struct menu *death_menu;
	bool done = false;
	const region area = { 51, 2, 0, DEATH_SCREEN_PRIMARY_COUNT };

	if (player->total_winner && !Term->screen_hook) display_winner();
	if (Term->screen_hook) {
		Term_clear();
	} else {
		display_exit_screen();
	}
	event_signal(EVENT_INPUT_FLUSH);
	event_signal(EVENT_MESSAGE_FLUSH);
	death_actions[DEATH_SCREEN_HOME].flags = textui_startup_menu_hook ?
		0 : MN_ACT_HIDDEN;
	death_actions[DEATH_SCREEN_HOME].tag = textui_startup_menu_hook ? 'r' : 0;
	death_menu = menu_new_action(death_actions,
		DEATH_SCREEN_PRIMARY_COUNT);
	death_menu->flags = MN_CASELESS_TAGS;
	death_menu->cmd_keys = "IiMmFfVvXxHhSs";
	death_menu->keys_hook = death_record_key;
	death_menu->present_hook = present_death_menu;
	menu_layout(death_menu, &area);

	while (!done && !play_again) {
		ui_event event = menu_select(death_menu, EVT_KBRD, false);

		if (event.type == EVT_SELECT) {
			apply_death_result(death_screen_primary_result(
				(enum death_screen_primary_action)death_menu->cursor,
				textui_startup_menu_hook != NULL), &done);
		} else if (event.type == EVT_ESCAPE) {
			if (terms_disconnecting) break;
			apply_death_result(textui_startup_menu_hook ?
				DEATH_SCREEN_RETURN_HOME : DEATH_SCREEN_EXIT_GAME, &done);
		} else if (event.type == EVT_KBRD) {
			if (event.key.code == KTRL('X')) {
				done = true;
			} else if (event.key.code == KTRL('N')) {
				play_again = true;
			}
		}
	}

	death_screen_clear_semantic();
	menu_free(death_menu);
}
