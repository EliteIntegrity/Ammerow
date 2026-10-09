/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-generation-geology.c
 * \brief Authored strata and sparse decoration for unpublished cave candidates.
 */

#include "world-spelunking-generation-geology.h"

#include "world-spelunking-generation.h"
#include "z-util.h"
#include "z-virt.h"

#include <stdlib.h>
#include <string.h>

struct geology_rng {
	uint32_t state;
};

static uint32_t hash_text(const char *text)
{
	uint32_t hash = 2166136261u;

	while (text && *text) {
		hash ^= (uint8_t)*text++;
		hash *= 16777619u;
	}
	return hash;
}

static uint32_t mix32(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

static uint32_t rng_next(struct geology_rng *rng)
{
	uint32_t value;

	rng->state += 0x9e3779b9u;
	value = rng->state;
	return mix32(value);
}

static int rng_range(struct geology_rng *rng, int minimum, int maximum)
{
	uint32_t span;
	uint32_t limit;
	uint32_t value;

	if (minimum >= maximum) return minimum;
	span = (uint32_t)(maximum - minimum + 1);
	limit = UINT32_MAX - UINT32_MAX % span;
	do {
		value = rng_next(rng);
	} while (value >= limit);
	return minimum + (int)(value % span);
}

static size_t cell_index(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	return (size_t)y * generated->width + x;
}

static bool location_is_protected(
		const struct world_spelunk_generated_layout *generated, int x, int y)
{
	int i;

	if (x == generated->entrance_x && y == generated->entrance_y) {
		return true;
	}
	for (i = 0; i < generated->landmark_count; i++) {
		if (generated->landmarks[i].x == x &&
				generated->landmarks[i].y == y) {
			return true;
		}
	}
	return false;
}

static bool has_cardinal_tile(
		const struct world_spelunk_generated_layout *generated, int x, int y,
		enum world_spelunk_tile tile)
{
	static const int dx[4] = { -1, 1, 0, 0 };
	static const int dy[4] = { 0, 0, -1, 1 };
	int direction;

	for (direction = 0; direction < 4; direction++) {
		if (generated->cells[cell_index(generated, x + dx[direction],
				y + dy[direction])] == tile) {
			return true;
		}
	}
	return false;
}

static bool decoration_fits(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_decoration_definition *decoration,
		int x, int y, bool require_empty)
{
	size_t index;

	if (!generated || !decoration || x <= 0 || y <= 0 ||
			x >= generated->width - 1 || y >= generated->height - 1) {
		return false;
	}
	index = cell_index(generated, x, y);
	if ((require_empty && generated->decoration_tags[index] !=
			WORLD_SPELUNK_GEOLOGY_TAG_NONE) ||
			location_is_protected(generated, x, y)) {
		return false;
	}
	switch (decoration->placement) {
	case WORLD_SPELUNK_DECORATION_ROCK_FACE:
		return generated->cells[index] == WORLD_SPELUNK_ROCK &&
			(has_cardinal_tile(generated, x, y, WORLD_SPELUNK_AIR) ||
			 has_cardinal_tile(generated, x, y, WORLD_SPELUNK_WATER));
	case WORLD_SPELUNK_DECORATION_CEILING:
		return generated->cells[index] == WORLD_SPELUNK_AIR &&
			generated->cells[cell_index(generated, x, y - 1)] ==
				WORLD_SPELUNK_ROCK;
	case WORLD_SPELUNK_DECORATION_FLOOR:
		return generated->cells[index] == WORLD_SPELUNK_AIR &&
			generated->cells[cell_index(generated, x, y + 1)] ==
				WORLD_SPELUNK_ROCK;
	case WORLD_SPELUNK_DECORATION_WATER_EDGE:
		return generated->cells[index] == WORLD_SPELUNK_AIR &&
			generated->cells[cell_index(generated, x, y + 1)] ==
				WORLD_SPELUNK_ROCK &&
			has_cardinal_tile(generated, x, y, WORLD_SPELUNK_WATER);
	}
	return false;
}

static const struct world_spelunk_material_definition *choose_material(
		const struct world_spelunk_geology_profile *profile,
		struct geology_rng *rng)
{
	uint32_t total = 0;
	uint32_t selected;
	int i;

	for (i = 0; i < profile->material_count; i++) {
		total += profile->materials[i].weight;
	}
	selected = (uint32_t)rng_range(rng, 1, (int)total);
	for (i = 0; i < profile->material_count; i++) {
		if (selected <= profile->materials[i].weight) {
			return &profile->materials[i];
		}
		selected -= profile->materials[i].weight;
	}
	return NULL;
}

static const struct world_spelunk_decoration_definition *choose_decoration(
		const struct world_spelunk_geology_profile *profile,
		struct geology_rng *rng)
{
	uint32_t total = 0;
	uint32_t selected;
	int i;

	for (i = 0; i < profile->decoration_count; i++) {
		total += profile->decorations[i].weight;
	}
	selected = (uint32_t)rng_range(rng, 1, (int)total);
	for (i = 0; i < profile->decoration_count; i++) {
		if (selected <= profile->decorations[i].weight) {
			return &profile->decorations[i];
		}
		selected -= profile->decorations[i].weight;
	}
	return NULL;
}

static bool decoration_has_spacing(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_geology_profile *profile,
		const struct world_spelunk_decoration_definition *decoration,
		int x, int y)
{
	int other_x;
	int other_y;

	for (other_y = 1; other_y < generated->height - 1; other_y++) {
		for (other_x = 1; other_x < generated->width - 1; other_x++) {
			uint8_t tag = generated->decoration_tags[
				cell_index(generated, other_x, other_y)];
			const struct world_spelunk_decoration_definition *other;
			int required;

			if (tag == WORLD_SPELUNK_GEOLOGY_TAG_NONE ||
					(other_x == x && other_y == y)) continue;
			other = world_spelunk_geology_decoration_by_tag(profile, tag);
			if (!other) return false;
			required = MAX(decoration->minimum_spacing,
				other->minimum_spacing);
			if (abs(other_x - x) + abs(other_y - y) < required) {
				return false;
			}
		}
	}
	return true;
}

static bool assign_strata(
		const struct world_spelunk_geology_profile *profile,
		struct geology_rng *rng,
		struct world_spelunk_generated_layout *generated)
{
	int y = 0;

	while (y < generated->height) {
		const struct world_spelunk_material_definition *material =
			choose_material(profile, rng);
		int band_height = rng_range(rng, profile->min_stratum_height,
			profile->max_stratum_height);
		int band_end = MIN(generated->height, y + band_height);
		int x;
		int band_y;

		if (!material) return false;
		for (band_y = y; band_y < band_end; band_y++) {
			for (x = 0; x < generated->width; x++) {
				size_t index = cell_index(generated, x, band_y);

				if (generated->cells[index] == WORLD_SPELUNK_ROCK) {
					generated->material_tags[index] = material->tag;
				}
			}
		}
		y = band_end;
	}
	return true;
}

static bool place_decorations(
		const struct world_spelunk_geology_profile *profile,
		struct geology_rng *rng,
		struct world_spelunk_generated_layout *generated)
{
	size_t cell_count = (size_t)generated->width * generated->height;
	int target = rng_range(rng, profile->min_decoration_count,
		profile->max_decoration_count);
	int attempt_limit = (int)MIN((size_t)INT_MAX, cell_count * 8u);
	int placed = 0;
	int attempt;

	for (attempt = 0; attempt < attempt_limit && placed < target; attempt++) {
		const struct world_spelunk_decoration_definition *decoration =
			choose_decoration(profile, rng);
		int x = rng_range(rng, 1, generated->width - 2);
		int y = rng_range(rng, 1, generated->height - 2);

		if (!decoration || !decoration_fits(generated, decoration, x, y,
				true) ||
				!decoration_has_spacing(generated, profile, decoration, x, y)) {
			continue;
		}
		generated->decoration_tags[cell_index(generated, x, y)] =
			decoration->tag;
		placed++;
	}
	return placed == target;
}

bool world_spelunk_generated_layout_apply_geology(
		const struct world_spelunk_geology_profile *profile,
		struct world_spelunk_generated_layout *generated)
{
	struct geology_rng rng;
	size_t cell_count;

	if (!profile || !generated || !generated->cells || profile->version != 1 ||
			!world_id_is_valid(profile->id) || !profile->material_count ||
			(profile->max_decoration_count > 0 &&
			 !profile->decoration_count) || generated->material_tags ||
			generated->decoration_tags) {
		return false;
	}
	cell_count = (size_t)generated->width * generated->height;
	generated->material_tags = mem_zalloc(cell_count);
	generated->decoration_tags = mem_zalloc(cell_count);
	my_strcpy(generated->material_profile_id, profile->id,
		sizeof(generated->material_profile_id));
	generated->geology_version = profile->version;
	rng.state = mix32(generated->seed ^ hash_text(profile->id) ^
		((uint32_t)profile->version << 16) ^ 0x67656f6cu);
	if (!assign_strata(profile, &rng, generated) ||
			!place_decorations(profile, &rng, generated)) {
		mem_free(generated->decoration_tags);
		mem_free(generated->material_tags);
		generated->decoration_tags = NULL;
		generated->material_tags = NULL;
		generated->material_profile_id[0] = '\0';
		generated->geology_version = 0;
		return false;
	}
	return true;
}

bool world_spelunk_generated_layout_has_valid_geology(
		const struct world_spelunk_generated_layout *generated,
		const struct world_spelunk_geology_profile *profile)
{
	int decoration_count = 0;
	int x;
	int y;

	if (!generated || !profile || !generated->cells ||
			!generated->material_tags || !generated->decoration_tags ||
			!streq(generated->material_profile_id, profile->id) ||
			generated->geology_version != profile->version) {
		return false;
	}
	for (y = 0; y < generated->height; y++) {
		for (x = 0; x < generated->width; x++) {
			size_t index = cell_index(generated, x, y);
			uint8_t material_tag = generated->material_tags[index];
			uint8_t decoration_tag = generated->decoration_tags[index];
			const struct world_spelunk_decoration_definition *decoration;

			if (generated->cells[index] == WORLD_SPELUNK_ROCK) {
				if (!world_spelunk_geology_material_by_tag(profile,
						material_tag)) return false;
			} else if (material_tag != WORLD_SPELUNK_GEOLOGY_TAG_NONE) {
				return false;
			}
			if (decoration_tag == WORLD_SPELUNK_GEOLOGY_TAG_NONE) continue;
			decoration = world_spelunk_geology_decoration_by_tag(profile,
				decoration_tag);
			if (!decoration || !decoration_fits(generated, decoration, x, y,
					false) ||
					!decoration_has_spacing(generated, profile, decoration,
						x, y)) {
				return false;
			}
			decoration_count++;
		}
	}
	return decoration_count >= profile->min_decoration_count &&
		decoration_count <= profile->max_decoration_count;
}
