/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-inventory.h
 * \brief Shared-inventory actions for the spelunking location mode.
 *
 *
 */

#ifndef WORLD_SPELUNKING_INVENTORY_H
#define WORLD_SPELUNKING_INVENTORY_H

struct object;
struct player;

enum world_spelunk_local_action {
	WORLD_SPELUNK_LOCAL_PITON = 0,
	WORLD_SPELUNK_LOCAL_ROPE,
	WORLD_SPELUNK_LOCAL_PICKUP,
	WORLD_SPELUNK_LOCAL_DROP
};

enum world_spelunk_local_result {
	WORLD_SPELUNK_LOCAL_INVALID = 0,
	WORLD_SPELUNK_LOCAL_NO_TOOL,
	WORLD_SPELUNK_LOCAL_UNSTABLE,
	WORLD_SPELUNK_LOCAL_ALREADY_PRESENT,
	WORLD_SPELUNK_LOCAL_NO_BRACE,
	WORLD_SPELUNK_LOCAL_NO_ANCHOR,
	WORLD_SPELUNK_LOCAL_NO_SHAFT,
	WORLD_SPELUNK_LOCAL_NOTHING_HERE,
	WORLD_SPELUNK_LOCAL_NO_ROOM,
	WORLD_SPELUNK_LOCAL_TURN
};

struct world_spelunk_local_report {
	enum world_spelunk_local_action action;
	enum world_spelunk_local_result result;
	unsigned int energy_use;
	int affected;
	int completion_message;
};

/** Commit a mode-owned tool placement or pickup through shared inventory. */
enum world_spelunk_local_result world_spelunk_player_apply_local(
		struct player *p, enum world_spelunk_local_action action,
		struct world_spelunk_local_report *report);

/** Move part of one carried stack to the side-view ground-object owner. */
enum world_spelunk_local_result world_spelunk_player_drop(
		struct player *p, struct object *obj, int amount,
		struct world_spelunk_local_report *report);

#endif /* !WORLD_SPELUNKING_INVENTORY_H */
