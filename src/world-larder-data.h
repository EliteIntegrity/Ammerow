/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-larder-data.h
 * \brief Authored village-larder milestones and their stable save identities.
 */

#ifndef WORLD_LARDER_DATA_H
#define WORLD_LARDER_DATA_H

#include "h-basic.h"

#include <stdint.h>

#define WORLD_LARDER_MILESTONE_DEFINITION_MAX 16

struct file_parser;
struct world_larder_state;

/**
 * Save indices are append-only identities.  Existing entries may not be
 * reordered or renumbered after release.
 */
struct world_larder_milestone_definition {
	uint8_t save_index;
	const char *id;
	uint32_t target;
	const char *name;
	const char *message;
	const char *reward;
};

extern struct file_parser larder_parser;

int world_larder_milestone_count(void);
const struct world_larder_milestone_definition *
	world_larder_milestone_by_save_index(uint8_t save_index);
const struct world_larder_milestone_definition *
	world_larder_milestone_by_id(const char *id);
const struct world_larder_milestone_definition *
	world_larder_next_milestone(const struct world_larder_state *state);
const struct world_larder_milestone_definition *
	world_larder_current_milestone(const struct world_larder_state *state);
bool world_larder_has_milestone(const struct world_larder_state *state,
		const char *id);

#endif /* !WORLD_LARDER_DATA_H */
