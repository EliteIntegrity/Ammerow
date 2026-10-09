/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/** \file player-resource.c
 * \brief Data-authored physical resource rules shared by all world modes.
 */

#include "player-resource.h"

#include "init.h"
#include "player.h"
#include "player-calcs.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking.h"
#include "world-spelunking-runtime.h"

#include <limits.h>

/* Safe defaults also keep narrow unit-test fixtures useful.  Production
 * values are authored in constants.txt and parsed into z_info. */
#define STAMINA_BASE_DEFAULT 50
#define STAMINA_STR_WEIGHT_DEFAULT 1
#define STAMINA_DEX_WEIGHT_DEFAULT 1
#define STAMINA_CON_WEIGHT_DEFAULT 2
#define STAMINA_CAP_DEFAULT 150
#define STAMINA_RECOVERY_DEFAULT 1
#define STAMINA_WAIT_RECOVERY_DEFAULT 15
#define AIR_CAPACITY_DEFAULT 6

static int configured(unsigned int value, int fallback)
{
	return value ? (int)value : fallback;
}

static int clamp_resource(int value, int maximum)
{
	if (value < 0) return 0;
	if (value > maximum) return maximum;
	return value;
}

int player_stamina_maximum_for_indices(int strength, int dexterity,
		int constitution)
{
	int base = z_info ? configured(z_info->stamina_base,
		STAMINA_BASE_DEFAULT) : STAMINA_BASE_DEFAULT;
	int str_weight = z_info ? configured(z_info->stamina_str_weight,
		STAMINA_STR_WEIGHT_DEFAULT) : STAMINA_STR_WEIGHT_DEFAULT;
	int dex_weight = z_info ? configured(z_info->stamina_dex_weight,
		STAMINA_DEX_WEIGHT_DEFAULT) : STAMINA_DEX_WEIGHT_DEFAULT;
	int con_weight = z_info ? configured(z_info->stamina_con_weight,
		STAMINA_CON_WEIGHT_DEFAULT) : STAMINA_CON_WEIGHT_DEFAULT;
	int cap = z_info ? configured(z_info->stamina_cap,
		STAMINA_CAP_DEFAULT) : STAMINA_CAP_DEFAULT;
	long total;

	if (strength < 0) strength = 0;
	if (dexterity < 0) dexterity = 0;
	if (constitution < 0) constitution = 0;
	total = base + (long)strength * str_weight +
		(long)dexterity * dex_weight + (long)constitution * con_weight;
	if (total < 1) total = 1;
	if (total > cap) total = cap;
	if (total > INT16_MAX) total = INT16_MAX;
	return (int)total;
}

int player_stamina_maximum(const struct player *p)
{
	if (!p) return 0;
	if (p->resources.initialized && p->resources.stamina.maximum > 0) {
		return p->resources.stamina.maximum;
	}
	return player_stamina_maximum_for_indices(p->state.stat_ind[STAT_STR],
		p->state.stat_ind[STAT_DEX], p->state.stat_ind[STAT_CON]);
}

int player_stamina_current(const struct player *p)
{
	if (!p) return 0;
	return p->resources.initialized ? p->resources.stamina.current :
		player_stamina_maximum(p);
}

int player_air_maximum(const struct player *p)
{
	if (!p) return 0;
	if (p->resources.initialized && p->resources.air.maximum > 0) {
		return p->resources.air.maximum;
	}
	return z_info ? configured(z_info->air_capacity, AIR_CAPACITY_DEFAULT) :
		AIR_CAPACITY_DEFAULT;
}

int player_air_current(const struct player *p)
{
	if (!p) return 0;
	return p->resources.initialized ? p->resources.air.current :
		player_air_maximum(p);
}

void player_resources_reset(struct player *p)
{
	if (!p) return;
	p->resources.stamina.maximum = (int16_t)
		player_stamina_maximum_for_indices(p->state.stat_ind[STAT_STR],
			p->state.stat_ind[STAT_DEX], p->state.stat_ind[STAT_CON]);
	p->resources.stamina.current = p->resources.stamina.maximum;
	p->resources.air.maximum = (int16_t)(z_info ?
		configured(z_info->air_capacity, AIR_CAPACITY_DEFAULT) :
		AIR_CAPACITY_DEFAULT);
	p->resources.air.current = p->resources.air.maximum;
	p->resources.initialized = true;
	if (p->upkeep) {
		p->upkeep->stamina_turn = PLAYER_STAMINA_TURN_NORMAL;
		p->upkeep->redraw |= PR_STATUS;
	}
}

void player_resources_ensure(struct player *p)
{
	if (p && !p->resources.initialized) player_resources_reset(p);
}

