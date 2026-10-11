/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef UI_ADVANCEMENT_H
#define UI_ADVANCEMENT_H
#include "game-event.h"
void ui_advancement_present(int level, int cursor, const char *notice);
void ui_advancement_event(game_event_type type, game_event_data *data, void *user);
void do_cmd_level_rewards(void);
#endif
