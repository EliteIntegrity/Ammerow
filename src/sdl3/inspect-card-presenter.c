/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/inspect-card-presenter.c
 * \brief Knowledge-safe subject selection for SDL3 examine cards.
 */

#include "angband.h"

#include "cave.h"
#include "obj-desc.h"
#include "obj-knowledge.h"
#include "obj-tval.h"
#include "object.h"
#include "player.h"
#include "player-timed.h"
#include "trap.h"
#include "sdl3/ascii-art.h"
#include "sdl3/inspect-card-presenter.h"
#include "sdl3/map-view.h"
#include "sdl3/monster-card-presenter.h"
#include "ui-map.h"
#include "ui-object.h"
#include "ui-term.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-view.h"

static bool cursor_grid(const struct sdl3_map_view *view,
		const struct term *main_term, int cursor_col, int cursor_row,
		struct loc *grid)
{
	if (!view || !main_term || !grid || cursor_col < view->term_col ||
			cursor_col >= view->term_col + view->term_cols ||
			cursor_row < view->term_row ||
			cursor_row >= view->term_row + view->term_rows) {
		return false;
	}
	grid->x = main_term->offset_x +
		(cursor_col - view->term_col) / tile_width;
	grid->y = main_term->offset_y +
		(cursor_row - view->term_row) / tile_height;
	return true;
}

static struct object *tracked_object_at_cursor(
		const struct sdl3_map_view *view, struct term *main_term,
		int cursor_col, int cursor_row)
{
	struct object *obj;
	struct loc grid;

	if (!player || !player->upkeep || player->timed[TMD_IMAGE] ||
			!cursor_grid(view, main_term, cursor_col, cursor_row, &grid)) {
		return NULL;
	}
	obj = player->upkeep->object;
	if (!obj || !obj->kind || !loc_eq(obj->grid, grid)) return NULL;
	return obj;
}

static void configure_item_object(struct sdl3_inspect_card *card,
		const struct object *obj, int cursor_col, int cursor_row, bool big)
{
	bool flavored;
	bool aware;

	if (!obj) return;
	flavored = obj->kind->flavor != NULL;
	aware = object_flavor_is_aware(obj);
	card->active = true;
	card->big = big;
	card->glyph = object_char(obj);
	card->attr = object_attr(obj);
	card->cursor_col = cursor_col;
	card->cursor_row = cursor_row;
	sdl3_ascii_art_make_item_key(card->asset_key, sizeof(card->asset_key),
		obj->kind->art_id, obj->kind->base ?
			obj->kind->base->unaware_art_id : NULL, flavored, aware);
	object_desc(card->name, sizeof(card->name), obj,
		ODESC_PREFIX | ODESC_FULL | ODESC_CAPITAL, player);
	object_base_name(card->subtitle, sizeof(card->subtitle), obj->tval,
		false);
	if (flavored && !aware) {
		my_strcpy(card->status, "Identity: unknown", sizeof(card->status));
	} else if (object_fully_known(obj)) {
		my_strcpy(card->status, "Identity: fully known", sizeof(card->status));
	} else {
		my_strcpy(card->status, "Identity: partly known", sizeof(card->status));
	}
	strnfmt(card->details[0], sizeof(card->details[0]), "Weight: %d.%d lb",
		obj->weight / 10, ABS(obj->weight % 10));
	if (obj->number > 1) {
		strnfmt(card->details[1], sizeof(card->details[1]),
			"Stack: %d items", obj->number);
	}
	if (!flavored || aware) {
		my_strcpy(card->flavor, obj->kind->text ? obj->kind->text : "",
			sizeof(card->flavor));
	}
}

static void configure_item(struct sdl3_inspect_card *card,
		const struct sdl3_map_view *view, struct term *main_term,
		int cursor_col, int cursor_row, bool big)
{
	configure_item_object(card, tracked_object_at_cursor(view, main_term,
		cursor_col, cursor_row), cursor_col, cursor_row, big);
}

