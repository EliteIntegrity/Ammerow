/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-fishing-site.h
 * \brief Mode-owned access to the player's current fishing site.
 *
 */

#ifndef WORLD_FISHING_SITE_H
#define WORLD_FISHING_SITE_H

#include "fishing-data.h"
#include "z-type.h"

struct chunk;
struct object;
struct player;

/** Why the active mode cannot currently provide a fishing site. */
enum world_fishing_site_result {
	WORLD_FISHING_SITE_OK = 0,
	WORLD_FISHING_SITE_INVALID,
	WORLD_FISHING_SITE_NO_HABITAT,
	WORLD_FISHING_SITE_NO_WATER,
	WORLD_FISHING_SITE_UNSTABLE
};

/** Immutable information needed to begin or validate one fishing session. */
struct world_fishing_site {
	struct loc origin;
	enum world_fishing_habitat habitat;
	int danger;
	int action_energy;
};

/**
 * Ask the active world mode for a real fishing site.
 *
 * Top-down mode may inspect \p chunk.  Side-view mode ignores it and reads
 * only the player's authoritative spelunking runtime.
 */
enum world_fishing_site_result world_fishing_site_query(
		const struct player *p, struct chunk *chunk,
		struct world_fishing_site *site);

/** Whether the player remains at the exact site where the session began. */
bool world_fishing_site_matches(const struct player *p,
		struct chunk *chunk, const struct world_fishing_site *site);

/**
 * Place an uncarrried catch in storage owned by the active mode.
 *
 * On success, ownership transfers to that mode and \p *object becomes NULL.
 */
bool world_fishing_site_drop_catch(struct player *p, struct chunk *chunk,
		struct object **object);

#endif /* !WORLD_FISHING_SITE_H */
