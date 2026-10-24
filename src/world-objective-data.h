/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-objective-data.h
 * \brief Validated optional-objective definitions and derived progress.
 */

#ifndef WORLD_OBJECTIVE_DATA_H
#define WORLD_OBJECTIVE_DATA_H

#include "h-basic.h"
#include "world-location.h"

struct file_parser;
struct object;
struct player;

#define WORLD_OBJECTIVE_MAX 16

enum world_objective_kind {
	WORLD_OBJECTIVE_ITEM = 0,
	WORLD_OBJECTIVE_LARDER,
	WORLD_OBJECTIVE_ROUTE,
	WORLD_OBJECTIVE_DONATION
};

struct world_objective_definition {
	const char *id;
	const char *title;
	const char *description;
	const char *reward;
	enum world_objective_kind kind;
	const char *target_id;
	const char *tval_name;
	int tval;
	int sval;
	int completion_message;
	uint32_t target;
	int32_t donation_gold;
};

/* Stable IDs, not definition indices: retain unknown IDs as claimed tombstones. */
struct world_request_state {
	bool initialized;
	uint8_t count;
	char completed[WORLD_OBJECTIVE_MAX][WORLD_ID_LEN];
};

struct world_objective_progress {
	uint32_t current;
	uint32_t target;
	bool complete;
};

extern struct file_parser objective_parser;
int world_objective_count(void);
const struct world_objective_definition *world_objective_by_index(int index);
const struct world_objective_definition *world_objective_by_id(const char *id);
bool world_objective_progress(const struct player *p,
		const struct world_objective_definition *definition,
		struct world_objective_progress *progress);
int world_objective_item_completion_message(const struct player *p,
		const struct object *obj);
bool world_request_state_valid(const struct world_request_state *state);
bool world_request_is_complete(const struct world_request_state *state,
		const char *id);
bool world_request_record(struct world_request_state *state, const char *id);
const struct world_objective_definition *world_objective_donation_request(
		const struct player *p, const struct object *obj);

#endif /* !WORLD_OBJECTIVE_DATA_H */
