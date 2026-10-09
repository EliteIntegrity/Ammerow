/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-larder.c
 * \brief Bounded persistent progress for the village food reserve.
 *
 */

#include "world-larder.h"
#include "world-larder-data.h"

#include <string.h>

void world_larder_reset(struct world_larder_state *state)
{
	if (state) memset(state, 0, sizeof(*state));
}

bool world_larder_is_valid(const struct world_larder_state *state)
{
	uint8_t expected = WORLD_LARDER_MILESTONE_NONE;
	int i;

	if (!state || !world_larder_milestone_count()) return false;
	for (i = 1; i <= world_larder_milestone_count(); i++) {
		const struct world_larder_milestone_definition *definition =
			world_larder_milestone_by_save_index((uint8_t)i);

		if (!definition || state->food_points < definition->target) break;
		expected = definition->save_index;
	}
	return state->milestone == expected;
}

uint32_t world_larder_fish_value(world_fishing_kind kind)
{
	return world_fishing_kind_larder_value(kind);
}

bool world_larder_donate(struct world_larder_state *state,
		world_fishing_kind kind, int quantity,
		struct world_larder_report *report)
{
	uint32_t value = world_larder_fish_value(kind);
	uint32_t contribution;
	uint32_t remaining;

	if (report) memset(report, 0, sizeof(*report));
	if (!world_larder_is_valid(state) || !value || quantity <= 0) return false;
	if ((uint32_t)quantity > UINT32_MAX / value) {
		contribution = UINT32_MAX;
	} else {
		contribution = (uint32_t)quantity * value;
	}
	remaining = UINT32_MAX - state->food_points;
	state->food_points += MIN(contribution, remaining);
	if (report) {
		uint64_t experience = (uint64_t)quantity *
			world_fishing_kind_donation_experience(kind);

		report->contributed_points = MIN(contribution, remaining);
		/* The command consumes the fish before awarding this reward.  Keep
		 * the pure rule safe even for callers with unbounded quantities. */
		report->experience = (int32_t)MIN(experience, INT32_MAX);
	}
	while (state->milestone < world_larder_milestone_count()) {
		const struct world_larder_milestone_definition *next =
			world_larder_milestone_by_save_index(state->milestone + 1);

		if (!next || state->food_points < next->target) break;
		state->milestone = next->save_index;
		if (report) report->milestone_reached = next;
	}
	return true;
}
