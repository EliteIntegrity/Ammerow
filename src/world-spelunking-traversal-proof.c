/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-traversal-proof.c
 * \brief Physics replay for generated-cave traversal witnesses.
 */

#include "world-spelunking-traversal-proof.h"

#include "world-spelunking-runtime.h"

#include <limits.h>
#include <string.h>

static enum world_spelunk_proof_result fail_proof(
		struct world_spelunk_proof_report *report,
		enum world_spelunk_proof_result result, size_t step,
		const struct world_spelunk_runtime *runtime)
{
	report->result = result;
	report->failed_step = step;
	if (runtime) {
		report->final_x = runtime->state.x;
		report->final_y = runtime->state.y;
	}
	return result;
}

static bool proof_arguments_are_valid(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_traversal_profile *profile,
		const struct world_spelunk_traversal_witness *witness)
{
	return generated && generated->cells &&
		world_id_is_valid(generated->system_id) &&
		world_id_is_valid(generated->traversal_profile_id) && profile &&
		world_id_is_valid(profile->id) &&
		streq(generated->traversal_profile_id, profile->id) && witness &&
		witness->steps && witness->step_count > 0 &&
		witness->step_count <= WORLD_SPELUNK_WITNESS_STEP_MAX &&
		generated->width > 2 && generated->height > 2;
}

static enum world_spelunk_proof_result process_proof_turn(
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_traversal_profile *profile,
		struct world_spelunk_proof_report *report, unsigned int *submersion,
		size_t step)
{
	struct world_spelunk_breath_report breath_report;

	if (world_spelunk_is_submerged(&runtime->state)) {
		(*submersion)++;
	} else {
		*submersion = 0;
	}
	if (*submersion > report->longest_submersion) {
		report->longest_submersion = (uint16_t)MIN(*submersion,
			UINT16_MAX);
	}
	if (*submersion > profile->max_submerged_turns) {
		return fail_proof(report, WORLD_SPELUNK_PROOF_SUBMERSION, step,
			runtime);
	}
	if (!world_spelunk_apply_breath_turn(&runtime->state, false,
			&breath_report) || breath_report.damage > 0) {
		return fail_proof(report, WORLD_SPELUNK_PROOF_DROWNING, step,
			runtime);
	}
	return WORLD_SPELUNK_PROOF_OK;
}

enum world_spelunk_proof_result world_spelunk_replay_traversal_witness(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_traversal_profile *profile,
		const struct world_spelunk_traversal_witness *witness,
		struct world_spelunk_proof_report *report)
{
	struct world_spelunk_proof_report local;
	struct world_spelunk_map map;
	struct world_spelunk_state state;
	struct world_spelunk_runtime *runtime = NULL;
	unsigned int submersion = 0;
	size_t i;
	enum world_spelunk_proof_result result = WORLD_SPELUNK_PROOF_OK;

