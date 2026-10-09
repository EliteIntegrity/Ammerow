/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-larder.h
 * \brief Bounded persistent progress for the village food reserve.
 *
 *
 */

#ifndef WORLD_LARDER_H
#define WORLD_LARDER_H

#include "h-basic.h"
#include "world-fishing.h"

#define WORLD_LARDER_MILESTONE_NONE 0U

struct world_larder_milestone_definition;

struct world_larder_state {
	uint32_t food_points;
	uint8_t milestone;
};

struct world_larder_report {
	uint32_t contributed_points;
	int32_t experience;
	const struct world_larder_milestone_definition *milestone_reached;
};

void world_larder_reset(struct world_larder_state *state);
bool world_larder_is_valid(const struct world_larder_state *state);
uint32_t world_larder_fish_value(world_fishing_kind kind);
bool world_larder_donate(struct world_larder_state *state,
		world_fishing_kind kind, int quantity,
		struct world_larder_report *report);

#endif /* !WORLD_LARDER_H */
