/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/input.c */
/* Exercise SDL keyboard translation without the frontend event loop. */

#include "unit-test.h"

#include "sdl3/input.h"

int setup_tests(void **data)
{
	(void)data;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	return 0;
}

static int test_special_and_control_keys(void *state)
{
	SDL_KeyboardEvent event;
	uint8_t modifiers = 0;
	(void)state;

	SDL_zero(event);
	event.key = SDLK_UP;
	event.mod = SDL_KMOD_SHIFT;
	eq(sdl3_input_translate_special(&event, &modifiers), ARROW_UP);
	eq(modifiers, KC_MOD_SHIFT);
	event.key = SDLK_C;
	event.mod = SDL_KMOD_CTRL;
	eq(sdl3_input_translate_special(&event, &modifiers), KTRL('C'));
	eq(modifiers, 0);
	ok;
}

static int test_keypad_modifier_respects_numlock(void *state)
{
	SDL_KeyboardEvent event;
	uint8_t modifiers = 0;
	(void)state;

	SDL_zero(event);
	event.key = SDLK_KP_7;
	eq(sdl3_input_translate_special(&event, &modifiers), '7');
	require(modifiers & KC_MOD_KEYPAD);
	event.mod = SDL_KMOD_NUM;
	eq(sdl3_input_translate_special(&event, &modifiers), '7');
	require(!(modifiers & KC_MOD_KEYPAD));
	ok;
}

static int test_shortcuts(void *state)
{
	SDL_KeyboardEvent event;
	(void)state;

	SDL_zero(event);
	event.scancode = SDL_SCANCODE_S;
	event.mod = SDL_KMOD_CTRL | SDL_KMOD_ALT;
	require(sdl3_input_is_settings_shortcut(&event));
	require(!sdl3_input_zoom_shortcut(&event));
	event.scancode = SDL_SCANCODE_EQUALS;
	event.mod = SDL_KMOD_CTRL;
	eq(sdl3_input_zoom_shortcut(&event), 1);
	event.scancode = SDL_SCANCODE_MINUS;
	eq(sdl3_input_zoom_shortcut(&event), -1);
	event.scancode = SDL_SCANCODE_0;
	eq(sdl3_input_zoom_shortcut(&event), 2);
	event.repeat = true;
	eq(sdl3_input_zoom_shortcut(&event), 0);
	event.repeat = false;
	event.mod = SDL_KMOD_CTRL;
	event.scancode = SDL_SCANCODE_1;
	eq(sdl3_input_overlay_shortcut(&event), SDL3_DOCK_TOP);
	event.scancode = SDL_SCANCODE_2;
	eq(sdl3_input_overlay_shortcut(&event), SDL3_DOCK_LEFT);
	event.scancode = SDL_SCANCODE_3;
	eq(sdl3_input_overlay_shortcut(&event), SDL3_DOCK_BOTTOM);
	event.scancode = SDL_SCANCODE_4;
	eq(sdl3_input_overlay_shortcut(&event),
		SDL3_DOCK_PLACEMENT_COUNT);
	event.scancode = SDL_SCANCODE_1;
	event.mod |= SDL_KMOD_SHIFT;
	eq(sdl3_input_overlay_shortcut(&event),
		SDL3_DOCK_PLACEMENT_COUNT);
	event.mod = SDL_KMOD_CTRL;
	event.repeat = true;
	eq(sdl3_input_overlay_shortcut(&event),
		SDL3_DOCK_PLACEMENT_COUNT);
	ok;
}

static int test_repeatable_gameplay_and_document_keys(void *state)
{
	SDL_KeyboardEvent event;
	uint8_t modifiers = 0;
	(void)state;

	SDL_zero(event);
	event.repeat = true;
	event.key = SDLK_RIGHT;
	require(!sdl3_input_is_document_scroll_key(&event));
	eq(sdl3_input_translate_repeated(&event, &modifiers), ARROW_RIGHT);
	eq(modifiers, 0);
	event.key = SDLK_DOWN;
	require(sdl3_input_is_document_scroll_key(&event));
	eq(sdl3_input_translate_repeated(&event, &modifiers), ARROW_DOWN);
	event.key = SDLK_KP_5;
	eq(sdl3_input_translate_repeated(&event, &modifiers), '5');
	require(modifiers & KC_MOD_KEYPAD);
	event.key = SDLK_H;
	event.mod = SDL_KMOD_SHIFT;
	eq(sdl3_input_translate_repeated(&event, &modifiers), 'H');
	eq(modifiers, 0);
	event.key = SDLK_PERIOD;
	event.mod = SDL_KMOD_NONE;
	eq(sdl3_input_translate_repeated(&event, &modifiers), '.');
	event.mod = SDL_KMOD_SHIFT;
	eq(sdl3_input_translate_repeated(&event, &modifiers), 0);
	event.key = SDLK_Q;
	event.mod = SDL_KMOD_NONE;
	eq(sdl3_input_translate_repeated(&event, &modifiers), 0);
	event.key = SDLK_PAGEDOWN;
	require(sdl3_input_is_document_scroll_key(&event));
	eq(sdl3_input_translate_repeated(&event, &modifiers), KC_PGDOWN);
	event.key = SDLK_RETURN;
	require(!sdl3_input_is_document_scroll_key(&event));
	event.repeat = false;
	event.key = SDLK_H;
	eq(sdl3_input_translate_repeated(&event, &modifiers), 0);
	ok;
}

