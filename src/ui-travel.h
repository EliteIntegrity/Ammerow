/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-travel.h
 * \brief Surface shortcut into the unified known-world map.
 */

#ifndef UI_TRAVEL_H
#define UI_TRAVEL_H

#include <stdbool.h>

struct chunk;
struct player;

bool textui_travel_uses_up_key_here(const struct player *p,
		struct chunk *c);
void textui_cmd_travel(void);

#endif /* !UI_TRAVEL_H */
