/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/legal.c
 * \brief Render the shipped legal/release manifest as a scrollable document.
 *
 *
 */

#include "sdl3/legal.h"

#include "angband.h"
#include "init.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define SDL3_LEGAL_MANIFEST "legal.txt"
#define SDL3_LEGAL_MAX_LINES 160
#define SDL3_LEGAL_TEXT 768

enum sdl3_legal_kind {
	SDL3_LEGAL_SUBTITLE = 0,
	SDL3_LEGAL_COMPONENT,
	SDL3_LEGAL_STATUS,
	SDL3_LEGAL_LICENCE,
	SDL3_LEGAL_NOTICE,
	SDL3_LEGAL_SOURCE,
	SDL3_LEGAL_BLANK,
	SDL3_LEGAL_ERROR
};

struct sdl3_legal_line {
	enum sdl3_legal_kind kind;
	char text[SDL3_LEGAL_TEXT];
};

struct sdl3_legal_manifest {
	bool loaded;
	bool valid;
	struct sdl3_legal_line lines[SDL3_LEGAL_MAX_LINES];
	int count;
};

struct sdl3_legal_sink {
	struct sdl3_visual *visual;
	const struct sdl3_theme *theme;
	int left;
	int top;
	int width;
	int offset;
	int available;
	int line;
};

static struct sdl3_legal_manifest manifest;

static char *trim(char *text)
{
	char *end;

	while (*text && isspace((unsigned char)*text)) text++;
	end = text + strlen(text);
	while (end > text && isspace((unsigned char)end[-1])) end--;
	*end = '\0';
	return text;
}

static void add_manifest_line(enum sdl3_legal_kind kind, const char *text)
{
	struct sdl3_legal_line *line;

	if (manifest.count >= SDL3_LEGAL_MAX_LINES) {
		manifest.valid = false;
		return;
	}
	line = &manifest.lines[manifest.count++];
	line->kind = kind;
	my_strcpy(line->text, text ? text : "", sizeof(line->text));
}

static bool manifest_kind(const char *key, enum sdl3_legal_kind *kind)
{
	if (streq(key, "subtitle")) *kind = SDL3_LEGAL_SUBTITLE;
	else if (streq(key, "component")) *kind = SDL3_LEGAL_COMPONENT;
	else if (streq(key, "status")) *kind = SDL3_LEGAL_STATUS;
	else if (streq(key, "licence")) *kind = SDL3_LEGAL_LICENCE;
	else if (streq(key, "notice")) *kind = SDL3_LEGAL_NOTICE;
	else if (streq(key, "source")) *kind = SDL3_LEGAL_SOURCE;
	else if (streq(key, "end")) *kind = SDL3_LEGAL_BLANK;
	else return false;
	return true;
}

static void load_manifest(void)
{
	char path[1024];
	char raw[SDL3_LEGAL_TEXT + 64];
	ang_file *file;
	int version = 0;
	bool component_active = false;
	bool component_status = false;
	bool component_licence = false;
	bool component_source = false;

	if (manifest.loaded) return;
	manifest.loaded = true;
	manifest.valid = true;
	path_build(path, sizeof(path), ANGBAND_DIR_SCREENS,
		SDL3_LEGAL_MANIFEST);
	file = file_open(path, MODE_READ, FTYPE_TEXT);
	if (!file) {
		manifest.valid = false;
		add_manifest_line(SDL3_LEGAL_ERROR,
			"The legal manifest is missing from this build.");
		return;
	}
	while (file_getl(file, raw, sizeof(raw))) {
		char *key = trim(raw);
		char *value;
		enum sdl3_legal_kind kind;

		if (!key[0] || key[0] == '#') continue;
		value = strchr(key, ':');
		if (!value) {
			manifest.valid = false;
			add_manifest_line(SDL3_LEGAL_ERROR,
				"Malformed entry in the shipped legal manifest.");
			continue;
		}
		*value++ = '\0';
		value = trim(value);
		if (streq(key, "version")) {
			version = atoi(value);
			continue;
		}
		if (!manifest_kind(key, &kind)) {
			manifest.valid = false;
			add_manifest_line(SDL3_LEGAL_ERROR,
				"Unknown entry in the shipped legal manifest.");
			continue;
		}
		if (kind == SDL3_LEGAL_COMPONENT) {
			if (component_active) {
				manifest.valid = false;
				add_manifest_line(SDL3_LEGAL_ERROR,
					"A legal component is missing its end marker.");
			}
			component_active = true;
			component_status = false;
			component_licence = false;
			component_source = false;
		} else if (kind == SDL3_LEGAL_STATUS && component_active) {
			component_status = true;
		} else if (kind == SDL3_LEGAL_LICENCE && component_active) {
			component_licence = true;
		} else if (kind == SDL3_LEGAL_SOURCE && component_active) {
			component_source = true;
		} else if (kind == SDL3_LEGAL_BLANK && component_active) {
			if (!component_status || !component_licence || !component_source) {
				manifest.valid = false;
				add_manifest_line(SDL3_LEGAL_ERROR,
					"A legal component lacks status, licence, or source data.");
			}
			component_active = false;
		}
		add_manifest_line(kind, value);
	}
	file_close(file);
	if (component_active &&
			(!component_status || !component_licence || !component_source)) {
		manifest.valid = false;
		add_manifest_line(SDL3_LEGAL_ERROR,
			"A legal component lacks status, licence, or source data.");
	}
	if (version != 1) {
		manifest.valid = false;
		add_manifest_line(SDL3_LEGAL_ERROR,
			"Unsupported legal manifest version.");
	}
}

