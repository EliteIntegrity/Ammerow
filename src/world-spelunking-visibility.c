/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-visibility.c
 * \brief Knowledge-safe visibility for side-view spelunking.
 *
 *
 */

#include "world-spelunking-visibility.h"

#include "h-basic.h"
#include "world-spelunking-runtime.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool in_bounds(const struct world_spelunk_runtime *runtime, int x,
		int y)
{
	return runtime && x >= 0 && y >= 0 && x < runtime->state.map.width &&
		y < runtime->state.map.height;
}

static bool is_rock(const struct world_spelunk_runtime *runtime, int x, int y)
{
	return in_bounds(runtime, x, y) &&
		runtime->cells[y * runtime->state.map.width + x] == WORLD_SPELUNK_ROCK;
}

static bool is_directly_visible(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return in_bounds(runtime, x, y) && runtime->visible &&
		runtime->visible[(size_t)y * runtime->state.map.width + x] != 0;
}

bool world_spelunk_visibility_is_surface_visible(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	static const int offsets[4][2] = {
		{ -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
	};
	int i;

	if (!is_rock(runtime, x, y)) return false;
	for (i = 0; i < 4; i++) {
		int open_x = x + offsets[i][0];
		int open_y = y + offsets[i][1];

		if (is_directly_visible(runtime, open_x, open_y) &&
				!is_rock(runtime, open_x, open_y)) {
			return true;
		}
	}
	return false;
}

bool world_spelunk_visibility_is_terrain_visible(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return is_directly_visible(runtime, x, y) ||
		world_spelunk_visibility_is_surface_visible(runtime, x, y);
}

/* A blocking destination is visible; only intervening rock occludes it.
 * Supercover traversal is symmetric and rejects sight through the exact
 * corner between either of two blocking cells. */
bool world_spelunk_visibility_line_is_clear(
		const struct world_spelunk_runtime *runtime,
		int x0, int y0, int x1, int y1)
{
	int nx = abs(x1 - x0);
	int ny = abs(y1 - y0);
	int sx = x0 < x1 ? 1 : -1;
	int sy = y0 < y1 ? 1 : -1;
	int x = x0;
	int y = y0;
	int ix = 0;
	int iy = 0;

	if (!in_bounds(runtime, x0, y0) || !in_bounds(runtime, x1, y1)) {
		return false;
	}
	while (ix < nx || iy < ny) {
		int64_t across_x = (int64_t)(1 + 2 * ix) * ny;
		int64_t across_y = (int64_t)(1 + 2 * iy) * nx;

		if (across_x == across_y) {
			/* The ray crosses a grid corner.  Neither touching wall may be
			 * looked around, even when the diagonal destination is open. */
			if (is_rock(runtime, x + sx, y) ||
					is_rock(runtime, x, y + sy)) {
				return false;
			}
			x += sx;
			y += sy;
			ix++;
			iy++;
		} else if (across_x < across_y) {
			x += sx;
			ix++;
		} else {
			y += sy;
			iy++;
		}
		if (x == x1 && y == y1) return true;
		if (is_rock(runtime, x, y)) return false;
	}
	return true;
}

static bool normal_cell_is_allowed(
		const struct world_spelunk_perception *perception, int dx, int dy)
{
	int horizontal = perception->sight_horizontal_reach;
	int vertical = dy > 0 ? perception->sight_downward_reach :
		perception->sight_upward_reach;
	int64_t horizontal_squared;
	int64_t vertical_squared;

	/* Downward peripheral sight is deliberately a short box around the
	 * nearest lip.  An ellipse would collapse to zero width at maximum depth,
	 * making the two-row value useless to a supported player. */
	if (dy > 0) {
		return dy <= vertical && abs(dx) <= vertical;
	}
	if (abs(dx) > horizontal || abs(dy) > vertical) return false;
	if (!vertical) return dy == 0;
	horizontal_squared = (int64_t)horizontal * horizontal;
	vertical_squared = (int64_t)vertical * vertical;
	return (int64_t)dx * dx * vertical_squared +
		(int64_t)dy * dy * horizontal_squared <=
		horizontal_squared * vertical_squared;
}

static bool normal_line_is_clear(
		const struct world_spelunk_runtime *runtime, int origin_x,
		int origin_y, int x, int y)
{
	int dx = x - origin_x;

	if (world_spelunk_visibility_line_is_clear(runtime, origin_x, origin_y,
			x, y)) {
		return true;
	}
	/* Standing beside an open lip gives a small, natural view into the first
	 * part of a drop.  This alternate eye point is only used by the already
	 * shallow ordinary downward field; Peek supplies all long-range sight. */
	if (y > origin_y && dx != 0) {
		int lip_x = origin_x + (dx > 0 ? 1 : -1);

		return !is_rock(runtime, lip_x, origin_y) &&
			world_spelunk_visibility_line_is_clear(runtime, lip_x, origin_y,
				x, y);
	}
	return false;
}

static bool peek_cell_is_allowed(
		const struct world_spelunk_runtime *runtime,
		const struct world_spelunk_perception *perception, int origin_x,
		int origin_y, int x, int y)
{
	int direction_x = runtime->peek_x - runtime->state.x;
	int direction_y = runtime->peek_y - runtime->state.y;
	int dx = x - origin_x;
	int dy = y - origin_y;
	int reach;
	int64_t distance_squared = (int64_t)dx * dx + (int64_t)dy * dy;
	int64_t close_squared = (int64_t)perception->peek_close_reach *
		perception->peek_close_reach;
	int64_t dot;
	int64_t cross;

	if (!direction_x && !direction_y) {
		return normal_cell_is_allowed(perception, dx, dy);
	}
	if (distance_squared <= close_squared) return true;
	/* Downward diagonals mean leaning over an edge and looking into the
	 * gravity-defined shaft below that edge. */
	if (direction_y > 0) {
		direction_x = 0;
		direction_y = 1;
		reach = perception->downward_peek_reach;
	} else {
		reach = perception->peek_reach;
	}
	if (distance_squared > (int64_t)reach * reach) return false;
	dot = (int64_t)dx * direction_x + (int64_t)dy * direction_y;
	if (dot <= 0) return false;
	cross = llabs((int64_t)dx * direction_y -
		(int64_t)dy * direction_x);
	return cross * reach <= (int64_t)perception->peek_lateral_reach * dot;
}

static void compute_from(struct world_spelunk_runtime *runtime, int origin_x,
		int origin_y)
{
	const struct world_spelunk_perception *perception;
	int horizontal_reach;
	int vertical_reach;
	int min_x;
	int max_x;
	int min_y;
	int max_y;
	int x;
	int y;
	size_t count;

	if (!runtime || !runtime->visible || !runtime->explored ||
			!in_bounds(runtime, origin_x, origin_y)) {
		return;
	}
	perception = &runtime->perception;
	horizontal_reach = runtime->peeking ?
		MAX(perception->peek_reach, perception->downward_peek_reach) :
		perception->sight_horizontal_reach;
	vertical_reach = runtime->peeking ? horizontal_reach :
		MAX(perception->sight_upward_reach,
			perception->sight_downward_reach);
	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	memset(runtime->visible, 0, count * sizeof(*runtime->visible));
	min_x = MAX(0, origin_x - horizontal_reach);
	max_x = MIN(runtime->state.map.width - 1,
		origin_x + horizontal_reach);
	min_y = MAX(0, origin_y - vertical_reach);
	max_y = MIN(runtime->state.map.height - 1, origin_y + vertical_reach);
	for (y = min_y; y <= max_y; y++) {
		for (x = min_x; x <= max_x; x++) {
			int dx = x - origin_x;
			int dy = y - origin_y;
			size_t index;

			if (!(runtime->peeking ?
					peek_cell_is_allowed(runtime, perception, origin_x,
						origin_y, x, y) :
					normal_cell_is_allowed(perception, dx, dy)) ||
					!(runtime->peeking ?
					  world_spelunk_visibility_line_is_clear(runtime,
						origin_x, origin_y, x, y) :
					  normal_line_is_clear(runtime, origin_x, origin_y,
						x, y))) {
				continue;
			}
			index = (size_t)y * runtime->state.map.width + x;
			runtime->visible[index] = 1;
			runtime->explored[index] = 1;
		}
	}
	/* Cell-centre rays correctly protect subjects, but they cannot describe a
	 * continuous floor, ceiling, or shaft wall: a nearer face occludes every
	 * farther rock centre.  Remember exactly the first rock skin bordering
	 * directly visible open terrain.  This does not propagate through either
	 * rock or unseen air and therefore cannot recreate the old context leak. */
	for (y = 0; y < runtime->state.map.height; y++) {
		for (x = 0; x < runtime->state.map.width; x++) {
			if (world_spelunk_visibility_is_surface_visible(runtime, x, y)) {
				runtime->explored[(size_t)y * runtime->state.map.width + x] = 1;
			}
		}
	}
}

bool world_spelunk_visibility_is_valid(
		const struct world_spelunk_runtime *runtime)
{
	size_t count;
	size_t i;

	if (!runtime || !runtime->visible || !runtime->explored ||
			!runtime->detected ||
			runtime->state.map.width <= 0 || runtime->state.map.height <= 0) {
		return false;
	}
	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	for (i = 0; i < count; i++) {
		if (runtime->visible[i] > 1 || runtime->explored[i] > 1 ||
				runtime->detected[i] > 1 ||
				(runtime->visible[i] && !runtime->explored[i])) {
			return false;
		}
	}
	return !runtime->peeking ||
		(in_bounds(runtime, runtime->peek_x, runtime->peek_y) &&
		!is_rock(runtime, runtime->peek_x, runtime->peek_y));
}

void world_spelunk_visibility_follow_player(
		struct world_spelunk_runtime *runtime)
{
	size_t count;

	if (!runtime) return;
	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	if (runtime->detected) {
		memset(runtime->detected, 0,
			count * sizeof(*runtime->detected));
	}
	runtime->peeking = false;
	runtime->peek_x = runtime->state.x;
	runtime->peek_y = runtime->state.y;
	compute_from(runtime, runtime->state.x, runtime->state.y);
}

void world_spelunk_visibility_reveal_layout(
		struct world_spelunk_runtime *runtime)
{
	size_t count;

	if (!runtime || !runtime->explored) return;
	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	memset(runtime->explored, 1, count * sizeof(*runtime->explored));
}

void world_spelunk_visibility_reveal_area(
		struct world_spelunk_runtime *runtime, int vertical_reach,
		int horizontal_reach)
{
	int min_x;
	int max_x;
	int min_y;
	int max_y;
	int x;
	int y;

	if (!runtime || !runtime->explored || vertical_reach < 0 ||
			horizontal_reach < 0) {
		return;
	}
	min_x = MAX(0, runtime->state.x - horizontal_reach);
	max_x = MIN(runtime->state.map.width - 1,
		runtime->state.x + horizontal_reach);
	min_y = MAX(0, runtime->state.y - vertical_reach);
	max_y = MIN(runtime->state.map.height - 1,
		runtime->state.y + vertical_reach);
	for (y = min_y; y <= max_y; y++) {
		for (x = min_x; x <= max_x; x++) {
			runtime->explored[(size_t)y * runtime->state.map.width + x] = 1;
		}
	}
}

bool world_spelunk_visibility_detect_cell(
		struct world_spelunk_runtime *runtime, int x, int y)
{
	size_t index;

	if (!in_bounds(runtime, x, y) || !runtime->detected ||
			!runtime->explored) {
		return false;
	}
	index = (size_t)y * runtime->state.map.width + x;
	runtime->detected[index] = 1;
	runtime->explored[index] = 1;
	return true;
}

bool world_spelunk_visibility_is_detected(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return in_bounds(runtime, x, y) && runtime->detected &&
		runtime->detected[(size_t)y * runtime->state.map.width + x] != 0;
}

bool world_spelunk_visibility_begin_peek(
		struct world_spelunk_runtime *runtime)
{
	if (!runtime || !in_bounds(runtime, runtime->state.x, runtime->state.y)) {
		return false;
	}
	runtime->peeking = true;
	runtime->peek_x = runtime->state.x;
	runtime->peek_y = runtime->state.y;
	compute_from(runtime, runtime->peek_x, runtime->peek_y);
	return true;
}

bool world_spelunk_visibility_move_peek(
		struct world_spelunk_runtime *runtime, int dx, int dy)
{
	int x;
	int y;

	if (!runtime || !runtime->peeking || dx < -1 || dx > 1 || dy < -1 ||
			dy > 1 || (dx == 0 && dy == 0)) {
		return false;
	}
	x = runtime->state.x + dx;
	y = runtime->state.y + dy;
	if (!in_bounds(runtime, x, y) || is_rock(runtime, x, y)) return false;
	runtime->peek_x = x;
	runtime->peek_y = y;
	compute_from(runtime, x, y);
	return true;
}

void world_spelunk_visibility_end_peek(
		struct world_spelunk_runtime *runtime)
{
	world_spelunk_visibility_follow_player(runtime);
}

bool world_spelunk_visibility_is_visible(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return is_directly_visible(runtime, x, y);
}

bool world_spelunk_visibility_is_explored(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return in_bounds(runtime, x, y) && runtime->explored &&
		runtime->explored[(size_t)y * runtime->state.map.width + x] != 0;
}

bool world_spelunk_visibility_apply_peek(
		struct world_spelunk_runtime *runtime,
		enum world_spelunk_peek_action action, int dx, int dy)
{
	switch (action) {
	case WORLD_SPELUNK_PEEK_BEGIN:
		return dx == 0 && dy == 0 &&
			world_spelunk_visibility_begin_peek(runtime);
	case WORLD_SPELUNK_PEEK_MOVE:
		return world_spelunk_visibility_move_peek(runtime, dx, dy);
	case WORLD_SPELUNK_PEEK_END:
		if (dx != 0 || dy != 0 || !runtime) return false;
		world_spelunk_visibility_end_peek(runtime);
		return true;
	case WORLD_SPELUNK_PEEK_TOGGLE:
		if (dx != 0 || dy != 0 || !runtime) return false;
		if (runtime->peeking) {
			world_spelunk_visibility_end_peek(runtime);
			return true;
		}
		return world_spelunk_visibility_begin_peek(runtime);
	default:
		return false;
	}
}
