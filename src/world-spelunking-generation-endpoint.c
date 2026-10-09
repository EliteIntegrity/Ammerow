/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-endpoint.c
 * \brief Deterministic placement of graph endpoints in generated sections.
 */

#include "world-spelunking-generation-endpoint.h"

#include "world-location.h"
#include "world-spelunking-generation-witness.h"

#include <limits.h>
#include <stdlib.h>

static uint32_t hash_text(const char *text)
{
	uint32_t hash = 2166136261u;

	while (text && *text) {
		hash ^= (uint8_t)*text++;
		hash *= 16777619u;
	}
	return hash;
}

static size_t generated_index(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	return (size_t)y * generated->width + x;
}

static int generated_depth_percent(
		const struct world_spelunk_generated_layout *generated, int y)
{
	return y * 100 / (generated->height - 1);
}

static bool is_supported_air(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	return x > 0 && x < generated->width - 1 && y > 0 &&
		y < generated->height - 1 &&
		generated->cells[generated_index(generated, x, y)] ==
			WORLD_SPELUNK_AIR &&
		generated->cells[generated_index(generated, x, y + 1)] ==
			WORLD_SPELUNK_ROCK;
}

static bool is_route_spur(
		const struct world_spelunk_generated_layout *generated,
		uint16_t station_index, int x, int y)
{
	const struct world_spelunk_generated_route_station *station;
	int left;
	int right;
	int cursor;

	if (station_index >= generated->route_station_count) return false;
	station = &generated->route_stations[station_index];
	if (y != station->y) return false;
	left = MIN(x, station->x);
	right = MAX(x, station->x);
	for (cursor = left; cursor <= right; cursor++) {
		if (!is_supported_air(generated, cursor, y)) return false;
	}
	return true;
}

static bool is_landmark_position(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	int i;

	for (i = 0; i < generated->landmark_count; i++) {
		if (generated->landmarks[i].x == x &&
				generated->landmarks[i].y == y) {
			return true;
		}
	}
	return false;
}

static bool is_placement_position(
		const struct world_spelunk_endpoint_placement *placements,
		size_t count, int x, int y)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (placements[i].x == x && placements[i].y == y) return true;
	}
	return false;
}

static int separation_penalty(
		const struct world_spelunk_endpoint_placement *placements,
		size_t count, int x, int y)
{
	int penalty = 0;
	size_t i;

	for (i = 0; i < count; i++) {
		int distance = abs(placements[i].x - x) + abs(placements[i].y - y);

		if (distance < 5) penalty += (5 - distance) * 1000;
	}
	return penalty;
}

static bool select_endpoint_position(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_endpoint_request *request,
		struct world_spelunk_endpoint_placement *placements,
		size_t selected_count)
{
	struct world_spelunk_endpoint_placement *placement =
		&placements[selected_count];
	int target_depth = (request->minimum_depth_percent +
		request->maximum_depth_percent) / 2;
	int target_x = 2 + (int)(hash_text(request->id) %
		(uint32_t)(generated->width - 4));
	int best_score = INT_MAX;
	uint16_t station_index;

	placement->x = -1;
	placement->y = -1;
	placement->route_station = UINT16_MAX;
	for (station_index = 0;
			station_index < generated->route_station_count; station_index++) {
		const struct world_spelunk_generated_route_station *station =
			&generated->route_stations[station_index];
		int y = station->y;
		int depth = generated_depth_percent(generated, y);
		int x;

		if ((request->required_route_station >= 0 &&
				station_index != request->required_route_station) ||
				depth < request->minimum_depth_percent ||
				depth > request->maximum_depth_percent) {
			continue;
		}
		for (x = 2; x < generated->width - 2; x++) {
			int score;

			if (!is_route_spur(generated, station_index, x, y) ||
					is_landmark_position(generated, x, y) ||
					is_placement_position(placements, selected_count, x, y)) {
				continue;
			}
			score = abs(depth - target_depth) * 10000 +
				abs(x - target_x) * 10 +
				abs(x - station->x) +
				separation_penalty(placements, selected_count, x, y);
			if (generated->cells[generated_index(generated, x - 1, y)] ==
					WORLD_SPELUNK_AIR) score -= 30;
			if (generated->cells[generated_index(generated, x + 1, y)] ==
					WORLD_SPELUNK_AIR) score -= 30;
			if (generated->decoration_tags &&
					generated->decoration_tags[generated_index(generated, x, y)]) {
				score += 100;
			}
			if (score < best_score) {
				best_score = score;
				placement->x = x;
				placement->y = y;
				placement->route_station = station_index;
			}
		}
	}
	return best_score != INT_MAX;
}

bool world_spelunk_certify_endpoints(
		struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_endpoint_placement *placements, size_t count,
		const struct world_spelunk_traversal_profile *profile,
		struct world_spelunk_proof_report *report)
{
	uint16_t original_visit_count;
	size_t i;

	if (!generated || !placements || !count || !profile ||
			count > WORLD_SPELUNK_GENERATED_ROUTE_VISIT_MAX ||
			generated->route_visit_count >
				WORLD_SPELUNK_GENERATED_ROUTE_VISIT_MAX - count ||
			generated->landmark_count + generated->route_visit_count + count >
				32) {
		return false;
	}
	original_visit_count = generated->route_visit_count;
	for (i = 0; i < count; i++) {
		struct world_spelunk_generated_route_visit *visit;
		uint16_t bit = generated->landmark_count +
			generated->route_visit_count;
		size_t j;

		if (!is_route_spur(generated, placements[i].route_station,
				placements[i].x, placements[i].y)) {
			generated->route_visit_count = original_visit_count;
			return false;
		}
		for (j = 0; j < generated->route_visit_count; j++) {
			if (generated->route_visits[j].x == placements[i].x &&
					generated->route_visits[j].y == placements[i].y) {
				generated->route_visit_count = original_visit_count;
				return false;
			}
		}
		visit = &generated->route_visits[generated->route_visit_count++];
		visit->station = placements[i].route_station;
		visit->x = placements[i].x;
		visit->y = placements[i].y;
		visit->checkpoint_mask = 1u << bit;
	}
	if (!world_spelunk_generated_layout_rebuild_witness(generated, profile,
			report)) {
		generated->route_visit_count = original_visit_count;
		return false;
	}
	return true;
}

bool world_spelunk_place_endpoints(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_endpoint_request *requests, size_t count,
		struct world_spelunk_endpoint_placement *placements)
{
	size_t i;

	if (!generated || !generated->cells || !requests || !count ||
			!placements || generated->width <= 4 || generated->height <= 2) {
		return false;
	}
	for (i = 0; i < count; i++) {
		if (!world_id_is_valid(requests[i].id) ||
				requests[i].minimum_depth_percent >
					requests[i].maximum_depth_percent ||
				requests[i].maximum_depth_percent > 100 ||
				requests[i].required_route_station < -1 ||
				requests[i].required_route_station >=
					generated->route_station_count ||
				!select_endpoint_position(generated, &requests[i], placements,
					i)) {
			return false;
		}
	}
	return true;
}
