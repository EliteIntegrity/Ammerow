/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-visibility.h
 * \brief Knowledge-safe visibility for side-view spelunking.
 *
 *
 */

#ifndef WORLD_SPELUNKING_VISIBILITY_H
#define WORLD_SPELUNKING_VISIBILITY_H

#include <stdbool.h>

struct world_spelunk_runtime;

enum world_spelunk_peek_action {
	WORLD_SPELUNK_PEEK_BEGIN = 0,
	WORLD_SPELUNK_PEEK_MOVE,
	WORLD_SPELUNK_PEEK_END,
	WORLD_SPELUNK_PEEK_TOGGLE
};

bool world_spelunk_visibility_is_valid(
		const struct world_spelunk_runtime *runtime);
void world_spelunk_visibility_follow_player(
		struct world_spelunk_runtime *runtime);
/** Reveal the authored layout without granting direct sight to occupants. */
void world_spelunk_visibility_reveal_layout(
		struct world_spelunk_runtime *runtime);
void world_spelunk_visibility_reveal_area(
		struct world_spelunk_runtime *runtime, int vertical_reach,
		int horizontal_reach);
/** Mark an occupant cell as sensed until the player changes position. */
bool world_spelunk_visibility_detect_cell(
		struct world_spelunk_runtime *runtime, int x, int y);
bool world_spelunk_visibility_is_detected(
		const struct world_spelunk_runtime *runtime, int x, int y);
bool world_spelunk_visibility_begin_peek(
		struct world_spelunk_runtime *runtime);
bool world_spelunk_visibility_move_peek(
		struct world_spelunk_runtime *runtime, int dx, int dy);
void world_spelunk_visibility_end_peek(
		struct world_spelunk_runtime *runtime);
bool world_spelunk_visibility_is_visible(
		const struct world_spelunk_runtime *runtime, int x, int y);
/** A rock face bordering directly visible open terrain. Never reveals
 * subjects. */
bool world_spelunk_visibility_is_surface_visible(
		const struct world_spelunk_runtime *runtime, int x, int y);
/** Direct sight or the derived one-cell surface skin used for terrain. */
bool world_spelunk_visibility_is_terrain_visible(
		const struct world_spelunk_runtime *runtime, int x, int y);
/** Whether rock leaves an unobstructed line between two side-view cells. */
bool world_spelunk_visibility_line_is_clear(
		const struct world_spelunk_runtime *runtime, int x0, int y0,
		int x1, int y1);
bool world_spelunk_visibility_is_explored(
		const struct world_spelunk_runtime *runtime, int x, int y);
bool world_spelunk_visibility_apply_peek(
		struct world_spelunk_runtime *runtime,
		enum world_spelunk_peek_action action, int dx, int dy);

#endif /* !WORLD_SPELUNKING_VISIBILITY_H */
