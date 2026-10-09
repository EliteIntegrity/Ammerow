/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/world-map-screen.c
 * \brief SDL3 renderer for the semantic discovered-world diagram.
 *
 *
 */

#include "angband.h"

#include "sdl3/render-internal.h"
#include "sdl3/screen-model.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"
#include "sdl3/world-map-screen.h"

static int map_scale(int value, int minimum, int maximum, int start,
		int span)
{
	if (maximum <= minimum || span <= 0) return start + MAX(0, span) / 2;
	return start + (int)(((int64_t)value - minimum) * span /
		((int64_t)maximum - minimum));
}

static void draw_map_edge(const struct sdl3_screen_map_edge *edge,
		const int *node_x, const int *node_y, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	SDL_Color color = sdl3_theme_color(theme, edge->attr);
	const char *horizontal = edge->ready ? "=" : ".";
	const char *vertical = edge->ready ? "|" : ":";
	const char *corner = edge->ready ? "+" : ":";
	int x1 = node_x[edge->from];
	int y1 = node_y[edge->from];
	int x2 = node_x[edge->to];
	int y2 = node_y[edge->to];
	int step;
	int x, y;

	step = x1 <= x2 ? 1 : -1;
	for (x = x1; x != x2; x += step) {
		sdl3_ui_draw_text(visual, horizontal, x, y1, 1, color);
	}
	step = y1 <= y2 ? 1 : -1;
	for (y = y1; y != y2; y += step) {
		sdl3_ui_draw_text(visual, vertical, x2, y, 1, color);
	}
	if (x1 != x2 && y1 != y2) {
		sdl3_ui_draw_text(visual, corner, x2, y1, 1, color);
	}
}

static int draw_wrapped_description(struct sdl3_visual *visual,
		const char *text, int col, int row, int width, int max_rows,
		SDL_Color color)
{
	const char *cursor = text;
	int drawn = 0;

	while (cursor && *cursor && drawn < max_rows && width > 0) {
		char line[SDL3_SCREEN_DESCRIPTION_CAPACITY];
		size_t remaining = strlen(cursor);
		size_t take = MIN(remaining, (size_t)width);
		size_t split = take;

		if (take < remaining) {
			while (split > 0 && cursor[split] != ' ') split--;
			if (!split) split = take;
		}
		while (split > 0 && cursor[split - 1] == ' ') split--;
		my_strcpy(line, "", sizeof(line));
		my_strcpy(line, cursor, MIN(sizeof(line), split + 1));
		sdl3_ui_draw_text(visual, line, col, row + drawn, width, color);
		cursor += take < remaining && split < take ? split : take;
		while (*cursor == ' ') cursor++;
		drawn++;
	}
	return drawn;
}

static void stamp_spacing(const int *node_x, const int *node_y, int count,
		int *horizontal, int *vertical)
{
	int i, j;

	*horizontal = INT_MAX;
	*vertical = INT_MAX;
	for (i = 0; i < count; i++) {
		for (j = i + 1; j < count; j++) {
			if (node_y[i] == node_y[j]) {
				*horizontal = MIN(*horizontal, ABS(node_x[i] - node_x[j]));
			}
			if (node_x[i] == node_x[j]) {
				*vertical = MIN(*vertical, ABS(node_y[i] - node_y[j]));
			}
		}
	}
}

