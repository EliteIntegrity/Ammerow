/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking-appearance.c
 * \brief Shared ASCII appearance adapter for side-view snapshots.
 *
 *
 */

#include "angband.h"

#include "game-world.h"
#include "terrain-visual-data.h"
#include "ui-spelunking-appearance.h"

static uint8_t nearest_terminal_colour(struct terrain_visual_rgb rgb)
{
	uint32_t best_distance = UINT32_MAX;
	uint8_t best = COLOUR_SLATE;
	int i;

	for (i = 1; i < BASIC_COLORS; i++) {
		int red = (int)rgb.r - angband_color_table[i][1];
		int green = (int)rgb.g - angband_color_table[i][2];
		int blue = (int)rgb.b - angband_color_table[i][3];
		uint32_t distance = (uint32_t)(red * red + green * green +
			blue * blue);

		if (distance < best_distance) {
			best_distance = distance;
			best = (uint8_t)i;
		}
	}
	return best;
}

static const struct world_spelunk_geology_profile *view_geology_profile(
		const struct world_spelunk_view *view)
{
	const struct world_spelunk_geology_profile *profile;

	if (!view || !view->geology_profile_id[0]) return NULL;
	profile = world_spelunk_geology_profile_by_id(view->geology_profile_id);
	return profile && profile->version == view->geology_version ? profile :
		NULL;
}

static bool material_appearance(const struct world_spelunk_view *view,
		int col, int row, int *attr, wchar_t *glyph)
{
	const struct world_spelunk_geology_profile *profile =
		view_geology_profile(view);
	const struct world_spelunk_material_definition *material;
	const struct terrain_visual_recipe *recipe;
	const struct level *level;
	const struct world_site *site;
	const char *visual_profile = "default";
	int x;
	int y;
	uint8_t tag;

	if (!profile || !view->material_tags || view->geology_stride <= 0) {
		return false;
	}
	x = view->source_x + col;
	y = view->source_y + row;
	tag = view->material_tags[(size_t)y * view->geology_stride + x];
	material = world_spelunk_geology_material_by_tag(profile, tag);
	if (!material) return false;
	level = level_by_id(view->location_id);
	site = level ? world_site_by_id(level->site_id) : NULL;
	if (site && site->visual_profile) visual_profile = site->visual_profile;
	recipe = terrain_visual_recipe_for(visual_profile,
		material->visual_material_id);
	if (!recipe) recipe = terrain_visual_recipe_for("default",
		material->visual_material_id);
	if (!recipe) return false;
	*attr = nearest_terminal_colour(recipe->base);
	*glyph = L'#';
	return true;
}

static bool decoration_appearance(const struct world_spelunk_view *view,
		int col, int row, int *attr, wchar_t *glyph)
{
	const struct world_spelunk_geology_profile *profile =
		view_geology_profile(view);
	const struct world_spelunk_decoration_definition *decoration;
	int x;
	int y;
	uint8_t tag;

	if (!profile || !view->decoration_tags || view->geology_stride <= 0) {
		return false;
	}
	x = view->source_x + col;
	y = view->source_y + row;
	tag = view->decoration_tags[(size_t)y * view->geology_stride + x];
	decoration = world_spelunk_geology_decoration_by_tag(profile, tag);
	if (!decoration) return false;
	*attr = decoration->attr;
	*glyph = decoration->glyph;
	return true;
}

void spelunking_view_cell_appearance(
		const struct world_spelunk_view *view, int col, int row,
		int *attr, wchar_t *glyph)
{
	enum world_spelunk_view_cell cell =
		world_spelunk_view_cell_at(view, col, row);

	if (!attr || !glyph) return;
	*attr = COLOUR_DARK;
	*glyph = L' ';
	switch (cell) {
	case WORLD_SPELUNK_VIEW_REMEMBERED_AIR:
		*attr = view->remembered_air_attr;
		*glyph = view->remembered_air_glyph;
		break;
	case WORLD_SPELUNK_VIEW_REMEMBERED_ROCK:
		*attr = COLOUR_L_DARK;
		*glyph = L'#';
		break;
	case WORLD_SPELUNK_VIEW_REMEMBERED_WATER:
		*attr = COLOUR_BLUE_SLATE;
		*glyph = L'~';
		break;
	case WORLD_SPELUNK_VIEW_ROCK:
		*attr = COLOUR_SLATE;
		*glyph = L'#';
		break;
	case WORLD_SPELUNK_VIEW_ROCK_CALCITE:
		*attr = COLOUR_L_WHITE;
		*glyph = L'#';
		break;
	case WORLD_SPELUNK_VIEW_ROCK_IRON:
		*attr = COLOUR_L_UMBER;
		*glyph = L'%';
		break;
	case WORLD_SPELUNK_VIEW_ROCK_COPPER:
		*attr = COLOUR_L_GREEN;
		*glyph = L'#';
		break;
	case WORLD_SPELUNK_VIEW_STALACTITE:
		*attr = COLOUR_L_WHITE;
		*glyph = L'v';
		break;
	case WORLD_SPELUNK_VIEW_STALAGMITE:
		*attr = COLOUR_SLATE;
		*glyph = L'^';
		break;
	case WORLD_SPELUNK_VIEW_MATERIAL:
		if (!material_appearance(view, col, row, attr, glyph)) {
			*attr = COLOUR_SLATE;
			*glyph = L'#';
		}
		break;
	case WORLD_SPELUNK_VIEW_DECORATION:
		if (!decoration_appearance(view, col, row, attr, glyph)) {
			*attr = COLOUR_SLATE;
			*glyph = L'#';
		}
		break;
	case WORLD_SPELUNK_VIEW_EXIT:
		*attr = COLOUR_L_GREEN;
		*glyph = L'O';
		break;
	case WORLD_SPELUNK_VIEW_PASSAGE_UP:
		*attr = COLOUR_L_BLUE;
		*glyph = L'<';
		break;
	case WORLD_SPELUNK_VIEW_PASSAGE_DOWN:
		*attr = COLOUR_L_UMBER;
		*glyph = L'>';
		break;
	case WORLD_SPELUNK_VIEW_ROPE:
		*attr = COLOUR_UMBER;
		*glyph = L'|';
		break;
	case WORLD_SPELUNK_VIEW_PITON:
		*attr = COLOUR_YELLOW;
		*glyph = L'\'';
		break;
	case WORLD_SPELUNK_VIEW_OBJECT:
		(void)world_spelunk_view_object_appearance_at(view, col, row,
			attr, glyph);
		break;
	case WORLD_SPELUNK_VIEW_ACTOR:
		(void)world_spelunk_view_actor_appearance_at(view, col, row,
			attr, glyph);
		break;
	case WORLD_SPELUNK_VIEW_GRIP:
		*attr = COLOUR_YELLOW;
		*glyph = L'+';
		break;
	case WORLD_SPELUNK_VIEW_PLAYER:
		*attr = COLOUR_L_BLUE;
		*glyph = L'@';
		break;
	case WORLD_SPELUNK_VIEW_WATER:
		*attr = COLOUR_BLUE;
		*glyph = L'~';
		break;
	case WORLD_SPELUNK_VIEW_PEEK:
		*attr = COLOUR_L_BLUE;
		*glyph = L'X';
		break;
	case WORLD_SPELUNK_VIEW_AIR:
		*attr = view->visible_air_attr;
		*glyph = view->visible_air_glyph;
		break;
	case WORLD_SPELUNK_VIEW_UNKNOWN:
	case WORLD_SPELUNK_VIEW_VOID:
	default:
		break;
	}
}
