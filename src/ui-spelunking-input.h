/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking-input.h
 * \brief Physical-key translation for side-view spelunking.
 *
 *
 */

#ifndef UI_SPELUNKING_INPUT_H
#define UI_SPELUNKING_INPUT_H

#include "h-basic.h"
#include "ui-event.h"
#include "world-spelunking.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-passage.h"
#include "world-spelunking-visibility.h"

enum textui_spelunking_binding_kind {
	TEXTUI_SPELUNKING_UNHANDLED = 0,
	TEXTUI_SPELUNKING_BLOCKED,
	TEXTUI_SPELUNKING_ACTION,
	TEXTUI_SPELUNKING_LOCAL,
	TEXTUI_SPELUNKING_PEEK,
	TEXTUI_SPELUNKING_DROP,
	TEXTUI_SPELUNKING_PASSAGE,
	TEXTUI_SPELUNKING_EXIT
};

struct textui_spelunking_binding {
	enum textui_spelunking_binding_kind kind;
	struct world_spelunk_command action;
	enum world_spelunk_local_action local_action;
	enum world_spelunk_peek_action peek_action;
	enum world_spelunk_passage_direction passage_direction;
};

/**
 * Translate one raw key before Angband's ordinary movement keymaps expand it.
 * Modified mode-owned keys are blocked rather than becoming lethal actions.
 */
enum textui_spelunking_binding_kind textui_spelunking_translate_key(
		struct keypress key, struct textui_spelunking_binding *binding);

#endif /* !UI_SPELUNKING_INPUT_H */
