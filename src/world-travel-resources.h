/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-travel-resources.h
 * \brief Bounded player-resource effects for aggregate route travel.
 *
 *
 */

#ifndef WORLD_TRAVEL_RESOURCES_H
#define WORLD_TRAVEL_RESOURCES_H

#include <stdbool.h>
#include <stdint.h>

struct object;
struct player;

enum world_travel_resources_result {
	WORLD_TRAVEL_RESOURCES_OK = 0,
	WORLD_TRAVEL_RESOURCES_INVALID,
	WORLD_TRAVEL_RESOURCES_ALREADY_STAGED,
	WORLD_TRAVEL_RESOURCES_UNSAFE_HUNGER,
	WORLD_TRAVEL_RESOURCES_UNSAFE_LIGHT
};

/** A reversible projection of the first approved aggregate travel effects. */
struct world_travel_resources {
	bool staged;
	int32_t departure_turn;
	int32_t arrival_turn;
	int source_depth;
	int food_before;
	int food_after;
	struct object *light;
	int light_before;
	int light_after;
};

enum world_travel_resources_result world_travel_resources_stage(
		struct world_travel_resources *resources, struct player *p,
		int32_t departure_turn, int32_t arrival_turn);
bool world_travel_resources_match(
		const struct world_travel_resources *resources, struct player *p);
void world_travel_resources_commit(
		const struct world_travel_resources *resources, struct player *p);

#endif /* !WORLD_TRAVEL_RESOURCES_H */
