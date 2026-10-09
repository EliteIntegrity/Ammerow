/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-fishing.c
 * \brief Full-terminal fallback presentation for fishing.
 */

#include "angband.h"

#include "player.h"
#include "ui-fishing.h"
#include "ui-term.h"
#include "world-fishing.h"

bool (*textui_fishing_overlay_hook)(void) = NULL;
void (*textui_fishing_overlay_changed_hook)(void) = NULL;

static int display_col(const struct world_fishing_runtime *runtime, int col,
		int water_left, int water_right)
{
	if (runtime->columns <= 1) return water_left;
	return water_left + col * (water_right - water_left) /
		(runtime->columns - 1);
}

static int display_row(const struct world_fishing_runtime *runtime, int row,
		int water_top, int water_bottom)
{
	if (runtime->depth_rows <= 1) return water_top;
	return water_top + (row - 1) * (water_bottom - water_top) /
		(runtime->depth_rows - 1);
}

bool textui_fishing_present(const struct player *p)
{
	const struct world_fishing_runtime *runtime;
	const char *title = "FISHING RIG";
	const char *controls;
	char status[224];
	int width, height, water_left, water_right, water_top, water_bottom;
	int title_col, row, col, i, hook_col, hook_row;

	if (!Term || !p || !(runtime = p->fishing) || !runtime->active) return false;
	if (textui_fishing_overlay_hook && (*textui_fishing_overlay_hook)()) {
		return true;
	}
	Term_get_size(&width, &height);
	if (width < 40 || height < 20) return false;
	Term_clear();
	if (runtime->phase == WORLD_FISHING_BITE) {
		title = "BITE - STRIKE NOW";
	} else if (runtime->phase == WORLD_FISHING_WINDING) {
		title = "REELING IN";
	}
	title_col = MAX(0, (width - (int)strlen(title)) / 2);
	Term_putstr(title_col, 0, width - title_col, COLOUR_L_BLUE, title);
	strnfmt(status, sizeof(status),
		"Hook: %s  Depth %d/%d  Reach %d/%d  Water %s  Tackle %s",
		runtime->depth ? "wet" : "dry", runtime->depth,
		runtime->maximum_depth, runtime->rod_reach, runtime->maximum_reach,
		world_fishing_habitat_name(runtime->habitat), runtime->rig_name);
	Term_putstr(1, 2, width - 2, COLOUR_WHITE, status);

	water_left = 4;
	water_right = width - 6;
	water_top = 5;
	water_bottom = height - 4;
	for (row = water_top; row <= water_bottom; row++) {
		Term_erase(water_left, row, water_right - water_left + 1);
	}
	for (col = water_left; col <= water_right; col++) {
		Term_putch(col, water_top - 1, COLOUR_L_BLUE,
			((col - water_left) % 5) < 2 ? '~' : '-');
	}
	for (i = 1; i <= runtime->depth_rows; i++) {
		strnfmt(status, sizeof(status), "%2d", i);
		Term_putstr(1, display_row(runtime, i, water_top, water_bottom),
			2, COLOUR_SLATE, status);
	}
	for (row = water_top - 1; row <= water_bottom; row++) {
		Term_putch(water_right + 2, row, COLOUR_UMBER, '#');
	}
	Term_putch(width - 3, water_top - 2, COLOUR_L_BLUE, '@');
	for (i = 0; i < runtime->fish_count; i++) {
		const struct world_fishing_fish *fish = &runtime->fish[i];
		int attr = fish->attr;

		if (runtime->phase == WORLD_FISHING_WINDING &&
				i == runtime->hooked_index) {
			continue;
		}
		Term_putch(display_col(runtime, fish->col, water_left, water_right),
			display_row(runtime, fish->row, water_top, water_bottom),
			attr, fish->glyph ? fish->glyph :
			world_fishing_kind_glyph(fish->kind));
	}
	hook_col = display_col(runtime, world_fishing_hook_col(runtime),
		water_left, water_right);
	hook_row = runtime->depth ? display_row(runtime, runtime->depth,
		water_top, water_bottom) : water_top - 2;
	for (row = water_top - 2; row < hook_row; row++) {
		Term_putch(hook_col, row, COLOUR_L_WHITE, '|');
	}
	Term_putch(hook_col, hook_row,
		runtime->phase == WORLD_FISHING_BITE ? COLOUR_L_RED : COLOUR_YELLOW,
		runtime->phase == WORLD_FISHING_BITE ? '*' :
		runtime->phase == WORLD_FISHING_WINDING ? '0' : 'o');
	controls = runtime->phase == WORLD_FISHING_BITE ?
		"Enter strike   Space wait   Esc stop" :
		runtime->phase == WORLD_FISHING_WINDING ?
		"Up/KP8 reel   Esc stop" :
		"Left/Right reach   Up/Down depth   Space wait   Esc stop";
	Term_putstr(MAX(0, width - (int)strlen(controls) - 1), height - 2,
		width - 1, COLOUR_SLATE, controls);
	Term_set_cursor(false);
	return true;
}