static void configure_terrain(struct sdl3_inspect_card *card,
		const struct sdl3_map_view *view, struct term *main_term,
		int cursor_col, int cursor_row, bool big)
{
	struct grid_data data;
	struct feature *feature;
	struct trap *trap;
	struct loc grid;
	int attr;
	int terrain_attr;
	wchar_t glyph;
	wchar_t terrain_glyph;

	if (!cave || !player || player->timed[TMD_IMAGE] ||
			!cursor_grid(view, main_term, cursor_col, cursor_row,
			&grid)) return;
	map_info(grid, &data);
	if (data.hallucinate || data.f_idx <= FEAT_NONE ||
			data.f_idx >= FEAT_MAX) return;
	/* Inspect remembered traps, including temporarily disabled ones which
	 * map_info omits from drawing. Never consult undiscovered actual traps. */
	trap = square_isknown(cave, grid) ? square_trap(player->cave, grid) : NULL;
	while (trap && (!trap->kind ||
			!(trf_has(trap->flags, TRF_TRAP) || trf_has(trap->flags, TRF_GLYPH) ||
			  trf_has(trap->flags, TRF_WEB)) ||
			!(trf_has(trap->flags, TRF_VISIBLE) || trf_has(trap->flags, TRF_GLYPH) ||
			  trf_has(trap->flags, TRF_WEB)))) trap = trap->next;
	if (trap) {
		const struct trap_kind *kind = trap->kind;
		card->active = true;
		card->big = big;
		card->glyph = kind->d_char;
		card->attr = kind->d_attr;
		card->cursor_col = cursor_col;
		card->cursor_row = cursor_row;
		my_strcpy(card->name, kind->desc ? kind->desc : kind->name,
			sizeof(card->name));
		my_strcpy(card->subtitle, "Trap", sizeof(card->subtitle));
		my_strcpy(card->status, trap->timeout ? "Temporarily disabled" :
			"Known trap", sizeof(card->status));
		my_strcpy(card->flavor, kind->text ? kind->text : "",
			sizeof(card->flavor));
		return;
	}
	feature = &f_info[data.f_idx];
	if (!feature->art_id) return;
	grid_data_as_text(&data, &attr, &glyph, &terrain_attr, &terrain_glyph);
	card->active = true;
	card->big = big;
	card->glyph = terrain_glyph;
	card->attr = terrain_attr % MAX_COLORS;
	card->cursor_col = cursor_col;
	card->cursor_row = cursor_row;
	sdl3_ascii_art_make_key(card->asset_key, sizeof(card->asset_key),
		SDL3_ASCII_ART_TERRAIN, feature->art_id, NULL);
	my_strcpy(card->name, square_apparent_name(player->cave, grid),
		sizeof(card->name));
	my_strcpy(card->subtitle, "Terrain", sizeof(card->subtitle));
	if (tf_has(feature->flags, TF_PERMANENT)) {
		my_strcpy(card->status, "Impenetrable barrier",
			sizeof(card->status));
	} else if (tf_has(feature->flags, TF_ROCK)) {
		my_strcpy(card->status, "Diggable rock", sizeof(card->status));
	} else if (tf_has(feature->flags, TF_PASSABLE)) {
		my_strcpy(card->status, tf_has(feature->flags, TF_LOS) ?
			"Open terrain" : "Walkable; blocks sight and missiles",
			sizeof(card->status));
	} else {
		my_strcpy(card->status, "Obstructed terrain", sizeof(card->status));
	}
	if (feature->dig > 0) {
		strnfmt(card->details[0], sizeof(card->details[0]),
			"Digging difficulty: %u", (unsigned)feature->dig);
	}
	if (tf_has(feature->flags, TF_GOLD)) {
		my_strcpy(card->details[1], "Contains visible treasure",
			sizeof(card->details[1]));
	}
	my_strcpy(card->flavor, feature->desc ? feature->desc : "",
		sizeof(card->flavor));
}

bool sdl3_inspect_card_cursor_tracks(const struct sdl3_map_view *view,
		struct term *main_term, int cursor_col, int cursor_row)
{
	return sdl3_monster_card_cursor_tracks(view, main_term, cursor_col,
		cursor_row) || tracked_object_at_cursor(view, main_term, cursor_col,
		cursor_row) != NULL;
}

void sdl3_inspect_card_configure(struct sdl3_inspect_card *card,
		const struct sdl3_map_view *view, struct term *main_term,
		int cursor_col, int cursor_row, bool cursor_visible,
		bool settings_visible, bool big)
{
	if (!card) return;
	memset(card, 0, sizeof(*card));
	if (!view || !view->active || !main_term || !cursor_visible ||
			settings_visible) return;
	sdl3_monster_card_configure(card, view, main_term, cursor_col, cursor_row,
		cursor_visible, settings_visible, big);
	if (card->active) return;
	configure_item(card, view, main_term, cursor_col, cursor_row, big);
	if (card->active) return;
	configure_terrain(card, view, main_term, cursor_col, cursor_row, big);
}

void sdl3_inspect_card_configure_spelunking(
		struct sdl3_inspect_card *card,
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime,
		const struct sdl3_map_view *view, int x, int y,
		bool cursor_visible, bool settings_visible, bool big)
{
	struct world_spelunk_inspect inspect;
	int cursor_col;
	int cursor_row;

	if (!card) return;
	memset(card, 0, sizeof(*card));
	if (!runtime || !view || !view->active || !cursor_visible ||
			settings_visible || !world_spelunk_view_inspect_system(system,
				runtime, x, y, &inspect)) {
		return;
	}
	/* Card placement only needs to know which half of the physical play area
	 * contains the semantic cursor. */
	cursor_col = view->col +
		(view->focus_col * view->cols) / MAX(1, view->source_cols);
	cursor_row = view->row +
		(view->focus_row * view->rows) / MAX(1, view->source_rows);
	if (inspect.kind == WORLD_SPELUNK_INSPECT_OBJECT && inspect.object) {
		configure_item_object(card, inspect.object, cursor_col, cursor_row, big);
		return;
	}
	if (inspect.kind != WORLD_SPELUNK_INSPECT_ACTOR || !inspect.actor ||
			!inspect.actor_definition) {
		return;
	}
	card->active = true;
	card->big = big;
	card->glyph = inspect.actor_definition->glyph;
	card->attr = inspect.actor_definition->attr;
	card->cursor_col = cursor_col;
	card->cursor_row = cursor_row;
	my_strcpy(card->name, inspect.actor_definition->name,
		sizeof(card->name));
	my_strcpy(card->subtitle, "Cave creature", sizeof(card->subtitle));
	strnfmt(card->status, sizeof(card->status), "Health: %d/%d",
		inspect.actor->hp, inspect.actor->max_hp);
	strnfmt(card->details[0], sizeof(card->details[0]), "Armour: %d",
		inspect.actor_definition->armour);
	strnfmt(card->details[1], sizeof(card->details[1]), "Attack: %dd%d",
		inspect.actor_definition->damage_dice,
		inspect.actor_definition->damage_sides);
	my_strcpy(card->footer, "Look: arrows move; Enter/Esc closes",
		sizeof(card->footer));
}
