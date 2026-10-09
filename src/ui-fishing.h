/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-fishing.h
 * \brief Native fallback presentation for the fishing activity.
 */

#ifndef UI_FISHING_H
#define UI_FISHING_H

#include <stdbool.h>

struct player;

/* A frontend returns true when it draws the fishing overlay itself. */
extern bool (*textui_fishing_overlay_hook)(void);
extern void (*textui_fishing_overlay_changed_hook)(void);

bool textui_fishing_present(const struct player *p);

#endif /* !UI_FISHING_H */
