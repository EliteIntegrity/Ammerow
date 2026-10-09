/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-witness.c
 * \brief Exact-action compiler for generator-authored traversal rails.
 */

#include "world-spelunking-generation-witness.h"

#include "world-spelunking-runtime.h"
#include "z-virt.h"

#include <stdlib.h>
#include <string.h>

struct witness_builder {
	struct world_spelunk_witness_step *steps;
	size_t count;
	struct world_spelunk_runtime *runtime;
	uint16_t pitons_used;
	uint16_t rope_segments_used;
};

static bool append_step(struct witness_builder *builder,
		enum world_spelunk_witness_step_kind kind, int x, int y)
{
	struct world_spelunk_witness_step *step;

	if (builder->count >= WORLD_SPELUNK_WITNESS_STEP_MAX) return false;
	step = &builder->steps[builder->count++];
	memset(step, 0, sizeof(*step));
	step->kind = kind;
	step->expected_x = x;
	step->expected_y = y;
	return true;
}

static bool append_command(struct witness_builder *builder,
		enum world_spelunk_command_kind kind, int dx, int dy, int x, int y)
{
	struct world_spelunk_command command;
	struct world_spelunk_action_report report;
	struct world_spelunk_witness_step *step;
	enum world_spelunk_action_outcome outcome;

	if (!builder->runtime || builder->count >=
			WORLD_SPELUNK_WITNESS_STEP_MAX) {
		return false;
	}
	command.kind = kind;
	command.dx = dx;
	command.dy = dy;
	outcome = world_spelunk_apply_command(&builder->runtime->state,
		&command, &report);
	if (outcome == WORLD_SPELUNK_ACTION_REJECTED || report.damage > 0 ||
			builder->runtime->state.x != x ||
			builder->runtime->state.y != y) {
		return false;
	}
	if (!append_step(builder, WORLD_SPELUNK_WITNESS_COMMAND, x, y)) {
		return false;
	}
	step = &builder->steps[builder->count - 1];
	step->command = command;
	return true;
}

static bool grip_supports_move(const struct witness_builder *builder,
		int next_x, int next_y)
{
	const struct world_spelunk_state *state = &builder->runtime->state;

	return world_spelunk_has_active_grip(state) &&
		abs(state->grip_target_x - next_x) <= 1 &&
		abs(state->grip_target_y - next_y) <= 1;
}

/** Record however many free grip-selection commands are needed for the next
 * exact move.  This is deliberately planned through the real grip cycle: the
 * compiler does not assume which adjacent wall the UI command will select. */
static bool append_grip_for_move(struct witness_builder *builder,
		int next_x, int next_y)
{
	int i;

	if (grip_supports_move(builder, next_x, next_y)) return true;
	for (i = 0; i < 8; i++) {
		int x = builder->runtime->state.x;
		int y = builder->runtime->state.y;

		if (!append_command(builder, WORLD_SPELUNK_COMMAND_GRIP, 0, 0,
				x, y)) {
			return false;
		}
		if (grip_supports_move(builder, next_x, next_y)) return true;
	}
	return false;
}

static bool append_checkpoint(struct witness_builder *builder,
		uint32_t checkpoint_mask)
{
	if (!checkpoint_mask) return true;
	if (!append_step(builder, WORLD_SPELUNK_WITNESS_CHECKPOINT,
			builder->runtime->state.x, builder->runtime->state.y)) {
		return false;
	}
	builder->steps[builder->count - 1].checkpoint_mask = checkpoint_mask;
	return true;
}

static bool append_rest(struct witness_builder *builder)
{
	while (builder->runtime->state.stamina <
			builder->runtime->state.max_stamina) {
		int stamina = builder->runtime->state.stamina;
		int x = builder->runtime->state.x;
		int y = builder->runtime->state.y;

		if (!append_command(builder, WORLD_SPELUNK_COMMAND_WAIT, 0, 0,
				x, y) || builder->runtime->state.stamina <= stamina) {
			return false;
		}
	}
	return true;
}

static bool append_walk_x(struct witness_builder *builder, int destination_x)
{
	while (builder->runtime->state.x != destination_x) {
		int dx = destination_x > builder->runtime->state.x ? 1 : -1;
		int x = builder->runtime->state.x;
		int y = builder->runtime->state.y;

		if (!append_command(builder, WORLD_SPELUNK_COMMAND_MOVE, dx, 0,
				x + dx, y)) {
			return false;
		}
	}
	return true;
}

