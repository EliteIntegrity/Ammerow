/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking.h
 * \brief Native terminal presentation for side-view spelunking.
 *
 *
 */

#ifndef UI_SPELUNKING_H
#define UI_SPELUNKING_H

#include <stdbool.h>
#include <stddef.h>

struct player;

/** Read-only status shared by the terminal fallback and graphical HUD. */
struct ui_spelunking_status {
	bool active;
	char location[160];
	char activity[32];
	char effort[96];
	int hp, max_hp;
	int stamina, max_stamina;
	int air, max_air;
	bool can_fish;
};

bool textui_spelunking_status(const struct player *p,
		struct ui_spelunking_status *status);

/** Draw the current side-view runtime into the active native terminal. */
bool textui_spelunking_present(const struct player *p, bool clear);

/** Run the no-energy side-view Look controller. */
void textui_spelunking_look(struct player *p);

/** Query the transient Look focus for semantic presenters. */
bool textui_spelunking_look_location(const struct player *p, int *x, int *y);

/** Map the semantic Look viewport back to authoritative terminal cells. */
bool textui_spelunking_look_view(const struct player *p, int *origin_col,
		int *origin_row, int *cols, int *rows, int *source_x, int *source_y);

/**
 * Describe one cell through the same knowledge boundary used by Look cards.
 * Exposed for non-terminal presenters and focused tests.
 */
bool textui_spelunking_describe(const struct player *p, int x, int y,
		char *description, size_t capacity);

#endif /* !UI_SPELUNKING_H */
