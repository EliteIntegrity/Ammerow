/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file terrain-visual-data.h
 * \brief Data-authored material and biome recipes for ASCII terrain.
 */

#ifndef TERRAIN_VISUAL_DATA_H
#define TERRAIN_VISUAL_DATA_H

#include "datafile.h"

#include <stdbool.h>
#include <stdint.h>

/** World-square semantics whose material mapping is authored in data. */
enum terrain_visual_semantic {
	TERRAIN_VISUAL_SEMANTIC_WATER = 0,
	TERRAIN_VISUAL_SEMANTIC_ROAD,
	TERRAIN_VISUAL_SEMANTIC_WOOD,
	TERRAIN_VISUAL_SEMANTIC_COUNT
};

struct terrain_visual_rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
};

/** A complete recipe for one semantic material in one environment profile. */
struct terrain_visual_recipe {
	const char *profile;
	const char *material;
	const char *tile_id;
	struct terrain_visual_rgb base;
};

extern struct file_parser terrain_visual_parser;

const struct terrain_visual_recipe *terrain_visual_recipe_for(
		const char *profile, const char *material);
const char *terrain_visual_material_for_semantic(
		enum terrain_visual_semantic semantic);
bool terrain_visual_material_is_known(const char *material);
bool terrain_visual_profile_is_known(const char *profile);
bool terrain_visual_data_validate_features(void);
bool terrain_visual_data_validate_world(void);

#endif /* TERRAIN_VISUAL_DATA_H */
