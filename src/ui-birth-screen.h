/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-birth-screen.h
 * \brief Semantic presentation adapter for character-creation choices.
 */

#ifndef INCLUDED_UI_BIRTH_SCREEN_H
#define INCLUDED_UI_BIRTH_SCREEN_H

#include "h-basic.h"

struct menu;
struct player_race;

/** Shared origin lore and mechanical summary; safe before character creation. */
void ui_birth_origin_context(const struct player_race *race,
		char *context, size_t capacity);

enum ui_birth_screen_stage {
	UI_BIRTH_SCREEN_RACE = 0,
	UI_BIRTH_SCREEN_CLASS,
	UI_BIRTH_SCREEN_ROLLER
};

void ui_birth_screen_present(struct menu *menu,
		enum ui_birth_screen_stage stage, const char *const *items,
		int item_count, const char *hint);
void ui_birth_screen_present_points(int selected, const int spent[],
		const int increase[], int remaining, const int buysell[]);
void ui_birth_screen_present_roll(bool previous_available);
void ui_birth_screen_present_name(const char *name, size_t cursor,
		bool first_time);
void ui_birth_screen_present_history(const char *history, size_t cursor,
		bool editing);
void ui_birth_screen_present_confirm(void);

#endif /* INCLUDED_UI_BIRTH_SCREEN_H */
