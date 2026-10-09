/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-population.h
 * \brief Deterministic population of unpublished generated runtimes.
 */

#ifndef WORLD_SPELUNKING_GENERATION_POPULATION_H
#define WORLD_SPELUNKING_GENERATION_POPULATION_H

#include "world-spelunking-generation.h"
#include "world-spelunking-population-data.h"
#include "world-spelunking-runtime.h"

#include <stdbool.h>

/** Populate one fresh runtime without consuming gameplay randomness. */
bool world_spelunk_populate_generated_runtime(
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_population_profile *profile);

#endif /* !WORLD_SPELUNKING_GENERATION_POPULATION_H */
