/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef INCLUDED_UI_STORY_H
#define INCLUDED_UI_STORY_H

#include "game-event.h"

void ui_story_event(game_event_type type, game_event_data *data, void *user);
void ui_story_show(const char *title, const char *subtitle,
		const char *body, const char *footer, bool wait_for_ack);

#endif /* INCLUDED_UI_STORY_H */
