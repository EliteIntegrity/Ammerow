/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/map-presenter.c
 * \brief Game-state adapter for the SDL3 play-area presentation.
 */

#include "angband.h"
#include "map-visual-data.h"

#include "game-world.h"
#include "init.h"
#include "mon-predicate.h"
#include "monster.h"
#include "obj-tval.h"
#include "player-timed.h"
#include "player-resource.h"
#include "sdl3/map-presenter.h"
#include "sdl3/inspect-card-presenter.h"
#include "sdl3/monster-art.h"
#include "sdl3/tiles.h"
#include "sdl3/zoom.h"
#include "ui-map.h"
#include "ui-object.h"
#include "ui-output.h"
#include "ui-spelunking.h"
#include "world-turn.h"
#include "world-spelunking-breath.h"
#include "world-spelunking-runtime.h"

enum sdl3_edge_warning sdl3_map_player_warning(const struct player *p,
		bool side_view)
{
	const struct world_spelunk_state *state = side_view && p && p->spelunking ?
		&p->spelunking->state : NULL;
	int air, max_air;
	bool exposed;
	if (!p || p->is_dead || p->mhp <= 0) return SDL3_EDGE_WARNING_NONE;
	/* Near-death HP outranks everything. Otherwise imminent drowning takes
	 * precedence over low HP, then hazardous exertion, then fainting hunger.
	 * Poison retains its explicit status beside HP rather than adding fog. */
	if (p->chp <= p->mhp / 10) return SDL3_EDGE_WARNING_HEALTH;
	if (state && world_spelunk_is_submerged(state) &&
			world_spelunk_player_air_status(p, &air, &max_air) &&
			max_air > 0 && air <= max_air / 4) return SDL3_EDGE_WARNING_AIR;
	if (p->chp <= p->mhp / 4) return SDL3_EDGE_WARNING_HEALTH;
	exposed = state && (state->movement == WORLD_SPELUNK_SWIMMING ||
		((state->movement == WORLD_SPELUNK_CLIMBING ||
		state->movement == WORLD_SPELUNK_HANGING) &&
		!world_spelunk_is_grounded(state)));
	if (exposed && player_stamina_maximum(p) > 0 &&
			player_stamina_current(p) <= player_stamina_maximum(p) / 5)
		return SDL3_EDGE_WARNING_STAMINA;
	if (p->timed[TMD_FOOD] < PY_FOOD_FAINT) return SDL3_EDGE_WARNING_HUNGER;
	return SDL3_EDGE_WARNING_NONE;
}

static void add_map_art_subject(struct sdl3_map_view *view, int col, int row,
		wchar_t glyph, int attr, enum sdl3_map_art_priority priority,
		const char *asset_key, const char *tile_key,
		const char *tile_fallback_key)
{
	struct sdl3_map_art_subject *subject;
	int i;
	int lowest = -1;

	if (!view || ((!asset_key || !asset_key[0]) &&
			(!tile_key || !tile_key[0]) &&
			(!tile_fallback_key || !tile_fallback_key[0]))) return;
	for (i = 0; i < view->art_subject_count; i++) {
		struct sdl3_map_art_subject *existing = &view->art_subjects[i];

		if (existing->col == col && existing->row == row) {
			if (existing->priority >= priority) return;
			subject = existing;
			goto configure;
		}
		if (lowest < 0 || existing->priority <
				view->art_subjects[lowest].priority) {
			lowest = i;
		}
	}
	if (view->art_subject_count < SDL3_MAP_ART_CAPACITY) {
		subject = &view->art_subjects[view->art_subject_count++];
	} else {
		if (lowest < 0 || view->art_subjects[lowest].priority >= priority) {
			return;
		}
		subject = &view->art_subjects[lowest];
	}

configure:
	memset(subject, 0, sizeof(*subject));
	subject->col = col;
	subject->row = row;
	subject->glyph = glyph;
	subject->attr = attr % MAX_COLORS;
	subject->priority = priority;
	my_strcpy(subject->asset_key, asset_key ? asset_key : "",
		sizeof(subject->asset_key));
	my_strcpy(subject->tile_key, tile_key ? tile_key : "",
		sizeof(subject->tile_key));
	my_strcpy(subject->tile_fallback_key,
		tile_fallback_key ? tile_fallback_key : "",
		sizeof(subject->tile_fallback_key));
}

