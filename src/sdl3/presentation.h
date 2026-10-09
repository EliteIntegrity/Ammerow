/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/presentation.h
 * \brief Explicit lifecycle and terminal-layer state for the SDL3 frontend.
 */

#ifndef INCLUDED_SDL3_PRESENTATION_H
#define INCLUDED_SDL3_PRESENTATION_H

#include <stdbool.h>
#include <stdint.h>

enum sdl3_presentation_phase {
	SDL3_PRESENTATION_STARTUP = 0,
	SDL3_PRESENTATION_HOME,
	SDL3_PRESENTATION_GAME_SETUP,
	SDL3_PRESENTATION_WORLD
};

enum sdl3_presentation_layer {
	SDL3_PRESENTATION_CLASSIC_MODAL = 0x01,
	SDL3_PRESENTATION_ITEM_SELECTOR = 0x02,
	SDL3_PRESENTATION_SEMANTIC_SCREEN = 0x04
};

struct sdl3_presentation_state {
	enum sdl3_presentation_phase phase;
	uint8_t layers;
};

void sdl3_presentation_init(struct sdl3_presentation_state *state);
void sdl3_presentation_set_phase(struct sdl3_presentation_state *state,
		enum sdl3_presentation_phase phase);
void sdl3_presentation_set_layer(struct sdl3_presentation_state *state,
		enum sdl3_presentation_layer layer, bool active);
bool sdl3_presentation_has_layer(
		const struct sdl3_presentation_state *state,
		enum sdl3_presentation_layer layer);
bool sdl3_presentation_dock_active(
		const struct sdl3_presentation_state *state, bool preferred_visible);
bool sdl3_presentation_map_zoom_active(
		const struct sdl3_presentation_state *state);
bool sdl3_presentation_world_animation_visible(
		const struct sdl3_presentation_state *state, bool window_active,
		bool map_active, bool on_surface, bool home_visible,
		bool pause_visible, bool settings_visible);

#endif /* INCLUDED_SDL3_PRESENTATION_H */
