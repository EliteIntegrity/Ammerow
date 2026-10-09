/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-traversal-data.h
 * \brief Parsed authority for procedural cave traversal proof budgets.
 */

#ifndef WORLD_SPELUNKING_TRAVERSAL_DATA_H
#define WORLD_SPELUNKING_TRAVERSAL_DATA_H

#include "init.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_SPELUNK_TRAVERSAL_PROFILE_MAX 16
#define WORLD_SPELUNK_TRAVERSAL_ROPE_SHAFT_MAX 31

/**
 * Limits used to prove a generated route.  These do not grant player items.
 */
struct world_spelunk_traversal_profile {
	const char *id;
	const char *name;
	const char *description;
	uint16_t version;
	uint16_t piton_budget;
	uint16_t rope_segment_budget;
	/** Leading route shafts that must demonstrate piton-and-rope traversal.
	 * If a recipe provides fewer shafts, all available shafts use rope. */
	uint16_t rope_shaft_count;
	uint16_t max_mandatory_fall_damage;
	uint16_t max_submerged_turns;
	bool return_required;
};

extern struct file_parser spelunking_traversal_parser;

int world_spelunk_traversal_profile_count(void);
const struct world_spelunk_traversal_profile *
	world_spelunk_traversal_profile_by_index(int index);
const struct world_spelunk_traversal_profile *
	world_spelunk_traversal_profile_by_id(const char *id);

#endif /* !WORLD_SPELUNKING_TRAVERSAL_DATA_H */