static int source_tile_width(const struct sdl3_map_view *view)
{
	return view->expanded_source ? 1 : SDL_max(1, view->tile_width);
}

static int source_tile_height(const struct sdl3_map_view *view)
{
	return view->expanded_source ? 1 : SDL_max(1, view->tile_height);
}

static bool source_position_for_grid(const struct sdl3_map_view *view,
		struct loc grid, int *col, int *row)
{
	int cell_width;
	int cell_height;

	if (!view || !col || !row) return false;
	cell_width = source_tile_width(view);
	cell_height = source_tile_height(view);
	*col = sdl3_zoom_source_cell_for_grid(grid.x, view->dungeon_col,
		view->source_col, cell_width, view->expanded_source);
	*row = sdl3_zoom_source_cell_for_grid(grid.y, view->dungeon_row,
		view->source_row, cell_height, view->expanded_source);
	return *col >= view->source_col &&
		*col < view->source_col + view->source_cols &&
		*row >= view->source_row &&
		*row < view->source_row + view->source_rows;
}

static struct loc grid_for_source_position(const struct sdl3_map_view *view,
		int col, int row)
{
	return loc(sdl3_zoom_grid_cell_for_source(col, view->source_col,
		view->dungeon_col, source_tile_width(view), view->expanded_source),
		sdl3_zoom_grid_cell_for_source(row, view->source_row,
		view->dungeon_row, source_tile_height(view), view->expanded_source));
}

static void configure_map_features_and_items(struct sdl3_map_view *view)
{
	int col;
	int row;
	int cell_width;
	int cell_height;

	if (!view || !cave || (!view->tile_mode && view->zoom_percent < 400)) {
		return;
	}
	cell_width = source_tile_width(view);
	cell_height = source_tile_height(view);
	for (row = view->source_row; row < view->source_row + view->source_rows;
			row += cell_height) {
		for (col = view->source_col; col < view->source_col + view->source_cols;
				col += cell_width) {
			struct grid_data data;
			struct loc grid = grid_for_source_position(view, col, row);
			char key[SDL3_ASCII_ART_KEY_CAPACITY] = "";
			char tile_key[SDL3_TILE_KEY_CAPACITY] = "";
			char tile_fallback[SDL3_TILE_KEY_CAPACITY] = "";
			int attr;
			int terrain_attr;
			wchar_t glyph;
			wchar_t terrain_glyph;

			if (!square_in_bounds(cave, grid)) continue;
			map_info(grid, &data);
			if (data.hallucinate || data.is_player) continue;
			grid_data_as_text(&data, &attr, &glyph, &terrain_attr,
				&terrain_glyph);
			if (data.first_kind && !data.multiple_objects &&
					!data.unseen_object && !data.unseen_money &&
					glyph == object_kind_char(data.first_kind) &&
					attr % MAX_COLORS ==
						object_kind_attr(data.first_kind) % MAX_COLORS) {
				struct object_kind *kind = data.first_kind;

				if (view->zoom_percent >= 400) {
					sdl3_ascii_art_make_item_key(key, sizeof(key), kind->art_id,
						kind->base ? kind->base->unaware_art_id : NULL,
						kind->flavor != NULL, kind->aware);
				}
				if (view->tile_mode) {
					const char *visible_art_id = kind->flavor && !kind->aware &&
						kind->base ? kind->base->unaware_art_id : kind->art_id;

					if (visible_art_id) {
						sdl3_tile_make_key(tile_key, sizeof(tile_key),
							SDL3_TILE_OBJECT, visible_art_id);
					}
					sdl3_tile_make_key(tile_fallback, sizeof(tile_fallback),
						SDL3_TILE_OBJECT, tval_find_name(kind->tval));
				}
				add_map_art_subject(view, col, row, glyph, attr,
					SDL3_MAP_ART_ITEM, key, tile_key, tile_fallback);
				continue;
			}
			if (view->tile_mode && data.first_kind && data.multiple_objects &&
					!data.unseen_object && !data.unseen_money && pile_kind &&
					glyph == object_kind_char(pile_kind) &&
					attr % MAX_COLORS == object_kind_attr(pile_kind) % MAX_COLORS) {
				const struct map_visual_binding *pile =
					map_visual_for("map", "visible-pile");
				if (pile) add_map_art_subject(view, col, row, glyph, attr,
					SDL3_MAP_ART_ITEM, NULL, pile->key, NULL);
			}
			if (data.first_kind || data.unseen_object || data.unseen_money ||
					data.multiple_objects) {
				continue;
			}
			if (view->zoom_percent >= 800 && data.f_idx > FEAT_NONE &&
					data.f_idx < FEAT_MAX && f_info[data.f_idx].art_id &&
					glyph == terrain_glyph && attr == terrain_attr) {
				sdl3_ascii_art_make_key(key, sizeof(key),
					SDL3_ASCII_ART_TERRAIN, f_info[data.f_idx].art_id, NULL);
				add_map_art_subject(view, col, row, glyph, attr,
					SDL3_MAP_ART_TERRAIN, key, NULL, NULL);
			}
		}
	}
}

