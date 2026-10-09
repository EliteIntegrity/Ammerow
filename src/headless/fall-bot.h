/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/fall-bot.h
 * \brief Semantic generated-cave fatal-fall acceptance policy.
 */

#ifndef HEADLESS_FALL_BOT_H
#define HEADLESS_FALL_BOT_H

#include "h-basic.h"

#include <stddef.h>

enum headless_fall_bot_result {
	HEADLESS_FALL_BOT_ACTION = 0,
	HEADLESS_FALL_BOT_ERROR
};

struct headless_fall_bot {
	bool initialized;
	int upper_y;
	int lip_x;
	int shaft_x;
};

void headless_fall_bot_init(struct headless_fall_bot *bot);
enum headless_fall_bot_result headless_fall_bot_next(
		struct headless_fall_bot *bot, char *action, size_t action_size,
		char *error, size_t error_size);

#endif /* !HEADLESS_FALL_BOT_H */