bool sdl3_legal_manifest_valid(void)
{
	load_manifest();
	return manifest.valid;
}

static void output_line(struct sdl3_legal_sink *sink, const char *text,
		int length, int indent, SDL_Color color)
{
	if (sink->visual && sink->line >= sink->offset &&
			sink->line < sink->offset + sink->available && length > 0) {
		char buffer[SDL3_LEGAL_TEXT];
		int copy = SDL_min(length, (int)sizeof(buffer) - 1);

		memcpy(buffer, text, copy);
		buffer[copy] = '\0';
		sdl3_ui_draw_text(sink->visual, buffer, sink->left + indent,
			sink->top + sink->line - sink->offset,
			sink->width - indent, color);
	}
	sink->line++;
}

static void output_wrapped(struct sdl3_legal_sink *sink, const char *text,
		int indent, SDL_Color color)
{
	const char *cursor = text;
	int available = SDL_max(1, sink->width - indent);

	while (cursor && *cursor) {
		const char *end;
		const char *break_at = NULL;
		int length = 0;

		while (*cursor && isspace((unsigned char)*cursor)) cursor++;
		if (!*cursor) break;
		end = cursor;
		while (*end && length < available) {
			if (isspace((unsigned char)*end)) break_at = end;
			end++;
			length++;
		}
		if (*end && break_at && break_at > cursor) end = break_at;
		while (end > cursor && isspace((unsigned char)end[-1])) end--;
		output_line(sink, cursor, (int)(end - cursor), indent, color);
		cursor = end;
	}
}

static void output_labelled(struct sdl3_legal_sink *sink, const char *label,
		const char *text, SDL_Color color)
{
	char buffer[SDL3_LEGAL_TEXT + 32];

	strnfmt(buffer, sizeof(buffer), "%s  %s", label, text);
	output_wrapped(sink, buffer, 2, color);
}

static void visit_manifest(struct sdl3_legal_sink *sink)
{
	int i;

	load_manifest();
	for (i = 0; i < manifest.count; i++) {
		const struct sdl3_legal_line *line = &manifest.lines[i];

		switch (line->kind) {
		case SDL3_LEGAL_SUBTITLE:
			output_wrapped(sink, line->text, 0, sink->theme->muted);
			sink->line++;
			break;
		case SDL3_LEGAL_COMPONENT:
			output_wrapped(sink, line->text, 0, sink->theme->title);
			break;
		case SDL3_LEGAL_STATUS:
			output_labelled(sink, "STATUS", line->text, sink->theme->accent);
			break;
		case SDL3_LEGAL_LICENCE:
			output_labelled(sink, "LICENCE", line->text, sink->theme->text);
			break;
		case SDL3_LEGAL_SOURCE:
			output_labelled(sink, "SOURCE", line->text, sink->theme->muted);
			break;
		case SDL3_LEGAL_NOTICE:
			output_wrapped(sink, line->text, 2, sink->theme->text);
			break;
		case SDL3_LEGAL_ERROR:
			output_wrapped(sink, line->text, 0,
				sdl3_theme_color(sink->theme, COLOUR_L_RED));
			break;
		case SDL3_LEGAL_BLANK:
		default:
			sink->line++;
			break;
		}
	}
}

static int manifest_line_count(int width)
{
	const struct sdl3_theme counting_theme = { 0 };
	struct sdl3_legal_sink sink = {
		NULL, &counting_theme, 0, 0, SDL_max(12, width), 0, 0, 0
	};

	visit_manifest(&sink);
	return sink.line;
}

int sdl3_legal_max_offset(int width, int available_rows)
{
	return SDL_max(0, manifest_line_count(width) - SDL_max(1, available_rows));
}

void sdl3_legal_draw(SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width, int offset)
{
	int top = 7;
	int available = SDL_max(1, visual->rows - top - 4);
	int maximum = sdl3_legal_max_offset(width, available);
	struct sdl3_legal_sink sink = {
		visual, theme, left, top, width, SDL_clamp(offset, 0, maximum),
		available, 0
	};
	char position[96];

	(void)renderer;
	sdl3_ui_draw_text(visual, "LEGAL / LICENCES", left, 3, width,
		theme->title);
	sdl3_ui_draw_text(visual,
		"Summary only. Full terms are in the accompanying licence files.",
		left, 5, width, theme->muted);
	visit_manifest(&sink);
	strnfmt(position, sizeof(position),
		"Up/Down scroll   %d / %d   Escape returns home",
		SDL_clamp(offset, 0, maximum) + 1, maximum + 1);
	sdl3_ui_draw_text(visual, position, left, visual->rows - 3, width,
		theme->muted);
}