static bool append_station_proof(struct witness_builder *builder,
		const struct world_spelunk_generated_layout *generated,
		uint16_t station_index)
{
	const struct world_spelunk_generated_route_station *station;
	uint16_t i;

	if (station_index >= generated->route_station_count) return false;
	station = &generated->route_stations[station_index];
	if (builder->runtime->state.x != station->x ||
			builder->runtime->state.y != station->y ||
			!append_checkpoint(builder, station->checkpoint_mask)) {
		return false;
	}
	for (i = 0; i < generated->route_visit_count; i++) {
		const struct world_spelunk_generated_route_visit *visit =
			&generated->route_visits[i];

		if (visit->station != station_index) continue;
		if (visit->y != station->y ||
				!append_walk_x(builder, visit->x) ||
				!append_checkpoint(builder, visit->checkpoint_mask) ||
				!append_walk_x(builder, station->x)) {
			return false;
		}
	}
	return append_rest(builder);
}

static bool append_piton(struct witness_builder *builder,
		const struct world_spelunk_traversal_profile *profile)
{
	if (builder->count >= WORLD_SPELUNK_WITNESS_STEP_MAX ||
			builder->pitons_used >= profile->piton_budget ||
			world_spelunk_runtime_place_piton(builder->runtime) !=
				WORLD_SPELUNK_INFRASTRUCTURE_OK ||
			!append_step(builder, WORLD_SPELUNK_WITNESS_PLACE_PITON,
				builder->runtime->state.x, builder->runtime->state.y)) {
		return false;
	}
	builder->pitons_used++;
	return true;
}

static bool append_rope(struct witness_builder *builder,
		const struct world_spelunk_traversal_profile *profile)
{
	int available = profile->rope_segment_budget -
		builder->rope_segments_used;
	int deployed = 0;

	if (builder->count >= WORLD_SPELUNK_WITNESS_STEP_MAX || available <= 0 ||
			world_spelunk_runtime_deploy_rope(builder->runtime, available,
				&deployed) != WORLD_SPELUNK_INFRASTRUCTURE_OK ||
			deployed <= 0 || deployed > available ||
			!append_step(builder, WORLD_SPELUNK_WITNESS_DEPLOY_ROPE,
				builder->runtime->state.x, builder->runtime->state.y)) {
		return false;
	}
	builder->rope_segments_used += (uint16_t)deployed;
	return true;
}

static int segment_route_side(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_generated_route_segment *segment)
{
	const struct world_spelunk_generated_route_station *from =
		&generated->route_stations[segment->from_station];
	const struct world_spelunk_generated_route_station *to =
		&generated->route_stations[segment->to_station];

	return segment->shaft_x < from->x && segment->shaft_x < to->x ?
		1 : -1;
}

static bool append_descent(struct witness_builder *builder,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_generated_route_segment *segment,
		const struct world_spelunk_traversal_profile *profile)
{
	const struct world_spelunk_generated_route_station *from =
		&generated->route_stations[segment->from_station];
	const struct world_spelunk_generated_route_station *to =
		&generated->route_stations[segment->to_station];
	int route_side = segment_route_side(generated, segment);
	int lip_x = segment->shaft_x + route_side;

	if (builder->runtime->state.x != from->x ||
			builder->runtime->state.y != from->y ||
			!append_walk_x(builder, lip_x) ||
			!append_grip_for_move(builder, segment->shaft_x, from->y) ||
			!append_command(builder, WORLD_SPELUNK_COMMAND_MOVE,
				-route_side, 0, segment->shaft_x, from->y)) {
		return false;
	}
	if (segment->uses_rope) {
		if (!append_piton(builder, profile) ||
				!append_rope(builder, profile)) {
			return false;
		}
	}
	while (builder->runtime->state.y < to->y) {
		int x = builder->runtime->state.x;
		int y = builder->runtime->state.y;
		bool destination_supported = generated->cells[
			(size_t)(y + 2) * generated->width + x] ==
			WORLD_SPELUNK_ROCK;

		/* The last step lands directly on the shaft floor.  It does not need
		 * a hold that remains adjacent after landing because resolution changes
		 * immediately to standing. */
		if (!segment->uses_rope && !destination_supported &&
				!append_grip_for_move(builder, x, y + 1)) {
			return false;
		}
		if (!append_command(builder, WORLD_SPELUNK_COMMAND_MOVE, 0, 1,
				x, y + 1)) {
			return false;
		}
	}
	if (!append_command(builder, WORLD_SPELUNK_COMMAND_MOVE, route_side, 0,
			lip_x, builder->runtime->state.y) ||
			!append_walk_x(builder, to->x) ||
			!append_station_proof(builder, generated,
				segment->to_station)) {
		return false;
	}
	return true;
}

