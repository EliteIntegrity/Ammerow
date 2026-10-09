/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/frontend-internal.h
 * \brief Private coordination boundary for the SDL3 frontend.
 *
 *
 */

#ifndef INCLUDED_SDL3_FRONTEND_INTERNAL_H
#define INCLUDED_SDL3_FRONTEND_INTERNAL_H

#include "angband.h"

#include <SDL3/SDL.h>

#include "sdl3/config.h"
#include "sdl3/effects.h"
#include "sdl3/home.h"
#include "sdl3/input.h"
#include "sdl3/layout.h"
#include "sdl3/map-presenter.h"
#include "sdl3/pause-menu.h"
#include "sdl3/presentation.h"
#include "sdl3/presenter.h"
#include "sdl3/screen.h"
#include "sdl3/settings-model.h"
#include "sdl3/store-art.h"
#include "sdl3/weather.h"

#define SDL3_KEY_QUEUE_SIZE 1024
#define SDL3_SUBWINDOW_KEY_QUEUE_SIZE 32

struct sdl3_app;

struct sdl3_pane {
	struct sdl3_app *app;
	const struct sdl3_pane_layout *layout;
	term term;
	bool term_inited;
};

/* This structure is private to the SDL3 frontend implementation.  Modules
 * receive it explicitly; g_app remains owned only by main-sdl3.c. */
struct sdl3_app {
	SDL_Window *window;
	SDL_Renderer *renderer;
#ifdef USE_HEADLESS
	SDL_Surface *offscreen_surface;
#endif
	struct sdl3_grid grid;
	struct sdl3_grid dock_grid;
	struct sdl3_layout layout;
	struct sdl3_pane panes[SDL3_LAYOUT_PANE_COUNT];
	struct sdl3_presenter *presenter;
	struct sdl3_config config;
	struct sdl3_home_screen home;
	struct sdl3_pause_menu pause_menu;
	struct sdl3_settings_overlay settings;
	struct sdl3_screen screen;
	struct sdl3_presentation_state presentation;
	struct sdl3_combat_effects combat_effects;
	struct sdl3_map_presenter map_presenter;
	struct sdl3_weather_state weather;
	struct sdl3_store_art store_art;
	const struct sdl3_theme *theme;
	int cursor_col;
	int cursor_row;
	bool cursor_visible;
	bool fullscreen;
	bool ttf_inited;
	bool verbose;
	bool font_cache_needs_compaction;
	bool dock_active;
	bool game_handlers_registered;
	bool store_enter_handler_registered;
	bool store_leave_handler_registered;
	bool window_active;
#ifdef USE_HEADLESS
	bool offscreen;
#endif
	bool spelunk_peek_key_down;
	char failed_save[256];
	struct player *audio_player;
	bool audio_option_value;
	Uint64 weather_next_tick_ms;
	Uint64 fishing_next_tick_ms;
	struct loc missile_previous;
	bool missile_previous_valid;
	keycode_t suppress_text_codepoint;
	struct sdl3_input_gate input_gate;
};

/* Coordinator-owned rendering and platform bridges. */
void sdl3_frontend_log_error(const char *action);
void sdl3_frontend_capture_windowed_size(struct sdl3_app *app);
void sdl3_frontend_render(struct sdl3_app *app);
bool sdl3_frontend_render_animated_overlay(struct sdl3_app *app);
void sdl3_frontend_remember_selected_fonts(struct sdl3_app *app);

/* Settings controller. */
bool sdl3_frontend_toggle_fullscreen(struct sdl3_app *app);
void sdl3_frontend_set_settings_visible(struct sdl3_app *app, bool visible);
bool sdl3_frontend_change_zoom(struct sdl3_app *app, int direction);
void sdl3_frontend_change_selected_setting(struct sdl3_app *app,
		int direction);
void sdl3_frontend_activate_settings_selection(struct sdl3_app *app);
void sdl3_frontend_change_stats_visibility(struct sdl3_app *app);
void sdl3_frontend_handle_settings_key(struct sdl3_app *app,
		const SDL_KeyboardEvent *event);

/* Raw SDL event routing. */
void sdl3_frontend_show_field_guide(struct sdl3_app *app);
void sdl3_frontend_handle_home_key(struct sdl3_app *app,
		const SDL_KeyboardEvent *event);
void sdl3_frontend_handle_pause_key(struct sdl3_app *app,
		const SDL_KeyboardEvent *event);
bool sdl3_frontend_dispatch_event(struct sdl3_app *app,
		const SDL_Event *event);
int sdl3_frontend_wait_event(struct sdl3_app *app, SDL_Event *event);

/* Presentation and game-event lifecycle. */
bool sdl3_frontend_apply_dock_layout(struct sdl3_app *app, bool visible,
		enum sdl3_dock_placement placement, int rows, int cols);
void sdl3_frontend_set_dock_view(struct sdl3_app *app, uint32_t flag);
void sdl3_frontend_remember_current_run(struct sdl3_app *app);
void sdl3_frontend_register_game_handlers(struct sdl3_app *app);
void sdl3_frontend_unregister_game_handlers(struct sdl3_app *app);
void sdl3_frontend_reinitialize(struct sdl3_app *app);

#endif /* INCLUDED_SDL3_FRONTEND_INTERNAL_H */
