/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-turn.c
 * \brief Resolve which location-mode systems may process the current turn.
 *
 *
 */

#include "world-turn.h"

#include "game-world.h"
#include "world-location.h"
#include "world-spelunking-adapter.h"

#include <stdint.h>

#define MODE_CAP(capability) (UINT32_C(1) << (capability))

struct world_mode_profile {
	enum world_turn_context context;
	uint32_t capabilities;
};

static const struct world_mode_profile mode_profiles[] = {
	{
		WORLD_TURN_CONTEXT_TOP_DOWN,
		MODE_CAP(WORLD_MODE_CAP_NATIVE_CAVE) |
		MODE_CAP(WORLD_MODE_CAP_MOUSE) |
		MODE_CAP(WORLD_MODE_CAP_FISHING) |
		MODE_CAP(WORLD_MODE_CAP_LEGACY_LEVEL_CHANGE)
	},
	{
		WORLD_TURN_CONTEXT_SPELUNKING,
		MODE_CAP(WORLD_MODE_CAP_SIDE_VIEW) |
		MODE_CAP(WORLD_MODE_CAP_PRIMARY_INPUT) |
		MODE_CAP(WORLD_MODE_CAP_FISHING) |
		MODE_CAP(WORLD_MODE_CAP_LOCAL_STORAGE)
	}
};

static const struct world_mode_profile *profile_for_context(
		enum world_turn_context context)
{
	size_t i;

	for (i = 0; i < N_ELEMENTS(mode_profiles); i++) {
		if (mode_profiles[i].context == context) return &mode_profiles[i];
	}
	return NULL;
}

enum world_turn_context world_turn_context_for_player(
		const struct player *p)
{
	const struct level *level = world_player_level(p);

	if (!level) return WORLD_TURN_CONTEXT_INVALID;
	switch (level->mode) {
	case WORLD_MODE_TOP_DOWN:
		return WORLD_TURN_CONTEXT_TOP_DOWN;
	case WORLD_MODE_SPELUNKING:
		return world_spelunk_player_is_active(p) ?
			WORLD_TURN_CONTEXT_SPELUNKING :
			WORLD_TURN_CONTEXT_INVALID;
	default:
		return WORLD_TURN_CONTEXT_INVALID;
	}
}

bool world_turn_context_is_valid(enum world_turn_context context)
{
	return profile_for_context(context) != NULL;
}

bool world_player_mode_is_valid(const struct player *p)
{
	return world_turn_context_is_valid(world_turn_context_for_player(p));
}

bool world_turn_context_has_capability(enum world_turn_context context,
		enum world_mode_capability capability)
{
	const struct world_mode_profile *profile = profile_for_context(context);

	if (!profile || capability < 0 || capability >= WORLD_MODE_CAP_MAX) {
		return false;
	}
	return (profile->capabilities & MODE_CAP(capability)) != 0;
}

bool world_player_mode_has_capability(const struct player *p,
		enum world_mode_capability capability)
{
	return world_turn_context_has_capability(
		world_turn_context_for_player(p), capability);
}