	if (!report) report = &local;
	memset(report, 0, sizeof(*report));
	report->failed_step = SIZE_MAX;
	if (!proof_arguments_are_valid(generated, profile, witness)) {
		return fail_proof(report, WORLD_SPELUNK_PROOF_INVALID_ARGUMENT,
			SIZE_MAX, NULL);
	}
	if (profile->version != 1) {
		return fail_proof(report, WORLD_SPELUNK_PROOF_UNSUPPORTED_PROFILE,
			SIZE_MAX, NULL);
	}
	memset(&map, 0, sizeof(map));
	map.cells = generated->cells;
	map.width = generated->width;
	map.height = generated->height;
	map.stride = generated->width;
	if (!world_spelunk_state_init(&state, &map, &generated->rules,
			witness->start_x, witness->start_y,
			generated->initial_stamina) ||
			state.movement != WORLD_SPELUNK_STANDING) {
		return fail_proof(report, WORLD_SPELUNK_PROOF_UNSAFE_START,
			SIZE_MAX, NULL);
	}
	runtime = world_spelunk_runtime_create_with_perception(
		generated->system_id, &state, &generated->perception);
	if (!runtime) {
		return fail_proof(report, WORLD_SPELUNK_PROOF_UNSAFE_START,
			SIZE_MAX, NULL);
	}
	for (i = 0; i < witness->step_count; i++) {
		const struct world_spelunk_witness_step *step = &witness->steps[i];
		bool consumes_turn = false;

		switch (step->kind) {
		case WORLD_SPELUNK_WITNESS_COMMAND: {
			struct world_spelunk_action_report action_report;
			enum world_spelunk_action_outcome outcome =
				world_spelunk_apply_command(&runtime->state, &step->command,
					&action_report);

			if (outcome == WORLD_SPELUNK_ACTION_REJECTED) {
				result = fail_proof(report,
					WORLD_SPELUNK_PROOF_STEP_REJECTED, i, runtime);
				goto cleanup;
			}
			consumes_turn = outcome == WORLD_SPELUNK_ACTION_TURN;
			if (action_report.damage > 0) {
				unsigned int total = report->fall_damage +
					(unsigned int)action_report.damage;

				report->fall_damage = (uint16_t)MIN(total, UINT16_MAX);
				if (total > profile->max_mandatory_fall_damage) {
					result = fail_proof(report,
						WORLD_SPELUNK_PROOF_FALL_DAMAGE, i, runtime);
					goto cleanup;
				}
			}
			break;
		}
		case WORLD_SPELUNK_WITNESS_PLACE_PITON:
			if (report->pitons_used >= profile->piton_budget) {
				result = fail_proof(report,
					WORLD_SPELUNK_PROOF_EQUIPMENT_BUDGET, i, runtime);
				goto cleanup;
			}
			if (world_spelunk_runtime_place_piton(runtime) !=
					WORLD_SPELUNK_INFRASTRUCTURE_OK) {
				result = fail_proof(report,
					WORLD_SPELUNK_PROOF_INFRASTRUCTURE_REJECTED, i,
					runtime);
				goto cleanup;
			}
			report->pitons_used++;
			consumes_turn = true;
			break;
		case WORLD_SPELUNK_WITNESS_DEPLOY_ROPE: {
			int available = profile->rope_segment_budget -
				report->rope_segments_used;
			int deployed = 0;

			if (available <= 0) {
				result = fail_proof(report,
					WORLD_SPELUNK_PROOF_EQUIPMENT_BUDGET, i, runtime);
				goto cleanup;
			}
			if (world_spelunk_runtime_deploy_rope(runtime, available,
					&deployed) != WORLD_SPELUNK_INFRASTRUCTURE_OK ||
					deployed <= 0 || deployed > available) {
				result = fail_proof(report,
					WORLD_SPELUNK_PROOF_INFRASTRUCTURE_REJECTED, i,
					runtime);
				goto cleanup;
			}
			report->rope_segments_used += (uint16_t)deployed;
			consumes_turn = true;
			break;
		}
		case WORLD_SPELUNK_WITNESS_CHECKPOINT:
			break;
		default:
			result = fail_proof(report,
				WORLD_SPELUNK_PROOF_INVALID_ARGUMENT, i, runtime);
			goto cleanup;
		}
		if (runtime->state.x != step->expected_x ||
				runtime->state.y != step->expected_y) {
			result = fail_proof(report,
				WORLD_SPELUNK_PROOF_POSITION_MISMATCH, i, runtime);
			goto cleanup;
		}
		if (step->kind == WORLD_SPELUNK_WITNESS_CHECKPOINT) {
			report->reached_checkpoint_mask |= step->checkpoint_mask;
		}
		if (consumes_turn) {
			result = process_proof_turn(runtime, profile, report,
				&submersion, i);
			if (result != WORLD_SPELUNK_PROOF_OK) goto cleanup;
		}
		report->steps_completed = i + 1;
	}
	if ((report->reached_checkpoint_mask & witness->required_checkpoint_mask) !=
			witness->required_checkpoint_mask) {
		result = fail_proof(report, WORLD_SPELUNK_PROOF_MISSING_CHECKPOINT,
			witness->step_count, runtime);
		goto cleanup;
	}
	if (profile->return_required &&
			(runtime->state.x != witness->start_x ||
			runtime->state.y != witness->start_y ||
			runtime->state.movement != WORLD_SPELUNK_STANDING)) {
		result = fail_proof(report, WORLD_SPELUNK_PROOF_RETURN_FAILED,
			witness->step_count, runtime);
		goto cleanup;
	}
	report->result = WORLD_SPELUNK_PROOF_OK;
	report->final_x = runtime->state.x;
	report->final_y = runtime->state.y;

cleanup:
	world_spelunk_runtime_free(runtime);
	return result;
}
