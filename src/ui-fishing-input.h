/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-fishing-input.h
 * \brief Raw-key translation for the fishing activity.
 */

#ifndef UI_FISHING_INPUT_H
#define UI_FISHING_INPUT_H

#include "h-basic.h"
#include "ui-event.h"
#include "world-fishing.h"

enum textui_fishing_binding_kind {
	TEXTUI_FISHING_UNHANDLED = 0,
	TEXTUI_FISHING_BLOCKED,
	TEXTUI_FISHING_ACTION
};

enum textui_fishing_binding_kind textui_fishing_translate_key(
		struct keypress key, enum world_fishing_phase phase,
		enum world_fishing_action *action);

#endif /* !UI_FISHING_INPUT_H */
