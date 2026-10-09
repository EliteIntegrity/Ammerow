/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-route.c
 * \brief Certified semantic rail geometry for generated caves.
 */

#include "world-spelunking-generation-route.h"

#include "z-util.h"
#include "z-virt.h"

static size_t cell_index(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	return (size_t)y * generated->width + x;
}

static void carve_vertical(struct world_spelunk_generated_layout *generated,
		int x, int y1, int y2)
{
	int top = MAX(1, MIN(y1, y2));
	int bottom = MIN(generated->height - 2, MAX(y1, y2));
	int y;

	for (y = top; y <= bottom; y++) {
		generated->cells[cell_index(generated, x, y)] =
			WORLD_SPELUNK_AIR;
	}
}

void world_spelunk_generated_route_carve_supported(
		struct world_spelunk_generated_layout *generated,
		int x1, int x2, int y)
{
	int left = MAX(1, MIN(x1, x2));
	int right = MIN(generated->width - 2, MAX(x1, x2));
	int x;

	if (y <= 1 || y >= generated->height - 2) return;
	for (x = left; x <= right; x++) {
		generated->cells[cell_index(generated, x, y - 1)] =
			WORLD_SPELUNK_AIR;
		generated->cells[cell_index(generated, x, y)] =
			WORLD_SPELUNK_AIR;
		generated->cells[cell_index(generated, x, y + 1)] =
			WORLD_SPELUNK_ROCK;
	}
}

int world_spelunk_generated_route_record_station(
		struct world_spelunk_generated_layout *generated, int x, int y)
{
	struct world_spelunk_generated_route_station *station;
	int index;

	if (generated->route_station_count >=
			WORLD_SPELUNK_GENERATED_ROUTE_STATION_MAX || x < 3 ||
			x >= generated->width - 3 || y <= 1 ||
			y >= generated->height - 2) {
		return -1;
	}
	index = generated->route_station_count++;
	station = &generated->route_stations[index];
	station->x = x;
	station->y = y;
	station->checkpoint_mask = 0;
	return index;
}

int world_spelunk_generated_route_add_station(
		struct world_spelunk_generated_layout *generated, int x, int y)
{
	world_spelunk_generated_route_carve_supported(generated,
		x - 2, x + 2, y);
	return world_spelunk_generated_route_record_station(generated, x, y);
}

bool world_spelunk_generated_route_connect(
		struct world_spelunk_generated_layout *generated, int from_index,
		int to_index, enum world_spelunk_route_side_preference preference,
		bool uses_rope)
{
	const struct world_spelunk_generated_route_station *from;
	const struct world_spelunk_generated_route_station *to;
	struct world_spelunk_generated_route_segment *segment;
	int shaft_x;
	int left_shaft_x;
	int right_shaft_x;
	int route_side;
	int far_wall_x;
	int y;

	if (from_index < 0 || to_index <= from_index ||
			to_index >= generated->route_station_count ||
			generated->route_segment_count >=
				WORLD_SPELUNK_GENERATED_ROUTE_SEGMENT_MAX) {
		return false;
	}
	from = &generated->route_stations[from_index];
	to = &generated->route_stations[to_index];
	if (to->y <= from->y) return false;
	left_shaft_x = MIN(from->x, to->x) - 3;
	right_shaft_x = MAX(from->x, to->x) + 3;
	/* Alternate sides at shared stations.  This keeps the retaining wall for
	 * a departing shaft from cutting across the arrival ledge. */
	if (preference == WORLD_SPELUNK_ROUTE_LEFT) {
		shaft_x = left_shaft_x >= 2 ? left_shaft_x : right_shaft_x;
	} else if (preference == WORLD_SPELUNK_ROUTE_RIGHT) {
		shaft_x = right_shaft_x < generated->width - 2 ?
			right_shaft_x : left_shaft_x;
	} else if ((generated->route_segment_count % 2) == 0) {
		shaft_x = left_shaft_x >= 2 ? left_shaft_x : right_shaft_x;
	} else {
		shaft_x = right_shaft_x < generated->width - 2 ?
			right_shaft_x : left_shaft_x;
	}
	if (generated->route_segment_count > 0) {
		const struct world_spelunk_generated_route_segment *incoming =
			&generated->route_segments[generated->route_segment_count - 1];

		/* If a boundary forces consecutive shafts onto the same side, put the
		 * departure beyond the arrival so its retaining wall cannot plug the
		 * old mouth or landing. */
		if (incoming->to_station == from_index) {
			if (shaft_x < from->x && incoming->shaft_x < from->x) {
				shaft_x = MIN(shaft_x, incoming->shaft_x - 2);
			} else if (shaft_x > from->x &&
					incoming->shaft_x > from->x) {
				shaft_x = MAX(shaft_x, incoming->shaft_x + 2);
			}
		}
	}
	if (shaft_x < 2 || shaft_x >= generated->width - 2) return false;
	route_side = shaft_x < from->x && shaft_x < to->x ? 1 : -1;
	if ((route_side > 0 && (from->x <= shaft_x || to->x <= shaft_x)) ||
			(route_side < 0 &&
			(from->x >= shaft_x || to->x >= shaft_x))) {
		return false;
	}
	world_spelunk_generated_route_carve_supported(generated, from->x,
		shaft_x + route_side, from->y);
	world_spelunk_generated_route_carve_supported(generated,
		shaft_x + route_side, to->x, to->y);
	carve_vertical(generated, shaft_x, from->y, to->y);
	/* Preserve a real climbable tube even where decorative chambers cross the
	 * certified rail.  Only the route-side openings at each end remain open. */
	for (y = from->y + 1; y < to->y; y++) {
		generated->cells[cell_index(generated, shaft_x - 1, y)] =
			WORLD_SPELUNK_ROCK;
		generated->cells[cell_index(generated, shaft_x + 1, y)] =
			WORLD_SPELUNK_ROCK;
	}
	far_wall_x = shaft_x - route_side;
	generated->cells[cell_index(generated, far_wall_x, from->y)] =
		WORLD_SPELUNK_ROCK;
	generated->cells[cell_index(generated, far_wall_x, to->y)] =
		WORLD_SPELUNK_ROCK;
	generated->cells[cell_index(generated, shaft_x, to->y + 1)] =
		WORLD_SPELUNK_ROCK;
	segment = &generated->route_segments[generated->route_segment_count++];
	segment->from_station = (uint16_t)from_index;
	segment->to_station = (uint16_t)to_index;
	segment->shaft_x = shaft_x;
	segment->uses_rope = uses_rope;
	return true;
}

