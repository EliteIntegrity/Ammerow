/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/loop-bot.h
 * \brief Observation-driven acceptance bot for the first cross-mode loop.
 */

#ifndef HEADLESS_LOOP_BOT_H
#define HEADLESS_LOOP_BOT_H

#include "h-basic.h"

#include <stddef.h>

struct headless_loop_route_guide;

enum headless_loop_bot_result {
	HEADLESS_LOOP_BOT_ACTION = 0,
	HEADLESS_LOOP_BOT_COMPLETE,
	HEADLESS_LOOP_BOT_ERROR
};

enum headless_loop_bot_stage {
	HEADLESS_LOOP_FIND_RIG = 0,
	HEADLESS_LOOP_FIND_BANK,
	HEADLESS_LOOP_FISH,
	HEADLESS_LOOP_RETURN,
	HEADLESS_LOOP_DONATE,
	HEADLESS_LOOP_BUY_REWARD,
	HEADLESS_LOOP_ENTER_MERIDIAN,
	HEADLESS_LOOP_USE_REWARD,
	HEADLESS_LOOP_DONE
};

struct headless_loop_bot {
	enum headless_loop_bot_stage stage;
	unsigned int actions;
	struct headless_loop_route_guide *guide;
};

void headless_loop_bot_init(struct headless_loop_bot *bot);
void headless_loop_bot_dispose(struct headless_loop_bot *bot);
const char *headless_loop_bot_stage_name(const struct headless_loop_bot *bot);
enum headless_loop_bot_result headless_loop_bot_next(
		struct headless_loop_bot *bot, char *action, size_t action_size,
		char *error, size_t error_size);

#endif /* HEADLESS_LOOP_BOT_H */
