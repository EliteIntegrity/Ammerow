/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-geology.h
 * \brief Deterministic, non-colliding geology overlays for cave candidates.
 */

#ifndef WORLD_SPELUNKING_GENERATION_GEOLOGY_H
#define WORLD_SPELUNKING_GENERATION_GEOLOGY_H

#include "world-spelunking-geology-data.h"

#include <stdbool.h>

struct world_spelunk_generated_layout;

bool world_spelunk_generated_layout_apply_geology(
		const struct world_spelunk_geology_profile *profile,
		struct world_spelunk_generated_layout *generated);
bool world_spelunk_generated_layout_has_valid_geology(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_geology_profile *profile);

#endif /* !WORLD_SPELUNKING_GENERATION_GEOLOGY_H */
