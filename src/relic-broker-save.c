/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
/** Optional versioned block: old saves start with an uninitialized catalogue. */
#include "angband.h"
#include "relic-broker.h"
#include "savefile.h"

void wr_relic_broker(void)
{
	int i;
	const struct relic_broker_state *state = &player->broker;
	wr_byte(state->initialized ? 1 : 0);
	wr_byte(state->count);
	for (i = 0; i < state->count; i++) {
		const struct relic_broker_lot *lot = &state->lots[i];
		wr_u16b(lot->artifact);
		wr_byte(lot->band);
		wr_s32b(lot->price);
		wr_byte(lot->sold ? 1 : 0);
	}
}

int rd_relic_broker(void)
{
	struct relic_broker_state staged = { 0 };
	uint8_t initialized;
	int i;
	rd_byte(&initialized);
	if (initialized > 1) return -1;
	staged.initialized = initialized != 0;
	rd_byte(&staged.count);
	if (staged.count > RELIC_BROKER_MAX_LOTS) return -1;
	for (i = 0; i < staged.count; i++) {
		struct relic_broker_lot *lot = &staged.lots[i];
		uint8_t sold;
		rd_u16b(&lot->artifact);
		rd_byte(&lot->band);
		rd_s32b(&lot->price);
		rd_byte(&sold);
		if (sold > 1) return -1;
		lot->sold = sold != 0;
	}
	if (!relic_broker_state_valid(&staged)) {
		note("Invalid relic broker catalogue in savefile.");
		return -1;
	}
	player->broker = staged;
	return 0;
}
