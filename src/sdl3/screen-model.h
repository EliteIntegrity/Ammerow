/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/screen-model.h
 * \brief Owned SDL3 copy of a transient semantic Angband screen.
 */

#ifndef INCLUDED_SDL3_SCREEN_MODEL_H
#define INCLUDED_SDL3_SCREEN_MODEL_H

#include "ui-screen.h"
#include "ui-event.h"

#define SDL3_SCREEN_ROW_CAPACITY 64
#define SDL3_SCREEN_TAB_CAPACITY 6
#define SDL3_SCREEN_MAP_NODE_CAPACITY 64
#define SDL3_SCREEN_MAP_EDGE_CAPACITY 128
#define SDL3_SCREEN_DUNGEON_MAP_COLS 140
#define SDL3_SCREEN_DUNGEON_MAP_ROWS 42
#define SDL3_SCREEN_DUNGEON_MAP_CAPACITY \
	(SDL3_SCREEN_DUNGEON_MAP_COLS * SDL3_SCREEN_DUNGEON_MAP_ROWS)
#define SDL3_SCREEN_DOCUMENT_CAPACITY 16384
#define SDL3_SCREEN_DOCUMENT_LINE_CAPACITY 512
#define SDL3_SCREEN_TITLE_CAPACITY 160
#define SDL3_SCREEN_SUBTITLE_CAPACITY 256
#define SDL3_SCREEN_HELP_CAPACITY 192
#define SDL3_SCREEN_CONTEXT_TITLE_CAPACITY 96
#define SDL3_SCREEN_CONTEXT_CAPACITY 1024
#define SDL3_SCREEN_LABEL_CAPACITY 160
#define SDL3_SCREEN_PREFIX_CAPACITY 80
#define SDL3_SCREEN_DETAIL_CAPACITY 80
#define SDL3_SCREEN_DESCRIPTION_CAPACITY 256
#define SDL3_SCREEN_TAB_LABEL_CAPACITY 32
#define SDL3_SCREEN_SUBJECT_NAME_CAPACITY 160
#define SDL3_SCREEN_SUBJECT_BASE_CAPACITY 96

struct sdl3_screen_row {
	char label[SDL3_SCREEN_LABEL_CAPACITY];
	char prefix[SDL3_SCREEN_PREFIX_CAPACITY];
	char detail[SDL3_SCREEN_DETAIL_CAPACITY];
	wchar_t glyph;
	uint8_t attr;
	char tag;
	bool enabled;
	uint8_t glyph_attr;
	bool has_glyph_attr;
};

struct sdl3_screen_tab {
	char label[SDL3_SCREEN_TAB_LABEL_CAPACITY];
	char key;
	bool active;
};

struct sdl3_screen_map_node {
	char label[SDL3_SCREEN_LABEL_CAPACITY];
	char detail[SDL3_SCREEN_DETAIL_CAPACITY];
	char description[SDL3_SCREEN_DESCRIPTION_CAPACITY];
	char stamp[UI_SCREEN_MAP_STAMP_HEIGHT][UI_SCREEN_MAP_STAMP_WIDTH + 1];
	int x;
	int y;
	wchar_t glyph;
	uint8_t attr;
	bool current;
};

struct sdl3_screen_map_edge {
	int from;
	int to;
	uint8_t attr;
	bool ready;
};

struct sdl3_screen_map_cell {
	wchar_t glyph;
	uint8_t attr;
};

struct sdl3_screen {
	bool active;
	enum ui_screen_kind kind;
	char title[SDL3_SCREEN_TITLE_CAPACITY];
	char subtitle[SDL3_SCREEN_SUBTITLE_CAPACITY];
	char help[SDL3_SCREEN_HELP_CAPACITY];
	char context_title[SDL3_SCREEN_CONTEXT_TITLE_CAPACITY];
	char context[SDL3_SCREEN_CONTEXT_CAPACITY];
	int content_col;
	int content_row;
	int content_cols;
	int content_rows;
	int cursor;
	int row_offset;
	int row_count;
	struct sdl3_screen_row rows[SDL3_SCREEN_ROW_CAPACITY];
	int tab_count;
	struct sdl3_screen_tab tabs[SDL3_SCREEN_TAB_CAPACITY];
	int map_node_count;
	struct sdl3_screen_map_node map_nodes[SDL3_SCREEN_MAP_NODE_CAPACITY];
	int map_edge_count;
	struct sdl3_screen_map_edge map_edges[SDL3_SCREEN_MAP_EDGE_CAPACITY];
	int dungeon_map_cols;
	int dungeon_map_rows;
	int dungeon_player_col;
	int dungeon_player_row;
	struct sdl3_screen_map_cell
		dungeon_map_cells[SDL3_SCREEN_DUNGEON_MAP_CAPACITY];
	size_t document_length;
	wchar_t document_text[SDL3_SCREEN_DOCUMENT_CAPACITY + 1];
	uint8_t document_attrs[SDL3_SCREEN_DOCUMENT_CAPACITY];
	int document_line_count;
	size_t document_line_starts[SDL3_SCREEN_DOCUMENT_LINE_CAPACITY];
	size_t document_line_lengths[SDL3_SCREEN_DOCUMENT_LINE_CAPACITY];
	int document_line_offset;
	int document_cols;
	char subject_name[SDL3_SCREEN_SUBJECT_NAME_CAPACITY];
	char subject_base[SDL3_SCREEN_SUBJECT_BASE_CAPACITY];
	char subject_art_id[SDL3_SCREEN_SUBJECT_NAME_CAPACITY];
	char subject_base_art_id[SDL3_SCREEN_SUBJECT_BASE_CAPACITY];
	wchar_t subject_glyph;
	uint8_t subject_attr;
	bool subject_unique;
};

struct sdl3_screen_layout {
	int left;
	int width;
	int tab_row;
	int first_row;
	int visible_rows;
	int detail_width;
	int label_width;
	int list_width;
	int prefix_width;
	int label_col;
	int detail_col;
	int context_col;
	int context_width;
};

void sdl3_screen_clear(struct sdl3_screen *screen);
void sdl3_screen_capture(struct sdl3_screen *screen,
		const struct ui_screen *source);
bool sdl3_screen_measure(const struct sdl3_screen *screen, int cols, int rows,
		struct sdl3_screen_layout *layout);
bool sdl3_screen_row_at(const struct sdl3_screen *screen, int cols, int rows,
		int col, int row, int *index);

/* Translate an action-menu pointer selection into controller navigation.
 * Returns zero for an invalid selection or insufficient output capacity. */
int sdl3_screen_action_menu_keys(const struct sdl3_screen *screen, int index,
		bool activate, keycode_t *keys, int capacity);

#endif /* INCLUDED_SDL3_SCREEN_MODEL_H */
