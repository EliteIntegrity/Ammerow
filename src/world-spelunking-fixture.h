/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef WORLD_SPELUNKING_FIXTURE_H
#define WORLD_SPELUNKING_FIXTURE_H

#include "world-spelunking-runtime.h"

struct file_parser;
struct player;
enum world_spelunk_fixture_kind { SPELUNK_TRAP = 1, SPELUNK_CHEST };
enum world_spelunk_fixture_state {
	SPELUNK_FIXTURE_UNKNOWN = 0, SPELUNK_FIXTURE_READY,
	SPELUNK_FIXTURE_LOCKED, SPELUNK_FIXTURE_SPENT
};
enum world_spelunk_trap_knowledge {
	SPELUNK_TRAP_UNKNOWN = 0, SPELUNK_TRAP_ARMED, SPELUNK_TRAP_SAFE
};
struct world_spelunk_fixture_definition {
	char id[WORLD_ID_LEN], name[80], trap_id[WORLD_ID_LEN];
	char tval[32], sval[80];
	uint8_t kind, lock_power, quantity;
	uint16_t search_power, disarm_power, damage;
};

extern struct file_parser spelunking_fixture_parser;
const struct world_spelunk_fixture_definition *world_spelunk_fixture_by_id(const char *id);
bool world_spelunk_fixture_data_validate(void);
const struct world_spelunk_fixture *world_spelunk_fixture_at(
	const struct world_spelunk_runtime *r, int x, int y);
struct world_spelunk_fixture *world_spelunk_fixture_at_mutable(
	struct world_spelunk_runtime *r, int x, int y);
bool world_spelunk_fixtures_valid(const struct world_spelunk_runtime *r);
bool world_spelunk_fixture_add(struct world_spelunk_runtime *r,
	const char *id, int x, int y);
/** Only chests and already discovered traps participate in ordinary sight. */
void world_spelunk_fixtures_observe(struct world_spelunk_runtime *r);
/** Threshold-based searching: no free random rerolls from redraws or Look. */
void world_spelunk_fixture_notice(struct player *p);
/** Single-use pressure traps trigger on grounded occupancy, including landing. */
void world_spelunk_fixture_step(struct player *p);
bool world_spelunk_fixture_detect(struct world_spelunk_runtime *r,
	int vertical, int horizontal, bool traps);
/** Resolve one explicit Open or Disarm attempt; caller owns turn energy. */
bool world_spelunk_fixture_interact(struct player *p,
	struct world_spelunk_fixture *f, bool disarm);
#endif
