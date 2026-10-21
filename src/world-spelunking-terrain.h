/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef WORLD_SPELUNKING_TERRAIN_H
#define WORLD_SPELUNKING_TERRAIN_H

#include "world-spelunking-runtime.h"

struct player;

enum world_spelunk_cut_result {
	SPELUNK_CUT_OK = 0, SPELUNK_CUT_INVALID, SPELUNK_CUT_NOT_ROCK,
	SPELUNK_CUT_BOUNDARY, SPELUNK_CUT_WATER, SPELUNK_CUT_FIXTURE,
	SPELUNK_CUT_ANCHOR, SPELUNK_CUT_ROUTE, SPELUNK_CUT_OCCUPIED
};

/** Read-only preflight: no random draws, resource use, knowledge or map changes. */
enum world_spelunk_cut_result world_spelunk_cut_check(struct player *p, int x, int y,
	int stamina_cost);
/** Rechecks and commits one rock-to-air cut, settling support and shared HP. */
enum world_spelunk_cut_result world_spelunk_cut_rock(struct player *p, int x, int y);
const char *world_spelunk_cut_reason(enum world_spelunk_cut_result result);
int world_spelunk_dig_chance(const struct world_spelunk_runtime *r,
	int x, int y, int skill);
const char *world_spelunk_rock_name(const struct world_spelunk_runtime *r, int x, int y);

#endif
