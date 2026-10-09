/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/effects.c
 * \brief Small, fixed-state combat presentation effects.
 */

#include "angband.h"

#include "sdl3/effects.h"

static void clear_effect(struct sdl3_combat_effect *effect)
{
	if (!effect) return;
	memset(effect, 0, sizeof(*effect));
}

static void summarize_effects(struct sdl3_combat_effects *effects)
{
	int i;

	effects->active = false;
	effects->next_frame_ms = UINT64_MAX;
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		const struct sdl3_combat_effect *effect = &effects->items[i];

		if (!effect->active) continue;
		effects->active = true;
		effects->next_frame_ms = MIN(effects->next_frame_ms,
			effect->next_frame_ms);
	}
	if (!effects->active) effects->next_frame_ms = 0;
}

static struct sdl3_combat_effect *reserve_effect(
		struct sdl3_combat_effects *effects)
{
	struct sdl3_combat_effect *oldest;
	int i;

	if (!effects) return NULL;
	oldest = &effects->items[0];
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		if (!effects->items[i].active) return &effects->items[i];
		if (effects->items[i].started_ms < oldest->started_ms) {
			oldest = &effects->items[i];
		}
	}
	return oldest;
}

static void begin_effect(struct sdl3_combat_effects *effects,
		struct sdl3_combat_effect *effect, enum sdl3_effect_kind kind,
		uint64_t now_ms, uint64_t duration_ms)
{
	clear_effect(effect);
	effect->active = true;
	effect->kind = kind;
	effect->started_ms = now_ms;
	effect->duration_ms = MAX((uint64_t)1, duration_ms);
	effect->next_frame_ms = now_ms + SDL3_EFFECT_FRAME_MS;
	effect->presented = false;
	effect->serial = effects->next_serial++;
	summarize_effects(effects);
}

void sdl3_effects_clear(struct sdl3_combat_effects *effects)
{
	if (!effects) return;
	memset(effects, 0, sizeof(*effects));
}

void sdl3_effects_start_melee(struct sdl3_combat_effects *effects,
		uint64_t now_ms, struct loc source, struct loc target, bool hit)
{
	struct sdl3_combat_effect *effect = NULL;
	int i;

	if (!effects) return;
	/* Multiple blows from one actor resolve in a single game turn.  Refresh the
	 * same lunge instead of drawing several copies of that actor at once. */
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		if (effects->items[i].active &&
				effects->items[i].kind == SDL3_EFFECT_MELEE &&
				loc_eq(effects->items[i].source, source) &&
				loc_eq(effects->items[i].target, target)) {
			effect = &effects->items[i];
			break;
		}
	}
	if (!effect) effect = reserve_effect(effects);
	if (!effect) return;
	begin_effect(effects, effect, SDL3_EFFECT_MELEE, now_ms,
		SDL3_MELEE_EFFECT_MS);
	effect->source = source;
	effect->target = target;
	effect->hit = hit;
}

void sdl3_effects_start_projectile(struct sdl3_combat_effects *effects,
		enum sdl3_effect_kind kind, uint64_t now_ms, uint64_t duration_ms,
		struct loc source, struct loc target, wchar_t glyph, uint8_t attr)
{
	struct sdl3_combat_effect *effect = NULL;
	int i;

	if (!effects || (kind != SDL3_EFFECT_MISSILE &&
			kind != SDL3_EFFECT_BOLT)) return;
	/* A missile or bolt is reported one map cell at a time.  Keep one moving
	 * presentation object for that path instead of creating a short-lived
	 * object for every cell. */
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		if (effects->items[i].active && effects->items[i].kind == kind) {
			effect = &effects->items[i];
			break;
		}
	}
	if (!effect) effect = reserve_effect(effects);
	if (!effect) return;
	begin_effect(effects, effect, kind, now_ms,
		MAX(duration_ms, (uint64_t)SDL3_PROJECTILE_EFFECT_MS));
	effect->source = source;
	effect->target = target;
	effect->glyph = glyph;
	effect->attr = attr;
}

void sdl3_effects_start_damage(struct sdl3_combat_effects *effects,
		uint64_t now_ms, struct loc target, int amount, bool target_is_player)
{
	struct sdl3_combat_effect *effect;

	if (!effects || amount <= 0) return;
	effect = reserve_effect(effects);
	if (!effect) return;
	begin_effect(effects, effect, SDL3_EFFECT_DAMAGE, now_ms,
		SDL3_DAMAGE_EFFECT_MS);
	effect->source = target;
	effect->target = target;
	effect->amount = amount;
	effect->target_is_player = target_is_player;
}

bool sdl3_effects_present(struct sdl3_combat_effects *effects,
		uint64_t now_ms)
{
	bool changed = false;
	int i;

	if (!effects || !effects->active) return false;
	/* Core combat can emit several events before the frontend presents again.
	 * Start their visual clocks on that first presentation so no effect can
	 * expire invisibly while the turn is still resolving. */
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		struct sdl3_combat_effect *effect = &effects->items[i];

		if (!effect->active || effect->presented) continue;
		effect->presented = true;
		effect->started_ms = now_ms;
		effect->next_frame_ms = now_ms + SDL3_EFFECT_FRAME_MS;
		changed = true;
	}
	if (changed) summarize_effects(effects);
	return changed;
}

bool sdl3_effects_advance(struct sdl3_combat_effects *effects,
		uint64_t now_ms)
{
	bool changed = false;
	int i;

	if (!effects || !effects->active) return false;
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		struct sdl3_combat_effect *effect = &effects->items[i];
		uint64_t end;

		if (!effect->active) continue;
		if (!effect->presented) {
			effect->presented = true;
			effect->started_ms = now_ms;
			effect->next_frame_ms = now_ms + SDL3_EFFECT_FRAME_MS;
			changed = true;
			continue;
		}
		end = effect->started_ms + effect->duration_ms;
		if (now_ms >= end) {
			clear_effect(effect);
			changed = true;
			continue;
		}
		if (now_ms >= effect->next_frame_ms) {
			effect->next_frame_ms = MIN(end,
				now_ms + (uint64_t)SDL3_EFFECT_FRAME_MS);
			changed = true;
		}
	}
	summarize_effects(effects);
	return changed;
}

bool sdl3_effects_has_projectile(const struct sdl3_combat_effects *effects)
{
	int i;

	if (!effects) return false;
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		enum sdl3_effect_kind kind = effects->items[i].kind;

		if (effects->items[i].active && (kind == SDL3_EFFECT_MISSILE ||
				kind == SDL3_EFFECT_BOLT)) return true;
	}
	return false;
}

void sdl3_effects_clear_projectiles(struct sdl3_combat_effects *effects)
{
	int i;

	if (!effects) return;
	for (i = 0; i < SDL3_EFFECT_CAPACITY; i++) {
		enum sdl3_effect_kind kind = effects->items[i].kind;

		if (kind == SDL3_EFFECT_MISSILE || kind == SDL3_EFFECT_BOLT) {
			clear_effect(&effects->items[i]);
		}
	}
	summarize_effects(effects);
}

int sdl3_effect_phase(const struct sdl3_combat_effect *effect,
		uint64_t now_ms, int phase_count)
{
	uint64_t elapsed;
	int phase;

	if (!effect || !effect->active || !effect->duration_ms ||
			phase_count <= 1) return 0;
	elapsed = now_ms > effect->started_ms ? now_ms - effect->started_ms : 0;
	phase = (int)(elapsed * (uint64_t)phase_count / effect->duration_ms);
	return MIN(phase_count - 1, MAX(0, phase));
}
