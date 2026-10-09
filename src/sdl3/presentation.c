/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/presentation.c
 * \brief Explicit lifecycle and terminal-layer state for the SDL3 frontend.
 */

#include "sdl3/presentation.h"

#include <string.h>

void sdl3_presentation_init(struct sdl3_presentation_state *state)
{
	if (!state) return;
	memset(state, 0, sizeof(*state));
	state->phase = SDL3_PRESENTATION_STARTUP;
}

void sdl3_presentation_set_phase(struct sdl3_presentation_state *state,
		enum sdl3_presentation_phase phase)
{
	if (!state) return;
	state->phase = phase;
	if (phase != SDL3_PRESENTATION_WORLD) state->layers = 0;
}

void sdl3_presentation_set_layer(struct sdl3_presentation_state *state,
		enum sdl3_presentation_layer layer, bool active)
{
	if (!state) return;
	if (active) state->layers |= (uint8_t)layer;
	else state->layers &= (uint8_t)~layer;
}

bool sdl3_presentation_has_layer(
		const struct sdl3_presentation_state *state,
		enum sdl3_presentation_layer layer)
{
	return state && (state->layers & (uint8_t)layer) != 0;
}

bool sdl3_presentation_dock_active(
		const struct sdl3_presentation_state *state, bool preferred_visible)
{
	const uint8_t blockers = SDL3_PRESENTATION_CLASSIC_MODAL |
		SDL3_PRESENTATION_ITEM_SELECTOR;

	return state && preferred_visible &&
		state->phase == SDL3_PRESENTATION_WORLD &&
		!(state->layers & blockers);
}

bool sdl3_presentation_map_zoom_active(
		const struct sdl3_presentation_state *state)
{
	const uint8_t blockers = SDL3_PRESENTATION_CLASSIC_MODAL |
		SDL3_PRESENTATION_ITEM_SELECTOR |
		SDL3_PRESENTATION_SEMANTIC_SCREEN;

	return state && state->phase == SDL3_PRESENTATION_WORLD &&
		!(state->layers & blockers);
}

bool sdl3_presentation_world_animation_visible(
		const struct sdl3_presentation_state *state, bool window_active,
		bool map_active, bool on_surface, bool home_visible,
		bool pause_visible, bool settings_visible)
{
	const uint8_t blockers = SDL3_PRESENTATION_CLASSIC_MODAL |
		SDL3_PRESENTATION_ITEM_SELECTOR;

	return state && window_active && map_active && on_surface &&
		!home_visible && !pause_visible && !settings_visible &&
		!(state->layers & blockers);
}
