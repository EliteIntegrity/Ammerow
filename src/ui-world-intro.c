/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Read-only introduction model. No live character, renderer or RNG required. */
#include "angband.h"
#include "player.h"
#include "ui-birth-screen.h"
#include "ui-world-intro.h"
#include "world-story-data.h"

int ui_world_intro_origin_count(void)
{
	int count = 0;
	for (const struct player_race *race = races; race; race = race->next) ++count;
	return count;
}

const struct player_race *ui_world_intro_origin_at(int index)
{
	if (index < 0) return NULL;
	/* Use the authored order, like character creation, not parser list order. */
	return player_id2race(index);
}

int ui_world_intro_people_count(void)
{
	return ui_world_intro_origin_count();
}

const char *ui_world_intro_people_label(int index)
{
	const struct player_race *race = ui_world_intro_origin_at(index);
	if (race) return race->name;
	return NULL;
}

static void append_class_titles(textblock *tb)
{
	/* Use character-creation order and the same level bands as the HUD.
	 * All class and title names are owned by the loaded class registry. */
	for (int i = 0; ; i++) {
		const struct player_class *class = player_id2class(i);
		if (!class) break;
		textblock_append_c(tb, COLOUR_L_BLUE, "\n%s\n", class->name);
		for (int rank = 0; rank < (int)N_ELEMENTS(class->title); rank++) {
			if (!class->title[rank]) continue;
			textblock_append(tb, "  Levels %d-%d: %s\n",
				rank * PY_TITLE_LEVELS + 1, (rank + 1) * PY_TITLE_LEVELS,
				class->title[rank]);
		}
	}
}

bool ui_world_intro_has_origins(const struct ui_world_intro *state)
{
	const struct world_story_scene *scene = state ?
		world_story_intro_at(state->tab) : NULL;
	return scene && scene->intro_content == WORLD_STORY_INTRO_ORIGINS;
}

bool ui_world_intro_measure(const struct ui_world_intro *state,
		int cols, int rows, struct ui_world_intro_layout *layout)
{
	if (!state || !layout || cols < 40 || rows < 16) return false;
	memset(layout, 0, sizeof(*layout));
	layout->width = MIN(96, cols - 8);
	layout->left = (cols - layout->width) / 2;
	layout->tab_count = world_story_intro_count();
	layout->tab_columns = MIN(MAX(1, layout->tab_count),
		MAX(1, layout->width / 20));
	layout->tab_row = 4;
	layout->top = 7 + MAX(1,
		(layout->tab_count + layout->tab_columns - 1) / layout->tab_columns);
	layout->visible_rows = rows - layout->top - 3;
	if (ui_world_intro_has_origins(state)) {
		layout->list_width = MIN(20, layout->width / 3);
		layout->list_first = MAX(0, state->origin - layout->visible_rows + 1);
	}
	layout->text_col = layout->left + layout->list_width;
	layout->text_width = layout->width - layout->list_width;
	return true;
}

textblock *ui_world_intro_document(const struct ui_world_intro *state)
{
	textblock *tb = textblock_new();
	const struct world_story_scene *scene = state ?
		world_story_intro_at(state->tab) : NULL;
	if (!scene) {
		textblock_append(tb, "The introduction is unavailable in this build.");
		return tb;
	}
	textblock_append(tb, "%s", scene->body);
	if (scene->intro_content == WORLD_STORY_INTRO_CLASSES) {
		textblock_append(tb, "\n");
		append_class_titles(tb);
		return tb;
	}
	if (ui_world_intro_has_origins(state)) {
		const struct player_race *race = ui_world_intro_origin_at(state->origin);
		char context[4096];
		if (race) {
			ui_birth_origin_context(race, context, sizeof(context));
			textblock_append_c(tb, COLOUR_L_BLUE, "\n\n%s\n\n", race->name);
			textblock_append(tb, "%s", context);
		}
	}
	return tb;
}

int ui_world_intro_max_offset(const struct ui_world_intro *state,
		const struct ui_world_intro_layout *layout)
{
	textblock *tb = ui_world_intro_document(state);
	size_t *starts = NULL, *lengths = NULL;
	int count = (int)textblock_calculate_lines(tb, &starts, &lengths,
		(size_t)MAX(1, layout->text_width));
	mem_free(starts);
	mem_free(lengths);
	textblock_free(tb);
	return MAX(0, count - layout->visible_rows);
}

void ui_world_intro_scroll(struct ui_world_intro *state, int delta,
		const struct ui_world_intro_layout *layout)
{
	state->offset = MIN(ui_world_intro_max_offset(state, layout),
		MAX(0, state->offset + delta));
}

void ui_world_intro_handle(struct ui_world_intro *state,
		enum ui_world_intro_action action, int cols, int rows)
{
	struct ui_world_intro_layout layout;
	int count = world_story_intro_count();
	if (!count || !ui_world_intro_measure(state, cols, rows, &layout)) return;
	if (action == UI_INTRO_PREVIOUS_TAB || action == UI_INTRO_NEXT_TAB) {
		state->tab = (state->tab + count +
			(action == UI_INTRO_NEXT_TAB ? 1 : -1)) % count;
		state->offset = 0;
	} else if (ui_world_intro_has_origins(state) &&
			(action == UI_INTRO_UP || action == UI_INTRO_DOWN)) {
		count = ui_world_intro_people_count();
		if (count) state->origin = (state->origin + count +
			(action == UI_INTRO_DOWN ? 1 : -1)) % count;
		state->offset = 0;
	} else {
		int delta = 0;
		switch (action) {
		case UI_INTRO_UP: delta = -1; break;
		case UI_INTRO_DOWN: delta = 1; break;
		case UI_INTRO_PAGE_UP: delta = -layout.visible_rows; break;
		case UI_INTRO_PAGE_DOWN: delta = layout.visible_rows; break;
		case UI_INTRO_FIRST_LINE: state->offset = 0; break;
		case UI_INTRO_LAST_LINE:
			state->offset = ui_world_intro_max_offset(state, &layout); break;
		default: break;
		}
		ui_world_intro_scroll(state, delta, &layout);
	}
}

bool ui_world_intro_select_at(struct ui_world_intro *state,
		int cols, int rows, int col, int row)
{
	struct ui_world_intro_layout layout;
	if (!ui_world_intro_measure(state, cols, rows, &layout)) return false;
	if (col >= layout.left && col < layout.left + layout.width) {
		/* Same integer edges as the renderer, including uneven divisions. */
		for (int i = 0; i < layout.tab_count; ++i) {
			int slot = i % layout.tab_columns;
			if (row == layout.tab_row + i / layout.tab_columns &&
					col >= layout.left + slot * layout.width / layout.tab_columns &&
					col < layout.left + (slot + 1) * layout.width / layout.tab_columns) {
				state->tab = i;
				state->offset = 0;
				return true;
			}
		}
	}
	if (layout.list_width && col >= layout.left &&
			col < layout.left + layout.list_width - 2 && row >= layout.top &&
			row < layout.top + layout.visible_rows) {
		int origin = layout.list_first + row - layout.top;
		if (!ui_world_intro_people_label(origin)) return false;
		state->origin = origin;
		state->offset = 0;
		return true;
	}
	return false;
}
