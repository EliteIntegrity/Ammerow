/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "angband.h"
#include "player.h"
#include "player-resource.h"
#include "player-timed.h"
#include "player-util.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-rest.h"
#include "world-spelunking-visibility.h"

const char *world_spelunk_rest_block_reason(const struct player *p)
{
	const struct world_spelunk_runtime *r;
	const struct world_spelunk_fixture *fixture;
	size_t i;
	if (!p || p->is_dead || !world_spelunk_player_is_active(p))
		return "this is not a safe cave resting place.";
	r = p->spelunking;
	if (!world_spelunk_has_stable_floor(&r->state) ||
		r->cells[r->state.y * r->state.map.width + r->state.x] != WORLD_SPELUNK_AIR)
		return "you need dry, stable ground, not a rope, piton or grip.";
	if (p->timed[TMD_FOOD] < PY_FOOD_HUNGRY)
		return "you are hungry; eat before resting further.";
	if (p->word_recall || p->deep_descent)
		return "a magical journey is pending; wait manually.";
	for (i = 0; i < r->actor_count; i++) {
		const struct world_spelunk_actor *actor = &r->actors[i];
		if (world_spelunk_visibility_is_visible(r, actor->x, actor->y))
			return "a creature is in sight.";
		if (world_spelunk_visibility_is_detected(r, actor->x, actor->y))
			return "a creature is detected nearby.";
	}
	fixture = world_spelunk_fixture_at(r, r->state.x, r->state.y);
	if (fixture && fixture->known && fixture->trap_known == SPELUNK_TRAP_ARMED &&
		world_spelunk_fixture_by_id(fixture->id)->kind == SPELUNK_TRAP)
		return "you are standing on a known armed trap.";
	return NULL;
}

bool world_spelunk_rest_is_complete(const struct player *p, int count)
{
	bool hp, focus, physical;
	if (!p) return false;
	hp = p->chp >= p->mhp;
	focus = p->csp >= p->msp;
	physical = player_stamina_current(p) >= player_stamina_maximum(p) &&
		player_air_current(p) >= player_air_maximum(p);
	if (count == REST_SOME_POINTS) return hp || (p->msp > 0 && focus);
	if (count == REST_ALL_POINTS) return hp && focus && physical;
	if (count != REST_COMPLETE) return false;
	return hp && (focus || player_has(p, PF_COMBAT_REGEN)) && physical &&
		!p->timed[TMD_BLIND] && !p->timed[TMD_CONFUSED] &&
		!p->timed[TMD_POISONED] && !p->timed[TMD_AFRAID] &&
		!p->timed[TMD_TERROR] && !p->timed[TMD_STUN] &&
		!p->timed[TMD_CUT] && !p->timed[TMD_SLOW] &&
		!p->timed[TMD_PARALYZED] && !p->timed[TMD_IMAGE] &&
		!p->word_recall && !p->deep_descent;
}
