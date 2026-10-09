/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/screen-model.c
 * \brief Owned SDL3 copy of a transient semantic Angband screen.
 */

#include "angband.h"

#include "sdl3/screen-model.h"

void sdl3_screen_clear(struct sdl3_screen *screen)
{
	if (!screen) return;
	memset(screen, 0, sizeof(*screen));
	screen->dungeon_player_col = -1;
	screen->dungeon_player_row = -1;
}

void sdl3_screen_capture(struct sdl3_screen *screen,
		const struct ui_screen *source)
{
	int i;

	if (!screen) return;
	sdl3_screen_clear(screen);
	if (!source || source->kind == UI_SCREEN_NONE) return;
	screen->active = true;
	screen->kind = source->kind;
	my_strcpy(screen->title, source->title ? source->title : "",
		sizeof(screen->title));
	my_strcpy(screen->subtitle, source->subtitle ? source->subtitle : "",
		sizeof(screen->subtitle));
	my_strcpy(screen->help, source->help ? source->help : "",
		sizeof(screen->help));
	my_strcpy(screen->context_title,
		source->context_title ? source->context_title : "",
		sizeof(screen->context_title));
	my_strcpy(screen->context, source->context ? source->context : "",
		sizeof(screen->context));
	screen->content_col = MAX(0, source->content_col);
	screen->content_row = MAX(0, source->content_row);
	screen->content_cols = MAX(0, source->content_cols);
	screen->content_rows = MAX(0, source->content_rows);
	screen->row_count = source->rows ?
		MIN(MAX(0, source->row_count), SDL3_SCREEN_ROW_CAPACITY) : 0;
	screen->cursor = source->cursor;
	screen->row_offset = MAX(0, source->row_offset);
	if (screen->row_count <= 0) {
		screen->cursor = -1;
	} else if (screen->cursor < 0) {
		screen->cursor = -1;
	} else {
		screen->cursor = MIN(MAX(0, screen->cursor),
			screen->row_count - 1);
		screen->row_offset = MIN(screen->row_offset,
			screen->row_count - 1);
	}
	for (i = 0; i < screen->row_count; i++) {
		const struct ui_screen_row *row = &source->rows[i];
		struct sdl3_screen_row *copy = &screen->rows[i];

		my_strcpy(copy->label, row->label ? row->label : "",
			sizeof(copy->label));
		my_strcpy(copy->prefix, row->prefix ? row->prefix : "",
			sizeof(copy->prefix));
		my_strcpy(copy->detail, row->detail ? row->detail : "",
			sizeof(copy->detail));
		copy->glyph = row->glyph;
		copy->attr = row->attr;
		copy->tag = row->tag;
		copy->enabled = row->enabled;
		copy->glyph_attr = row->glyph_attr;
		copy->has_glyph_attr = row->has_glyph_attr;
	}
	screen->tab_count = source->tabs ?
		MIN(MAX(0, source->tab_count), SDL3_SCREEN_TAB_CAPACITY) : 0;
	for (i = 0; i < screen->tab_count; i++) {
		const struct ui_screen_tab *tab = &source->tabs[i];
		struct sdl3_screen_tab *copy = &screen->tabs[i];

		my_strcpy(copy->label, tab->label ? tab->label : "",
			sizeof(copy->label));
		copy->key = tab->key;
		copy->active = tab->active;
	}
	screen->map_node_count = source->map_nodes ?
		MIN(MAX(0, source->map_node_count), SDL3_SCREEN_MAP_NODE_CAPACITY) : 0;
	for (i = 0; i < screen->map_node_count; i++) {
		const struct ui_screen_map_node *node = &source->map_nodes[i];
		struct sdl3_screen_map_node *copy = &screen->map_nodes[i];

		my_strcpy(copy->label, node->label ? node->label : "",
			sizeof(copy->label));
		my_strcpy(copy->detail, node->detail ? node->detail : "",
			sizeof(copy->detail));
		my_strcpy(copy->description,
			node->description ? node->description : "",
			sizeof(copy->description));
		for (int row = 0; row < UI_SCREEN_MAP_STAMP_HEIGHT; row++) {
			my_strcpy(copy->stamp[row], node->stamp[row],
				sizeof(copy->stamp[row]));
		}
		copy->x = node->x;
		copy->y = node->y;
		copy->glyph = node->glyph;
		copy->attr = node->attr;
		copy->current = node->current;
	}
	screen->map_edge_count = 0;
	if (source->map_edges) {
		int source_count = MIN(MAX(0, source->map_edge_count),
			SDL3_SCREEN_MAP_EDGE_CAPACITY);

		for (i = 0; i < source_count; i++) {
			const struct ui_screen_map_edge *edge = &source->map_edges[i];
			struct sdl3_screen_map_edge *copy;

			if (edge->from < 0 || edge->to < 0 ||
					edge->from >= screen->map_node_count ||
					edge->to >= screen->map_node_count ||
					edge->from == edge->to) {
				continue;
			}
			copy = &screen->map_edges[screen->map_edge_count++];
			copy->from = edge->from;
			copy->to = edge->to;
			copy->attr = edge->attr;
			copy->ready = edge->ready;
		}
	}
	screen->dungeon_map_cols = source->dungeon_map_cells ?
		MIN(MAX(0, source->dungeon_map_cols),
			SDL3_SCREEN_DUNGEON_MAP_COLS) : 0;
	if (screen->dungeon_map_cols > 0) {
		screen->dungeon_map_rows = MIN(MAX(0, source->dungeon_map_rows),
			MIN(SDL3_SCREEN_DUNGEON_MAP_ROWS,
				SDL3_SCREEN_DUNGEON_MAP_CAPACITY /
					screen->dungeon_map_cols));
	}
	for (i = 0; i < screen->dungeon_map_rows; i++) {
		int j;

		for (j = 0; j < screen->dungeon_map_cols; j++) {
			const struct ui_screen_map_cell *cell =
				&source->dungeon_map_cells[
					i * source->dungeon_map_cols + j];
			struct sdl3_screen_map_cell *copy =
				&screen->dungeon_map_cells[
					i * screen->dungeon_map_cols + j];

			copy->glyph = cell->glyph;
			copy->attr = cell->attr;
		}
	}
	if (source->dungeon_player_col >= 0 &&
			source->dungeon_player_col < screen->dungeon_map_cols &&
			source->dungeon_player_row >= 0 &&
			source->dungeon_player_row < screen->dungeon_map_rows) {
		screen->dungeon_player_col = source->dungeon_player_col;
		screen->dungeon_player_row = source->dungeon_player_row;
	}
	screen->document_length = source->document_text ?
		MIN(source->document_length, (size_t)SDL3_SCREEN_DOCUMENT_CAPACITY) : 0;
	if (screen->document_length > 0) {
		memcpy(screen->document_text, source->document_text,
			screen->document_length * sizeof(*screen->document_text));
		if (source->document_attrs) {
			memcpy(screen->document_attrs, source->document_attrs,
				screen->document_length * sizeof(*screen->document_attrs));
		} else {
			memset(screen->document_attrs, COLOUR_WHITE,
				screen->document_length * sizeof(*screen->document_attrs));
		}
	}
	screen->document_text[screen->document_length] = L'\0';
	screen->document_line_count = 0;
	if (source->document_line_starts && source->document_line_lengths) {
		int source_count = MIN(MAX(0, source->document_line_count),
			SDL3_SCREEN_DOCUMENT_LINE_CAPACITY);

		for (i = 0; i < source_count; i++) {
			size_t start = source->document_line_starts[i];
			size_t length;

			if (start >= screen->document_length) continue;
			length = MIN(source->document_line_lengths[i],
				screen->document_length - start);
			screen->document_line_starts[screen->document_line_count] = start;
			screen->document_line_lengths[screen->document_line_count] = length;
			screen->document_line_count++;
		}
	}
	if (screen->document_line_count > 0) {
		screen->document_line_offset = MIN(MAX(0,
			source->document_line_offset), screen->document_line_count - 1);
	}
	screen->document_cols = MAX(0, source->document_cols);
	my_strcpy(screen->subject_name,
		source->subject_name ? source->subject_name : "",
		sizeof(screen->subject_name));
	my_strcpy(screen->subject_base,
		source->subject_base ? source->subject_base : "",
		sizeof(screen->subject_base));
	my_strcpy(screen->subject_art_id,
		source->subject_art_id ? source->subject_art_id : "",
		sizeof(screen->subject_art_id));
	my_strcpy(screen->subject_base_art_id,
		source->subject_base_art_id ? source->subject_base_art_id : "",
		sizeof(screen->subject_base_art_id));
	screen->subject_glyph = source->subject_glyph;
	screen->subject_attr = source->subject_attr;
	screen->subject_unique = source->subject_unique;
}

