/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-view.h
 * \brief Immutable presentation snapshots for side-view spelunking.
 *
 *
 */

#ifndef WORLD_SPELUNKING_VIEW_H
#define WORLD_SPELUNKING_VIEW_H

#include "world-spelunking-runtime.h"
#include "world-spelunking-geology-data.h"

#include <stddef.h>
#include <stdint.h>

struct world_spelunk_system;

/** Semantic cell kinds stay independent of renderer glyph and colour maps. */
enum world_spelunk_view_cell {
	WORLD_SPELUNK_VIEW_VOID = 0,
	WORLD_SPELUNK_VIEW_UNKNOWN,
	WORLD_SPELUNK_VIEW_REMEMBERED_AIR,
	WORLD_SPELUNK_VIEW_REMEMBERED_ROCK,
	WORLD_SPELUNK_VIEW_REMEMBERED_WATER,
	WORLD_SPELUNK_VIEW_AIR,
	WORLD_SPELUNK_VIEW_ROCK,
	WORLD_SPELUNK_VIEW_ROCK_CALCITE,
	WORLD_SPELUNK_VIEW_ROCK_IRON,
	WORLD_SPELUNK_VIEW_ROCK_COPPER,
	WORLD_SPELUNK_VIEW_STALACTITE,
	WORLD_SPELUNK_VIEW_STALAGMITE,
	WORLD_SPELUNK_VIEW_MATERIAL,
	WORLD_SPELUNK_VIEW_DECORATION,
	WORLD_SPELUNK_VIEW_EXIT,
	WORLD_SPELUNK_VIEW_PASSAGE_UP,
	WORLD_SPELUNK_VIEW_PASSAGE_DOWN,
	WORLD_SPELUNK_VIEW_ROPE,
	WORLD_SPELUNK_VIEW_PITON,
	WORLD_SPELUNK_VIEW_OBJECT,
	WORLD_SPELUNK_VIEW_ACTOR,
	WORLD_SPELUNK_VIEW_GRIP,
	WORLD_SPELUNK_VIEW_PLAYER,
	WORLD_SPELUNK_VIEW_WATER,
	WORLD_SPELUNK_VIEW_PEEK
};

/** Presentation data for one currently visible actor in the snapshot. */
struct world_spelunk_view_actor_appearance {
	int col;
	int row;
	wchar_t glyph;
	uint8_t attr;
	const char *actor_id;
};

/** Presentation data for one currently visible ground object. */
struct world_spelunk_view_object_appearance {
	int col;
	int row;
	wchar_t glyph;
	uint8_t attr;
	const char *art_id;
	const char *fallback_id;
};

/**
 * A complete, read-only view of one visible part of a spelunking location.
 * The cell storage is supplied by the caller and is immutable after capture.
 */
struct world_spelunk_view {
	const uint8_t *cells;
	size_t cell_count;
	struct world_spelunk_view_actor_appearance
		actor_appearances[WORLD_SPELUNK_ACTOR_MAX];
	size_t actor_appearance_count;
	struct world_spelunk_view_object_appearance
		object_appearances[WORLD_SPELUNK_OBJECT_MAX];
	size_t object_appearance_count;
	int cols;
	int rows;
	int source_x;
	int source_y;
	int map_width;
	int map_height;
	int player_col;
	int player_row;
	int peek_col;
	int peek_row;
	bool peeking;
	int stamina;
	int max_stamina;
	int breath;
	int max_breath;
	enum world_spelunk_movement movement;
	wchar_t visible_air_glyph;
	uint8_t visible_air_attr;
	wchar_t remembered_air_glyph;
	uint8_t remembered_air_attr;
	uint8_t remembered_brightness_percent;
	/** Read-only generated terrain layers retained for this frame. */
	const uint8_t *material_tags;
	const uint8_t *decoration_tags;
	int geology_stride;
	uint16_t geology_version;
	char geology_profile_id[WORLD_ID_LEN];
	char location_id[WORLD_ID_LEN];
};