static void configure_map_monsters(struct sdl3_map_view *view,
		term *main_term)
{
	int i;

	if (!view || !main_term || !cave || !player ||
			player->timed[TMD_IMAGE] || (!view->tile_mode &&
			!sdl3_monster_art_map_enabled(view->zoom_percent))) {
		return;
	}
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		int col;
		int row;
		const struct sdl3_cell *cell;

		/* Lethal-hit messages can render before the core removes the monster.
		 * Keep the same art for as long as its map occupant still exists; HP is
		 * not a presentation-lifetime boundary (map_info also keeps its glyph). */
		if (!mon || !mon->race ||
				!monster_is_obvious(mon) || monster_is_mimicking(mon) ||
				rf_has(mon->race->flags, RF_ATTR_CLEAR) ||
				rf_has(mon->race->flags, RF_CHAR_CLEAR)) {
			continue;
		}
		if (!source_position_for_grid(view, mon->grid, &col, &row)) continue;
		cell = sdl3_grid_cell(view->source, col, row);
		if (!cell) continue;
		{
			char key[SDL3_ASCII_ART_KEY_CAPACITY] = "";
			char tile_key[SDL3_TILE_KEY_CAPACITY] = "";

			if (sdl3_monster_art_map_enabled(view->zoom_percent)) {
				sdl3_ascii_art_make_key(key, sizeof(key),
					SDL3_ASCII_ART_MONSTER, mon->race->art_id,
					mon->race->base ? mon->race->base->art_id : NULL);
			}
			if (view->tile_mode) {
				sdl3_tile_make_key(tile_key, sizeof(tile_key),
					SDL3_TILE_MONSTER, mon->race->art_id);
			}
			add_map_art_subject(view, col, row, cell->codepoint,
				cell->foreground, SDL3_MAP_ART_MONSTER, key, tile_key, NULL);
		}
	}
}

static int compare_map_art_subjects(const void *first, const void *second)
{
	const struct sdl3_map_art_subject *a = first;
	const struct sdl3_map_art_subject *b = second;

	if (a->row != b->row) return a->row < b->row ? -1 : 1;
	if (a->col != b->col) return a->col < b->col ? -1 : 1;
	return 0;
}

static bool cursor_dungeon_location(
		const struct sdl3_map_presenter_options *options,
		const struct sdl3_map_view *view, const term *main_term, int *col,
		int *row)
{
	if (!options || !view || !main_term || !col || !row ||
			options->cursor_col < view->term_col ||
			options->cursor_col >= view->term_col + view->term_cols ||
			options->cursor_row < view->term_row ||
			options->cursor_row >= view->term_row + view->term_rows) {
		return false;
	}
	*col = main_term->offset_x +
		(options->cursor_col - view->term_col) / tile_width;
	*row = main_term->offset_y +
		(options->cursor_row - view->term_row) / tile_height;
	return true;
}

