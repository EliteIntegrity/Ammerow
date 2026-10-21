/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */

#include "world-spelunking-actor-effects.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-visibility.h"
#include "init.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-slays.h"
#include "option.h"
#include "player.h"
#include "player-timed.h"
#include "project.h"
#include "z-rand.h"
#include "z-util.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

bool world_spelunk_actor_is_living(const struct world_spelunk_actor_definition *d)
{
	return d && !rf_has(d->flags, RF_UNDEAD) && !rf_has(d->flags, RF_NONLIVING);
}

bool world_spelunk_actor_projection_supported(int type)
{
	if (type >= 0 && type < ELEM_MAX) return true;
	switch (type) {
	case PROJ_LIGHT_WEAK: case PROJ_DARK_WEAK:
	case PROJ_AWAY_UNDEAD: case PROJ_AWAY_SPIRIT: case PROJ_AWAY_EVIL: case PROJ_AWAY_ALL:
	case PROJ_TURN_UNDEAD: case PROJ_TURN_EVIL: case PROJ_TURN_LIVING: case PROJ_TURN_ALL:
	case PROJ_DISP_UNDEAD: case PROJ_DISP_EVIL: case PROJ_DISP_ALL:
	case PROJ_SLEEP_UNDEAD: case PROJ_SLEEP_EVIL: case PROJ_SLEEP_ALL:
	case PROJ_MON_POLY: case PROJ_MON_HEAL: case PROJ_MON_SPEED: case PROJ_MON_SLOW:
	case PROJ_MON_CONF: case PROJ_MON_HOLD: case PROJ_MON_STUN: case PROJ_MON_DRAIN:
	case PROJ_MON_CRUSH:
		return true;
	default: return false;
	}
}

static bool matches(const struct world_spelunk_actor_definition *d, int type)
{
	switch (type) {
	case PROJ_AWAY_UNDEAD: case PROJ_TURN_UNDEAD: case PROJ_DISP_UNDEAD: case PROJ_SLEEP_UNDEAD:
		return rf_has(d->flags, RF_UNDEAD);
	case PROJ_AWAY_EVIL: case PROJ_TURN_EVIL: case PROJ_DISP_EVIL: case PROJ_SLEEP_EVIL:
		return rf_has(d->flags, RF_EVIL);
	case PROJ_AWAY_SPIRIT: return rf_has(d->flags, RF_SPIRIT);
	case PROJ_TURN_LIVING: case PROJ_MON_DRAIN: return world_spelunk_actor_is_living(d);
	default: return true;
	}
}

int world_spelunk_actor_damage(const struct world_spelunk_actor_definition *d,
		int type, int power)
{
	int resist = 0, vulnerable = 0;
	if (!d || power <= 0 || !matches(d, type)) return 0;
	switch (type) {
	case PROJ_ACID: resist = RF_IM_ACID; break;
	case PROJ_ELEC: resist = RF_IM_ELEC; break;
	case PROJ_FIRE: resist = RF_IM_FIRE; vulnerable = RF_HURT_FIRE; break;
	case PROJ_COLD: case PROJ_ICE: resist = RF_IM_COLD; vulnerable = RF_HURT_COLD; break;
	case PROJ_POIS: resist = RF_IM_POIS; break;
	case PROJ_NETHER:
		if (rf_has(d->flags, RF_UNDEAD)) return 0;
		resist = RF_IM_NETHER; break;
	case PROJ_WATER: resist = RF_IM_WATER; break;
	case PROJ_PLASMA: resist = RF_IM_PLASMA; break;
	case PROJ_NEXUS: resist = RF_IM_NEXUS; break;
	case PROJ_DISEN: resist = RF_IM_DISEN; break;
	case PROJ_LIGHT: vulnerable = RF_HURT_LIGHT; break;
	case PROJ_LIGHT_WEAK: return rf_has(d->flags, RF_HURT_LIGHT) ? power : 0;
	case PROJ_HOLY_ORB: vulnerable = RF_EVIL; break;
	case PROJ_MON_CRUSH: return power;
	case PROJ_DISP_UNDEAD: case PROJ_DISP_EVIL: case PROJ_DISP_ALL: case PROJ_MON_DRAIN:
		return power;
	default: if (type < 0 || type >= ELEM_MAX) return 0;
	}
	if (resist && rf_has(d->flags, resist)) return power / 9;
	if (vulnerable && rf_has(d->flags, vulnerable)) return MIN(power, INT_MAX / 2) * 2;
	return power;
}

