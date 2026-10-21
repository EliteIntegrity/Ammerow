/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef PLAYER_EXPLORE_H
#define PLAYER_EXPLORE_H

#include "z-type.h"
struct player;
struct command;
struct monster;

bool player_explore_active(const struct player *p);
void player_explore_command(struct command *cmd);
/* Reports once and releases state; caller still owns ordinary disturbance. */
void player_explore_cancel(struct player *p, const char *reason);
void player_explore_free(struct player *p);
bool player_explore_new_loot(const struct player *p);
bool player_explore_check(struct player *p);
/* Called after perception flags are updated; never reveals hidden occupants. */
void player_explore_notice_monster(struct player *p, const struct monster *mon);

#endif
