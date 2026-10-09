/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-endpoint.h
 * \brief Deterministic placement of graph endpoints in generated sections.
 */

#ifndef WORLD_SPELUNKING_GENERATION_ENDPOINT_H
#define WORLD_SPELUNKING_GENERATION_ENDPOINT_H

#include "world-spelunking-generation.h"
#include "world-spelunking-traversal-data.h"
#include "world-spelunking-traversal-proof.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct world_spelunk_endpoint_request {
	const char *id;
	uint16_t minimum_depth_percent;
	uint16_t maximum_depth_percent;
	/** -1 selects any station in the band; otherwise require this station. */
	int required_route_station;
};

struct world_spelunk_endpoint_placement {
	int x;
	int y;
	uint16_t route_station;
};

/** Select unique coordinates on safe spurs of the certified route. */
bool world_spelunk_place_endpoints(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_endpoint_request *requests, size_t count,
		struct world_spelunk_endpoint_placement *placements);

/** Add endpoint visits and rebuild the exact witness before publication. */
bool world_spelunk_certify_endpoints(
		struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_endpoint_placement *placements, size_t count,
		const struct world_spelunk_traversal_profile *profile,
		struct world_spelunk_proof_report *report);

#endif /* !WORLD_SPELUNKING_GENERATION_ENDPOINT_H */
