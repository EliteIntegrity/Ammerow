/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef PLAYER_ADVANCEMENT_H
#define PLAYER_ADVANCEMENT_H
#include "h-basic.h"

struct player;
#define ADVANCEMENT_MAX_LEVEL 50
/* Append-only save block; neither level drain nor dismissal clears highest. */
struct player_advancement {
	bool initialized;
	uint8_t highest;
	uint8_t cards[ADVANCEMENT_MAX_LEVEL + 1];
	uint16_t rewards;
};
enum advancement_choice { ADVANCEMENT_POTION, ADVANCEMENT_SCROLL, ADVANCEMENT_GOLD };
enum advancement_result { ADVANCEMENT_OK, ADVANCEMENT_UNAVAILABLE, ADVANCEMENT_NO_ROOM };
struct advancement_offer {
	const char *potion;
	int potions;
	const char *scroll;
	int scrolls;
	int gold;
};
void player_advancement_reset(struct player *p, int highest);
void player_advancement_ensure(struct player *p);
bool player_advancement_valid(const struct player_advancement *state);
void player_advancement_gain(struct player *p, int level);
int player_advancement_next_card(const struct player *p);
int player_advancement_next_reward(const struct player *p);
void player_advancement_dismiss(struct player *p, int level);
const struct advancement_offer *player_advancement_offer(int level);
enum advancement_result player_advancement_claim(struct player *p, int level,
	enum advancement_choice choice);
#endif