static int test_modal_gate_blocks_old_holds_and_text(void *state)
{
	struct sdl3_input_gate gate = { 0 };
	SDL_Event event = { 0 };
	const SDL_Scancode held[] = {
		SDL_SCANCODE_DOWN, SDL_SCANCODE_KP_2, SDL_SCANCODE_N,
		SDL_SCANCODE_H, SDL_SCANCODE_R, SDL_SCANCODE_Q,
		SDL_SCANCODE_RETURN, SDL_SCANCODE_ESCAPE
	};
	size_t i;
	(void)state;

	/* Normal gameplay is unchanged before the first modal boundary. */
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.repeat = true;
	require(sdl3_input_gate_accept(&gate, &event));
	sdl3_input_gate_arm(&gate);
	for (i = 0; i < N_ELEMENTS(held); i++) {
		event.type = SDL_EVENT_KEY_DOWN;
		event.key.scancode = held[i];
		event.key.repeat = true;
		require(!sdl3_input_gate_accept(&gate, &event));
		event.type = SDL_EVENT_TEXT_INPUT;
		require(!sdl3_input_gate_accept(&gate, &event));
	}
	/* Releasing does not itself dismiss the record. */
	event.type = SDL_EVENT_KEY_UP;
	event.key.scancode = SDL_SCANCODE_N;
	require(sdl3_input_gate_accept(&gate, &event));
	event.type = SDL_EVENT_TEXT_INPUT;
	require(!sdl3_input_gate_accept(&gate, &event));
	/* The next deliberate press, and its text, work without a time delay. */
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.scancode = SDL_SCANCODE_N;
	event.key.repeat = false;
	require(sdl3_input_gate_accept(&gate, &event));
	event.type = SDL_EVENT_TEXT_INPUT;
	require(sdl3_input_gate_accept(&gate, &event));
	ok;
}

static int test_modal_gate_is_per_key_and_rearms(void *state)
{
	struct sdl3_input_gate gate = { 0 };
	SDL_Event event = { 0 };
	(void)state;

	sdl3_input_gate_arm(&gate);
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.scancode = SDL_SCANCODE_DOWN;
	require(sdl3_input_gate_accept(&gate, &event));
	event.key.repeat = true;
	require(sdl3_input_gate_accept(&gate, &event));
	/* Pressing a different key does not unlock the old held New Run key. */
	event.key.scancode = SDL_SCANCODE_N;
	require(!sdl3_input_gate_accept(&gate, &event));
	event.type = SDL_EVENT_TEXT_INPUT;
	require(!sdl3_input_gate_accept(&gate, &event));
	sdl3_input_gate_arm(&gate);
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.scancode = SDL_SCANCODE_DOWN;
	event.key.repeat = true;
	require(!sdl3_input_gate_accept(&gate, &event));
	event.key.repeat = false;
	require(sdl3_input_gate_accept(&gate, &event));
	event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
	require(sdl3_input_gate_accept(&gate, &event));
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.repeat = true;
	require(!sdl3_input_gate_accept(&gate, &event));
	ok;
}

static int test_modal_gate_preserves_mouse_and_window_events(void *state)
{
	struct sdl3_input_gate gate = { 0 };
	SDL_Event event = { 0 };
	const Uint32 types[] = { SDL_EVENT_MOUSE_BUTTON_DOWN,
		SDL_EVENT_MOUSE_BUTTON_UP, SDL_EVENT_MOUSE_MOTION,
		SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_WINDOW_EXPOSED, SDL_EVENT_QUIT };
	size_t i;
	(void)state;

	sdl3_input_gate_arm(&gate);
	for (i = 0; i < N_ELEMENTS(types); i++) {
		event.type = types[i];
		require(sdl3_input_gate_accept(&gate, &event));
	}
	event.type = SDL_EVENT_KEY_DOWN;
	event.key.scancode = SDL_SCANCODE_UNKNOWN;
	event.key.repeat = true;
	require(!sdl3_input_gate_accept(&gate, &event));
	event.key.repeat = false;
	require(sdl3_input_gate_accept(&gate, &event));
	event.key.scancode = SDL_SCANCODE_COUNT;
	event.key.repeat = true;
	require(!sdl3_input_gate_accept(&gate, &event));
	ok;
}

const char *suite_name = "sdl3/input";
struct test tests[] = {
	{ "special and control keys", test_special_and_control_keys },
	{ "keypad modifier respects Num Lock",
		test_keypad_modifier_respects_numlock },
	{ "frontend shortcuts", test_shortcuts },
	{ "repeatable gameplay and document keys",
		test_repeatable_gameplay_and_document_keys },
	{ "modal gate blocks old holds and text",
		test_modal_gate_blocks_old_holds_and_text },
	{ "modal gate is per key and rearms", test_modal_gate_is_per_key_and_rearms },
	{ "modal gate preserves mouse and window events",
		test_modal_gate_preserves_mouse_and_window_events },
	{ NULL, NULL },
};
