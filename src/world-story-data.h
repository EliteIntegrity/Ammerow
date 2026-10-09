/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-story-data.h
 * \brief Validated, data-authored story scenes.
 */

#ifndef WORLD_STORY_DATA_H
#define WORLD_STORY_DATA_H

struct file_parser;

#define WORLD_STORY_SCENE_MAX 32
#define WORLD_STORY_INTRO_MAX 6

enum world_story_intro_content {
	WORLD_STORY_INTRO_PROSE = 0,
	WORLD_STORY_INTRO_ORIGINS,
	WORLD_STORY_INTRO_CLASSES
};

/** Authored story text: a modal scene or explicitly ordered read-only intro. */
struct world_story_scene {
	char *id;
	char *title;
	char *subtitle;
	char *body;
	char *footer;
	/* Optional one-line history entry when this is the run's opening scene. */
	char *history;
	/* Optional canonical summary for the single completed-run outcome. */
	char *victory_summary;
	/* Optional one-based position in the read-only Home introduction. */
	int intro_order;
	enum world_story_intro_content intro_content;
};

extern struct file_parser story_parser;

int world_story_scene_count(void);
const struct world_story_scene *world_story_scene_by_id(const char *id);
/** Return the sole data-authored completed-run summary, if declared. */
const char *world_story_victory_summary(void);
int world_story_intro_count(void);
const struct world_story_scene *world_story_intro_at(int index);

#endif /* !WORLD_STORY_DATA_H */
