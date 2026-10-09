/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-score.c
 * \brief Highscore display for Ammerow
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
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
#include "buildid.h"
#include "game-world.h"
#include "score.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-score.h"
#include "ui-screen.h"
#include "ui-term.h"
#include "world-story-data.h"

/** Return a safe generic fallback if the story data has no outcome summary. */
static const char *victory_summary(void)
{
	const char *summary = world_story_victory_summary();

	return summary ? summary : "Completed the final expedition.";
}

/** Return the first non-space character in a score field. */
static const char *score_field(const char *field)
{
	while (*field && isspace((unsigned char)*field)) field++;
	return field;
}

/** Clean up the compact date retained in the ancestral score format. */
static const char *score_date(const char *field, char *storage, size_t len)
{
	const char *when = score_field(field);

	if (*when == '@' && strlen(when) == 9) {
		strnfmt(storage, len, "%.4s-%.2s-%.2s", when + 1, when + 5,
			when + 7);
		return storage;
	}
	return when;
}

/** Present one five-entry score page as a readable memorial. */
static void present_memorial_page(const struct high_score scores[], int start,
		int count, int highlight)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[5] = { 0 };
	char labels[5][160];
	char summaries[5][80];
	char details[5][80];
	char dates[5][16];
	const char *subtitle =
		"Scored expeditions. Retired, debug and otherwise unscored runs are not recorded.";
	int page_count = MIN(5, MAX(0, count - start));
	int i;

	if (!Term || !Term->screen_hook) return;
	if (page_count == 0) {
		page_count = 1;
		my_strcpy(labels[0], "No scored expeditions yet.", sizeof(labels[0]));
		my_strcpy(summaries[0], "A future run will be remembered here.",
			sizeof(summaries[0]));
		rows[0].label = labels[0];
		rows[0].prefix = summaries[0];
		rows[0].detail = "";
		rows[0].attr = COLOUR_L_DARK;
		rows[0].enabled = false;
	} else {
		for (i = 0; i < page_count; i++) {
			const struct high_score *score = &scores[start + i];
			struct player_class *c = player_id2class(atoi(score->p_c));
			struct player_race *r = player_id2race(atoi(score->p_r));
			int clev = atoi(score->cur_lev);
			int mlev = atoi(score->max_lev);
			int cdun = atoi(score->cur_dun);
			int mdun = atoi(score->max_dun);
			const char *when = score_date(score->day, dates[i],
				sizeof(dates[i]));

			strnfmt(labels[i], sizeof(labels[i]), "%d. %s - %s %s, level %d%s",
				start + i + 1, score->who,
				r ? r->name : "unknown", c ? c->name : "unknown", clev,
				mlev > clev ? format(" (highest %d)", mlev) : "");
			strnfmt(summaries[i], sizeof(summaries[i]),
				"Score %s   Gold %s   Turns %s   %s",
				score_field(score->pts), score_field(score->gold),
				score_field(score->turns), when);
			if (highscore_winner_how(score->how)) {
				my_strcpy(details[i], victory_summary(),
					sizeof(details[i]));
			} else {
				strnfmt(details[i], sizeof(details[i]),
					"Ended by %s at danger %d%s", score->how, cdun,
					mdun > cdun ? format("; deepest %d", mdun) : "");
			}
			rows[i].label = labels[i];
			rows[i].prefix = summaries[i];
			rows[i].detail = details[i];
			rows[i].attr = start + i == highlight ?
				COLOUR_L_GREEN : COLOUR_WHITE;
			rows[i].enabled = true;
		}
	}
	screen.kind = UI_SCREEN_MEMORIAL;
	screen.title = "Memorial";
	screen.subtitle = subtitle;
	screen.help =
		"Up shows the previous page   Enter, Down or Space advances   Escape returns";
	screen.content_col = 4;
	screen.content_row = 7;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = page_count;
	screen.cursor = highlight >= start && highlight < start + page_count ?
		highlight - start : -1;
	screen.row_count = page_count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}

/**
 * Display a page of scores
 */
