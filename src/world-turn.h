/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-turn.h
 * \brief Resolve which location-mode systems may process the current turn.
 *
 *
 */

#ifndef WORLD_TURN_H
#define WORLD_TURN_H

#include <stdbool.h>

struct player;

/** A fail-closed runtime context, distinct from parsed location metadata. */
enum world_turn_context {
	WORLD_TURN_CONTEXT_INVALID = 0,
	WORLD_TURN_CONTEXT_TOP_DOWN,
	WORLD_TURN_CONTEXT_SPELUNKING
};

/**
 * Semantic facilities owned by a runtime mode.  Callers ask for the facility
 * they need rather than inferring it from a parsed mode enum.
 */
enum world_mode_capability {
	WORLD_MODE_CAP_NATIVE_CAVE = 0,
	WORLD_MODE_CAP_SIDE_VIEW,
	WORLD_MODE_CAP_PRIMARY_INPUT,
	WORLD_MODE_CAP_MOUSE,
	WORLD_MODE_CAP_FISHING,
	WORLD_MODE_CAP_LEGACY_LEVEL_CHANGE,
	WORLD_MODE_CAP_LOCAL_STORAGE,
	WORLD_MODE_CAP_MAX
};

/** Resolve the systems allowed to process turns for \p p right now. */
enum world_turn_context world_turn_context_for_player(
		const struct player *p);

/** Whether the context names one registered, schedulable mode. */
bool world_turn_context_is_valid(enum world_turn_context context);

/** Whether the player currently resolves to one registered runtime mode. */
bool world_player_mode_is_valid(const struct player *p);

/** Query one semantic facility without exposing the mode profile. */
bool world_turn_context_has_capability(enum world_turn_context context,
		enum world_mode_capability capability);

/** Resolve the active mode and query one semantic facility, failing closed. */
bool world_player_mode_has_capability(const struct player *p,
		enum world_mode_capability capability);

#endif /* !WORLD_TURN_H */
