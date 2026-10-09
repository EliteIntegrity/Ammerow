/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-mode-input.h
 * \brief Semantic input routing for the active world mode.
 *
 *
 */

#ifndef UI_MODE_INPUT_H
#define UI_MODE_INPUT_H

#include "cmd-core.h"
#include "ui-event.h"

struct player;

/* Private-use terminal keycodes preserve one held semantic input chord. */
#define KC_MODE_PEEK_BEGIN 0xE001
#define KC_MODE_PEEK_END 0xE002

/** Whether the active mode owns this key before ordinary keymap expansion. */
bool textui_mode_input_owns_key(const struct player *p, struct keypress key);

/** Translate and queue a mode-owned key; return false when it is unhandled. */
bool textui_mode_input_process_key(struct player *p, struct keypress key);

/** Cancel transient input state such as an offset peek. */
void textui_mode_input_cancel_transient(struct player *p);

/** Whether the active mode supports the held physical peek chord. */
bool textui_mode_input_uses_held_peek(const struct player *p);

/** NULL when mouse input may use the inherited handler, otherwise a reason. */
const char *textui_mode_input_mouse_rejection(const struct player *p);

/** Gate an ordinary command or synchronous hook before it touches map state. */
bool textui_mode_input_allows_command(const struct player *p,
		cmd_code command, bool has_hook, bool safe_in_side_view);

#endif /* !UI_MODE_INPUT_H */
