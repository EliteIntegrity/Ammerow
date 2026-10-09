/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-adapter.h
 * \brief Commit spelunking actions to the authoritative Angband player.
 *
 *
 */

#ifndef WORLD_SPELUNKING_ADAPTER_H
#define WORLD_SPELUNKING_ADAPTER_H

#include "world-spelunking.h"
#include "world-spelunking-inventory.h"

struct player;

/** Result after resolving and, where appropriate, committing one command. */
enum world_spelunk_player_result {
	WORLD_SPELUNK_PLAYER_INVALID = 0,
	WORLD_SPELUNK_PLAYER_REJECTED,
	WORLD_SPELUNK_PLAYER_FREE,
	WORLD_SPELUNK_PLAYER_TURN,
	WORLD_SPELUNK_PLAYER_DEAD
};

/** Whether the player's stable location and owned runtime form one context. */
bool world_spelunk_player_is_active(const struct player *p);

/** Copy the player-owned physical resources into the active rules mirror. */
bool world_spelunk_player_sync_resources(struct player *p);

/**
 * Resolve one semantic command against the runtime owned by \p p.
 *
 * The caller must enter with no energy already committed for the player.  A
 * rejected or free action consumes no energy.  A completed action commits the
 * exact energy reported by the rules core, and any fall damage is applied to
 * the shared Angband player.  Invalid input is transactional: the runtime,
 * player health, and energy remain unchanged.
 */
enum world_spelunk_player_result world_spelunk_player_apply(
		struct player *p, const struct world_spelunk_command *command,
		struct world_spelunk_action_report *report);

#endif /* !WORLD_SPELUNKING_ADAPTER_H */