bool sdl3_world_map_screen_draw(const struct sdl3_screen *screen,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	int node_x[SDL3_SCREEN_MAP_NODE_CAPACITY];
	int node_y[SDL3_SCREEN_MAP_NODE_CAPACITY];
	int minimum_x = INT_MAX, maximum_x = INT_MIN;
	int minimum_y = INT_MAX, maximum_y = INT_MIN;
	int diagram_top = 7;
	int detail_top = visual->rows - 8;
	int diagram_bottom = detail_top - 2;
	int x_start, x_span, y_start, y_span;
	int horizontal_spacing, vertical_spacing;
	int stamp_width = UI_SCREEN_MAP_STAMP_WIDTH;
	int stamp_height = UI_SCREEN_MAP_STAMP_HEIGHT;
	int i;

	if ((screen->kind != UI_SCREEN_WORLD_MAP &&
			screen->kind != UI_SCREEN_CAVE_SURVEY) ||
			screen->map_node_count <= 0 ||
			width < 48 || diagram_bottom - diagram_top < 6) {
		return false;
	}
	for (i = 0; i < screen->map_node_count; i++) {
		minimum_x = MIN(minimum_x, screen->map_nodes[i].x);
		maximum_x = MAX(maximum_x, screen->map_nodes[i].x);
		minimum_y = MIN(minimum_y, screen->map_nodes[i].y);
		maximum_y = MAX(maximum_y, screen->map_nodes[i].y);
	}
	x_start = left + UI_SCREEN_MAP_STAMP_WIDTH / 2 + 1;
	x_span = MAX(0, width - UI_SCREEN_MAP_STAMP_WIDTH - 2);
	y_start = diagram_top + UI_SCREEN_MAP_STAMP_HEIGHT / 2;
	y_span = MAX(0, diagram_bottom - diagram_top -
		UI_SCREEN_MAP_STAMP_HEIGHT);
	for (i = 0; i < screen->map_node_count; i++) {
		node_x[i] = map_scale(screen->map_nodes[i].x, minimum_x, maximum_x,
			x_start, x_span);
		node_y[i] = map_scale(screen->map_nodes[i].y, minimum_y, maximum_y,
			y_start, y_span);
	}
	stamp_spacing(node_x, node_y, screen->map_node_count,
		&horizontal_spacing, &vertical_spacing);
	if (horizontal_spacing < UI_SCREEN_MAP_STAMP_WIDTH + 2 ||
			vertical_spacing < UI_SCREEN_MAP_STAMP_HEIGHT + 2) {
		stamp_width = 3;
		stamp_height = 1;
	}
	if (horizontal_spacing < 4 || vertical_spacing < 2) {
		stamp_width = 1;
		stamp_height = 1;
	}
	for (i = 0; i < screen->map_edge_count; i++) {
		draw_map_edge(&screen->map_edges[i], node_x, node_y, visual, theme);
	}
	for (i = 0; i < screen->map_node_count; i++) {
		const struct sdl3_screen_map_node *node = &screen->map_nodes[i];
		int node_left = MIN(MAX(left, node_x[i] - stamp_width / 2),
			left + width - stamp_width);
		int node_top = node_y[i] - stamp_height / 2;
		bool selected = i == screen->cursor;
		SDL_Color node_color = sdl3_theme_color(theme, node->attr);
		int row;

		if (selected) {
			sdl3_ui_fill_cells(renderer, visual, node_left - 1, node_top,
				stamp_width + 2, stamp_height, theme->selection);
		}
		for (row = 0; row < stamp_height; row++) {
			const char *stamp;

			if (stamp_width == UI_SCREEN_MAP_STAMP_WIDTH) {
				stamp = node->stamp[row];
			} else if (stamp_width == 3) {
				stamp = node->stamp[UI_SCREEN_MAP_STAMP_HEIGHT / 2] + 1;
			} else {
				stamp = node->stamp[UI_SCREEN_MAP_STAMP_HEIGHT / 2] +
					UI_SCREEN_MAP_STAMP_WIDTH / 2;
			}
			sdl3_ui_draw_text(visual, stamp, node_left, node_top + row,
				stamp_width, node_color);
		}
		if (node->current && node->glyph && node->glyph >= 32) {
			sdl3_font_draw(&visual->font, (uint32_t)node->glyph,
				theme->accent,
				(float)(visual->origin_x + node_x[i] *
					visual->cell_width),
				(float)(visual->origin_y + node_y[i] *
					visual->cell_height),
				(float)visual->cell_width, (float)visual->cell_height);
		}
	}
	if (screen->cursor >= 0 && screen->cursor < screen->map_node_count) {
		const struct sdl3_screen_map_node *selected =
			&screen->map_nodes[screen->cursor];
		static const char *const rule =
			"----------------------------------------------------------------"
			"----------------------------------------------------------------";

		sdl3_ui_draw_text(visual, rule, left, detail_top - 1, width,
			theme->panel_border);
		sdl3_ui_draw_text(visual, selected->label, left, detail_top, width,
			theme->title);
		sdl3_ui_draw_text(visual, selected->detail, left, detail_top + 1,
			width, theme->accent);
		draw_wrapped_description(visual, selected->description, left,
			detail_top + 2, width, 2, theme->text);
		sdl3_ui_draw_text(visual, screen->subtitle, left, detail_top + 5,
			width, theme->muted);
	}
	return true;
}