int world_spelunk_generated_route_maximum_grip_span(
		const struct world_spelunk_generated_layout *generated)
{
	/* Ascending is the limiting free-climb direction. */
	int cost = MAX(1, generated->rules.grip_up_stamina_cost);

	/* Entering the shaft accounts for the first stamina charge; landing on its
	 * supported floor accounts for no extra unsupported step.  Leave at least
	 * one stamina point so zero cannot trigger the exhaustion fall. */
	return (generated->initial_stamina - 1) / cost;
}

void world_spelunk_generated_route_seal_disconnected(
		struct world_spelunk_generated_layout *generated)
{
	uint8_t *visited;
	uint32_t *queue;
	size_t cell_count;
	size_t head = 0;
	size_t tail = 0;
	size_t i;

	if (!generated || !generated->cells || generated->entrance_x <= 0 ||
			generated->entrance_y <= 0) {
		return;
	}
	cell_count = (size_t)generated->width * generated->height;
	visited = mem_zalloc(cell_count);
	queue = mem_alloc(cell_count * sizeof(*queue));
	i = cell_index(generated, generated->entrance_x,
		generated->entrance_y);
	if (generated->cells[i] != WORLD_SPELUNK_ROCK) {
		visited[i] = 1;
		queue[tail++] = (uint32_t)i;
	}
	while (head < tail) {
		uint32_t index = queue[head++];
		int x = (int)(index % generated->width);
		int y = (int)(index / generated->width);
		static const int dx[4] = { -1, 1, 0, 0 };
		static const int dy[4] = { 0, 0, -1, 1 };
		int direction;

		for (direction = 0; direction < 4; direction++) {
			int next_x = x + dx[direction];
			int next_y = y + dy[direction];
			size_t next;

			if (next_x <= 0 || next_x >= generated->width - 1 ||
					next_y <= 0 || next_y >= generated->height - 1) {
				continue;
			}
			next = cell_index(generated, next_x, next_y);
			if (!visited[next] && generated->cells[next] !=
					WORLD_SPELUNK_ROCK) {
				visited[next] = 1;
				queue[tail++] = (uint32_t)next;
			}
		}
	}
	for (i = 0; i < cell_count; i++) {
		if (!visited[i] && generated->cells[i] != WORLD_SPELUNK_ROCK) {
			generated->cells[i] = WORLD_SPELUNK_ROCK;
		}
	}
	mem_free(queue);
	mem_free(visited);
}
