/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-layout.h
 * \brief Deterministic initial layouts for authored spelunking locations.
 *
 *
 */

#ifndef WORLD_SPELUNKING_LAYOUT_H
#define WORLD_SPELUNKING_LAYOUT_H

#include <stdbool.h>

struct level;
struct world_spelunk_runtime;

enum world_spelunk_layout_result {
	WORLD_SPELUNK_LAYOUT_OK = 0,
	WORLD_SPELUNK_LAYOUT_INVALID_ARGUMENT,
	WORLD_SPELUNK_LAYOUT_UNSUPPORTED_LOCATION,
	WORLD_SPELUNK_LAYOUT_INVALID_METADATA,
	WORLD_SPELUNK_LAYOUT_INVALID_ENTRY,
	WORLD_SPELUNK_LAYOUT_INVALID_STATE
};

/** Build a fresh owned runtime for one registered location and entry. */
enum world_spelunk_layout_result world_spelunk_layout_create(
		const struct level *level, const char *entry_id,
		unsigned int action_energy,
		struct world_spelunk_runtime **runtime);

/** Add append-only authored terrain to a retained runtime from an older save. */
bool world_spelunk_layout_upgrade_runtime(
		struct world_spelunk_runtime *runtime);

#endif /* !WORLD_SPELUNKING_LAYOUT_H */
