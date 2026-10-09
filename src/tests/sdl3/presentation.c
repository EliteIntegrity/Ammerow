/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/presentation.c */
/* Exercise derived lifecycle and dock state for the SDL3 frontend. */

#include "unit-test.h"

#include "sdl3/presentation.h"

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

static int test_dock_requires_ready_world_and_preference(void *state)
{
	struct sdl3_presentation_state presentation;
	(void)state;

	sdl3_presentation_init(&presentation);
	require(!sdl3_presentation_dock_active(&presentation, true));
	sdl3_presentation_set_phase(&presentation, SDL3_PRESENTATION_HOME);
	require(!sdl3_presentation_dock_active(&presentation, true));
	sdl3_presentation_set_phase(&presentation,
		SDL3_PRESENTATION_GAME_SETUP);
	require(!sdl3_presentation_dock_active(&presentation, true));
	sdl3_presentation_set_phase(&presentation, SDL3_PRESENTATION_WORLD);
	require(sdl3_presentation_dock_active(&presentation, true));
	require(!sdl3_presentation_dock_active(&presentation, false));
	ok;
}

static int test_modal_and_selector_layers_compose(void *state)
{
	struct sdl3_presentation_state presentation;
	(void)state;

	sdl3_presentation_init(&presentation);
	sdl3_presentation_set_phase(&presentation, SDL3_PRESENTATION_WORLD);
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_CLASSIC_MODAL, true);
	require(!sdl3_presentation_dock_active(&presentation, true));
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_ITEM_SELECTOR, true);
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_CLASSIC_MODAL, false);
	require(!sdl3_presentation_dock_active(&presentation, true));
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_ITEM_SELECTOR, false);
	require(sdl3_presentation_dock_active(&presentation, true));
	ok;
}

static int test_semantic_layer_does_not_change_layout(void *state)
{
	struct sdl3_presentation_state presentation;
	(void)state;

	sdl3_presentation_init(&presentation);
	sdl3_presentation_set_phase(&presentation, SDL3_PRESENTATION_WORLD);
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_SEMANTIC_SCREEN, true);
	require(sdl3_presentation_has_layer(&presentation,
		SDL3_PRESENTATION_SEMANTIC_SCREEN));
	require(sdl3_presentation_dock_active(&presentation, true));
	ok;
}

static int test_map_zoom_requires_unobscured_world(void *state)
{
	struct sdl3_presentation_state presentation;
	(void)state;

	sdl3_presentation_init(&presentation);
	require(!sdl3_presentation_map_zoom_active(&presentation));
	sdl3_presentation_set_phase(&presentation, SDL3_PRESENTATION_WORLD);
	require(sdl3_presentation_map_zoom_active(&presentation));
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_SEMANTIC_SCREEN, true);
	require(!sdl3_presentation_map_zoom_active(&presentation));
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_SEMANTIC_SCREEN, false);
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_CLASSIC_MODAL, true);
	require(!sdl3_presentation_map_zoom_active(&presentation));
	ok;
}

static int test_leaving_world_clears_transient_layers(void *state)
{
	struct sdl3_presentation_state presentation;
	(void)state;

	sdl3_presentation_init(&presentation);
	sdl3_presentation_set_phase(&presentation, SDL3_PRESENTATION_WORLD);
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_CLASSIC_MODAL, true);
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_SEMANTIC_SCREEN, true);
	sdl3_presentation_set_phase(&presentation,
		SDL3_PRESENTATION_GAME_SETUP);
	eq(presentation.layers, 0);
	require(!sdl3_presentation_dock_active(&presentation, true));
	ok;
}

static int test_world_animation_requires_unobscured_surface(void *state)
{
	struct sdl3_presentation_state presentation;
	(void)state;

	sdl3_presentation_init(&presentation);
	sdl3_presentation_set_phase(&presentation, SDL3_PRESENTATION_WORLD);
	require(sdl3_presentation_world_animation_visible(&presentation, true,
		true, true, false, false, false));
	require(!sdl3_presentation_world_animation_visible(&presentation, false,
		true, true, false, false, false));
	require(!sdl3_presentation_world_animation_visible(&presentation, true,
		false, true, false, false, false));
	require(!sdl3_presentation_world_animation_visible(&presentation, true,
		true, false, false, false, false));
	require(!sdl3_presentation_world_animation_visible(&presentation, true,
		true, true, true, false, false));
	require(!sdl3_presentation_world_animation_visible(&presentation, true,
		true, true, false, true, false));
	require(!sdl3_presentation_world_animation_visible(&presentation, true,
		true, true, false, false, true));
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_CLASSIC_MODAL, true);
	require(!sdl3_presentation_world_animation_visible(&presentation, true,
		true, true, false, false, false));
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_CLASSIC_MODAL, false);
	sdl3_presentation_set_layer(&presentation,
		SDL3_PRESENTATION_ITEM_SELECTOR, true);
	require(!sdl3_presentation_world_animation_visible(&presentation, true,
		true, true, false, false, false));
	ok;
}

const char *suite_name = "sdl3/presentation";
struct test tests[] = {
	{ "dock requires ready world and preference",
		test_dock_requires_ready_world_and_preference },
	{ "modal and selector layers compose",
		test_modal_and_selector_layers_compose },
	{ "semantic layer keeps terminal layout",
		test_semantic_layer_does_not_change_layout },
	{ "map zoom requires unobscured world",
		test_map_zoom_requires_unobscured_world },
	{ "leaving world clears transient layers",
		test_leaving_world_clears_transient_layers },
	{ "world animation requires unobscured surface",
		test_world_animation_requires_unobscured_surface },
	{ NULL, NULL },
};
