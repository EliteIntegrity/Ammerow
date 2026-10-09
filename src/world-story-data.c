/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-story-data.c
 * \brief Parser and immutable registry for concise story scenes.
 */

#include "angband.h"
#include "datafile.h"
#include "world-story-data.h"

enum story_field {
	STORY_FIELD_BODY = 0x01
};

struct story_record {
	struct world_story_scene scene;
	unsigned int fields;
	struct story_record *next;
};

struct story_parse_state {
	int count;
	struct story_record *records;
	struct story_record *current;
};

static struct world_story_scene *story_scenes;
static int story_scene_count;

static void free_scene(struct world_story_scene *scene)
{
	if (!scene) return;
	string_free(scene->id);
	string_free(scene->title);
	string_free(scene->subtitle);
	string_free(scene->body);
	string_free(scene->footer);
	string_free(scene->history);
	string_free(scene->victory_summary);
}

static void cleanup_story(void)
{
	int i;

	for (i = 0; i < story_scene_count; i++) free_scene(&story_scenes[i]);
	mem_free(story_scenes);
	story_scenes = NULL;
	story_scene_count = 0;
}

static void free_parse_state(struct story_parse_state *state)
{
	struct story_record *record;

	if (!state) return;
	record = state->records;
	while (record) {
		struct story_record *next = record->next;

		free_scene(&record->scene);
		mem_free(record);
		record = next;
	}
	mem_free(state);
}