static void display_score_page(const struct high_score scores[], int start,
							   int count, int highlight)
{
	int n;

	/* Dump 5 entries */
	for (n = 0; start < count && n < 5; start++, n++) {
		const struct high_score *score = &scores[start];
		uint8_t attr;
		int clev, mlev, cdun, mdun;
		const char *user, *gold, *when, *aged;
		struct player_class *c;
		struct player_race *r;
		char out_val[160];
		char tmp_val[160];

		/* Indicate death in yellow */
		attr = (start == highlight) ? COLOUR_L_GREEN : COLOUR_WHITE;

		c = player_id2class(atoi(score->p_c));
		r = player_id2race(atoi(score->p_r));

		/* Extract the level info */
		clev = atoi(score->cur_lev);
		mlev = atoi(score->max_lev);
		cdun = atoi(score->cur_dun);
		mdun = atoi(score->max_dun);

		/* Extract the gold and such */
		for (user = score->uid; isspace((unsigned char)*user); user++)
			/* loop */;
		for (when = score->day; isspace((unsigned char)*when); when++)
			/* loop */;
		for (gold = score->gold; isspace((unsigned char)*gold); gold++)
			/* loop */;
		for (aged = score->turns; isspace((unsigned char)*aged); aged++)
			/* loop */;

		/* Dump some info */
		strnfmt(out_val, sizeof(out_val),
				"%3d.%9s  %s the %s %s, level %d",
				start + 1, score->pts, score->who,
				r ? r->name : "<none>", c ? c->name : "<none>",
				clev);

		/* Append a "maximum level" */
		if (mlev > clev)
			my_strcat(out_val, format(" (Max %d)", mlev), sizeof(out_val));

		/* Dump the first line */
		c_put_str(attr, out_val, n * 4 + 2, 0);


		/* The score format has a danger depth but no stable world location. */
		if (highscore_winner_how(score->how)) {
			my_strcpy(out_val, victory_summary(), sizeof(out_val));
		} else {
			strnfmt(out_val, sizeof(out_val),
				"Ended by %s at recorded depth %d", score->how, cdun);
		}

		/* Append the deepest danger depth retained by the old format. */
		if (mdun > cdun)
			my_strcat(out_val, format(" (Deepest recorded %d)", mdun),
				sizeof(out_val));

		/* Dump the info */
		c_put_str(attr, out_val, n * 4 + 3, 15);


		/* Clean up standard encoded form of "when" */
		if ((*when == '@') && strlen(when) == 9) {
			strnfmt(tmp_val, sizeof(tmp_val), "%.4s-%.2s-%.2s", when + 1,
					when + 5, when + 7);
			when = tmp_val;
		}

		/* And still another line of info */
		strnfmt(out_val, sizeof(out_val),
				"(User %s, Date %s, Gold %s, Turn %s).",
				user, when, gold, aged);
		c_put_str(attr, out_val, n * 4 + 4, 15);
	}
}

/**
 * Display the scores in a given range.
 */
static void display_scores_aux(const struct high_score scores[], int from,
							   int to, int highlight, bool allow_scrolling)
{
	struct keypress ch;
	int k, count;

	/* Assume we will show the first 10 */
	if (from < 0) from = 0;
	if (to < 0) to = allow_scrolling ? 5 : 10;
	if (to > MAX_HISCORES) to = MAX_HISCORES;

	/* Count the high scores */
	for (count = 0; count < MAX_HISCORES; count++)
		if (!scores[count].what[0])
			break;

	/* Forget about the last entries */
	if ((count > to) && !allow_scrolling) count = to;

	/*
	 * Move 5 entries at a time.  Unless scrolling is allowed, only
	 * move forward and stop once the end is reached.
	 */
	k = from;
	while (1) {
		/* Clear screen */
		Term_clear();

		if (Term->screen_hook) {
			present_memorial_page(scores, k, count, highlight);
		} else {
			/* Title */
			if (k > 0) {
				put_str(format("%s Hall of Fame (from position %d)",
					VERSION_NAME, k + 1), 0, 21);
			} else {
				put_str(format("%s Hall of Fame", VERSION_NAME), 0, 30);
			}

			display_score_page(scores, k, count, highlight);

			/* Wait prompt centered on the traditional 80 character line. */
			if (allow_scrolling) {
				prt("[Press ESC to exit, up for prior page, any other key for next page.]", 23, 6);
			} else {
				prt("[Press ESC to exit, any other key to page forward till done.]", 23, 9);
			}
		}
		ch = inkey();
		if (!Term->screen_hook) prt("", 23, 0);

		if (ch.code == ESCAPE) {
			break;
		} else if (ch.code == ARROW_UP && allow_scrolling) {
			if (count <= 0) {
				k = 0;
			} else if (k == 0) {
				k = count - 5;
				while (k % 5) k++;
			} else if (k < 5) {
				k = 0;
			} else {
				k = k - 5;
			}
		} else {
			k += 5;
			if (k >= count) {
				if (allow_scrolling) {
					k = 0;
				} else {
					break;
				}
			}
		}
	}
	if (Term->screen_hook) Term->screen_hook(NULL);

	return;
}

/**
 * Predict the players location, and display it.
 */
void predict_score(bool allow_scrolling)
{
	int j;
	struct high_score the_score;
	struct high_score scores[MAX_HISCORES];


	/* Read scores, place current score */
	highscore_read(scores, N_ELEMENTS(scores));
	build_score(&the_score, player, "nobody (yet!)", NULL);

	if (player->is_dead)
		j = highscore_where(&the_score, scores, N_ELEMENTS(scores));
	else
		j = highscore_add(&the_score, scores, N_ELEMENTS(scores));

	/* Top fifteen scores if on the top ten, otherwise ten surrounding */
	if (j < 10) {
		display_scores_aux(scores, 0, 15, j, allow_scrolling);
	} else {
		display_scores_aux(scores, j - 2, j + 7, j, allow_scrolling);
	}
}


/**
 * Show scores.
 */
void show_scores(void)
{
	screen_save();

	/* Display the scores */
	if (character_generated) {
		predict_score(true);
	} else {
		/* Currently unused, but leaving in in case we re-implement looking
		 * at the scores without loading a character */
		struct high_score scores[MAX_HISCORES];
		highscore_read(scores, N_ELEMENTS(scores));
		display_scores_aux(scores, 0, MAX_HISCORES, -1, true);
	}

	screen_load();

	/* Hack - Flush it */
	Term_fresh();
}

/** Show only the recorded score ledger, without predicting a live run. */
void show_memorial(void)
{
	struct high_score scores[MAX_HISCORES];

	screen_save();
	highscore_read(scores, N_ELEMENTS(scores));
	display_scores_aux(scores, 0, MAX_HISCORES, -1, true);
	screen_load();
	Term_fresh();
}