static bool append_ascent(struct witness_builder *builder,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_generated_route_segment *segment)
{
	const struct world_spelunk_generated_route_station *from =
		&generated->route_stations[segment->from_station];
	const struct world_spelunk_generated_route_station *to =
		&generated->route_stations[segment->to_station];
	int route_side = segment_route_side(generated, segment);
	int lip_x = segment->shaft_x + route_side;

	if (builder->runtime->state.x != to->x ||
			builder->runtime->state.y != to->y ||
			!append_walk_x(builder, lip_x) ||
			!append_command(builder, WORLD_SPELUNK_COMMAND_MOVE,
				-route_side, 0, segment->shaft_x, to->y)) {
		return false;
	}
	while (builder->runtime->state.y > from->y) {
		int x = builder->runtime->state.x;
		int y = builder->runtime->state.y;

		if (!segment->uses_rope &&
				!append_grip_for_move(builder, x, y - 1)) {
			return false;
		}
		if (!append_command(builder, WORLD_SPELUNK_COMMAND_MOVE, 0, -1,
				x, y - 1)) {
			return false;
		}
	}
	if (!append_command(builder, WORLD_SPELUNK_COMMAND_MOVE, route_side, 0,
			lip_x, builder->runtime->state.y) ||
			!append_walk_x(builder, from->x) ||
			!append_rest(builder)) {
		return false;
	}
	return true;
}

bool world_spelunk_generated_layout_build_witness(
		struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_traversal_profile *profile,
		struct world_spelunk_proof_report *report)
{
	struct witness_builder builder;
	struct world_spelunk_traversal_witness witness;
	struct world_spelunk_witness_step *retained;
	struct world_spelunk_map map;
	struct world_spelunk_state state;
	uint32_t required_mask;
	bool compiled = true;
	int i;

	if (!generated || !profile || generated->witness_steps ||
			generated->route_station_count < 2 ||
			generated->route_segment_count !=
				generated->route_station_count - 1 ||
			generated->landmark_count >= 32) {
		return false;
	}
	memset(&builder, 0, sizeof(builder));
	builder.steps = mem_zalloc(WORLD_SPELUNK_WITNESS_STEP_MAX *
		sizeof(*builder.steps));
	memset(&map, 0, sizeof(map));
	map.cells = generated->cells;
	map.width = generated->width;
	map.height = generated->height;
	map.stride = generated->width;
	if (!world_spelunk_state_init(&state, &map, &generated->rules,
			generated->route_stations[0].x,
			generated->route_stations[0].y,
			generated->initial_stamina)) {
		mem_free(builder.steps);
		return false;
	}
	builder.runtime = world_spelunk_runtime_create_with_perception(
		generated->system_id, &state, &generated->perception);
	if (!builder.runtime) {
		mem_free(builder.steps);
		return false;
	}
	if (!append_station_proof(&builder, generated, 0)) {
		compiled = false;
	}
	for (i = 0; compiled && i < generated->route_segment_count; i++) {
		const struct world_spelunk_generated_route_segment *segment =
			&generated->route_segments[i];

		if (segment->from_station != i || segment->to_station != i + 1 ||
				!append_descent(&builder, generated, segment, profile)) {
			compiled = false;
		}
	}
	for (i = generated->route_segment_count - 1; compiled && i >= 0; i--) {
		if (!append_ascent(&builder, generated,
				&generated->route_segments[i])) {
			compiled = false;
		}
	}
	world_spelunk_runtime_free(builder.runtime);
	builder.runtime = NULL;
	if (!compiled) {
		mem_free(builder.steps);
		return false;
	}
	required_mask = 0;
	for (i = 0; i < generated->route_station_count; i++) {
		required_mask |= generated->route_stations[i].checkpoint_mask;
	}
	for (i = 0; i < generated->route_visit_count; i++) {
		required_mask |= generated->route_visits[i].checkpoint_mask;
	}
	memset(&witness, 0, sizeof(witness));
	witness.start_x = generated->route_stations[0].x;
	witness.start_y = generated->route_stations[0].y;
	witness.steps = builder.steps;
	witness.step_count = builder.count;
	witness.required_checkpoint_mask = required_mask;
	if (world_spelunk_replay_traversal_witness(generated, profile, &witness,
			report) != WORLD_SPELUNK_PROOF_OK) {
		mem_free(builder.steps);
		return false;
	}
	retained = mem_alloc(builder.count * sizeof(*retained));
	memcpy(retained, builder.steps, builder.count * sizeof(*retained));
	mem_free(builder.steps);
	generated->witness_steps = retained;
	generated->witness_step_count = builder.count;
	generated->witness_required_checkpoint_mask = required_mask;
	return true;
}

bool world_spelunk_generated_layout_rebuild_witness(
		struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_traversal_profile *profile,
		struct world_spelunk_proof_report *report)
{
	if (!generated) return false;
	mem_free(generated->witness_steps);
	generated->witness_steps = NULL;
	generated->witness_step_count = 0;
	generated->witness_required_checkpoint_mask = 0;
	return world_spelunk_generated_layout_build_witness(generated, profile,
		report);
}
