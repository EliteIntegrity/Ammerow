/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-passage.h
 * \brief Reciprocal travel between materialized cave-system sections.
 */

#ifndef WORLD_SPELUNKING_PASSAGE_H
#define WORLD_SPELUNKING_PASSAGE_H

#include "world-spelunking-publication.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-system.h"

struct player;

enum world_spelunk_passage_result {
	WORLD_SPELUNK_PASSAGE_OK = 0,
	WORLD_SPELUNK_PASSAGE_INVALID,
	WORLD_SPELUNK_PASSAGE_NOT_HERE,
	WORLD_SPELUNK_PASSAGE_UNSTABLE,
	WORLD_SPELUNK_PASSAGE_DESTINATION_UNAVAILABLE,
	WORLD_SPELUNK_PASSAGE_DESTINATION_BLOCKED
};

enum world_spelunk_player_passage_result {
	WORLD_SPELUNK_PLAYER_PASSAGE_OK = 0,
	WORLD_SPELUNK_PLAYER_PASSAGE_INVALID,
	WORLD_SPELUNK_PLAYER_PASSAGE_NOT_HERE,
	WORLD_SPELUNK_PLAYER_PASSAGE_UNSTABLE,
	WORLD_SPELUNK_PLAYER_PASSAGE_GENERATION_FAILED,
	WORLD_SPELUNK_PLAYER_PASSAGE_BLOCKED
};

/** Exact diagnostics for one player-facing section transition. */
struct world_spelunk_player_passage_report {
	enum world_spelunk_player_passage_result result;
	enum world_spelunk_passage_result passage;
	enum world_spelunk_publication_result publication;
	bool first_visit;
};

/**
 * Traverse one endpoint from the active section to its reciprocal endpoint.
 * Both runtimes must already be materialized.  No player alias, energy or UI
 * state is changed by this system-level operation.
 */
enum world_spelunk_passage_result world_spelunk_system_traverse_passage(
		struct world_spelunk_system *system, const char *endpoint_id);

/** Enumerate materialized internal endpoints owned by one runtime. */
bool world_spelunk_system_passage_grid_at(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, size_t index,
		struct loc *grid, enum world_spelunk_passage_direction *direction);

/**
 * Use an up or down endpoint as one ordinary player action. An unvisited
 * destination is generated on a private stream and attached only as part of
 * this synchronous transaction. Failure leaves the graph and player intact.
 */
enum world_spelunk_player_passage_result world_spelunk_player_use_passage(
		struct player *p, enum world_spelunk_passage_direction direction,
		struct world_spelunk_player_passage_report *report);

#endif /* !WORLD_SPELUNKING_PASSAGE_H */
