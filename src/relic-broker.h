/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef INCLUDED_RELIC_BROKER_H
#define INCLUDED_RELIC_BROKER_H

#include "h-basic.h"

/* Serialization/validation capacities, not authored stock quantities. */
#define RELIC_BROKER_MAX_BANDS 8
#define RELIC_BROKER_MAX_LOTS 32

struct player;
struct object;
struct command;
struct artifact;

struct relic_broker_lot {
	uint16_t artifact;
	uint8_t band;
	int32_t price;
	bool sold;
};

struct relic_broker_state {
	bool initialized;
	uint8_t count;
	struct relic_broker_lot lots[RELIC_BROKER_MAX_LOTS];
};

enum relic_broker_result {
	RELIC_BROKER_OK,
	RELIC_BROKER_UNAVAILABLE,
	RELIC_BROKER_NO_GOLD,
	RELIC_BROKER_NO_ROOM,
	RELIC_BROKER_WRONG_STORE
};

bool relic_broker_artifact_eligible(const struct artifact *art);
int32_t relic_broker_artifact_power(const struct artifact *art);
bool relic_broker_ensure(struct player *p);
bool relic_broker_state_valid(const struct relic_broker_state *state);
bool relic_broker_lot_available(const struct relic_broker_lot *lot);
const char *relic_broker_lot_category(const struct relic_broker_lot *lot);
enum relic_broker_result relic_broker_buy(struct player *p, int lot,
	struct object **received);
void do_cmd_buy_relic(struct command *cmd);
void wr_relic_broker(void);
int rd_relic_broker(void);

#endif
