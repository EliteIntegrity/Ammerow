/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-mode-input.c
 * \brief Semantic input routing for the active world mode.
 *
 *
 */

#include "angband.h"
#include "cmd-mode-policy.h"
#include "cmd-spelunking.h"
#include "ui-mode-input.h"
#include "ui-spelunking-input.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-visibility.h"
#include "world-turn.h"

static bool process_spelunking_key(struct player *p, struct keypress key)
{
	struct textui_spelunking_binding binding;
	enum textui_spelunking_binding_kind kind;
	bool peeking = p->spelunking && p->spelunking->peeking;

	/* SDL preserves Shift while it is physically held.  Once peek owns the
	 * direction, that modifier cannot turn it into movement. */
	if (peeking) key.mods &= ~KC_MOD_SHIFT;
	kind = textui_spelunking_translate_key(key, &binding);

	switch (kind) {
	case TEXTUI_SPELUNKING_UNHANDLED:
		return false;
	case TEXTUI_SPELUNKING_BLOCKED:
		return true;
	case TEXTUI_SPELUNKING_ACTION:
		if (peeking && binding.action.kind == WORLD_SPELUNK_COMMAND_MOVE) {
			if (cmdq_push_spelunk_peek(WORLD_SPELUNK_PEEK_MOVE,
					binding.action.dx, binding.action.dy)) {
				msg("That peek direction cannot be queued.");
			}
			return true;
		}
		if (peeking) world_spelunk_visibility_end_peek(p->spelunking);
		if (cmdq_push_spelunk_action(binding.action.kind,
				binding.action.dx, binding.action.dy)) {
			msg("That side-view action cannot be queued.");
		}
		return true;
	case TEXTUI_SPELUNKING_LOCAL:
		if (peeking) world_spelunk_visibility_end_peek(p->spelunking);
		if (cmdq_push_spelunk_local(binding.local_action)) {
			msg("That side-view object action cannot be queued.");
		}
		return true;
	case TEXTUI_SPELUNKING_PEEK:
		if (cmdq_push_spelunk_peek(binding.peek_action, 0, 0)) {
			msg("Peek cannot be queued.");
		}
		return true;
	case TEXTUI_SPELUNKING_DROP:
		if (peeking) world_spelunk_visibility_end_peek(p->spelunking);
		if (cmdq_push(CMD_DROP)) msg("Drop cannot be queued.");
		return true;
	case TEXTUI_SPELUNKING_PASSAGE:
		if (peeking) world_spelunk_visibility_end_peek(p->spelunking);
		if (cmdq_push_spelunk_passage(binding.passage_direction)) {
			msg("The cave passage cannot be queued.");
		}
		return true;
	case TEXTUI_SPELUNKING_EXIT:
		if (peeking) world_spelunk_visibility_end_peek(p->spelunking);
		if (cmdq_push_spelunk_exit()) {
			msg("The shaft exit cannot be queued.");
		}
		return true;
	default:
		return true;
	}
}

bool textui_mode_input_owns_key(const struct player *p, struct keypress key)
{
	struct textui_spelunking_binding binding;

	if (!world_player_mode_has_capability(p,
			WORLD_MODE_CAP_PRIMARY_INPUT)) {
		return false;
	}
	return textui_spelunking_translate_key(key, &binding) !=
		TEXTUI_SPELUNKING_UNHANDLED;
}

bool textui_mode_input_process_key(struct player *p, struct keypress key)
{
	if (!p || p != player || !world_player_mode_has_capability(p,
			WORLD_MODE_CAP_PRIMARY_INPUT)) {
		return false;
	}
	return process_spelunking_key(p, key);
}

void textui_mode_input_cancel_transient(struct player *p)
{
	if (world_player_mode_has_capability(p, WORLD_MODE_CAP_SIDE_VIEW) &&
			p->spelunking) {
		world_spelunk_visibility_end_peek(p->spelunking);
	}
}

bool textui_mode_input_uses_held_peek(const struct player *p)
{
	return world_player_mode_has_capability(p,
		WORLD_MODE_CAP_PRIMARY_INPUT);
}

const char *textui_mode_input_mouse_rejection(const struct player *p)
{
	if (world_player_mode_has_capability(p, WORLD_MODE_CAP_MOUSE)) return NULL;
	if (world_player_mode_has_capability(p, WORLD_MODE_CAP_SIDE_VIEW)) {
		return "Mouse actions are not available while spelunking yet.";
	}
	return "Mouse actions are not available in this location.";
}

bool textui_mode_input_allows_command(const struct player *p,
		cmd_code command, bool has_hook, bool safe_in_side_view)
{
	enum world_turn_context context = world_turn_context_for_player(p);

	if (world_turn_context_has_capability(context,
			WORLD_MODE_CAP_NATIVE_CAVE)) {
		return true;
	}
	if (command != CMD_NULL) {
		return cmd_mode_policy_allows(context, CTX_GAME, command);
	}
	if (!has_hook) return true;
	return world_turn_context_has_capability(context,
		WORLD_MODE_CAP_SIDE_VIEW) && safe_in_side_view;
}
