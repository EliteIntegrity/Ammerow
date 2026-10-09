/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */
#ifndef MAP_VISUAL_DATA_H
#define MAP_VISUAL_DATA_H

#include "init.h"

/** Presentation bindings contain no atlas coordinates or gameplay rules. */
struct map_visual_binding {
	const char *scope;
	const char *semantic;
	const char *domain;
	const char *art_id;
	char key[128];
};

extern struct file_parser map_visual_parser;
const struct map_visual_binding *map_visual_for(
		const char *scope, const char *semantic);

#endif
