/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-breath.h
 * \brief Bind side-view breath rules to player HP and charged equipment.
 *
 *
 */

#ifndef WORLD_SPELUNKING_BREATH_H
#define WORLD_SPELUNKING_BREATH_H

#include "world-spelunking.h"

struct player;

enum world_spelunk_environment_result {
	WORLD_SPELUNK_ENVIRONMENT_INVALID = 0,
	WORLD_SPELUNK_ENVIRONMENT_SAFE,
	WORLD_SPELUNK_ENVIRONMENT_DAMAGED,
	WORLD_SPELUNK_ENVIRONMENT_DEAD
};

struct world_spelunk_environment_report {
	enum world_spelunk_breath_event event;
	int breath_before;
	int breath_after;
	int air_supply_before;
	int air_supply_after;
	int air_supply_capacity;
	int damage;
};

/** Return total immediately usable air and capacity, including held breath. */
bool world_spelunk_player_air_status(const struct player *p, int *remaining,
		int *capacity);

/** Resolve the environmental consequence of one already-committed turn. */
enum world_spelunk_environment_result
world_spelunk_player_process_environment_turn(struct player *p,
		struct world_spelunk_environment_report *report);

#endif /* !WORLD_SPELUNKING_BREATH_H */