/** The knowledge-safe subject occupying one side-view map cell. */
enum world_spelunk_inspect_kind {
	WORLD_SPELUNK_INSPECT_VOID = 0,
	WORLD_SPELUNK_INSPECT_UNKNOWN,
	WORLD_SPELUNK_INSPECT_AIR,
	WORLD_SPELUNK_INSPECT_STALACTITE,
	WORLD_SPELUNK_INSPECT_STALAGMITE,
	WORLD_SPELUNK_INSPECT_ROCK,
	WORLD_SPELUNK_INSPECT_WATER,
	WORLD_SPELUNK_INSPECT_EXIT,
	WORLD_SPELUNK_INSPECT_PASSAGE_UP,
	WORLD_SPELUNK_INSPECT_PASSAGE_DOWN,
	WORLD_SPELUNK_INSPECT_ROPE,
	WORLD_SPELUNK_INSPECT_PITON,
	WORLD_SPELUNK_INSPECT_OBJECT,
	WORLD_SPELUNK_INSPECT_ACTOR,
	WORLD_SPELUNK_INSPECT_GRIP,
	WORLD_SPELUNK_INSPECT_PLAYER
};

enum world_spelunk_inspect_knowledge {
	WORLD_SPELUNK_INSPECT_UNSEEN = 0,
	WORLD_SPELUNK_INSPECT_REMEMBERED,
	WORLD_SPELUNK_INSPECT_DETECTED,
	WORLD_SPELUNK_INSPECT_VISIBLE
};

/**
 * Renderer-independent Look result.  Occupant pointers are exposed only when
 * direct sight or an active detection effect permits them.
 */
struct world_spelunk_inspect {
	enum world_spelunk_inspect_kind kind;
	enum world_spelunk_inspect_knowledge knowledge;
	int x;
	int y;
	const struct object *object;
	const struct world_spelunk_actor *actor;
	const struct world_spelunk_actor_definition *actor_definition;
	const struct world_spelunk_material_definition *material;
	const struct world_spelunk_decoration_definition *decoration;
};

/**
 * Capture a bounded viewport centered on the player and clamped at map edges.
 * No allocation is performed.  Only the visible cells are copied, so redraw
 * cost depends on the display rather than the full location size.
 */
bool world_spelunk_view_capture(
		const struct world_spelunk_runtime *runtime,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view);

/** Capture while overlaying external portals owned by a cave system. */
bool world_spelunk_view_capture_system(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view);

/** Capture a viewport around an explicit presentation focus. */
bool world_spelunk_view_capture_at(
		const struct world_spelunk_runtime *runtime, int focus_x, int focus_y,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view);

/** System-aware capture around an explicit presentation focus. */
bool world_spelunk_view_capture_at_system(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int focus_x, int focus_y,
		int requested_cols, int requested_rows, uint8_t *storage,
		size_t storage_capacity, struct world_spelunk_view *view);

/** Resolve one cell without disclosing unobserved transient occupants. */
bool world_spelunk_view_inspect(
		const struct world_spelunk_runtime *runtime, int x, int y,
		struct world_spelunk_inspect *inspect);

/** Inspect with system-owned external-portal knowledge. */
bool world_spelunk_view_inspect_system(
		const struct world_spelunk_system *system,
		const struct world_spelunk_runtime *runtime, int x, int y,
		struct world_spelunk_inspect *inspect);

/** Read a local viewport cell, returning VOID for invalid coordinates. */
enum world_spelunk_view_cell world_spelunk_view_base_cell_at(
		const struct world_spelunk_runtime *runtime, int x, int y);

enum world_spelunk_view_cell world_spelunk_view_cell_at(
		const struct world_spelunk_view *view, int col, int row);

/** Resolve data-authored appearance for an actor cell in this snapshot. */
bool world_spelunk_view_actor_appearance_at(
		const struct world_spelunk_view *view, int col, int row,
		int *attr, wchar_t *glyph);

/** Resolve data-authored appearance for a ground-object cell. */
bool world_spelunk_view_object_appearance_at(
		const struct world_spelunk_view *view, int col, int row,
		int *attr, wchar_t *glyph);

#endif /* !WORLD_SPELUNKING_VIEW_H */
