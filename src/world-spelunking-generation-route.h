/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-route.h
 * \brief Certified semantic rail geometry for generated caves.
 */

#ifndef WORLD_SPELUNKING_GENERATION_ROUTE_H
#define WORLD_SPELUNKING_GENERATION_ROUTE_H

#include "world-spelunking-generation.h"

#include <stdbool.h>

enum world_spelunk_route_side_preference {
	WORLD_SPELUNK_ROUTE_LEFT = -1,
	WORLD_SPELUNK_ROUTE_ALTERNATE = 0,
	WORLD_SPELUNK_ROUTE_RIGHT = 1
};

/** Add a supported station and return its route index, or -1. */
int world_spelunk_generated_route_add_station(
		struct world_spelunk_generated_layout *generated, int x, int y);

/** Record an already-supported station and return its route index, or -1. */
int world_spelunk_generated_route_record_station(
		struct world_spelunk_generated_layout *generated, int x, int y);

/** Join two ordered stations with a wall-braced descending shaft. */
bool world_spelunk_generated_route_connect(
		struct world_spelunk_generated_layout *generated, int from_index,
		int to_index, enum world_spelunk_route_side_preference preference,
		bool uses_rope);

/** Carve a stable two-cell-high landing over a rock floor. */
void world_spelunk_generated_route_carve_supported(
		struct world_spelunk_generated_layout *generated,
		int x1, int x2, int y);

/** Maximum unsupported downward span that cannot exhaust a fresh grip. */
int world_spelunk_generated_route_maximum_grip_span(
		const struct world_spelunk_generated_layout *generated);

/** Seal non-rock pockets that are no longer connected to the entrance. */
void world_spelunk_generated_route_seal_disconnected(
		struct world_spelunk_generated_layout *generated);

#endif /* !WORLD_SPELUNKING_GENERATION_ROUTE_H */
