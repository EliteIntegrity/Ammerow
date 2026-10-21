/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef WORLD_SPELUNKING_DOOR_H
#define WORLD_SPELUNKING_DOOR_H

#include "world-spelunking-runtime.h"

struct file_parser;
struct world_spelunk_door_definition {
	char id[WORLD_ID_LEN];
	char name[80];
	uint8_t lock_power;
};

extern struct file_parser spelunking_door_parser;
const struct world_spelunk_door_definition *world_spelunk_door_by_id(const char *id);
const struct world_spelunk_door *world_spelunk_door_at(
	const struct world_spelunk_runtime *r, int x, int y);
struct world_spelunk_door *world_spelunk_door_at_mutable(
	struct world_spelunk_runtime *r, int x, int y);
bool world_spelunk_doors_valid(const struct world_spelunk_runtime *r);
bool world_spelunk_door_site(const struct world_spelunk_runtime *r, int x, int y);
bool world_spelunk_door_add(struct world_spelunk_runtime *r,
	const char *id, int x, int y);
/** Rebuild collision cache after decode or a validated state change. */
void world_spelunk_doors_rebuild(struct world_spelunk_runtime *r);
/** Remember only currently observable states, not unseen changes or locks. */
void world_spelunk_doors_observe(struct world_spelunk_runtime *r);
bool world_spelunk_door_set_open(struct world_spelunk_runtime *r,
	int x, int y, bool open);
#endif
