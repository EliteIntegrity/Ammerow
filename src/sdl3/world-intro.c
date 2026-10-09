/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include "angband.h"
#include "player.h"
#include "ui-world-intro.h"
#include "world-story-data.h"
#include "sdl3/world-intro.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

void sdl3_world_intro_draw(const struct ui_world_intro *state,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme)
{
	struct ui_world_intro_layout layout;
	const struct world_story_scene *scene = world_story_intro_at(state->tab);
	textblock *tb;
	size_t *starts = NULL, *lengths = NULL;
	const wchar_t *text;
	const uint8_t *attrs;
	int count, offset;
	if (!ui_world_intro_measure(state, visual->cols, visual->rows, &layout)) return;
	sdl3_ui_draw_text(visual, "LORE", layout.left, 2, layout.width, theme->title);
	for (int i = 0; i < layout.tab_count; ++i) {
		int slot = i % layout.tab_columns;
		int row = layout.tab_row + i / layout.tab_columns;
		int col = layout.left + slot * layout.width / layout.tab_columns;
		int width = (slot + 1) * layout.width / layout.tab_columns -
			slot * layout.width / layout.tab_columns;
		const struct world_story_scene *tab = world_story_intro_at(i);
		if (i == state->tab) sdl3_ui_draw_selection(renderer, visual, theme,
			col, row, width);
		sdl3_ui_draw_text(visual, tab->title, col + 1, row,
			width - 1, i == state->tab ? theme->accent : theme->text);
	}
	if (scene && scene->subtitle) sdl3_ui_draw_text(visual, scene->subtitle,
		layout.left, layout.top - 2, layout.width, theme->muted);
	if (layout.list_width) {
		for (int i = 0; i < layout.visible_rows; ++i) {
			int index = layout.list_first + i;
			const char *label = ui_world_intro_people_label(index);
			if (!label) break;
			if (index == state->origin) sdl3_ui_draw_selection(renderer, visual,
				theme, layout.left, layout.top + i, layout.list_width - 2);
			sdl3_ui_draw_text(visual, label, layout.left + 1, layout.top + i,
				layout.list_width - 3, index == state->origin ? theme->accent : theme->text);
		}
	}
	tb = ui_world_intro_document(state);
	count = (int)textblock_calculate_lines(tb, &starts, &lengths, layout.text_width);
	offset = MIN(state->offset, MAX(0, count - layout.visible_rows));
	text = textblock_text(tb);
	attrs = textblock_attrs(tb);
	for (int i = 0; i < layout.visible_rows && offset + i < count; ++i) {
		int line = offset + i;
		for (size_t j = 0; j < lengths[line]; ++j) {
			size_t index = starts[line] + j;
			if (text[index] < 32) continue;
			sdl3_font_draw(&visual->font, (uint32_t)text[index],
				sdl3_theme_color(theme, attrs[index]),
				(float)(visual->origin_x + (layout.text_col + (int)j) * visual->cell_width),
				(float)(visual->origin_y + (layout.top + i) * visual->cell_height),
				(float)visual->cell_width, (float)visual->cell_height);
		}
	}
	sdl3_ui_draw_text(visual, format("%d-%d / %d", offset + 1,
		MIN(count, offset + layout.visible_rows), count), layout.left,
		visual->rows - 3, layout.width, theme->muted);
	sdl3_ui_draw_text(visual,
		"Left/Right tabs  Up/Down list/scroll",
		layout.left, visual->rows - 2, layout.width, theme->muted);
	sdl3_ui_draw_text(visual, "PgUp/PgDn / wheel scroll   Esc home",
		layout.left, visual->rows - 1, layout.width, theme->muted);
	mem_free(starts);
	mem_free(lengths);
	textblock_free(tb);
}
