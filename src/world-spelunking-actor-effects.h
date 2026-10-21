/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef WORLD_SPELUNKING_ACTOR_EFFECTS_H
#define WORLD_SPELUNKING_ACTOR_EFFECTS_H

#include "world-spelunking-runtime.h"
#include "world-spelunking-actor-data.h"

struct player;
struct object;

bool world_spelunk_actor_projection_supported(int type);
bool world_spelunk_actor_is_living(const struct world_spelunk_actor_definition *d);
int world_spelunk_actor_damage(const struct world_spelunk_actor_definition *d,
	int type, int power);
/* Applies non-damaging effects and returns the remaining HP damage. */
int world_spelunk_actor_effect(struct player *p, struct world_spelunk_actor *a,
	int type, int power);
bool world_spelunk_actor_destination(const struct world_spelunk_runtime *r, int x, int y);
/* Best non-stacking modifier; 0 means no effective brand/slay. */
int world_spelunk_attack_modifier(struct player *p,
	const struct world_spelunk_actor_definition *d, struct object *first,
	struct object *second, bool melee, bool learn);
void world_spelunk_actor_status_text(const struct world_spelunk_actor *a,
	char *text, size_t size);
#endif