bool sdl3_screen_measure(const struct sdl3_screen *screen, int cols, int rows,
		struct sdl3_screen_layout *layout)
{
	int available_rows;
	int i;

	if (!screen || !screen->active || !layout || cols < 9 || rows < 4) {
		return false;
	}
	memset(layout, 0, sizeof(*layout));
	layout->width = MIN(screen->content_cols > 0 ? screen->content_cols : 88,
		cols - 8);
	if (layout->width <= 0) return false;
	layout->left = MAX(4, MIN(screen->content_col,
		cols - layout->width - 4));
	layout->tab_row = 6;
	layout->first_row = MIN(MAX(0, screen->row_offset), screen->row_count);
	available_rows = MAX(0, rows - screen->content_row - 3);
	layout->visible_rows = MIN(MAX(0, screen->content_rows), available_rows);
	layout->visible_rows = MIN(layout->visible_rows,
		MAX(0, screen->row_count - layout->first_row));
	layout->detail_width = layout->width >= 64 ?
		MIN(18, layout->width / 4) : 0;
	layout->list_width = layout->width;
	if (screen->context[0] && screen->kind != UI_SCREEN_STORE &&
			(layout->width >= 72 || screen->kind == UI_SCREEN_STORY)) {
		layout->list_width = screen->kind == UI_SCREEN_STORY ? 0 :
			screen->kind == UI_SCREEN_CHARACTER_DOSSIER ?
			MIN(44, MAX(34, layout->width / 2)) :
			MIN(38, MAX(28, layout->width / 3));
		layout->context_col = screen->kind == UI_SCREEN_STORY ? layout->left :
			layout->left + layout->list_width + 3;
		layout->context_width = screen->kind == UI_SCREEN_STORY ? layout->width :
			layout->width - layout->list_width - 3;
		layout->detail_width =
			screen->kind == UI_SCREEN_CHARACTER_DOSSIER ?
			MIN(14, layout->list_width / 3) : 0;
	}
	if (screen->kind == UI_SCREEN_BIRTH_STATS) {
		/* Numeric columns must never be clipped by the ordinary choice-menu
		 * split. At compact widths put context below the stat table. */
		layout->list_width = layout->width < 100 ? layout->width : 54;
		layout->context_col = layout->width < 100 ? layout->left :
			layout->left + layout->list_width + 3;
		layout->context_width = layout->width < 100 ? layout->width :
			layout->width - layout->list_width - 3;
		layout->detail_width = 0;
	}
	if (screen->kind == UI_SCREEN_STORE) {
		int maximum_prefix = 0;
		int maximum_detail = 0;

		/* Measure the complete inventory so columns do not jump while the
		 * player scrolls between pages. */
		for (i = 0; i < screen->row_count; i++) {
			const struct sdl3_screen_row *row = &screen->rows[i];

			maximum_prefix = MAX(maximum_prefix, (int)strlen(row->prefix));
			maximum_detail = MAX(maximum_detail, (int)strlen(row->detail));
		}
		layout->prefix_width = MIN(maximum_prefix, 12);
		layout->detail_width = maximum_detail ?
			MIN(maximum_detail + 2, MIN(20, layout->list_width / 3)) : 0;
		layout->label_col = 8 +
			(layout->prefix_width ? layout->prefix_width + 1 : 0);
		layout->detail_col = layout->detail_width ?
			layout->list_width - layout->detail_width : layout->list_width;
		/* Keep a visible gutter between a clipped name and the price. */
		layout->label_width = MAX(1,
			layout->detail_col - layout->label_col - 2);
	} else {
		layout->label_col = 8;
		layout->detail_col = layout->list_width - layout->detail_width;
		layout->label_width = layout->list_width - layout->detail_width - 8;
	}
	return true;
}

