/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking-appearance.h
 * \brief Shared ASCII appearance adapter for side-view snapshots.
 *
 *
 */

#ifndef UI_SPELUNKING_APPEARANCE_H
#define UI_SPELUNKING_APPEARANCE_H

#include "world-spelunking-view.h"

#include <wchar.h>

/** Resolve one snapshot cell without consulting simulation state or IDs. */
void spelunking_view_cell_appearance(
		const struct world_spelunk_view *view, int col, int row,
		int *attr, wchar_t *glyph);

#endif /* !UI_SPELUNKING_APPEARANCE_H */
