/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-traversal-proof.h
 * \brief Replay recorded generated-cave routes through the real rules core.
 */

#ifndef WORLD_SPELUNKING_TRAVERSAL_PROOF_H
#define WORLD_SPELUNKING_TRAVERSAL_PROOF_H

#include "world-spelunking-generation.h"
#include "world-spelunking-traversal-data.h"

#include <stddef.h>
#include <stdint.h>

#define WORLD_SPELUNK_WITNESS_STEP_MAX 8192u

enum world_spelunk_witness_step_kind {
	WORLD_SPELUNK_WITNESS_COMMAND = 0,
	WORLD_SPELUNK_WITNESS_PLACE_PITON,
	WORLD_SPELUNK_WITNESS_DEPLOY_ROPE,
	WORLD_SPELUNK_WITNESS_CHECKPOINT
};

/** One exact action or assertion recorded by a route producer. */
struct world_spelunk_witness_step {
	enum world_spelunk_witness_step_kind kind;
	struct world_spelunk_command command;
	int expected_x;
	int expected_y;
	uint32_t checkpoint_mask;
};

struct world_spelunk_traversal_witness {
	int start_x;
	int start_y;
	const struct world_spelunk_witness_step *steps;
	size_t step_count;
	uint32_t required_checkpoint_mask;
};

enum world_spelunk_proof_result {
	WORLD_SPELUNK_PROOF_OK = 0,
	WORLD_SPELUNK_PROOF_INVALID_ARGUMENT,
	WORLD_SPELUNK_PROOF_UNSUPPORTED_PROFILE,
	WORLD_SPELUNK_PROOF_UNSAFE_START,
	WORLD_SPELUNK_PROOF_STEP_REJECTED,
	WORLD_SPELUNK_PROOF_INFRASTRUCTURE_REJECTED,
	WORLD_SPELUNK_PROOF_POSITION_MISMATCH,
	WORLD_SPELUNK_PROOF_EQUIPMENT_BUDGET,
	WORLD_SPELUNK_PROOF_FALL_DAMAGE,
	WORLD_SPELUNK_PROOF_SUBMERSION,
	WORLD_SPELUNK_PROOF_DROWNING,
	WORLD_SPELUNK_PROOF_MISSING_CHECKPOINT,
	WORLD_SPELUNK_PROOF_RETURN_FAILED
};

/** Compact deterministic diagnostics for candidate rejection and tests. */
struct world_spelunk_proof_report {
	enum world_spelunk_proof_result result;
	size_t failed_step;
	size_t steps_completed;
	int final_x;
	int final_y;
	uint16_t pitons_used;
	uint16_t rope_segments_used;
	uint16_t fall_damage;
	uint16_t longest_submersion;
	uint32_t reached_checkpoint_mask;
};

enum world_spelunk_proof_result world_spelunk_replay_traversal_witness(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_traversal_profile *profile,
		const struct world_spelunk_traversal_witness *witness,
		struct world_spelunk_proof_report *report);

#endif /* !WORLD_SPELUNKING_TRAVERSAL_PROOF_H */
