/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-layout-data.h
 * \brief Parsed, revisioned definitions for authored spelunking layouts.
 */

#ifndef WORLD_SPELUNKING_LAYOUT_DATA_H
#define WORLD_SPELUNKING_LAYOUT_DATA_H

#include "init.h"
#include "world-spelunking-perception.h"
#include "world-spelunking-runtime.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define WORLD_SPELUNK_LAYOUT_DEFINITION_MAX 16
#define WORLD_SPELUNK_LAYOUT_OPERATION_MAX 128
#define WORLD_SPELUNK_LAYOUT_OBJECT_MAX 32

struct object_kind;

enum world_spelunk_layout_shape {
	WORLD_SPELUNK_LAYOUT_RECTANGLE = 0,
	WORLD_SPELUNK_LAYOUT_ELLIPSE
};

enum world_spelunk_layout_match {
	WORLD_SPELUNK_LAYOUT_MATCH_ANY = 0,
	WORLD_SPELUNK_LAYOUT_MATCH_AIR,
	WORLD_SPELUNK_LAYOUT_MATCH_ROCK,
	WORLD_SPELUNK_LAYOUT_MATCH_WATER
};

/** One explicitly ordered terrain operation in an authored revision. */
struct world_spelunk_layout_operation {
	uint16_t revision;
	uint16_t priority;
	enum world_spelunk_layout_shape shape;
	enum world_spelunk_layout_match match;
	enum world_spelunk_tile result;
	int x1;
	int y1;
	int x2;
	int y2;
};

/** One ordinary object introduced by an append-only layout revision. */
struct world_spelunk_layout_object {
	const char *id;
	uint16_t revision;
	uint16_t priority;
	int x;
	int y;
	const char *tval_name;
	const char *item_name;
	struct object_kind *kind;
};

/** Immutable data definition keyed by the world's stable location ID. */
struct world_spelunk_layout_definition {
	const char *id;
	uint16_t width;
	uint16_t height;
	uint16_t initial_stamina;
	uint16_t current_revision;
	enum world_spelunk_tile fill;
	struct world_spelunk_rules rules;
	struct world_spelunk_perception perception;
	struct world_spelunk_layout_operation operations[
		WORLD_SPELUNK_LAYOUT_OPERATION_MAX];
	int operation_count;
	struct world_spelunk_layout_object objects[
		WORLD_SPELUNK_LAYOUT_OBJECT_MAX];
	int object_count;
};

extern struct file_parser spelunking_layout_parser;

int world_spelunk_layout_definition_count(void);
const struct world_spelunk_layout_definition *
	world_spelunk_layout_definition_by_index(int index);
const struct world_spelunk_layout_definition *
	world_spelunk_layout_definition_by_id(const char *id);

/** Copy authored balance rules and inject the engine's action-energy unit. */
bool world_spelunk_layout_rules(
		const struct world_spelunk_layout_definition *definition,
		unsigned int action_energy, struct world_spelunk_rules *rules);

/** Build the current complete terrain template. */
bool world_spelunk_layout_build_cells(
		const struct world_spelunk_layout_definition *definition,
		enum world_spelunk_tile *cells, int width, int height);

/** Apply only append-only operations newer than from_revision. */
bool world_spelunk_layout_migrate_cells(
		const struct world_spelunk_layout_definition *definition,
		enum world_spelunk_tile *cells, int width, int height,
		uint16_t from_revision);

/** Cross-check definitions against world entries and actor spawn support. */
bool world_spelunk_layout_data_validate_world(void);

#endif /* !WORLD_SPELUNKING_LAYOUT_DATA_H */