static bool populate_expanded_map(struct sdl3_map_presenter *presenter,
		struct sdl3_grid *grid, struct sdl3_map_view *view,
		const term *main_term, int focus_col, int focus_row,
		bool cursor_available, int cursor_col, int cursor_row,
		enum sdl3_terrain_style terrain_style)
{
	int cols;
	int rows;
	int first_col;
	int first_row;
	int col;
	int row;
	bool resized;
	bool refresh;
	bool terrain_ready;

	if (!presenter || !grid || !view || !main_term || !cave ||
			cave->width <= 0 || cave->height <= 0) {
		return false;
	}
	cols = MIN(cave->width,
		sdl3_zoom_visible_cells(view->cols, view->zoom_percent));
	rows = MIN(cave->height,
		sdl3_zoom_visible_cells(view->rows, view->zoom_percent));
	if (cols <= 0 || rows <= 0) return false;
	resized = !presenter->expanded_map.cells ||
		presenter->expanded_map.cols != cols ||
		presenter->expanded_map.rows != rows;
	if (!presenter->expanded_map.cells) {
		if (!sdl3_grid_init(&presenter->expanded_map, cols, rows)) return false;
	} else if (!sdl3_grid_resize(&presenter->expanded_map, cols, rows)) {
		return false;
	}
	terrain_ready = sdl3_terrain_overlay_resize(
		&presenter->expanded_terrain_overlay, cols, rows);
	first_col = sdl3_zoom_centered_start(focus_col, cols, cave->width);
	first_row = sdl3_zoom_centered_start(focus_row, rows, cave->height);
	refresh = resized || !presenter->expanded_positioned ||
		presenter->expanded_cave != cave || presenter->expanded_turn != turn ||
		first_col != presenter->expanded_dungeon_col ||
		first_row != presenter->expanded_dungeon_row;
	if (!refresh) {
		for (row = view->term_row;
				row < view->term_row + view->term_rows && !refresh;
				row++) {
			for (col = view->term_col;
					col < view->term_col + view->term_cols; col++) {
				const struct sdl3_cell *cell =
					sdl3_grid_cell(grid, col, row);

				if (cell && cell->dirty) {
					refresh = true;
					break;
				}
			}
		}
	}
	if (refresh) {
		for (row = 0; row < rows; row++) {
			for (col = 0; col < cols; col++) {
				struct grid_data data;
				int attr;
				int terrain_attr;
				wchar_t glyph;
				wchar_t terrain_glyph;
				struct sdl3_terrain_variant variant;
				const struct sdl3_terrain_variant *replacement = NULL;
				const struct terrain_visual_recipe *recipe;

				map_info(loc(first_col + col, first_row + row), &data);
				grid_data_as_text(&data, &attr, &glyph, &terrain_attr,
					&terrain_glyph);
				recipe = sdl3_terrain_recipe_for_grid(cave,
					loc(first_col + col, first_row + row), data.f_idx);
				memset(&variant, 0, sizeof(variant));
				if (terrain_style != SDL3_TERRAIN_CLASSIC &&
						!data.hallucinate &&
						attr == terrain_attr && glyph == terrain_glyph) {
					sdl3_terrain_variant(recipe, data.lighting,
							terrain_glyph, terrain_attr % MAX_COLORS,
							terrain_style, &variant);
				}
				if (data.f_idx != FEAT_NONE) {
					variant.codepoint = terrain_glyph;
					variant.foreground = terrain_attr % MAX_COLORS;
					variant.lighting = (uint8_t)data.lighting;
					variant.style = (uint8_t)terrain_style;
					variant.in_view = data.in_view;
					variant.tile_id = recipe && recipe->tile_id ? recipe->tile_id :
						(data.f_idx > FEAT_NONE && data.f_idx < FEAT_MAX ?
							f_info[data.f_idx].art_id : NULL);
					variant.present = true;
					replacement = &variant;
				}
				sdl3_grid_put_cell(&presenter->expanded_map, col, row, attr,
					glyph);
				if (terrain_ready && sdl3_terrain_overlay_set(
						&presenter->expanded_terrain_overlay, col, row,
						replacement)) {
					presenter->expanded_map.cells[row * cols + col].dirty = true;
					sdl3_grid_mark_dirty(&presenter->expanded_map);
				}
			}
		}
	}
	presenter->expanded_dungeon_col = first_col;
	presenter->expanded_dungeon_row = first_row;
	presenter->expanded_turn = turn;
	presenter->expanded_cave = cave;
	presenter->expanded_positioned = true;
	view->expanded_source = true;
	view->source = &presenter->expanded_map;
	view->terrain = terrain_ready ?
		&presenter->expanded_terrain_overlay : NULL;
	view->terrain_source_coordinates = true;
	view->source_col = 0;
	view->source_row = 0;
	view->source_cols = cols;
	view->source_rows = rows;
	view->focus_col = focus_col - first_col;
	view->focus_row = focus_row - first_row;
	view->cursor_col = cursor_available ? cursor_col - first_col : -1;
	view->cursor_row = cursor_available ? cursor_row - first_row : -1;
	view->dungeon_col = first_col;
	view->dungeon_row = first_row;
	view->term_offset_col = main_term->offset_x;
	view->term_offset_row = main_term->offset_y;
	view->tile_width = tile_width;
	view->tile_height = tile_height;
	return true;
}