void player_resources_migrate_legacy(struct player *p, int stamina,
		int maximum, int air)
{
	int new_max;
	int new_air;

	if (!p) return;
	new_max = player_stamina_maximum_for_indices(
		p->state.stat_ind[STAT_STR], p->state.stat_ind[STAT_DEX],
		p->state.stat_ind[STAT_CON]);
	new_air = z_info ? configured(z_info->air_capacity,
		AIR_CAPACITY_DEFAULT) : AIR_CAPACITY_DEFAULT;
	p->resources.stamina.maximum = (int16_t)new_max;
	if (maximum > 0) {
		long scaled = (long)clamp_resource(stamina, maximum) * new_max;
		p->resources.stamina.current = (int16_t)((scaled + maximum / 2) /
			maximum);
	} else {
		p->resources.stamina.current = (int16_t)new_max;
	}
	p->resources.air.maximum = (int16_t)new_air;
	p->resources.air.current = (int16_t)clamp_resource(air, new_air);
	p->resources.initialized = true;
	if (p->upkeep) p->upkeep->redraw |= PR_STATUS;
}

void player_resources_update_max(struct player *p)
{
	int old_max;
	int old_current;
	int new_max;

	if (!p) return;
	player_resources_ensure(p);
	old_max = p->resources.stamina.maximum;
	old_current = p->resources.stamina.current;
	new_max = player_stamina_maximum_for_indices(p->state.stat_ind[STAT_STR],
		p->state.stat_ind[STAT_DEX], p->state.stat_ind[STAT_CON]);
	if (new_max == old_max) return;
	p->resources.stamina.maximum = (int16_t)new_max;
	if (old_max > 0) {
		p->resources.stamina.current = (int16_t)(((long)old_current *
			new_max + old_max / 2) / old_max);
	} else {
		p->resources.stamina.current = (int16_t)new_max;
	}
	if (p->upkeep) p->upkeep->redraw |= PR_STATUS;
}

bool player_stamina_spend(struct player *p, int cost)
{
	if (!p || cost < 0) return false;
	player_resources_ensure(p);
	if (p->resources.stamina.current < cost) return false;
	p->resources.stamina.current -= (int16_t)cost;
	if (p->upkeep) {
		p->upkeep->stamina_turn = PLAYER_STAMINA_TURN_EXERTED;
		p->upkeep->redraw |= PR_STATUS;
	}
	return true;
}

void player_stamina_mark_exerted(struct player *p)
{
	if (p && p->upkeep) {
		p->upkeep->stamina_turn = PLAYER_STAMINA_TURN_EXERTED;
	}
}

void player_stamina_recover(struct player *p, int amount)
{
	int recovered;

	if (!p || amount <= 0) return;
	player_resources_ensure(p);
	recovered = clamp_resource(p->resources.stamina.current + amount,
		p->resources.stamina.maximum);
	if (recovered == p->resources.stamina.current) return;
	p->resources.stamina.current = (int16_t)recovered;
	if (p->upkeep) p->upkeep->redraw |= PR_STATUS;
}

void player_stamina_mark_waiting(struct player *p)
{
	if (p && p->upkeep &&
			p->upkeep->stamina_turn != PLAYER_STAMINA_TURN_EXERTED) {
		p->upkeep->stamina_turn = PLAYER_STAMINA_TURN_WAITING;
	}
}

void player_stamina_mark_resolved(struct player *p)
{
	if (p && p->upkeep &&
			p->upkeep->stamina_turn != PLAYER_STAMINA_TURN_EXERTED) {
		p->upkeep->stamina_turn = PLAYER_STAMINA_TURN_RESOLVED;
	}
}

static bool stamina_has_safe_support(const struct player *p)
{
	if (!world_spelunk_player_is_active(p)) return true;
	return p->spelunking &&
		world_spelunk_has_stable_floor(&p->spelunking->state);
}

void player_stamina_process_turn(struct player *p)
{
	enum player_stamina_turn turn;
	int recovery;

	if (!p || !p->upkeep || p->upkeep->energy_use <= 0) return;
	player_resources_ensure(p);
	turn = p->upkeep->stamina_turn;
	if (!world_spelunk_player_is_active(p)) player_air_refill(p);
	if (stamina_has_safe_support(p)) {
		if (turn == PLAYER_STAMINA_TURN_WAITING) {
			recovery = z_info ? configured(z_info->stamina_wait_recovery,
				STAMINA_WAIT_RECOVERY_DEFAULT) : STAMINA_WAIT_RECOVERY_DEFAULT;
			player_stamina_recover(p, recovery);
		} else if (turn == PLAYER_STAMINA_TURN_NORMAL) {
			recovery = z_info ? configured(z_info->stamina_recovery,
				STAMINA_RECOVERY_DEFAULT) : STAMINA_RECOVERY_DEFAULT;
			player_stamina_recover(p, recovery);
		}
	}
	p->upkeep->stamina_turn = PLAYER_STAMINA_TURN_NORMAL;
}

void player_air_set(struct player *p, int current)
{
	if (!p) return;
	player_resources_ensure(p);
	current = clamp_resource(current, p->resources.air.maximum);
	if (current == p->resources.air.current) return;
	p->resources.air.current = (int16_t)current;
	if (p->upkeep) p->upkeep->redraw |= PR_STATUS;
}

void player_air_refill(struct player *p)
{
	if (!p) return;
	player_resources_ensure(p);
	player_air_set(p, p->resources.air.maximum);
}
