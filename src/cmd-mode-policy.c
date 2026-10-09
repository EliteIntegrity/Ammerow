/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-mode-policy.c
 * \brief Fail-closed command availability for alternate location modes.
 *
 *
 */

#include "cmd-mode-policy.h"

bool cmd_mode_policy_allows(enum world_turn_context turn_context,
		cmd_context command_context, cmd_code code)
{
	/* Birth, store, death, and initialization have their own bounded loops.
	 * This gate protects commands dispatched by the active world scheduler. */
	if (command_context != CTX_GAME) return true;

	switch (turn_context) {
	case WORLD_TURN_CONTEXT_TOP_DOWN:
		return true;
	case WORLD_TURN_CONTEXT_SPELUNKING:
		/* Semantic spelunking commands and explicitly adapted shared commands
		 * own side-view state.  Sleep is the audited shared forced-wait used
		 * for paralysis and unconsciousness; it only commits ordinary energy.
		 * Retirement changes only shared player lifecycle state. */
		switch (code) {
		case CMD_SPELUNK_ACTION:
		case CMD_SPELUNK_LOCAL:
		case CMD_SPELUNK_PEEK:
		case CMD_SPELUNK_PASSAGE:
		case CMD_SPELUNK_EXIT:
		case CMD_INSCRIBE:
		case CMD_UNINSCRIBE:
		case CMD_WIELD:
		case CMD_TAKEOFF:
		case CMD_DROP:
		case CMD_REFILL:
		case CMD_EAT:
		case CMD_QUAFF:
		case CMD_READ_SCROLL:
		case CMD_USE_STAFF:
		case CMD_USE_WAND:
		case CMD_USE_ROD:
		case CMD_ACTIVATE:
		case CMD_USE:
		case CMD_BROWSE_SPELL:
		case CMD_STUDY:
		case CMD_CAST:
		case CMD_FIRE:
		case CMD_THROW:
		case CMD_FISHING_START:
		case CMD_FISHING_ACTION:
		case CMD_FISHING_STOP:
		case CMD_SLEEP:
		case CMD_RETIRE:
			return true;
		default:
			return false;
		}
	case WORLD_TURN_CONTEXT_INVALID:
	default:
		return false;
	}
}
