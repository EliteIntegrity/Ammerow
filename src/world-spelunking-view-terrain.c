/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
/** Stable terrain layers use explored cells only, never hidden adjacency. */
#include "angband.h"
#include "world-spelunking-view-terrain.h"
#include "world-spelunking-visibility.h"

static bool known_tile(const struct world_spelunk_runtime *r, int x, int y,
		enum world_spelunk_tile tile)
{
	return world_spelunk_visibility_is_explored(r, x, y) &&
		r->state.map.cells[y * r->state.map.stride + x] == tile;
}

void world_spelunk_view_terrain_at(const struct world_spelunk_runtime *r,
		const struct world_spelunk_view *view, int col, int row,
		struct world_spelunk_view_terrain *out)
{
	enum world_spelunk_view_cell cell, terrain;
	int x, y;
	if (!out) return;
	memset(out, 0, sizeof(*out));
	if (!r || !view) return;
	cell = world_spelunk_view_cell_at(view, col, row);
	if (cell == WORLD_SPELUNK_VIEW_VOID) return;
	x = view->source_x + col;
	y = view->source_y + row;
	out->preserve_glyph = true;
	if (!world_spelunk_visibility_is_explored(r, x, y)) return;
	out->preserve_glyph = cell == WORLD_SPELUNK_VIEW_PLAYER ||
		cell == WORLD_SPELUNK_VIEW_PEEK || cell == WORLD_SPELUNK_VIEW_GRIP ||
		cell == WORLD_SPELUNK_VIEW_OBJECT || cell == WORLD_SPELUNK_VIEW_ACTOR ||
		cell == WORLD_SPELUNK_VIEW_EXIT || cell == WORLD_SPELUNK_VIEW_PASSAGE_UP ||
		cell == WORLD_SPELUNK_VIEW_PASSAGE_DOWN;
	out->base_scope = "cave";
	out->overlay_scope = "cave-decoration";
	if (known_tile(r, x, y, WORLD_SPELUNK_ROCK)) out->base = "rock";
	if (known_tile(r, x, y, WORLD_SPELUNK_WATER)) {
		out->base = known_tile(r, x, y - 1, WORLD_SPELUNK_AIR) ?
			"water-surface" : "water-body";
	}
	/* Generated geology is immutable once explored. Remembering it does not
	 * reveal a new surface or any occupants behind it. */
	if (r->geology_profile_id[0]) {
		const struct world_spelunk_geology_profile *profile =
			world_spelunk_geology_profile_by_id(r->geology_profile_id);
		size_t index = (size_t)y * r->state.map.stride + x;
		if (profile && profile->version == r->geology_version) {
			const struct world_spelunk_material_definition *material =
				world_spelunk_geology_material_by_tag(profile, r->material_tags[index]);
			const struct world_spelunk_decoration_definition *decoration =
				world_spelunk_geology_decoration_by_tag(profile, r->decoration_tags[index]);
			if (out->base && streq(out->base, "rock") && material) {
				out->base_scope = "cave-material";
				out->base = material->id;
			}
			if (decoration) out->overlay = decoration->id;
		}
	}
	/* Untagged legacy formations can still occur in generated open air. */
	if (!out->overlay) {
		terrain = world_spelunk_view_base_cell_at(r, x, y);
		switch (terrain) {
		case WORLD_SPELUNK_VIEW_ROCK_CALCITE: out->overlay = "calcite-seam"; break;
		case WORLD_SPELUNK_VIEW_ROCK_COPPER: out->overlay = "copper-seam"; break;
		case WORLD_SPELUNK_VIEW_ROCK_IRON:
			out->base_scope = "cave-material"; out->base = "iron-stained"; break;
		case WORLD_SPELUNK_VIEW_STALACTITE: out->overlay = "stalactite"; break;
		case WORLD_SPELUNK_VIEW_STALAGMITE: out->overlay = "stalagmite"; break;
		default: break;
		}
	}
	/* Only the winning stable fixture occupies the foreground. Actors/items
	 * cover it; their bitmap is supplied separately by the snapshot. */
	switch (cell) {
	case WORLD_SPELUNK_VIEW_ROPE:
		out->overlay_scope = "cave";
		out->overlay = world_spelunk_visibility_is_explored(r, x, y + 1) &&
			!world_spelunk_runtime_has_rope(r, x, y + 1) ? "rope-end" : "rope-middle";
		break;
	case WORLD_SPELUNK_VIEW_PITON:
		out->overlay_scope = "cave";
		out->overlay = known_tile(r, x - 1, y, WORLD_SPELUNK_ROCK) ? "piton-left" :
			(known_tile(r, x + 1, y, WORLD_SPELUNK_ROCK) ? "piton-right" : NULL);
		if (!out->overlay) out->preserve_glyph = true;
		break;
	case WORLD_SPELUNK_VIEW_EXIT:
		out->overlay_scope = "cave"; out->overlay = "exit"; break;
	case WORLD_SPELUNK_VIEW_PASSAGE_UP:
		out->overlay_scope = "cave"; out->overlay = "passage-up"; break;
	case WORLD_SPELUNK_VIEW_PASSAGE_DOWN:
		out->overlay_scope = "cave"; out->overlay = "passage-down"; break;
	default: break;
	}
}
