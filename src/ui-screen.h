/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-screen.h
 * \brief Semantic descriptions for optional frontend-owned screens.
 *
 * Angband's text UI remains authoritative.  A frontend may use these
 * short-lived descriptions to present the same controller state with a
 * native screen; frontends which do not install a hook keep the terminal
 * rendering unchanged.
 */

#ifndef INCLUDED_UI_SCREEN_H
#define INCLUDED_UI_SCREEN_H

#include "h-basic.h"

enum ui_screen_kind {
	UI_SCREEN_NONE = 0,
	UI_SCREEN_ITEM_SELECTOR,
	UI_SCREEN_CHARACTER_CREATION,
	UI_SCREEN_CHARACTER_DOSSIER,
	UI_SCREEN_STORE,
	UI_SCREEN_WORLD_MAP,
	UI_SCREEN_CAVE_SURVEY,
	UI_SCREEN_TRAVEL,
	UI_SCREEN_RUN_SUMMARY,
	UI_SCREEN_MEMORIAL,
	UI_SCREEN_ABILITIES,
	UI_SCREEN_KNOWLEDGE,
	UI_SCREEN_HISTORY,
	UI_SCREEN_NEARBY,
	UI_SCREEN_DUNGEON_MAP,
	UI_SCREEN_MONSTER_LORE,
	UI_SCREEN_ACTION_MENU,
	UI_SCREEN_STORY,
	UI_SCREEN_OPTIONS,
	UI_SCREEN_BIRTH_STATS,
	UI_SCREEN_HELP
};

#define UI_SCREEN_MAP_STAMP_WIDTH 5
#define UI_SCREEN_MAP_STAMP_HEIGHT 3

struct ui_screen_tab {
	const char *label;
	char key;
	bool active;
};

struct ui_screen_row {
	const char *label;
	const char *prefix;
	const char *detail;
	wchar_t glyph;
	uint8_t attr;
	char tag;
	bool enabled;
	uint8_t glyph_attr;
	bool has_glyph_attr;
};

/** One discovered site in a frontend-independent spatial diagram. */
struct ui_screen_map_node {
	const char *label;
	const char *detail;
	const char *description;
	char stamp[UI_SCREEN_MAP_STAMP_HEIGHT][UI_SCREEN_MAP_STAMP_WIDTH + 1];
	int x;
	int y;
	wchar_t glyph;
	uint8_t attr;
	bool current;
};

/** One visible, undirected connection between diagram nodes. */
struct ui_screen_map_edge {
	int from;
	int to;
	uint8_t attr;
	bool ready;
};

/** One already-resolved, knowledge-safe cell in a level overview. */
struct ui_screen_map_cell {
	wchar_t glyph;
	uint8_t attr;
};

/**
 * A synchronous, read-only view of one screen.  All pointers are borrowed
 * and are valid only for the duration of the frontend hook call.
 */
struct ui_screen {
	enum ui_screen_kind kind;
	const char *title;
	const char *subtitle;
	const char *help;
	const char *context_title;
	const char *context;
	int content_col;
	int content_row;
	int content_cols;
	int content_rows;
	int cursor;
	int row_offset;
	int row_count;
	const struct ui_screen_row *rows;
	int tab_count;
	const struct ui_screen_tab *tabs;
	int map_node_count;
	const struct ui_screen_map_node *map_nodes;
	int map_edge_count;
	const struct ui_screen_map_edge *map_edges;
	int dungeon_map_cols;
	int dungeon_map_rows;
	int dungeon_player_col;
	int dungeon_player_row;
	const struct ui_screen_map_cell *dungeon_map_cells;
	size_t document_length;
	const wchar_t *document_text;
	const uint8_t *document_attrs;
	int document_line_count;
	const size_t *document_line_starts;
	const size_t *document_line_lengths;
	int document_line_offset;
	int document_cols;
	const char *subject_name;
	const char *subject_base;
	const char *subject_art_id;
	const char *subject_base_art_id;
	wchar_t subject_glyph;
	uint8_t subject_attr;
	bool subject_unique;
};

#endif /* INCLUDED_UI_SCREEN_H */