static enum parser_error parse_story_scene(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	const char *id = parser_getsym(p, "id");
	const char *title = parser_getstr(p, "title");
	struct story_record *cursor;
	struct story_record *record;

	if (!world_id_is_valid(id) || !title[0]) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	for (cursor = state ? state->records : NULL; cursor;
			cursor = cursor->next) {
		if (streq(cursor->scene.id, id)) {
			return PARSE_ERROR_REPEATED_DIRECTIVE;
		}
	}
	if (!state || state->count >= WORLD_STORY_SCENE_MAX) {
		return PARSE_ERROR_TOO_MANY_ENTRIES;
	}
	record = mem_zalloc(sizeof(*record));
	record->scene.id = string_make(id);
	record->scene.title = string_make(title);
	record->next = state->records;
	state->records = record;
	state->current = record;
	state->count++;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_story_subtitle(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state || !state->current) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->scene.subtitle) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->scene.subtitle = string_make(text);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_story_body(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state || !state->current) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->scene.body) {
		state->current->scene.body = string_append(
			state->current->scene.body, "\n\n");
		state->current->scene.body = string_append(
			state->current->scene.body, text);
	} else {
		state->current->scene.body = string_make(text);
	}
	state->current->fields |= STORY_FIELD_BODY;
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_story_footer(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state || !state->current) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->scene.footer) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->scene.footer = string_make(text);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_story_history(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state || !state->current) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->scene.history) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->scene.history = string_make(text);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_story_victory_summary(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	const char *text = parser_getstr(p, "text");

	if (!state || !state->current) {
		return PARSE_ERROR_MISSING_RECORD_HEADER;
	}
	if (!text[0]) return PARSE_ERROR_INVALID_VALUE;
	if (state->current->scene.victory_summary) {
		return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	state->current->scene.victory_summary = string_make(text);
	return PARSE_ERROR_NONE;
}

static enum parser_error parse_story_intro(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	const char *content = parser_getsym(p, "content");
	int order = parser_getint(p, "order");

	if (!state || !state->current) return PARSE_ERROR_MISSING_RECORD_HEADER;
	if (state->current->scene.intro_order) return PARSE_ERROR_REPEATED_DIRECTIVE;
	if (order < 1 || order > WORLD_STORY_INTRO_MAX ||
			(!streq(content, "prose") && !streq(content, "origins") &&
			 !streq(content, "classes"))) {
		return PARSE_ERROR_INVALID_VALUE;
	}
	state->current->scene.intro_order = order;
	state->current->scene.intro_content = streq(content, "origins") ?
		WORLD_STORY_INTRO_ORIGINS : streq(content, "classes") ?
		WORLD_STORY_INTRO_CLASSES : WORLD_STORY_INTRO_PROSE;
	return PARSE_ERROR_NONE;
}

static struct parser *init_parse_story(void)
{
	struct parser *p = parser_new();
	struct story_parse_state *state = mem_zalloc(sizeof(*state));

	parser_setpriv(p, state);
	parser_reg(p, "scene sym id str title", parse_story_scene);
	parser_reg(p, "subtitle str text", parse_story_subtitle);
	parser_reg(p, "body str text", parse_story_body);
	parser_reg(p, "footer str text", parse_story_footer);
	parser_reg(p, "history str text", parse_story_history);
	parser_reg(p, "intro int order sym content", parse_story_intro);
	parser_reg(p, "victory-summary str text",
		parse_story_victory_summary);
	return p;
}

static errr run_parse_story(struct parser *p)
{
	return parse_file_quit_not_found(p, "story");
}

static errr finish_parse_story(struct parser *p)
{
	struct story_parse_state *state = parser_priv(p);
	struct story_record *record;
	struct story_record *next;
	errr r = PARSE_ERROR_NONE;
	int victory_summaries = 0;
	bool intro_orders[WORLD_STORY_INTRO_MAX] = { false };
	int intro_count = 0;
	int index;

	if (!state || !state->count) r = PARSE_ERROR_TOO_FEW_ENTRIES;
	for (record = state ? state->records : NULL; record;
			record = record->next) {
		if ((record->fields & STORY_FIELD_BODY) == 0) {
			r = PARSE_ERROR_TOO_FEW_ENTRIES;
			break;
		}
		if (record->scene.victory_summary && ++victory_summaries > 1) {
			r = PARSE_ERROR_REPEATED_DIRECTIVE;
			break;
		}
		if (record->scene.intro_order) {
			int order = record->scene.intro_order - 1;
			if (intro_orders[order] || record->scene.history ||
					record->scene.victory_summary) {
				r = PARSE_ERROR_INVALID_VALUE;
				break;
			}
			intro_orders[order] = true;
			intro_count++;
		}
	}
	for (index = 0; index < intro_count; ++index) {
		if (!intro_orders[index]) r = PARSE_ERROR_INVALID_VALUE;
	}
	if (r != PARSE_ERROR_NONE) {
		free_parse_state(state);
		parser_destroy(p);
		return r;
	}
	cleanup_story();
	story_scenes = mem_zalloc((size_t)state->count * sizeof(*story_scenes));
	index = state->count - 1;
	for (record = state->records; record; record = next, index--) {
		next = record->next;
		story_scenes[index] = record->scene;
		mem_free(record);
	}
	story_scene_count = state->count;
	mem_free(state);
	parser_destroy(p);
	return PARSE_ERROR_NONE;
}

struct file_parser story_parser = {
	"story", init_parse_story, run_parse_story, finish_parse_story,
	cleanup_story
};

int world_story_scene_count(void)
{
	return story_scene_count;
}

const struct world_story_scene *world_story_scene_by_id(const char *id)
{
	int i;

	if (!id) return NULL;
	for (i = 0; i < story_scene_count; i++) {
		if (streq(story_scenes[i].id, id)) return &story_scenes[i];
	}
	return NULL;
}

const char *world_story_victory_summary(void)
{
	int i;

	for (i = 0; i < story_scene_count; i++) {
		if (story_scenes[i].victory_summary) {
			return story_scenes[i].victory_summary;
		}
	}
	return NULL;
}

int world_story_intro_count(void)
{
	int count = 0;
	for (int i = 0; i < story_scene_count; ++i) {
		if (story_scenes[i].intro_order) ++count;
	}
	return count;
}

const struct world_story_scene *world_story_intro_at(int index)
{
	if (index < 0 || index >= WORLD_STORY_INTRO_MAX) return NULL;
	for (int i = 0; i < story_scene_count; ++i) {
		if (story_scenes[i].intro_order == index + 1) return &story_scenes[i];
	}
	return NULL;
}