bool sdl3_screen_row_at(const struct sdl3_screen *screen, int cols, int rows,
		int col, int row, int *index)
{
	struct sdl3_screen_layout layout;
	int visible_index;

	if (!index || !sdl3_screen_measure(screen, cols, rows, &layout) ||
			col < layout.left || col >= layout.left + layout.list_width ||
			row < screen->content_row ||
			row >= screen->content_row + layout.visible_rows) {
		return false;
	}
	visible_index = row - screen->content_row;
	*index = layout.first_row + visible_index;
	return *index >= 0 && *index < screen->row_count;
}

int sdl3_screen_action_menu_keys(const struct sdl3_screen *screen, int index,
		bool activate, keycode_t *keys, int capacity)
{
	int distance, count, i;
	keycode_t direction;
	if (!screen || !screen->active || screen->kind != UI_SCREEN_ACTION_MENU ||
			!keys || index < 0 || index >= screen->row_count ||
			index >= SDL3_SCREEN_ROW_CAPACITY ||
			screen->cursor < 0 || screen->cursor >= screen->row_count ||
			!screen->rows[index].enabled) return 0;
	distance = ABS(index - screen->cursor);
	count = distance + (activate ? 1 : 0);
	if (count > capacity) return 0;
	direction = index < screen->cursor ? ARROW_UP : ARROW_DOWN;
	for (i = 0; i < distance; i++) keys[i] = direction;
	if (activate) keys[i] = KC_ENTER;
	return count;
}
