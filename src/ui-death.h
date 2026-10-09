/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-death.h
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

#ifndef UI_DEATH_H
#define UI_DEATH_H

#include "h-basic.h"

/** The small, deliberate set of ways to leave an ended run. */
enum death_screen_primary_action {
	DEATH_SCREEN_HOME = 0,
	DEATH_SCREEN_NEW_RUN,
	DEATH_SCREEN_QUIT,
	DEATH_SCREEN_PRIMARY_COUNT
};

/** Side-effect-free result used by the modal controller and its tests. */
enum death_screen_result {
	DEATH_SCREEN_STAY = 0,
	DEATH_SCREEN_RETURN_HOME,
	DEATH_SCREEN_START_NEW_RUN,
	DEATH_SCREEN_EXIT_GAME
};

enum death_screen_result death_screen_primary_result(
		enum death_screen_primary_action action, bool home_available);
void death_screen_recent_messages(char *buffer, size_t length,
		unsigned int maximum);
extern void death_screen(void);

#endif /* !UI_DEATH_H */
