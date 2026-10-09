/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef WORLD_SPELUNKING_VIEW_TERRAIN_H
#define WORLD_SPELUNKING_VIEW_TERRAIN_H

#include "world-spelunking-view.h"

/** Knowledge-safe stable layers, independent of glyph/bitmap rendering. */
struct world_spelunk_view_terrain {
	const char *base_scope;
	const char *base;
	const char *overlay_scope;
	const char *overlay;
	bool preserve_glyph;
};

void world_spelunk_view_terrain_at(const struct world_spelunk_runtime *runtime,
		const struct world_spelunk_view *view, int col, int row,
		struct world_spelunk_view_terrain *result);

#endif
