/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-witness.h
 * \brief Compile semantic cave rails into exact physics witnesses.
 */

#ifndef WORLD_SPELUNKING_GENERATION_WITNESS_H
#define WORLD_SPELUNKING_GENERATION_WITNESS_H

#include "world-spelunking-traversal-proof.h"

#include <stdbool.h>

/** Build and replay the candidate's owned transient witness. */
bool world_spelunk_generated_layout_build_witness(
		struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_traversal_profile *profile,
		struct world_spelunk_proof_report *report);

/** Replace an earlier witness after publication-time checkpoints are added. */
bool world_spelunk_generated_layout_rebuild_witness(
		struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_traversal_profile *profile,
		struct world_spelunk_proof_report *report);

#endif /* !WORLD_SPELUNKING_GENERATION_WITNESS_H */