static void configure_terrain_overlay(struct sdl3_map_presenter *presenter,
		struct sdl3_grid *grid, struct sdl3_map_view *view,
		const term *main_term, enum sdl3_terrain_style terrain_style)
{
	uint32_t level_key;
	bool force;
	int col;
	int row;

	if (!presenter || !grid || !view || !main_term || !cave) {
		if (view) view->terrain = NULL;
		return;
	}
	/* An expanded source supplies dedicated source-coordinate metadata. */
	if (view->expanded_source) return;
	if (!sdl3_terrain_overlay_resize(&presenter->terrain_overlay,
			grid->cols, grid->rows)) {
		view->terrain = NULL;
		return;
	}
	level_key = sdl3_terrain_level_key(cave->turn, cave->depth, cave->width,
		cave->height);
	force = !presenter->terrain_positioned ||
		presenter->terrain_cave != cave ||
		presenter->terrain_level_key != level_key ||
		presenter->terrain_dungeon_col != main_term->offset_x ||
		presenter->terrain_dungeon_row != main_term->offset_y;
	if (force) {
		sdl3_terrain_overlay_clear(&presenter->terrain_overlay);
		sdl3_grid_mark_all_dirty(grid);
	}
	for (row = view->row; row < view->row + view->rows; row++) {
		for (col = view->col; col < view->col + view->cols; col++) {
			const struct sdl3_cell *cell = sdl3_grid_cell(grid, col, row);
			struct grid_data data;
			struct sdl3_terrain_variant variant;
			const struct sdl3_terrain_variant *replacement = NULL;
			const struct terrain_visual_recipe *recipe;
			int attr;
			int terrain_attr;
			wchar_t glyph;
			wchar_t terrain_glyph;
			int dungeon_col;
			int dungeon_row;

			if (!cell || (!force && !cell->dirty)) continue;
			dungeon_col = main_term->offset_x +
				(col - view->col) / tile_width;
			dungeon_row = main_term->offset_y +
				(row - view->row) / tile_height;
			if (dungeon_col < 0 || dungeon_col >= cave->width ||
					dungeon_row < 0 || dungeon_row >= cave->height) {
				sdl3_terrain_overlay_set(&presenter->terrain_overlay, col,
					row, NULL);
				continue;
			}
			map_info(loc(dungeon_col, dungeon_row), &data);
			grid_data_as_text(&data, &attr, &glyph, &terrain_attr,
				&terrain_glyph);
			recipe = sdl3_terrain_recipe_for_grid(cave,
				loc(dungeon_col, dungeon_row), data.f_idx);
			memset(&variant, 0, sizeof(variant));
			/* Match the rendered terminal cell as well as map_info(). */
			if (!data.hallucinate && attr == terrain_attr &&
					glyph == terrain_glyph && cell->codepoint == glyph &&
					cell->foreground == attr % MAX_COLORS) {
				sdl3_terrain_variant(recipe, data.lighting, terrain_glyph,
						terrain_attr % MAX_COLORS, terrain_style, &variant);
			}
			if (data.f_idx != FEAT_NONE) {
				variant.codepoint = terrain_glyph;
				variant.foreground = terrain_attr % MAX_COLORS;
				variant.lighting = (uint8_t)data.lighting;
				variant.style = (uint8_t)terrain_style;
				variant.in_view = data.in_view;
				variant.tile_id = recipe && recipe->tile_id ? recipe->tile_id :
					(data.f_idx > FEAT_NONE && data.f_idx < FEAT_MAX ?
						f_info[data.f_idx].art_id : NULL);
				variant.present = true;
				replacement = &variant;
			}
			sdl3_terrain_overlay_set(&presenter->terrain_overlay, col, row,
				replacement);
		}
	}
	presenter->terrain_cave = cave;
	presenter->terrain_level_key = level_key;
	presenter->terrain_dungeon_col = main_term->offset_x;
	presenter->terrain_dungeon_row = main_term->offset_y;
	presenter->terrain_positioned = true;
	view->terrain = &presenter->terrain_overlay;
	view->terrain_source_coordinates = false;
}