bool world_spelunk_actor_destination(const struct world_spelunk_runtime *r, int x, int y)
{
	return x > 0 && y > 0 && x + 1 < r->state.map.width && y + 1 < r->state.map.height &&
		!(x == r->state.x && y == r->state.y) &&
		r->cells[y*r->state.map.width+x] == WORLD_SPELUNK_AIR &&
		r->cells[(y+1)*r->state.map.width+x] == WORLD_SPELUNK_ROCK &&
		!world_spelunk_map_blocks(&r->state.map, x, y) &&
		!world_spelunk_runtime_actor_at(r, x, y) &&
		!world_spelunk_fixture_at(r, x, y);
}

static bool teleport_actor(struct world_spelunk_runtime *r, struct world_spelunk_actor *a, int power)
{
	int x, y, best = INT_MAX, count = 0, tx = a->x, ty = a->y;
	for (y = 1; y + 1 < r->state.map.height; y++) {
		for (x = 1; x + 1 < r->state.map.width; x++) {
			int score;
			if (!world_spelunk_actor_destination(r, x, y)) continue;
			score = abs(distance(loc(a->x,a->y), loc(x,y)) - MIN(power, 1000));
			if (score > best) continue;
			if (score < best) { best = score; count = 0; }
			if (randint0(++count) == 0) { tx = x; ty = y; }
		}
	}
	if (!count) return false;
	r->detected[a->y*r->state.map.width+a->x] = 0;
	a->x = tx; a->y = ty; a->energy = 0;
	return true;
}

int world_spelunk_actor_effect(struct player *p, struct world_spelunk_actor *a, int type, int power)
{
	const struct world_spelunk_actor_definition *d = world_spelunk_actor_by_id(a->id);
	int status = -1, immunity = 0, damage;
	bool changed = false;
	bool seen = !p->timed[TMD_BLIND] && !p->timed[TMD_IMAGE] &&
		world_spelunk_visibility_is_visible(p->spelunking, a->x, a->y);
	if (!d || power < 0 || !matches(d, type)) return 0;
	damage = world_spelunk_actor_damage(d, type, power);
	if (type == PROJ_MON_CRUSH && power <= a->hp) damage = 0;
	switch (type) {
	case PROJ_MON_HEAL:
		a->hp += MIN(power, a->max_hp - a->hp); changed = true; break;
	case PROJ_AWAY_UNDEAD: case PROJ_AWAY_SPIRIT: case PROJ_AWAY_EVIL: case PROJ_AWAY_ALL:
		changed = teleport_actor(p->spelunking, a, MAX(1, power)); break;
	case PROJ_MON_POLY: {
		const struct world_spelunk_actor_definition *replacement = NULL;
		int i, choices = 0;
		/* This registry contains ground creatures only. Never import a native race. */
		for (i = 0; i < world_spelunk_actor_definition_count(); i++) {
			const struct world_spelunk_actor_definition *candidate = world_spelunk_actor_by_index(i);
			if (candidate == d) continue;
			if (randint0(++choices) == 0) replacement = candidate;
		}
		if (replacement) {
			a->hp = MAX(1, (int)((int64_t)a->hp * replacement->hitpoints / a->max_hp));
			a->max_hp = replacement->hitpoints;
			a->reward = (uint16_t)MIN(a->reward, replacement->experience);
			my_strcpy(a->id, replacement->id, sizeof(a->id));
			a->energy = 0;
			changed = true;
			if (seen) msg("The %s changes into a %s.", d->name, replacement->name);
		}
		break;
	}
	case PROJ_SLEEP_UNDEAD: case PROJ_SLEEP_EVIL: case PROJ_SLEEP_ALL:
		status = WORLD_SPELUNK_STATUS_SLEEP; immunity = RF_NO_SLEEP; break;
	case PROJ_MON_HOLD: status = WORLD_SPELUNK_STATUS_HOLD; immunity = RF_NO_HOLD; break;
	case PROJ_MON_SLOW: case PROJ_INERTIA: case PROJ_GRAVITY:
		status = WORLD_SPELUNK_STATUS_SLOW; immunity = RF_NO_SLOW; break;
	case PROJ_MON_SPEED: status = WORLD_SPELUNK_STATUS_FAST; break;
	case PROJ_MON_CONF: case PROJ_CHAOS:
		status = WORLD_SPELUNK_STATUS_CONFUSED; immunity = RF_NO_CONF; break;
	case PROJ_MON_STUN: case PROJ_SOUND: case PROJ_FORCE: case PROJ_ICE:
		status = WORLD_SPELUNK_STATUS_STUN; immunity = RF_NO_STUN; break;
	case PROJ_TURN_UNDEAD: case PROJ_TURN_EVIL: case PROJ_TURN_LIVING: case PROJ_TURN_ALL:
		status = WORLD_SPELUNK_STATUS_FEAR; immunity = RF_NO_FEAR; break;
	default: break;
	}
	if (status >= 0 && power > 0 && (!immunity || !rf_has(d->flags, immunity))) {
		/* One duration unit is ten shared world ticks, capped at 200 turns.
		 * Reapplication refreshes rather than stacking unbounded durations. */
		a->status[status] = (uint16_t)MAX(a->status[status], MIN(power, 200) * 10);
		changed = true;
	}
	if (damage > 0) a->status[WORLD_SPELUNK_STATUS_SLEEP] = 0;
	if (seen && !damage && type != PROJ_MON_POLY) {
		if (changed && status >= 0) {
			char text[128];
			world_spelunk_actor_status_text(a,text,sizeof(text));
			msg("The %s is %s.",d->name,text);
		} else if (changed && type == PROJ_MON_HEAL) msg("The %s looks healthier.",d->name);
		else if (changed) msg("The %s disappears from its perch.",d->name);
		else msg("The %s is unaffected.", d->name);
	}
	return damage;
}

