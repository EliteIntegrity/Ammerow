/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-story.c
 * \brief Frontend-neutral presentation of data-authored story scenes.
 */

#include "angband.h"

#include "init.h"
#include "ui-input.h"
#include "ui-output.h"
#include "ui-screen.h"
#include "ui-story.h"
#include "world-story-data.h"

void ui_story_show(const char *title, const char *subtitle,
		const char *body, const char *footer, bool wait_for_ack)
{
	if (!title || !body || !Term) return;
	/* The headless runner records this event itself.  It has no interactive
	 * input loop with which to dismiss a modal story screen. */
	if (wait_for_ack && streq(ANGBAND_SYS, "headless")) return;
	if (Term->screen_hook) {
		struct ui_screen screen = { 0 };
		char context[4096];

		my_strcpy(context, body, sizeof(context));
		if (footer && footer[0]) {
			my_strcat(context, "\n\n", sizeof(context));
			my_strcat(context, footer, sizeof(context));
		}
		if (wait_for_ack) screen_save();
		screen.kind = UI_SCREEN_STORY;
		screen.title = title;
		screen.subtitle = subtitle ? subtitle : "";
		screen.help = "Enter / Space / Escape or click to continue";
		screen.context_title = "";
		screen.context = context;
		screen.content_col = 8;
		screen.content_row = 6;
		screen.content_cols = MAX(40, Term->wid - 16);
		screen.content_rows = MAX(1, Term->hgt - 12);
		screen.cursor = -1;
		Term->screen_hook(&screen);
		/* Read-only off-screen previews use exactly the interactive presenter. */
		if (!wait_for_ack) return;
		while (true) {
			ui_event event = inkey_m();
			if (event.type == EVT_DISCONNECT || event.type == EVT_ESCAPE ||
					(event.type == EVT_MOUSE && event.mouse.button == 1) ||
					(event.type == EVT_KBRD &&
					(event.key.code == KC_ENTER || event.key.code == ' ' ||
					event.key.code == ESCAPE))) break;
		}
		Term->screen_hook(NULL);
		screen_load();
	} else if (wait_for_ack) {
		textblock *tb = textblock_new();

		if (subtitle && subtitle[0]) {
			textblock_append_c(tb, COLOUR_L_BLUE, "%s\n\n", subtitle);
		}
		textblock_append(tb, "%s", body);
		if (footer && footer[0]) {
			textblock_append_c(tb, COLOUR_SLATE, "\n\n%s", footer);
		}
		(void)textui_textblock_show(tb, SCREEN_REGION, title);
		textblock_free(tb);
	}
}

void ui_story_event(game_event_type type, game_event_data *data, void *user)
{
	const struct world_story_scene *scene;
	(void)type;
	(void)user;
	if (!data || !data->story) return;
	scene = data->story;
	ui_story_show(scene->title, scene->subtitle, scene->body, scene->footer, true);
}
