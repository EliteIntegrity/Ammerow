/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking-input.c
 * \brief Physical-key translation for side-view spelunking.
 *
 *
 */

#include "ui-spelunking-input.h"
#include "ui-mode-input.h"

#include <string.h>

static bool direction_for_key(keycode_t code, int *dx, int *dy)
{
	*dx = 0;
	*dy = 0;
	switch (code) {
	case '1':
		*dx = -1;
		*dy = 1;
		return true;
	case '2':
	case ARROW_DOWN:
		*dy = 1;
		return true;
	case '3':
		*dx = 1;
		*dy = 1;
		return true;
	case '4':
	case ARROW_LEFT:
		*dx = -1;
		return true;
	case '6':
	case ARROW_RIGHT:
		*dx = 1;
		return true;
	case '7':
		*dx = -1;
		*dy = -1;
		return true;
	case '8':
	case ARROW_UP:
		*dy = -1;
		return true;
	case '9':
		*dx = 1;
		*dy = -1;
		return true;
	default:
		return false;
	}
}

enum textui_spelunking_binding_kind textui_spelunking_translate_key(
		struct keypress key, struct textui_spelunking_binding *binding)
{
	struct textui_spelunking_binding translated = { 0 };
	int dx;
	int dy;
	bool direction = direction_for_key(key.code, &dx, &dy);
	bool wait = key.code == '5' || key.code == KC_BEGIN || key.code == '.' ||
		key.code == ' ';
	bool grip = key.code == 'h' || key.code == 'H';
	bool jump = key.code == 'j' || key.code == 'J';
	bool peek = key.code == 'p' || key.code == 'P';
	bool peek_begin = key.code == KC_MODE_PEEK_BEGIN;
	bool peek_end = key.code == KC_MODE_PEEK_END;
	bool piton = key.code == '\'';
	bool rope = key.code == ';';
	/* Preserve Angband's uppercase G for studying.  Cave pickup uses the
	 * ordinary lowercase g mnemonic. */
	bool pickup = key.code == 'g';
	bool drop = key.code == 'd' || key.code == 'D';
	bool passage = key.code == '<' || key.code == '>';
	/* Printable input normally encodes Shift in the character, but some
	 * frontends send a lowercase character plus KC_MOD_SHIFT.  H/J are owned
	 * by this mode in either representation, so accepting Shift here keeps the
	 * physical action independent of the frontend. */
	uint8_t allowed_modifiers = KC_MOD_KEYPAD |
		((grip || jump || peek) ? KC_MOD_SHIFT : 0);
	uint8_t disallowed_modifiers = key.mods & ~allowed_modifiers;

	if (!binding) return TEXTUI_SPELUNKING_BLOCKED;
	memset(binding, 0, sizeof(*binding));
	if (!direction && !wait && !grip && !jump && !peek && !peek_begin &&
			!peek_end && !piton && !rope &&
			!pickup && !drop && !passage) {
		return TEXTUI_SPELUNKING_UNHANDLED;
	}
	if (disallowed_modifiers) {
		binding->kind = TEXTUI_SPELUNKING_BLOCKED;
		return binding->kind;
	}
	if (passage) {
		binding->kind = TEXTUI_SPELUNKING_PASSAGE;
		binding->passage_direction = key.code == '<' ?
			WORLD_SPELUNK_PASSAGE_UP : WORLD_SPELUNK_PASSAGE_DOWN;
		return binding->kind;
	}
	if (peek || peek_begin || peek_end) {
		binding->kind = TEXTUI_SPELUNKING_PEEK;
		binding->peek_action = peek_begin ? WORLD_SPELUNK_PEEK_BEGIN :
			peek_end ? WORLD_SPELUNK_PEEK_END : WORLD_SPELUNK_PEEK_TOGGLE;
		return binding->kind;
	}
	if (drop) {
		binding->kind = TEXTUI_SPELUNKING_DROP;
		return binding->kind;
	}
	if (piton || rope || pickup) {
		binding->kind = TEXTUI_SPELUNKING_LOCAL;
		binding->local_action = piton ? WORLD_SPELUNK_LOCAL_PITON :
			rope ? WORLD_SPELUNK_LOCAL_ROPE : WORLD_SPELUNK_LOCAL_PICKUP;
		return binding->kind;
	}
	translated.kind = TEXTUI_SPELUNKING_ACTION;
	translated.action.dx = 0;
	translated.action.dy = 0;
	if (direction) {
		translated.action.kind = WORLD_SPELUNK_COMMAND_MOVE;
		translated.action.dx = dx;
		translated.action.dy = dy;
	} else if (wait) {
		translated.action.kind = WORLD_SPELUNK_COMMAND_WAIT;
	} else if (grip) {
		translated.action.kind = WORLD_SPELUNK_COMMAND_GRIP;
	} else {
		translated.action.kind = WORLD_SPELUNK_COMMAND_JUMP;
	}
	*binding = translated;
	return binding->kind;
}
