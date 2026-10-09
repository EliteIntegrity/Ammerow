/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/spelunking-presenter.c
 * \brief SDL3 play-area adapter for side-view spelunking.
 */

#include "angband.h"

#include "sdl3/spelunking-presenter.h"
#include "map-visual-data.h"
#include "sdl3/tiles.h"
#include "sdl3/zoom.h"
#include "ui-spelunking-appearance.h"
#include "world-spelunking-visibility.h"
#include "world-spelunking-view-terrain.h"

static void tile_layers(const struct world_spelunk_runtime *runtime,
		const struct world_spelunk_view *snapshot, int col, int row,
		struct sdl3_terrain_variant *variant)
{
	struct world_spelunk_view_terrain terrain;
	const struct map_visual_binding *base, *overlay;
	world_spelunk_view_terrain_at(runtime, snapshot, col, row, &terrain);
	base = map_visual_for(terrain.base_scope, terrain.base);
	if (!base && terrain.base_scope && streq(terrain.base_scope, "cave-material")) {
		base = map_visual_for("cave", "rock");
	}
	overlay = map_visual_for(terrain.overlay_scope, terrain.overlay);
	variant->tile_id = base ? base->art_id : NULL;
	variant->tile_overlay_key = overlay ? overlay->key : NULL;
	variant->tile_glyph_policy = terrain.preserve_glyph ||
		(terrain.overlay && !overlay) ? SDL3_TILE_GLYPH_PRESERVE :
		SDL3_TILE_GLYPH_REPLACE;
}

static void tile_subjects(const struct world_spelunk_view *snapshot,
		struct sdl3_map_view *view)
{
	int row, col;
	size_t actor_index = 0, object_index = 0;
	/* Snapshot lists and render subjects are row-major. No runtime occupants
	 * are queried here: hidden and covered subjects never enter the lists. */
	for (row = 0; row < snapshot->rows; row++) {
		for (col = 0; col < snapshot->cols; col++) {
			enum world_spelunk_view_cell cell =
				world_spelunk_view_cell_at(snapshot, col, row);
			struct sdl3_map_art_subject subject = { 0 };
			subject.col = col;
			subject.row = row;
			if (cell == WORLD_SPELUNK_VIEW_ACTOR &&
					actor_index < snapshot->actor_appearance_count) {
				const struct world_spelunk_view_actor_appearance *actor =
					&snapshot->actor_appearances[actor_index++];
				const struct map_visual_binding *binding =
					map_visual_for("cave-actor", actor->actor_id);
				if (!binding) continue;
				subject.glyph = actor->glyph;
				subject.attr = actor->attr;
				subject.priority = SDL3_MAP_ART_MONSTER;
				my_strcpy(subject.tile_key, binding->key, sizeof(subject.tile_key));
			} else if (cell == WORLD_SPELUNK_VIEW_OBJECT &&
					object_index < snapshot->object_appearance_count) {
				const struct world_spelunk_view_object_appearance *object =
					&snapshot->object_appearances[object_index++];
				subject.glyph = object->glyph;
				subject.attr = object->attr;
				subject.priority = SDL3_MAP_ART_ITEM;
				sdl3_tile_make_key(subject.tile_key, sizeof(subject.tile_key),
					SDL3_TILE_OBJECT, object->art_id);
				sdl3_tile_make_key(subject.tile_fallback_key,
					sizeof(subject.tile_fallback_key), SDL3_TILE_OBJECT,
					object->fallback_id);
			} else {
				continue;
			}
			if (view->art_subject_count < SDL3_MAP_ART_CAPACITY &&
					(subject.tile_key[0] || subject.tile_fallback_key[0])) {
				view->art_subjects[view->art_subject_count++] = subject;
			}
		}
	}
}

static void base_terrain_appearance(const struct world_spelunk_view *snapshot,
		int col, int row, int *attr, wchar_t *glyph)
{
	switch (world_spelunk_view_cell_at(snapshot, col, row)) {
	case WORLD_SPELUNK_VIEW_REMEMBERED_AIR:
		*attr = snapshot->visible_air_attr;
		*glyph = snapshot->visible_air_glyph;
		break;
	case WORLD_SPELUNK_VIEW_REMEMBERED_ROCK:
		*attr = COLOUR_SLATE;
		*glyph = L'#';
		break;
	case WORLD_SPELUNK_VIEW_REMEMBERED_WATER:
		*attr = COLOUR_BLUE;
		*glyph = L'~';
		break;
	default:
		spelunking_view_cell_appearance(snapshot, col, row, attr, glyph);
		break;
	}
}

void sdl3_spelunking_presenter_free(
		struct sdl3_spelunking_presenter *presenter)
{
	if (!presenter) return;
	sdl3_terrain_overlay_free(&presenter->terrain);
	sdl3_grid_free(&presenter->map);
	memset(presenter, 0, sizeof(*presenter));
}