void sdl3_map_presenter_free(struct sdl3_map_presenter *presenter)
{
	if (!presenter) return;
	sdl3_grid_free(&presenter->expanded_map);
	sdl3_spelunking_presenter_free(&presenter->spelunking);
	sdl3_terrain_overlay_free(&presenter->terrain_overlay);
	sdl3_terrain_overlay_free(&presenter->expanded_terrain_overlay);
	memset(presenter, 0, sizeof(*presenter));
}

void sdl3_map_presenter_reset(struct sdl3_map_presenter *presenter)
{
	if (!presenter) return;
	memset(&presenter->view, 0, sizeof(presenter->view));
	memset(&presenter->inspect_card, 0, sizeof(presenter->inspect_card));
	presenter->expanded_cave = NULL;
	presenter->expanded_positioned = false;
	presenter->terrain_cave = NULL;
	presenter->terrain_positioned = false;
}

void sdl3_map_presenter_invalidate_layout(
		struct sdl3_map_presenter *presenter)
{
	if (!presenter) return;
	presenter->expanded_positioned = false;
	presenter->terrain_positioned = false;
}

void sdl3_map_presenter_invalidate_terrain(
		struct sdl3_map_presenter *presenter)
{
	if (!presenter) return;
	presenter->terrain_positioned = false;
	presenter->expanded_positioned = false;
	sdl3_terrain_overlay_clear(&presenter->terrain_overlay);
}

