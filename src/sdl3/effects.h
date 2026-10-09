/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/effects.h
 * \brief Small, fixed-state combat presentation effects.
 */

#ifndef INCLUDED_SDL3_EFFECTS_H
#define INCLUDED_SDL3_EFFECTS_H

#include "z-type.h"

#define SDL3_EFFECT_FRAME_MS 24
#define SDL3_MELEE_EFFECT_MS 240
#define SDL3_PROJECTILE_EFFECT_MS 180
#define SDL3_DAMAGE_EFFECT_MS 1000
#define SDL3_EFFECT_CAPACITY 16

enum sdl3_effect_kind {
	SDL3_EFFECT_NONE = 0,
	SDL3_EFFECT_MELEE,
	SDL3_EFFECT_MISSILE,
	SDL3_EFFECT_BOLT,
	SDL3_EFFECT_DAMAGE
};

struct sdl3_combat_effect {
	bool active;
	enum sdl3_effect_kind kind;
	struct loc source;
	struct loc target;
	bool hit;
	bool target_is_player;
	wchar_t glyph;
	uint8_t attr;
	int amount;
	uint32_t serial;
	bool presented;
	uint64_t started_ms;
	uint64_t duration_ms;
	uint64_t next_frame_ms;
};

struct sdl3_combat_effects {
	struct sdl3_combat_effect items[SDL3_EFFECT_CAPACITY];
	bool active;
	uint64_t next_frame_ms;
	uint32_t next_serial;
};

void sdl3_effects_clear(struct sdl3_combat_effects *effects);
void sdl3_effects_start_melee(struct sdl3_combat_effects *effects,
		uint64_t now_ms, struct loc source, struct loc target, bool hit);
void sdl3_effects_start_projectile(struct sdl3_combat_effects *effects,
		enum sdl3_effect_kind kind, uint64_t now_ms, uint64_t duration_ms,
		struct loc source, struct loc target, wchar_t glyph, uint8_t attr);
void sdl3_effects_start_damage(struct sdl3_combat_effects *effects,
		uint64_t now_ms, struct loc target, int amount, bool target_is_player);
bool sdl3_effects_present(struct sdl3_combat_effects *effects,
		uint64_t now_ms);
bool sdl3_effects_advance(struct sdl3_combat_effects *effects,
		uint64_t now_ms);
bool sdl3_effects_has_projectile(const struct sdl3_combat_effects *effects);
void sdl3_effects_clear_projectiles(struct sdl3_combat_effects *effects);
int sdl3_effect_phase(const struct sdl3_combat_effect *effect,
		uint64_t now_ms, int phase_count);

#endif /* INCLUDED_SDL3_EFFECTS_H */
