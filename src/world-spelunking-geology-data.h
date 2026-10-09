/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-geology-data.h
 * \brief Parsed authority for generated cave materials and decoration.
 */

#ifndef WORLD_SPELUNKING_GEOLOGY_DATA_H
#define WORLD_SPELUNKING_GEOLOGY_DATA_H

#include "init.h"
#include "world-location.h"

#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#define WORLD_SPELUNK_GEOLOGY_PROFILE_MAX 32
#define WORLD_SPELUNK_MATERIAL_MAX 16
#define WORLD_SPELUNK_DECORATION_MAX 16
#define WORLD_SPELUNK_GEOLOGY_TAG_NONE 0

enum world_spelunk_decoration_placement {
	WORLD_SPELUNK_DECORATION_ROCK_FACE = 0,
	WORLD_SPELUNK_DECORATION_CEILING,
	WORLD_SPELUNK_DECORATION_FLOOR,
	WORLD_SPELUNK_DECORATION_WATER_EDGE
};

/** Stable material tag and its presentation identity. */
struct world_spelunk_material_definition {
	const char *id;
	const char *visual_material_id;
	const char *name;
	uint8_t tag;
	uint16_t weight;
};

/** Stable, non-colliding presentation overlay. */
struct world_spelunk_decoration_definition {
	const char *id;
	const char *name;
	enum world_spelunk_decoration_placement placement;
	uint8_t tag;
	uint16_t weight;
	uint16_t minimum_spacing;
	wchar_t glyph;
	uint8_t attr;
};

/** Immutable composition profile referenced by a procedural recipe. */
struct world_spelunk_geology_profile {
	const char *id;
	uint16_t version;
	uint16_t min_stratum_height;
	uint16_t max_stratum_height;
	uint16_t min_decoration_count;
	uint16_t max_decoration_count;
	uint16_t material_count;
	uint16_t decoration_count;
	struct world_spelunk_material_definition
		materials[WORLD_SPELUNK_MATERIAL_MAX];
	struct world_spelunk_decoration_definition
		decorations[WORLD_SPELUNK_DECORATION_MAX];
};

extern struct file_parser spelunking_geology_parser;

int world_spelunk_geology_profile_count(void);
const struct world_spelunk_geology_profile *
	world_spelunk_geology_profile_by_index(int index);
const struct world_spelunk_geology_profile *
	world_spelunk_geology_profile_by_id(const char *id);
const struct world_spelunk_material_definition *
	world_spelunk_geology_material_by_tag(
		const struct world_spelunk_geology_profile *profile, uint8_t tag);
const struct world_spelunk_decoration_definition *
	world_spelunk_geology_decoration_by_tag(
		const struct world_spelunk_geology_profile *profile, uint8_t tag);

/** Resolve each material through the shared Natural-theme registry. */
bool world_spelunk_geology_data_validate_visuals(void);

#endif /* !WORLD_SPELUNKING_GEOLOGY_DATA_H */
