/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/input.c
 * \brief SDL keyboard translation and frontend shortcut recognition.
 */

#include "angband.h"

#include "sdl3/input.h"
#include "sdl3/layout.h"

void sdl3_input_gate_arm(struct sdl3_input_gate *gate)
{
	if (!gate) return;
	memset(gate, 0, sizeof(*gate));
	gate->armed = true;
}

bool sdl3_input_gate_accept(struct sdl3_input_gate *gate,
		const SDL_Event *event)
{
	int scan;

	if (!event) return false;
	if (!gate || !gate->armed) return true;
	switch (event->type) {
	case SDL_EVENT_KEY_DOWN:
		scan = (int)event->key.scancode;
		if (scan > SDL_SCANCODE_UNKNOWN && scan < SDL_SCANCODE_COUNT) {
			if (!event->key.repeat) gate->fresh[scan] = true;
			gate->text_allowed = gate->fresh[scan];
		} else {
			/* A synthetic/unknown key may be pressed, but cannot establish
			 * physical ownership of an auto-repeat stream. */
			gate->text_allowed = !event->key.repeat;
		}
		return gate->text_allowed;
	case SDL_EVENT_KEY_UP:
		scan = (int)event->key.scancode;
		if (scan > SDL_SCANCODE_UNKNOWN && scan < SDL_SCANCODE_COUNT) {
			gate->fresh[scan] = false;
		}
		return true;
	case SDL_EVENT_TEXT_INPUT:
		/* SDL text events have no repeat flag.  Route the companion text
		 * with its keydown, including letters not translated as special keys. */
		return gate->text_allowed;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		sdl3_input_gate_arm(gate);
		return true;
	default:
		/* Mouse clicks, key releases, window close and redraws still work. */
		return true;
	}
}

uint8_t sdl3_input_translate_modifiers(SDL_Keymod modifiers)
{
	uint8_t result = 0;

	if (modifiers & SDL_KMOD_CTRL) result |= KC_MOD_CONTROL;
	if (modifiers & SDL_KMOD_SHIFT) result |= KC_MOD_SHIFT;
	if (modifiers & SDL_KMOD_ALT) result |= KC_MOD_ALT;
	if (modifiers & SDL_KMOD_GUI) result |= KC_MOD_META;
	return result;
}

bool sdl3_input_is_keypad_key(SDL_Keycode key)
{
	switch (key) {
	case SDLK_KP_0:
	case SDLK_KP_1:
	case SDLK_KP_2:
	case SDLK_KP_3:
	case SDLK_KP_4:
	case SDLK_KP_5:
	case SDLK_KP_6:
	case SDLK_KP_7:
	case SDLK_KP_8:
	case SDLK_KP_9:
	case SDLK_KP_MULTIPLY:
	case SDLK_KP_PERIOD:
	case SDLK_KP_DIVIDE:
	case SDLK_KP_EQUALS:
	case SDLK_KP_MINUS:
	case SDLK_KP_PLUS:
	case SDLK_KP_ENTER:
		return true;
	default:
		return false;
	}
}

keycode_t sdl3_input_translate_special(const SDL_KeyboardEvent *event,
		uint8_t *modifiers)
{
	keycode_t key = 0;

	if (!event || !modifiers) return 0;
	*modifiers = sdl3_input_translate_modifiers(event->mod);
	switch (event->key) {
	case SDLK_UP: key = ARROW_UP; break;
	case SDLK_DOWN: key = ARROW_DOWN; break;
	case SDLK_LEFT: key = ARROW_LEFT; break;
	case SDLK_RIGHT: key = ARROW_RIGHT; break;
	case SDLK_INSERT: key = KC_INSERT; break;
	case SDLK_HOME: key = KC_HOME; break;
	case SDLK_PAGEUP: key = KC_PGUP; break;
	case SDLK_DELETE: key = KC_DELETE; break;
	case SDLK_END: key = KC_END; break;
	case SDLK_PAGEDOWN: key = KC_PGDOWN; break;
	case SDLK_ESCAPE: key = ESCAPE; break;
	case SDLK_BACKSPACE: key = KC_BACKSPACE; break;
	case SDLK_RETURN: key = KC_ENTER; break;
	case SDLK_TAB: key = KC_TAB; break;
	case SDLK_F1: key = KC_F1; break;
	case SDLK_F2: key = KC_F2; break;
	case SDLK_F3: key = KC_F3; break;
	case SDLK_F4: key = KC_F4; break;
	case SDLK_F5: key = KC_F5; break;
	case SDLK_F6: key = KC_F6; break;
	case SDLK_F7: key = KC_F7; break;
	case SDLK_F8: key = KC_F8; break;
	case SDLK_F9: key = KC_F9; break;
	case SDLK_F10: key = KC_F10; break;
	case SDLK_F11: key = KC_F11; break;
	case SDLK_F12: key = KC_F12; break;
	case SDLK_F13: key = KC_F13; break;
	case SDLK_F14: key = KC_F14; break;
	case SDLK_F15: key = KC_F15; break;
	case SDLK_KP_0: key = '0'; break;
	case SDLK_KP_1: key = '1'; break;
	case SDLK_KP_2: key = '2'; break;
	case SDLK_KP_3: key = '3'; break;
	case SDLK_KP_4: key = '4'; break;
	case SDLK_KP_5: key = '5'; break;
	case SDLK_KP_6: key = '6'; break;
	case SDLK_KP_7: key = '7'; break;
	case SDLK_KP_8: key = '8'; break;
	case SDLK_KP_9: key = '9'; break;
	case SDLK_KP_MULTIPLY: key = '*'; break;
	case SDLK_KP_PERIOD: key = '.'; break;
	case SDLK_KP_DIVIDE: key = '/'; break;
	case SDLK_KP_EQUALS: key = '='; break;
	case SDLK_KP_MINUS: key = '-'; break;
	case SDLK_KP_PLUS: key = '+'; break;
	case SDLK_KP_ENTER: key = KC_ENTER; break;
	default: break;
	}
	if (sdl3_input_is_keypad_key(event->key)) {
		bool number_without_keypad_modifier =
			(event->mod & SDL_KMOD_NUM) &&
			!(event->mod & SDL_KMOD_SHIFT) &&
			(event->key == SDLK_KP_0 ||
			(event->key >= SDLK_KP_1 && event->key <= SDLK_KP_9));

		if (!number_without_keypad_modifier) *modifiers |= KC_MOD_KEYPAD;
	}
	if (!key && (*modifiers & KC_MOD_CONTROL)) {
		if (event->key >= SDLK_A && event->key <= SDLK_Z) {
			key = KTRL('A' + (event->key - SDLK_A));
			*modifiers &= ~KC_MOD_CONTROL;
		} else if (event->key == SDLK_LEFTBRACKET) {
			key = KTRL('[');
			*modifiers &= ~KC_MOD_CONTROL;
		} else if (event->key == SDLK_RIGHTBRACKET) {
			key = KTRL(']');
			*modifiers &= ~KC_MOD_CONTROL;
		} else if (event->key == SDLK_BACKSLASH) {
			key = KTRL('\\');
			*modifiers &= ~KC_MOD_CONTROL;
		}
	}
	return key;
}

