/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-mode-policy.h
 * \brief Fail-closed command availability for alternate location modes.
 *
 *
 */

#ifndef CMD_MODE_POLICY_H
#define CMD_MODE_POLICY_H

#include "cmd-core.h"
#include "world-turn.h"

/**
 * Return whether a command may reach its core handler in this runtime mode.
 * Non-game command contexts retain their existing lifecycle behaviour.
 */
bool cmd_mode_policy_allows(enum world_turn_context turn_context,
		cmd_context command_context, cmd_code code);

#endif /* !CMD_MODE_POLICY_H */