void sdl3_map_presenter_configure(struct sdl3_map_presenter *presenter,
		struct sdl3_grid *grid, const struct sdl3_pane_layout *pane,
		term *main_term, const struct sdl3_map_presenter_options *options)
{
	struct sdl3_map_view *view;
	int sidebar;
	int cursor_dungeon_col = -1;
	int cursor_dungeon_row = -1;
	int focus_dungeon_col;
	int focus_dungeon_row;
	bool cursor_available;
	bool cursor_focus;
	bool side_view = false;
	bool side_view_look = false;
	int side_view_look_x = -1;
	int side_view_look_y = -1;

	if (!presenter || !grid || !options) return;
	view = &presenter->view;
	memset(view, 0, sizeof(*view));
	view->zoom_percent = options->zoom_percent;
	view->tile_mode = options->tile_mode;
	if (!main_term || !pane) goto finish;
	sidebar = main_term->sidebar_mode;
	if (sidebar < 0 || sidebar >= SIDEBAR_MAX) goto finish;
	/* Side-view gameplay owns a semantic, knowledge-safe snapshot rather than
	 * Angband's inert top-down save envelope.  Feed that snapshot through the
	 * same crisp map camera, reserving a separate semantic status strip. */
	side_view = character_dungeon && textui_map_is_visible() && player &&
		player->spelunking && world_player_mode_has_capability(player,
			WORLD_MODE_CAP_SIDE_VIEW);
	if (side_view) {
		int term_col = pane->col + col_map[sidebar];
		int term_row = pane->row + row_top_map[sidebar];
		int term_cols = main_term->wid - col_map[sidebar];
		int status_row = sidebar == SIDEBAR_TOP ? 3 : main_term->hgt - 1;
		int term_rows = sidebar == SIDEBAR_TOP ?
			main_term->hgt - row_top_map[sidebar] :
			status_row - row_top_map[sidebar];

		side_view_look = textui_spelunking_look_location(player,
			&side_view_look_x, &side_view_look_y);
		if (sdl3_spelunking_presenter_configure_focus(&presenter->spelunking,
				player->spelunking_system, player->spelunking,
				pane->col, pane->row, pane->cols,
				MAX(1, pane->rows - SDL3_LAYOUT_CAVE_STATUS_ROWS),
				options->zoom_percent, side_view_look_x,
				side_view_look_y, side_view_look, view)) {
			view->tile_mode = options->tile_mode;
			view->term_col = term_col;
			view->term_row = term_row;
			view->term_cols = term_cols;
			view->term_rows = term_rows;
			view->sidebar_mode = sidebar;
			if (textui_spelunking_status(player, &view->cave_status))
				view->status_rows = SDL3_LAYOUT_CAVE_STATUS_ROWS;
			if (side_view_look) {
				int look_origin_col;
				int look_origin_row;
				int look_cols;
				int look_rows;
				int look_source_x;
				int look_source_y;

				/* SDL may show more or fewer semantic cells because of zoom.  Mouse
				 * events still address the authoritative terminal controller, so
				 * preserve that controller's source origin for exact translation. */
				if (textui_spelunking_look_view(player, &look_origin_col,
						&look_origin_row, &look_cols, &look_rows,
						&look_source_x, &look_source_y)) {
					view->term_col = pane->col + look_origin_col;
					view->term_row = pane->row + look_origin_row;
					view->term_cols = look_cols;
					view->term_rows = look_rows;
					view->term_offset_col = look_source_x;
					view->term_offset_row = look_source_y;
				}
			}
		}
		if (view->active && options->verbose &&
				view->zoom_percent != presenter->logged_zoom_percent) {
			SDL_Log("Side-view presentation: viewport %d,%d %dx%d; source "
				"%dx%d; focus %d,%d", view->col, view->row, view->cols,
				view->rows, view->source_cols, view->source_rows,
				view->focus_col, view->focus_row);
			presenter->logged_zoom_percent = view->zoom_percent;
		}
		goto finish;
	}
	view->term_col = pane->col + col_map[sidebar];
	view->term_row = pane->row + row_top_map[sidebar];
	view->term_cols = (main_term->wid - col_map[sidebar] - 1) / tile_width;
	view->term_rows = (main_term->hgt - row_top_map[sidebar] -
		row_bottom_map[sidebar]) / tile_height;
	view->sidebar_mode = sidebar;
	view->col = pane->col;
	view->row = pane->row;
	view->cols = pane->cols;
	view->rows = pane->rows;
	view->source = grid;
	view->source_col = view->term_col;
	view->source_row = view->term_row;
	view->source_cols = view->term_cols;
	view->source_rows = view->term_rows;
	view->dungeon_col = main_term->offset_x;
	view->dungeon_row = main_term->offset_y;
	view->term_offset_col = main_term->offset_x;
	view->term_offset_row = main_term->offset_y;
	main_term->panel_margin_x = sdl3_zoom_panel_margin(view->term_cols,
		view->zoom_percent);
	main_term->panel_margin_y = sdl3_zoom_panel_margin(view->term_rows,
		view->zoom_percent);
	view->focus_col = view->term_col + view->term_cols / 2;
	view->focus_row = view->term_row + view->term_rows / 2;
	view->cursor_col = options->cursor_col;
	view->cursor_row = options->cursor_row;
	/* Non-top-down modes own their terminal presentation.  Leaving this false
	 * makes SDL3 render that native terminal grid directly and preserves it as
	 * the fallback instead of interpreting the inert save envelope as a map. */
	view->active = character_dungeon && textui_map_is_visible() &&
		world_player_mode_has_capability(player,
			WORLD_MODE_CAP_NATIVE_CAVE);
	if (!view->active) goto finish;
	cursor_available = cursor_dungeon_location(options, view, main_term,
		&cursor_dungeon_col, &cursor_dungeon_row);
	/* Only the modal Look/aim controller may move focus away from the player.
	 * A retained combat target may keep its cursor and tracking information,
	 * but neither the visible marker nor a tracked subject owns the camera. */
	cursor_focus = cursor_available && options->inspection_visible;
	if (cursor_focus) {
		view->focus_col = options->cursor_col;
		view->focus_row = options->cursor_row;
	} else if (player) {
		int col = view->term_col + player->grid.x - main_term->offset_x;
		int row = view->term_row + player->grid.y - main_term->offset_y;

		if (col >= view->term_col &&
				col < view->term_col + view->term_cols &&
				row >= view->term_row &&
				row < view->term_row + view->term_rows) {
			view->focus_col = col;
			view->focus_row = row;
		}
	}
	focus_dungeon_col = cursor_focus ? cursor_dungeon_col :
		(player ? player->grid.x : main_term->offset_x + view->term_cols / 2);
	focus_dungeon_row = cursor_focus ? cursor_dungeon_row :
		(player ? player->grid.y : main_term->offset_y + view->term_rows / 2);
	if (!populate_expanded_map(presenter, grid, view, main_term,
				focus_dungeon_col, focus_dungeon_row, cursor_available,
				cursor_dungeon_col, cursor_dungeon_row,
				options->terrain_style)) {
		/* Allocation failure must leave the authoritative map readable. */
		view->zoom_percent = SDL3_ZOOM_DEFAULT;
		view->col = view->term_col;
		view->row = view->term_row;
		view->cols = view->term_cols;
		view->rows = view->term_rows;
		view->source = grid;
		view->source_col = view->term_col;
		view->source_row = view->term_row;
		view->source_cols = view->term_cols;
		view->source_rows = view->term_rows;
	}
	configure_terrain_overlay(presenter, grid, view, main_term,
		options->terrain_style);
	if (options->verbose &&
			view->zoom_percent != presenter->logged_zoom_percent) {
		SDL_Log("Map presentation: viewport %d,%d %dx%d; source %d,%d "
			"%dx%d; focus %d,%d; %s", view->col, view->row, view->cols,
			view->rows, view->source_col, view->source_row,
			view->source_cols, view->source_rows, view->focus_col,
			view->focus_row, view->expanded_source ? "expanded" : "terminal");
		presenter->logged_zoom_percent = view->zoom_percent;
	}
	configure_map_features_and_items(view);
	configure_map_monsters(view, main_term);
	if (view->art_subject_count > 1) {
		qsort(view->art_subjects, view->art_subject_count,
			sizeof(view->art_subjects[0]), compare_map_art_subjects);
	}
	if (options->animated_combat && player && player->mhp > 0) {
		view->player_vitality_visible = true;
		view->player_dungeon_col = player->grid.x;
		view->player_dungeon_row = player->grid.y;
		view->player_hp = player->chp;
		view->player_max_hp = player->mhp;
		view->player_warning_hp = player->mhp *
			player->opts.hitpoint_warn / 10;
	}

finish:
	if (view->active && options->edge_warnings && !options->settings_visible)
		view->edge_warning = sdl3_map_player_warning(player, side_view);
	view->hud_insets = sdl3_layout_hud_insets(view->sidebar_mode,
		view->term_col, view->term_row, options->hud_stats_visible,
		options->messages_visible, options->message_placement,
		options->message_rows);
	if (side_view) {
		/* Side-view subjects resolve through their semantic visibility owner,
		 * never through the inert compatibility cave envelope. */
		sdl3_inspect_card_configure_spelunking(&presenter->inspect_card,
			player ? player->spelunking_system : NULL,
			player ? player->spelunking : NULL, view, side_view_look_x,
			side_view_look_y, side_view_look, options->settings_visible,
			options->big_stat_cards);
	} else {
		sdl3_inspect_card_configure(&presenter->inspect_card, view, main_term,
			options->cursor_col, options->cursor_row,
			options->cursor_visible && options->inspection_visible,
			options->settings_visible, options->big_stat_cards);
	}
}
