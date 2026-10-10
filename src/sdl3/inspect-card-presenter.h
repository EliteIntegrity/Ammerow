/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/inspect-card-presenter.h
 * \brief Knowledge-safe subject selection for SDL3 examine cards.
 */

#ifndef INCLUDED_SDL3_INSPECT_CARD_PRESENTER_H
#define INCLUDED_SDL3_INSPECT_CARD_PRESENTER_H

#include "sdl3/inspect-card.h"

struct sdl3_map_view;
struct term;
struct world_spelunk_runtime;
struct world_spelunk_system;

void sdl3_inspect_card_configure(struct sdl3_inspect_card *card,
		const struct sdl3_map_view *view, struct term *main_term,
		int cursor_col, int cursor_row, bool cursor_visible,
		bool settings_visible, bool big);

/** Configure a card from a knowledge-safe side-view Look subject. */
void sdl3_inspect_card_configure_spelunking(
		struct sdl3_inspect_card *card,
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime,
		const struct sdl3_map_view *view, int x, int y,
		bool cursor_visible, bool settings_visible, bool big);

#endif /* INCLUDED_SDL3_INSPECT_CARD_PRESENTER_H */
