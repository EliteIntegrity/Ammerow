/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-fishing-input.c
 * \brief Raw-key translation for the fishing activity.
 */

#include "ui-fishing-input.h"

enum textui_fishing_binding_kind textui_fishing_translate_key(
		struct keypress key, enum world_fishing_phase phase,
		enum world_fishing_action *action)
{
	bool unmodified = !(key.mods & ~KC_MOD_KEYPAD);

	if (!action) return TEXTUI_FISHING_BLOCKED;
	if (key.code == ESCAPE && !key.mods) {
		*action = WORLD_FISHING_CANCEL;
		return TEXTUI_FISHING_ACTION;
	}
	if (!unmodified) return TEXTUI_FISHING_BLOCKED;
	if (phase == WORLD_FISHING_BITE) {
		if (key.code == KC_ENTER) {
			*action = WORLD_FISHING_STRIKE;
			return TEXTUI_FISHING_ACTION;
		}
		if (key.code == ' ' || key.code == '5' || key.code == KC_BEGIN) {
			*action = WORLD_FISHING_WAIT;
			return TEXTUI_FISHING_ACTION;
		}
		return TEXTUI_FISHING_BLOCKED;
	}
	if (phase == WORLD_FISHING_WINDING) {
		if (key.code == ARROW_UP || key.code == '8') {
			*action = WORLD_FISHING_REEL;
			return TEXTUI_FISHING_ACTION;
		}
		return TEXTUI_FISHING_BLOCKED;
	}
	if (phase != WORLD_FISHING_WAITING) return TEXTUI_FISHING_BLOCKED;
	switch (key.code) {
	case ARROW_LEFT:
	case '4':
		*action = WORLD_FISHING_EXTEND;
		break;
	case ARROW_RIGHT:
	case '6':
		*action = WORLD_FISHING_RETRACT;
		break;
	case ARROW_DOWN:
	case '2':
		*action = WORLD_FISHING_LOWER;
		break;
	case ARROW_UP:
	case '8':
		*action = WORLD_FISHING_RAISE;
		break;
	case ' ':
	case '5':
	case KC_BEGIN:
	case '.':
		*action = WORLD_FISHING_WAIT;
		break;
	default:
		return TEXTUI_FISHING_BLOCKED;
	}
	return TEXTUI_FISHING_ACTION;
}
