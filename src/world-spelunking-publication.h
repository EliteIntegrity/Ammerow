/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-publication.h
 * \brief Transactional assembly of unpublished procedural cave systems.
 */

#ifndef WORLD_SPELUNKING_PUBLICATION_H
#define WORLD_SPELUNKING_PUBLICATION_H

#include "world-spelunking-generation.h"
#include "world-spelunking-system-data.h"

#include <stdint.h>

enum world_spelunk_publication_result {
	WORLD_SPELUNK_PUBLICATION_OK = 0,
	WORLD_SPELUNK_PUBLICATION_INVALID_ARGUMENT,
	WORLD_SPELUNK_PUBLICATION_PLAN_FAILED,
	WORLD_SPELUNK_PUBLICATION_GENERATION_FAILED,
	WORLD_SPELUNK_PUBLICATION_ENDPOINT_FAILED,
	WORLD_SPELUNK_PUBLICATION_RUNTIME_FAILED,
	WORLD_SPELUNK_PUBLICATION_SYSTEM_FAILED
};

/** Read-only reconstruction diagnostics for generated-section audits. */
enum world_spelunk_reconstruction_result {
	WORLD_SPELUNK_RECONSTRUCTION_OK = 0,
	WORLD_SPELUNK_RECONSTRUCTION_INVALID_ARGUMENT,
	WORLD_SPELUNK_RECONSTRUCTION_DATA_MISMATCH,
	WORLD_SPELUNK_RECONSTRUCTION_GENERATION_FAILED,
	WORLD_SPELUNK_RECONSTRUCTION_ENDPOINT_MISMATCH,
	WORLD_SPELUNK_RECONSTRUCTION_RUNTIME_MISMATCH
};

/**
 * Build a complete persistent owner with its root section materialized.
 *
 * This is a candidate boundary: the function never changes the player, active
 * world or global gameplay RNG.  On success, ownership is transferred through
 * published.  On failure, published is null and all provisional state has
 * been destroyed.
 */
enum world_spelunk_publication_result
	world_spelunk_publish_system_candidate(
		const struct world_spelunk_system_profile *profile, uint32_t seed,
		unsigned int action_energy, const char *arrival_entry_id,
		struct world_spelunk_system **published);

/**
 * Transactionally materialize a planned section at one reciprocal endpoint.
 * The system and gameplay RNG are unchanged on failure.  This does not select
 * the node as active or discover its connecting passage.
 */
enum world_spelunk_publication_result
	world_spelunk_materialize_node_candidate(
		struct world_spelunk_system *system, const char *node_id,
		unsigned int action_energy, const char *arrival_endpoint_id);

/**
 * Rebuild one materialized section from its persistent recipe identity and
 * verify it against the saved graph coordinates and immutable runtime layers.
 *
 * This diagnostic never mutates the system, runtime, player, or gameplay RNG.
 * On success, reconstructed owns a certified traversal witness and must be
 * released with world_spelunk_generated_layout_dispose().  On failure it is
 * empty and owns nothing.
 */
enum world_spelunk_reconstruction_result
	world_spelunk_reconstruct_node_candidate(
		const struct world_spelunk_system *system, const char *node_id,
		unsigned int action_energy,
		struct world_spelunk_generated_layout *reconstructed);

#endif /* !WORLD_SPELUNKING_PUBLICATION_H */
