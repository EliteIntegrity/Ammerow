/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/** \file player-resource.h
 * \brief Authoritative stamina and held-air resource rules.
 */

#ifndef PLAYER_RESOURCE_H
#define PLAYER_RESOURCE_H

#include <stdbool.h>

struct player;

int player_stamina_maximum_for_indices(int strength, int dexterity,
		int constitution);
int player_stamina_maximum(const struct player *p);
int player_stamina_current(const struct player *p);
int player_air_maximum(const struct player *p);
int player_air_current(const struct player *p);

void player_resources_reset(struct player *p);
void player_resources_ensure(struct player *p);
void player_resources_migrate_legacy(struct player *p, int stamina,
		int maximum, int air);
void player_resources_update_max(struct player *p);

bool player_stamina_spend(struct player *p, int cost);
void player_stamina_recover(struct player *p, int amount);
void player_stamina_mark_exerted(struct player *p);
void player_stamina_mark_waiting(struct player *p);
void player_stamina_mark_resolved(struct player *p);
void player_stamina_process_turn(struct player *p);

void player_air_set(struct player *p, int current);
void player_air_refill(struct player *p);

#endif /* !PLAYER_RESOURCE_H */
