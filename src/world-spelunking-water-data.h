/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-water-data.h
 * \brief Parsed authority for procedural cave hydrology profiles.
 */

#ifndef WORLD_SPELUNKING_WATER_DATA_H
#define WORLD_SPELUNKING_WATER_DATA_H

#include "init.h"

#include <stdint.h>

#define WORLD_SPELUNK_WATER_PROFILE_MAX 32

enum world_spelunk_water_family {
	WORLD_SPELUNK_WATER_LOWER_BASIN = 0
};

/** Immutable hydrology tuning.  Algorithms recognize families, not IDs. */
struct world_spelunk_water_profile {
	const char *id;
	enum world_spelunk_water_family family;
	uint16_t version;
	uint16_t min_surface_depth_percent;
	uint16_t max_surface_depth_percent;
	uint16_t min_width_percent;
	uint16_t max_width_percent;
	uint16_t min_depth_tiles;
	uint16_t max_depth_tiles;
	uint16_t min_shore_tiles;
};

extern struct file_parser spelunking_water_parser;

int world_spelunk_water_profile_count(void);
const struct world_spelunk_water_profile *
	world_spelunk_water_profile_by_index(int index);
const struct world_spelunk_water_profile *
	world_spelunk_water_profile_by_id(const char *id);

#endif /* !WORLD_SPELUNKING_WATER_DATA_H */