static bool object_has(const struct object *o, int index, bool brand)
{
	return o && (brand ? o->brands && o->brands[index] : o->slays && o->slays[index]);
}

int world_spelunk_attack_modifier(struct player *p,
		const struct world_spelunk_actor_definition *d, struct object *first,
		struct object *second, bool melee, bool learn)
{
	int i, best = 0;
	bool pct = OPT(p, birth_percent_damage);
	learn = learn && !p->timed[TMD_BLIND] && !p->timed[TMD_IMAGE];
	for (i = 1; i < z_info->brand_max + z_info->slay_max; i++) {
		bool brand = i < z_info->brand_max;
		int index = brand ? i : i - z_info->brand_max;
		bool present, on_object;
		int mult;
		struct object *o;
		if (!index) continue;
		present = object_has(first, index, brand) || object_has(second, index, brand);
		on_object = present;
		if (melee) {
			present |= brand ? player_has_temporary_brand(p,index) : player_has_temporary_slay(p,index);
			for (o = p->gear; o; o = o->next) {
				if (object_is_equipped(p->body, o) && o != equipped_item_by_slot_name(p,"shooting"))
					on_object |= object_has(o, index, brand);
			}
			present |= on_object;
		}
		if (!present) continue;
		if (brand) {
			const struct brand *b = &brands[index];
			if (b->resist_flag && rf_has(d->flags,b->resist_flag)) continue;
			mult = pct ? b->o_multiplier : b->multiplier;
			if (b->vuln_flag && rf_has(d->flags,b->vuln_flag)) mult = pct ? 2*(mult-10)+10 : 2*mult;
			if (learn && on_object) player_learn_brand(p,index);
		} else {
			const struct slay *s = &slays[index];
			/* No cave creature currently authors a native monster-base identity. */
			if (!s->race_flag || !rf_has(d->flags,s->race_flag)) continue;
			mult = pct ? s->o_multiplier : s->multiplier;
			if (learn && on_object) player_learn_slay(p,index);
		}
		best = MAX(best,mult);
	}
	return best;
}

void world_spelunk_actor_status_text(const struct world_spelunk_actor *a, char *text, size_t size)
{
	static const char *names[] = { "asleep", "held", "slowed", "hasted", "confused", "stunned", "afraid" };
	int i;
	my_strcpy(text, "", size);
	for (i = 0; i < WORLD_SPELUNK_STATUS_MAX; i++) {
		if (!a->status[i]) continue;
		if (text[0]) my_strcat(text, ", ", size);
		my_strcat(text, names[i], size);
	}
}
