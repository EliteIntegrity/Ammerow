/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef WORLD_SPELUNKING_REST_H
#define WORLD_SPELUNKING_REST_H

#include <stdbool.h>
struct player;

/** Read-only safety gate; hidden occupants do not grant free knowledge. */
const char *world_spelunk_rest_block_reason(const struct player *p);
/** Cave conditional rest includes stamina and held air, never scuba refill. */
bool world_spelunk_rest_is_complete(const struct player *p, int count);

#endif