keycode_t sdl3_input_translate_repeated(const SDL_KeyboardEvent *event,
		uint8_t *modifiers)
{
	keycode_t key;
	bool upper;

	if (!event || !modifiers || !event->repeat) return 0;
	*modifiers = 0;
	/* Alt and GUI combinations remain one-shot frontend/desktop gestures. */
	if (event->mod & (SDL_KMOD_ALT | SDL_KMOD_GUI)) return 0;
	switch (event->key) {
	case SDLK_UP:
	case SDLK_DOWN:
	case SDLK_LEFT:
	case SDLK_RIGHT:
	case SDLK_PAGEUP:
	case SDLK_PAGEDOWN:
	case SDLK_KP_1:
	case SDLK_KP_2:
	case SDLK_KP_3:
	case SDLK_KP_4:
	case SDLK_KP_5:
	case SDLK_KP_6:
	case SDLK_KP_7:
	case SDLK_KP_8:
	case SDLK_KP_9:
		return sdl3_input_translate_special(event, modifiers);
	case SDLK_H:
	case SDLK_J:
	case SDLK_K:
	case SDLK_L:
	case SDLK_Y:
	case SDLK_U:
	case SDLK_B:
	case SDLK_N:
		if (event->mod & SDL_KMOD_CTRL) return 0;
		key = (keycode_t)event->key;
		upper = !!(event->mod & SDL_KMOD_SHIFT) !=
			!!(event->mod & SDL_KMOD_CAPS);
		if (upper) key = (keycode_t)toupper((unsigned char)key);
		return key;
	case SDLK_PERIOD:
	case SDLK_COMMA:
		/* Unmodified period/comma are the two command-set wait keys.  Shifted
		 * punctuation continues through SDL text input, which supplies the
		 * correct keyboard-layout character. */
		if (event->mod & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT)) return 0;
		return (keycode_t)event->key;
	default:
		return 0;
	}
}

bool sdl3_input_is_document_scroll_key(const SDL_KeyboardEvent *event)
{
	if (!event) return false;
	switch (event->key) {
	case SDLK_UP:
	case SDLK_DOWN:
	case SDLK_KP_8:
	case SDLK_KP_2:
	case SDLK_PAGEUP:
	case SDLK_PAGEDOWN:
		return true;
	default:
		return false;
	}
}

bool sdl3_input_is_settings_shortcut(const SDL_KeyboardEvent *event)
{
	return event && event->scancode == SDL_SCANCODE_S &&
		(event->mod & SDL_KMOD_CTRL) && (event->mod & SDL_KMOD_ALT);
}

int sdl3_input_zoom_shortcut(const SDL_KeyboardEvent *event)
{
	if (!event || event->repeat || !(event->mod & SDL_KMOD_CTRL) ||
			(event->mod & (SDL_KMOD_ALT | SDL_KMOD_GUI))) {
		return 0;
	}
	switch (event->scancode) {
	case SDL_SCANCODE_EQUALS:
	case SDL_SCANCODE_KP_PLUS:
		return 1;
	case SDL_SCANCODE_MINUS:
	case SDL_SCANCODE_KP_MINUS:
		return -1;
	case SDL_SCANCODE_0:
	case SDL_SCANCODE_KP_0:
		return 2;
	default:
		return 0;
	}
}

enum sdl3_dock_placement sdl3_input_overlay_shortcut(
		const SDL_KeyboardEvent *event)
{
	if (!event || event->repeat || !(event->mod & SDL_KMOD_CTRL) ||
			(event->mod & (SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI))) {
		return SDL3_DOCK_PLACEMENT_COUNT;
	}
	switch (event->scancode) {
	case SDL_SCANCODE_1: return SDL3_DOCK_TOP;
	case SDL_SCANCODE_2: return SDL3_DOCK_LEFT;
	case SDL_SCANCODE_3: return SDL3_DOCK_BOTTOM;
	default: return SDL3_DOCK_PLACEMENT_COUNT;
	}
}
