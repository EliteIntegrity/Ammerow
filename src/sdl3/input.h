/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/input.h
 * \brief SDL keyboard translation and frontend shortcut recognition.
 */

#ifndef INCLUDED_SDL3_INPUT_H
#define INCLUDED_SDL3_INPUT_H

#include <SDL3/SDL.h>

#include "ui-event.h"
#include "sdl3/layout.h"

/* A modal boundary must not inherit physical key holds from gameplay.  Once
 * armed, each key needs its own new (non-repeat) press before it can repeat.
 * Keep the gate across nested screens; another key must not unlock an old hold.
 * Zero initialization leaves ordinary startup/gameplay input unchanged. */
struct sdl3_input_gate {
	bool armed;
	bool fresh[SDL_SCANCODE_COUNT];
	bool text_allowed;
};

void sdl3_input_gate_arm(struct sdl3_input_gate *gate);
bool sdl3_input_gate_accept(struct sdl3_input_gate *gate,
		const SDL_Event *event);

uint8_t sdl3_input_translate_modifiers(SDL_Keymod modifiers);
bool sdl3_input_is_keypad_key(SDL_Keycode key);
keycode_t sdl3_input_translate_special(const SDL_KeyboardEvent *event,
		uint8_t *modifiers);
keycode_t sdl3_input_translate_repeated(const SDL_KeyboardEvent *event,
		uint8_t *modifiers);
bool sdl3_input_is_document_scroll_key(const SDL_KeyboardEvent *event);
bool sdl3_input_is_settings_shortcut(const SDL_KeyboardEvent *event);
int sdl3_input_zoom_shortcut(const SDL_KeyboardEvent *event);
enum sdl3_dock_placement sdl3_input_overlay_shortcut(
		const SDL_KeyboardEvent *event);

#endif /* INCLUDED_SDL3_INPUT_H */
