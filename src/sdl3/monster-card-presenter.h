/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-card-presenter.h
 * \brief Lore-safe game-state adapter for SDL3 monster examine cards.
 */

#ifndef INCLUDED_SDL3_MONSTER_CARD_PRESENTER_H
#define INCLUDED_SDL3_MONSTER_CARD_PRESENTER_H

#include "sdl3/inspect-card.h"

struct sdl3_map_view;
struct term;

bool sdl3_monster_card_cursor_tracks(const struct sdl3_map_view *view,
		struct term *main_term, int cursor_col, int cursor_row);
void sdl3_monster_card_configure(struct sdl3_inspect_card *card,
		const struct sdl3_map_view *view, struct term *main_term,
		int cursor_col, int cursor_row, bool cursor_visible,
		bool settings_visible, bool big);

#endif /* INCLUDED_SDL3_MONSTER_CARD_PRESENTER_H */
