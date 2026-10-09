/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-history-screen.h
 * \brief Semantic presentation adapters for recorded histories.
 */

#ifndef INCLUDED_UI_HISTORY_SCREEN_H
#define INCLUDED_UI_HISTORY_SCREEN_H

#include "h-basic.h"

struct history_info;
struct player;

void ui_history_screen_present_messages(int first, int horizontal_offset,
		const char *search, int total, int page_size);
void ui_history_screen_present_character(const struct history_info *entries,
		size_t total, size_t first, int page_size,
		const struct player *history_player);

#endif /* INCLUDED_UI_HISTORY_SCREEN_H */
