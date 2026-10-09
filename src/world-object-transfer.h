/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-object-transfer.h
 * \brief Transfer detached objects to the active location owner.
 *
 *
 */

#ifndef WORLD_OBJECT_TRANSFER_H
#define WORLD_OBJECT_TRANSFER_H

#include <stdbool.h>

struct chunk;
struct object;
struct player;

/** Whether the active location can own one more object at the player's feet. */
bool world_player_location_accepts_object(const struct player *p,
		struct chunk *native_chunk);

/**
 * Give a detached object to the active location at the player's feet.
 *
 * On success, ownership has transferred and *object is set to NULL.  On
 * failure, the caller retains the unchanged detached object.
 */
bool world_player_place_detached_object(struct player *p,
		struct chunk *native_chunk, struct object **object);

#endif /* !WORLD_OBJECT_TRANSFER_H */
