/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-effects.h
 * \brief Effect policy for items used in side-view locations.
 *
 *
 */

#ifndef WORLD_SPELUNKING_EFFECTS_H
#define WORLD_SPELUNKING_EFFECTS_H

#include <stdbool.h>

struct effect;
struct effect_handler_context_s;

enum world_spelunk_effect_dispatch {
	WORLD_SPELUNK_EFFECT_UNHANDLED = 0,
	WORLD_SPELUNK_EFFECT_COMPLETED,
	WORLD_SPELUNK_EFFECT_FAILED
};

/** Whether an entire effect chain is player-local in side-view mode. */
bool world_spelunk_effect_chain_is_player_local(const struct effect *effect);

/**
 * Whether every member of a player-initiated effect chain is player-local or
 * has an explicit side-view adapter.  The caller must still invoke the chain
 * with the player, or an object carried by the player, as its origin.
 */
bool world_spelunk_effect_chain_is_supported(const struct effect *effect);

/** Compatibility name for carried item-effect callers. */
bool world_spelunk_item_effect_is_safe(const struct effect *effect);

/** Whether an effect comes from the player or an object in the player's gear. */
bool world_spelunk_effect_origin_is_player_owned(
		const struct effect_handler_context_s *context);

/** Intercept map-dependent effects while the player owns a side-view map. */
enum world_spelunk_effect_dispatch world_spelunk_effect_dispatch(
		struct effect_handler_context_s *context);

#endif /* !WORLD_SPELUNKING_EFFECTS_H */