bool sdl3_spelunking_presenter_configure(
		struct sdl3_spelunking_presenter *presenter,
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int viewport_col,
		int viewport_row, int viewport_cols, int viewport_rows,
		int zoom_percent, struct sdl3_map_view *view)
{
	return sdl3_spelunking_presenter_configure_focus(presenter, system, runtime,
		viewport_col, viewport_row, viewport_cols, viewport_rows,
		zoom_percent, -1, -1, false, view);
}

bool sdl3_spelunking_presenter_configure_focus(
		struct sdl3_spelunking_presenter *presenter,
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int viewport_col,
		int viewport_row, int viewport_cols, int viewport_rows,
		int zoom_percent, int focus_x, int focus_y, bool cursor_visible,
		struct sdl3_map_view *view)
{
	struct world_spelunk_view snapshot;
	int source_cols;
	int source_rows;
	int col;
	int row;

	if (!presenter || !runtime || !view || viewport_col < 0 ||
			viewport_row < 0 || viewport_cols <= 0 || viewport_rows <= 0 ||
			!world_spelunk_runtime_is_valid(runtime)) {
		return false;
	}
	zoom_percent = sdl3_zoom_clamp(zoom_percent);
	source_cols = MIN(runtime->state.map.width,
		sdl3_zoom_visible_cells(viewport_cols, zoom_percent));
	source_rows = MIN(runtime->state.map.height,
		sdl3_zoom_visible_cells(viewport_rows, zoom_percent));
	if (source_cols <= 0 || source_rows <= 0) return false;
	if (!presenter->map.cells) {
		if (!sdl3_grid_init(&presenter->map, source_cols, source_rows)) {
			return false;
		}
	} else if (!sdl3_grid_resize(&presenter->map, source_cols, source_rows)) {
		return false;
	}
	if (!sdl3_terrain_overlay_resize(&presenter->terrain, source_cols,
			source_rows)) {
		return false;
	}
	sdl3_terrain_overlay_clear(&presenter->terrain);
	if (!((focus_x >= 0 && focus_y >= 0) ?
			world_spelunk_view_capture_at_system(system, runtime,
				focus_x, focus_y,
				source_cols, source_rows, presenter->snapshot,
				N_ELEMENTS(presenter->snapshot), &snapshot) :
			world_spelunk_view_capture_system(system, runtime, source_cols,
				source_rows,
				presenter->snapshot, N_ELEMENTS(presenter->snapshot),
				&snapshot))) {
		return false;
	}
	for (row = 0; row < snapshot.rows; row++) {
		for (col = 0; col < snapshot.cols; col++) {
			struct sdl3_terrain_variant variant;
			enum world_spelunk_view_cell cell =
				world_spelunk_view_cell_at(&snapshot, col, row);
			int map_x = snapshot.source_x + col;
			int map_y = snapshot.source_y + row;
			int attr;
			wchar_t glyph;

			spelunking_view_cell_appearance(&snapshot, col, row, &attr,
				&glyph);
			sdl3_grid_put_cell(&presenter->map, col, row, attr, glyph);
			if (cell == WORLD_SPELUNK_VIEW_UNKNOWN ||
					cell == WORLD_SPELUNK_VIEW_VOID) {
				continue;
			}
			memset(&variant, 0, sizeof(variant));
			base_terrain_appearance(&snapshot, col, row, &attr, &glyph);
			variant.codepoint = glyph;
			variant.foreground = (uint8_t)attr;
			variant.remembered_brightness = (uint8_t)
				((snapshot.remembered_brightness_percent * 255 + 50) / 100);
			variant.in_view =
				world_spelunk_visibility_is_terrain_visible(runtime, map_x,
					map_y);
			variant.present = true;
			variant.active = true;
			tile_layers(runtime, &snapshot, col, row, &variant);
			(void)sdl3_terrain_overlay_set(&presenter->terrain, col, row,
				&variant);
		}
	}
	memset(view, 0, sizeof(*view));
	view->active = true;
	view->expanded_source = true;
	view->col = viewport_col;
	view->row = viewport_row;
	view->cols = viewport_cols;
	view->rows = viewport_rows;
	view->source = &presenter->map;
	view->terrain = &presenter->terrain;
	view->terrain_source_coordinates = true;
	view->source_col = 0;
	view->source_row = 0;
	view->source_cols = snapshot.cols;
	view->source_rows = snapshot.rows;
	view->focus_col = focus_x >= 0 ? focus_x - snapshot.source_x :
		(snapshot.peeking ? snapshot.peek_col : snapshot.player_col);
	view->focus_row = focus_y >= 0 ? focus_y - snapshot.source_y :
		(snapshot.peeking ? snapshot.peek_row : snapshot.player_row);
	view->cursor_col = cursor_visible ? view->focus_col : -1;
	view->cursor_row = cursor_visible ? view->focus_row : -1;
	view->dungeon_col = snapshot.source_x;
	view->dungeon_row = snapshot.source_y;
	view->term_offset_col = snapshot.source_x;
	view->term_offset_row = snapshot.source_y;
	view->tile_width = 1;
	view->tile_height = 1;
	view->zoom_percent = zoom_percent;
	tile_subjects(&snapshot, view);
	return true;
}
