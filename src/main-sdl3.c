/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file main-sdl3.c
 * \brief Angband hooks, terminal adapter, and platform owner for SDL3.
 *
 *
 */

#include "angband.h"

#ifdef USE_SDL3

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#ifdef USE_HEADLESS
#include <SDL3_image/SDL_image.h>
#include <errno.h>
#include <stdlib.h>
#include "ui-menu.h"
#include "ui-store-screen.h"
#include "sdl3/monster-art.h"
#endif

#include "buildid.h"
#include "game-world.h"
#include "init.h"
#include "main.h"
#include "mon-predicate.h"
#include "monster.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "project.h"
#include "savefile.h"
#include "snd-sdl3.h"
#include "store.h"
#include "target.h"
#include "ui-birth.h"
#include "ui-display.h"
#include "ui-fishing.h"
#include "ui-game.h"
#include "ui-help.h"
#include "world-story-data.h"
#include "ui-map.h"
#include "ui-mode-input.h"
#include "ui-output.h"
#include "ui-object.h"
#include "ui-prefs.h"
#include "ui-score.h"
#include "ui-store.h"
#include "world-turn.h"
#include "world-spelunking-adapter.h"
#include "sdl3/config.h"
#include "sdl3/credits.h"
#include "sdl3/effects.h"
#include "sdl3/fishing.h"
#include "sdl3/frontend-internal.h"
#include "sdl3/home.h"
#include "sdl3/home-feature.h"
#include "sdl3/input.h"
#include "sdl3/layout.h"
#include "sdl3/legal.h"
#include "sdl3/map-presenter.h"
#include "sdl3/inspect-card.h"
#include "sdl3/pause-menu.h"
#include "sdl3/presentation.h"
#include "sdl3/presenter.h"
#include "sdl3/screen.h"
#include "sdl3/settings.h"
#include "sdl3/store-art.h"
#include "sdl3/terrain.h"
#include "sdl3/term.h"
#include "sdl3/theme.h"
#include "sdl3/weather.h"
#include "sdl3/zoom.h"
#include "ui-target.h"

/* The one application instance.  Extracted frontend modules receive it only
 * through the explicit context interface in frontend-internal.h. */
static struct sdl3_app g_app;
#ifdef USE_HEADLESS
static bool g_offscreen_requested;
static bool g_capture_inspection;

struct sdl3_capture_sequence {
	bool active;
	bool failed;
	char directory[1024];
	unsigned int fps;
	unsigned int frame_count;
	Uint64 next_frame_ms;
};

static struct sdl3_capture_sequence g_capture_sequence;

static void sdl3_capture_sequence_after_render(void)
{
	char filename[32];
	char path[1200];
	Uint64 now;

	if (!g_capture_sequence.active || g_capture_sequence.failed ||
			!g_app.offscreen_surface) {
		return;
	}
	now = SDL_GetTicks();
	if (now < g_capture_sequence.next_frame_ms) return;
	strnfmt(filename, sizeof(filename), "frame-%06u.png",
		g_capture_sequence.frame_count + 1);
	path_build(path, sizeof(path), g_capture_sequence.directory, filename);
	if (!IMG_SavePNG(g_app.offscreen_surface, path)) {
		g_capture_sequence.failed = true;
		return;
	}
	g_capture_sequence.frame_count++;
	g_capture_sequence.next_frame_ms = SDL_GetTicks() +
		(1000U + g_capture_sequence.fps - 1U) /
		g_capture_sequence.fps;
}
#endif

static bool sdl3_owns_fishing_overlay(void)
{
	return true;
}

static void sdl3_fishing_overlay_changed(void)
{
	g_app.fishing_next_tick_ms = player &&
		sdl3_fishing_is_active(player->fishing) ?
		SDL_GetTicks() + SDL3_FISHING_ANIMATION_MS : 0;
	sdl3_grid_mark_dirty(&g_app.grid);
}

static void term_screen_hook(const struct ui_screen *screen);
static enum textui_escape_menu_action sdl3_escape_menu(void);
static bool sdl3_load_failure_screen(const char *filename);

static int sdl3_store_usable_width(int term_width)
{
	return g_app.store_art.active ?
		sdl3_store_art_content_cols(term_width) : term_width;
}

const char help_sdl3[] =
	"SDL3 ASCII frontend, subopts -v(ersion details) -T(smoke test) "
	"-M(font-memory stress test) "
	"-f<interface font> -m<map font> -t<theme> "
	"-a<classic|natural> "
	"-g ascii|hybrid-32|hybrid-64 "
#ifdef USE_HEADLESS
	"-z<width>x<height> "
#endif
	"-F(ullscreen) -W(indowed); "
	"F11/Alt+Enter toggles fullscreen, Ctrl+Alt+S opens settings, "
	"Ctrl+1/2/3 toggles the top/left/bottom gameplay overlays, "
	"Ctrl+Plus/Minus or the mouse wheel zooms the play area, Ctrl+0 resets map zoom";

void sdl3_frontend_log_error(const char *action)
{
	SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", action,
		SDL_GetError());
}

#ifdef USE_HEADLESS
static bool parse_output_size(const char *text, int *width, int *height)
{
	char *end;
	long parsed_width;
	long parsed_height;

	errno = 0;
	parsed_width = strtol(text, &end, 10);
	if (errno == ERANGE || end == text || (*end != 'x' && *end != 'X')) {
		return false;
	}
	text = end + 1;
	errno = 0;
	parsed_height = strtol(text, &end, 10);
	if (errno == ERANGE || end == text || *end ||
			parsed_width < SDL3_MIN_WINDOW_WIDTH ||
			parsed_width > SDL3_MAX_WINDOW_SIZE ||
			parsed_height < SDL3_MIN_WINDOW_HEIGHT ||
			parsed_height > SDL3_MAX_WINDOW_SIZE) {
		return false;
	}
	*width = (int)parsed_width;
	*height = (int)parsed_height;
	return true;
}
#endif

void sdl3_frontend_capture_windowed_size(struct sdl3_app *app)
{
	int width;
	int height;

	if (!app->window || app->fullscreen ||
			!SDL_GetWindowSize(app->window, &width, &height)) {
		return;
	}
	if (width >= SDL3_MIN_WINDOW_WIDTH && width <= SDL3_MAX_WINDOW_SIZE &&
			height >= SDL3_MIN_WINDOW_HEIGHT &&
			height <= SDL3_MAX_WINDOW_SIZE) {
		app->config.window_width = width;
		app->config.window_height = height;
	}
}

static void shutdown_sdl3(struct sdl3_app *app)
{
	struct sdl3_presenter_info info;
	int i;

	if (textui_escape_menu_hook == sdl3_escape_menu) {
		textui_escape_menu_hook = NULL;
	}
	if (textui_startup_menu_hook == sdl3_startup_menu) {
		textui_startup_menu_hook = NULL;
	}
	if (textui_load_failure_hook == sdl3_load_failure_screen) {
		textui_load_failure_hook = NULL;
	}
	if (textui_store_usable_width_hook == sdl3_store_usable_width) {
		textui_store_usable_width_hook = NULL;
	}
	if (textui_fishing_overlay_hook == sdl3_owns_fishing_overlay) {
		textui_fishing_overlay_hook = NULL;
	}
	if (textui_fishing_overlay_changed_hook ==
			sdl3_fishing_overlay_changed) {
		textui_fishing_overlay_changed_hook = NULL;
	}
	sdl3_audio_set_active(false);
	sdl3_frontend_unregister_game_handlers(app);

	if (app->presenter) {
#ifdef USE_HEADLESS
		if (!app->offscreen) {
#endif
		sdl3_frontend_capture_windowed_size(app);
		app->config.fullscreen = app->fullscreen;
		sdl3_frontend_remember_current_run(app);
		sdl3_config_save(&app->config);
#ifdef USE_HEADLESS
		}
#endif
		if (app->verbose) {
			sdl3_presenter_get_info(app->presenter, &info);
			SDL_Log("Presentation work: %llu frames, %llu cell updates",
				(unsigned long long)info.frames_presented,
				(unsigned long long)info.cell_updates_total);
			if (info.frames_presented) {
				SDL_Log("Present wait: %llu us average, %llu us maximum",
					(unsigned long long)(info.present_wait_ns_total /
					info.frames_presented / 1000),
					(unsigned long long)(info.present_wait_ns_max /
					1000));
			}
		}
	}
	if (app->window) SDL_StopTextInput(app->window);
	sdl3_presenter_destroy(app->presenter);
	if (app->renderer) SDL_DestroyRenderer(app->renderer);
	if (app->window) SDL_DestroyWindow(app->window);
#ifdef USE_HEADLESS
	if (app->offscreen_surface) SDL_DestroySurface(app->offscreen_surface);
#endif
	if (app->ttf_inited) TTF_Quit();
	sdl3_map_presenter_free(&app->map_presenter);
	sdl3_home_cleanup(&app->home);
	sdl3_grid_free(&app->dock_grid);
	sdl3_grid_free(&app->grid);
	SDL_Quit();

	app->window = NULL;
	app->renderer = NULL;
#ifdef USE_HEADLESS
	app->offscreen_surface = NULL;
#endif
	app->presenter = NULL;
	app->ttf_inited = false;
	for (i = 0; i < SDL3_LAYOUT_PANE_COUNT; i++) {
		app->panes[i].term_inited = false;
	}
}

static void term_init_sdl3(term *t)
{
	(void)t;
}

static void term_nuke_sdl3(term *t)
{
	struct sdl3_pane *pane = t ? t->data : NULL;

	if (!pane) return;
	pane->term_inited = false;
	angband_term[pane->layout->term_index] = NULL;
	if (pane->layout->term_index == SDL3_LAYOUT_MAIN_TERM) {
		shutdown_sdl3(pane->app);
	}
}

static struct sdl3_pane *active_pane(void)
{
	struct sdl3_pane *pane = Term ? Term->data : NULL;

	if (pane && pane->app == &g_app) return pane;
	return &g_app.panes[SDL3_LAYOUT_MAIN_TERM];
}

static struct sdl3_grid *pane_grid(const struct sdl3_pane *pane)
{
	if (!pane || !pane->app) return &g_app.grid;
	return pane->layout->term_index == SDL3_LAYOUT_MESSAGE_TERM ?
		&pane->app->dock_grid : &pane->app->grid;
}

static void sync_audio_option(struct sdl3_app *app)
{
	bool option_value;

	if (!player) return;
	if (app->audio_player != player) {
		player->opts.opt[OPT_use_sound] = app->config.audio.enabled;
		app->audio_player = player;
		app->audio_option_value = app->config.audio.enabled;
		return;
	}
	option_value = OPT(player, use_sound);
	if (option_value != app->audio_option_value) {
		app->config.audio.enabled = option_value;
		app->audio_option_value = option_value;
		sdl3_audio_set_settings(&app->config.audio);
	}
}

static void start_home_music(struct sdl3_app *app)
{
	/*
	 * Home opens before the first frontend render.  Synchronize the saved
	 * frontend setting now so Angband's sound() gate does not discard the
	 * one-shot request and leave the otherwise healthy music voice silent.
	 */
	sync_audio_option(app);
	sound(MSG_MUSIC_HOME);
}

static void configure_weather(struct sdl3_app *app)
{
	bool visible;
	uint32_t seed = 1U;

	if (!app) return;
	visible = app->config.weather_mode != SDL3_WEATHER_OFF && cave &&
		world_player_mode_has_capability(player,
			WORLD_MODE_CAP_NATIVE_CAVE) &&
		sdl3_presentation_world_animation_visible(&app->presentation,
			app->window_active, app->map_presenter.view.active,
			cave->depth == 0, app->home.visible, app->pause_menu.visible,
			app->settings.visible);
	if (cave) {
		seed = sdl3_terrain_level_key(cave->turn, cave->depth, cave->width,
			cave->height);
	}
	if (app->weather.mode != SDL3_WEATHER_AUTO ||
			app->weather.seed != seed) {
		sdl3_weather_reset(&app->weather, SDL3_WEATHER_AUTO, seed);
	}
	if (visible != app->weather.visible) {
		sdl3_weather_set_visible(&app->weather, visible);
		app->weather_next_tick_ms = app->weather.visible ?
			SDL_GetTicks() + SDL3_WEATHER_TICK_MS : 0;
		sdl3_grid_mark_dirty(&app->grid);
	}
}

void sdl3_frontend_render(struct sdl3_app *app)
{
	struct sdl3_map_presenter_options map_options;
	struct sdl3_presentation_frame frame;
	struct sdl3_presenter_info info;

	if (!app->renderer || !app->presenter) return;
	sdl3_effects_present(&app->combat_effects, SDL_GetTicks());
	sdl3_presenter_get_info(app->presenter, &info);
	sync_audio_option(app);
	memset(&map_options, 0, sizeof(map_options));
	map_options.cursor_col = app->cursor_col;
	map_options.cursor_row = app->cursor_row;
	map_options.cursor_visible = app->cursor_visible;
	map_options.inspection_visible = textui_target_is_inspecting();
#ifdef USE_HEADLESS
	map_options.inspection_visible |= g_capture_inspection;
#endif
	map_options.settings_visible = app->settings.visible;
	map_options.hud_stats_visible = app->config.hud_stats_visible &&
		!(player && player->fishing && player->fishing->active);
	map_options.messages_visible = app->dock_active;
	map_options.message_placement = app->config.dock_placement;
	map_options.message_rows = app->dock_grid.rows;
	map_options.big_stat_cards = app->config.big_stat_cards;
	map_options.animated_combat = app->config.animated_combat;
	map_options.edge_warnings = app->config.edge_warnings && !app->pause_menu.visible;
	map_options.tile_mode = app->config.tile_mode;
	map_options.zoom_percent = app->config.map_zoom_percent;
	map_options.terrain_style = app->config.terrain_style;
	map_options.verbose = app->verbose;
	sdl3_map_presenter_configure(&app->map_presenter, &app->grid,
		app->panes[SDL3_LAYOUT_MAIN_TERM].layout,
		angband_term[SDL3_LAYOUT_MAIN_TERM], &map_options);
	configure_weather(app);
	memset(&frame, 0, sizeof(frame));
	frame.grid = &app->grid;
	frame.dock_grid = &app->dock_grid;
	frame.theme = app->theme;
	frame.home = &app->home;
	frame.pause_menu = &app->pause_menu;
	frame.settings = &app->settings;
	frame.game_options = player ? &player->opts : NULL;
	frame.playing = character_dungeon && player && !player->is_dead;
	frame.screen = &app->screen;
	frame.screen_parent = app->screen.modal ? &app->screen_parent : NULL;
	frame.interface_font_name = info.interface_font_loaded ?
		sdl3_font_filename(info.interface_font_path) : "";
	frame.map_font_name = info.map_font_path[0] ?
		sdl3_font_filename(info.map_font_path) : "";
	frame.interface_density = app->config.interface_density;
	frame.map_view = &app->map_presenter.view;
	frame.weather = &app->weather;
	frame.combat_effects = &app->combat_effects;
	frame.fishing = player && player->fishing ? player->fishing : NULL;
	frame.store_art = &app->store_art;
	frame.inspect_card = &app->map_presenter.inspect_card;
	frame.fullscreen = app->fullscreen;
	frame.cursor_visible = app->cursor_visible ||
		app->map_presenter.inspect_card.active;
	frame.cursor_col = app->cursor_col;
	frame.cursor_row = app->cursor_row;
	frame.dock_active = app->dock_active;
	frame.dock_visible = app->config.dock_visible;
	frame.hud_stats_visible = app->config.hud_stats_visible;
	frame.big_stat_cards = app->config.big_stat_cards;
	frame.animated_combat = app->config.animated_combat;
	frame.edge_warnings = app->config.edge_warnings;
	frame.tile_mode = app->config.tile_mode;
	frame.hybrid_tile_size = app->config.hybrid_tile_size;
	frame.dock_placement = app->config.dock_placement;
	frame.dock_rows = app->config.dock_rows;
	frame.dock_cols = app->config.dock_cols;
	frame.audio = &app->config.audio;
	frame.terrain_style = app->config.terrain_style;
	frame.visible_weather = app->config.weather_mode != SDL3_WEATHER_OFF;
	sdl3_presenter_render(app->presenter, app->renderer, &frame);
#ifdef USE_HEADLESS
	sdl3_capture_sequence_after_render();
#endif
}

bool sdl3_frontend_render_animated_overlay(struct sdl3_app *app)
{
	struct sdl3_overlay_frame frame;

	if (!app) return false;
	sdl3_effects_present(&app->combat_effects, SDL_GetTicks());
	memset(&frame, 0, sizeof(frame));
	frame.theme = app->theme;
	frame.grid = &app->grid;
	frame.dock_grid = &app->dock_grid;
	frame.map_view = &app->map_presenter.view;
	frame.weather = &app->weather;
	frame.combat_effects = &app->combat_effects;
	frame.fishing = player && player->fishing ? player->fishing : NULL;
	frame.inspect_card = &app->map_presenter.inspect_card;
	frame.cursor_visible = app->cursor_visible ||
		app->map_presenter.inspect_card.active;
	frame.cursor_col = app->cursor_col;
	frame.cursor_row = app->cursor_row;
	frame.dock_active = app->dock_active;
	frame.hud_stats_visible = app->config.hud_stats_visible;
	frame.dock_placement = app->config.dock_placement;
	{
		bool rendered = sdl3_presenter_render_overlay(app->presenter,
			app->renderer, &frame);
#ifdef USE_HEADLESS
		if (rendered) sdl3_capture_sequence_after_render();
#endif
		return rendered;
	}
}

static bool populate_memory_stress_map_font(struct sdl3_app *app)
{
	return sdl3_presenter_prime_map_glyphs(app->presenter);
}

static bool run_font_memory_stress(struct sdl3_app *app)
{
	static const int zooms[] = {
		50, 67, 80, 100, 125, 150, 200, 250, 300, 400, 500, 600, 800
	};
	bool original_dock_visible = app->config.dock_visible;
	enum sdl3_presentation_phase original_phase = app->presentation.phase;
	enum sdl3_dock_placement original_dock_placement =
		app->config.dock_placement;
	int original_dock_rows = app->config.dock_rows;
	int original_dock_cols = app->config.dock_cols;
	enum sdl3_interface_density original_density =
		app->config.interface_density;
	int original_zoom = app->config.map_zoom_percent;
	int baseline_allocations = SDL_GetNumAllocations();
	int pass;
	struct sdl3_font_resource_counts counts;

	sdl3_presentation_set_phase(&app->presentation,
		SDL3_PRESENTATION_WORLD);
	sdl3_font_get_resource_counts(&counts);
	SDL_Log("Font-memory stress: initial SDL allocations %d",
		baseline_allocations);
	SDL_Log("Font-memory stress: initial live resources %d fonts, %d engines, "
		"%d texts", counts.fonts, counts.engines, counts.texts);
	for (pass = 0; pass < 5; pass++) {
		int i;

		for (i = SDL3_LAYOUT_MIN_DOCK_ROWS;
				i <= SDL3_LAYOUT_MAX_DOCK_ROWS; i++) {
			if (!sdl3_frontend_apply_dock_layout(app, true, SDL3_DOCK_BOTTOM, i,
					original_dock_cols)) {
				return false;
			}
			if (!populate_memory_stress_map_font(app)) return false;
			sdl3_frontend_render(app);
		}
		for (i = SDL3_LAYOUT_MIN_DOCK_ROWS;
				i <= SDL3_LAYOUT_MAX_DOCK_ROWS; i++) {
			if (!sdl3_frontend_apply_dock_layout(app, true, SDL3_DOCK_TOP, i,
					original_dock_cols)) {
				return false;
			}
			if (!populate_memory_stress_map_font(app)) return false;
			sdl3_frontend_render(app);
		}
		for (i = 0; i < (int)N_ELEMENTS(zooms); i++) {
			if (!sdl3_presenter_set_map_zoom(app->presenter, app->renderer,
					zooms[i])) {
				return false;
			}
			if (!populate_memory_stress_map_font(app)) return false;
			app->config.map_zoom_percent = zooms[i];
			sdl3_grid_mark_dirty(&app->grid);
			sdl3_frontend_render(app);
		}
		for (i = SDL3_INTERFACE_LARGE;
				i < SDL3_INTERFACE_DENSITY_COUNT; i++) {
			app->config.interface_density =
				(enum sdl3_interface_density)i;
			if (!sdl3_frontend_apply_dock_layout(app, true, app->config.dock_placement,
					app->config.dock_rows, app->config.dock_cols)) {
				return false;
			}
			if (!populate_memory_stress_map_font(app)) return false;
			sdl3_frontend_render(app);
		}
		sdl3_font_get_resource_counts(&counts);
		SDL_Log("Font-memory stress: pass %d SDL allocations %d (delta %+d); "
			"live %d/%d/%d; created %llu/%llu/%llu",
			pass + 1, SDL_GetNumAllocations(),
			SDL_GetNumAllocations() - baseline_allocations, counts.fonts,
			counts.engines, counts.texts,
			(unsigned long long)counts.fonts_opened,
			(unsigned long long)counts.engines_created,
			(unsigned long long)counts.texts_created);
		SDL_Delay(500);
	}

	sdl3_presentation_set_phase(&app->presentation, original_phase);
	app->config.interface_density = original_density;
	if (!sdl3_frontend_apply_dock_layout(app, original_dock_visible,
			original_dock_placement, original_dock_rows,
			original_dock_cols) ||
			!sdl3_presenter_set_map_zoom(app->presenter, app->renderer,
			original_zoom)) {
		return false;
	}
	app->config.map_zoom_percent = original_zoom;
	{
		struct sdl3_presenter_info info;
		char font_path[SDL3_FONT_PATH_CAPACITY];

		sdl3_presenter_get_info(app->presenter, &info);
		SDL_strlcpy(font_path, info.interface_font_path, sizeof(font_path));
		if (info.interface_font_loaded &&
				!sdl3_presenter_set_font(app->presenter, app->renderer,
				font_path)) {
			return false;
		}
	}
	app->font_cache_needs_compaction = false;
	sdl3_grid_mark_all_dirty(&app->grid);
	sdl3_frontend_render(app);
	sdl3_font_get_resource_counts(&counts);
	SDL_Log("Font-memory stress: compacted SDL allocations %d (delta %+d); "
		"live %d/%d/%d; created %llu/%llu/%llu",
		SDL_GetNumAllocations(),
		SDL_GetNumAllocations() - baseline_allocations, counts.fonts,
		counts.engines, counts.texts,
		(unsigned long long)counts.fonts_opened,
		(unsigned long long)counts.engines_created,
		(unsigned long long)counts.texts_created);
	SDL_Delay(1000);
	return true;
}

static void term_screen_hook(const struct ui_screen *screen)
{
	if (screen && (screen->kind == UI_SCREEN_RUN_SUMMARY ||
			screen->kind == UI_SCREEN_STORY || screen->kind == UI_SCREEN_ADVANCEMENT) &&
			g_app.screen.kind != screen->kind) {
		/* Flush queued commands AND fence future OS repeats.  Repainting the
		 * same card must not re-arm; entering a new modal card should. */
		sdl3_input_gate_arm(&g_app.input_gate);
		g_app.suppress_text_codepoint = 0;
		event_signal(EVENT_INPUT_FLUSH);
	}
	sdl3_screen_present(&g_app.screen, &g_app.screen_parent, screen);
	sdl3_presentation_set_layer(&g_app.presentation,
		SDL3_PRESENTATION_SEMANTIC_SCREEN,
		g_app.screen.active);
	sdl3_grid_mark_dirty(&g_app.grid);
}

static enum textui_escape_menu_action sdl3_escape_menu(void)
{
	SDL_Event event;
	enum textui_escape_menu_action result = TEXTUI_ESCAPE_CANCEL;

	if (!g_app.window || !g_app.renderer || terms_disconnecting) {
		return TEXTUI_ESCAPE_CANCEL;
	}
	sdl3_pause_menu_open(&g_app.pause_menu);
	sdl3_grid_mark_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	while (!terms_disconnecting &&
			g_app.pause_menu.action == SDL3_PAUSE_ACTION_NONE) {
		if (!SDL_WaitEvent(&event)) {
			sdl3_frontend_log_error("Could not wait for an SDL3 game-menu event");
			terms_disconnecting = 1;
			break;
		}
		sdl3_frontend_dispatch_event(&g_app, &event);
	}
	if (terms_disconnecting ||
			g_app.pause_menu.action == SDL3_PAUSE_ACTION_SAVE_QUIT) {
		result = TEXTUI_ESCAPE_SAVE_QUIT;
	} else if (g_app.pause_menu.action == SDL3_PAUSE_ACTION_ABANDON) {
		result = TEXTUI_ESCAPE_RETIRE;
	}
	g_app.pause_menu.visible = false;
	sdl3_grid_mark_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	return result;
}

static bool sdl3_load_failure_screen(const char *filename)
{
	SDL_Event event;
	const char *name = filename;

	if (!g_app.window || !g_app.renderer) return false;
	if (name && path_filename_index(name) < strlen(name)) {
		name += path_filename_index(name);
	}
	sdl3_presentation_set_phase(&g_app.presentation,
		SDL3_PRESENTATION_HOME);
	sdl3_home_show_load_error(&g_app.home, name);
	start_home_music(&g_app);
	my_strcpy(g_app.failed_save, name ? name : "",
		sizeof(g_app.failed_save));
	if (name && streq(g_app.config.last_save, name)) {
		g_app.config.last_save[0] = '\0';
		sdl3_config_save(&g_app.config);
	}
	if (name && savefile[0] && streq(savefile +
			path_filename_index(savefile), name)) {
		savefile[0] = '\0';
	}
	sdl3_grid_mark_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	while (!terms_disconnecting &&
			g_app.home.action == SDL3_HOME_ACTION_NONE) {
		if (!SDL_WaitEvent(&event)) {
			sdl3_frontend_log_error("Could not wait for an SDL3 load-error event");
			terms_disconnecting = 1;
			break;
		}
		sdl3_frontend_dispatch_event(&g_app, &event);
	}
	g_app.home.visible = false;
	sdl3_audio_stop_background();
	return !terms_disconnecting &&
		g_app.home.action == SDL3_HOME_ACTION_ACKNOWLEDGE;
}

bool sdl3_startup_menu(enum game_mode_type *mode)
{
	SDL_Event event;
	const char *filename;
	bool save_unavailable = false;
	bool save_deleted = false;
	bool delete_failed = false;
	bool reopen_load = false;
	bool selection_complete = false;

	if (!mode || !g_app.window || !g_app.renderer) return false;
	sdl3_presentation_set_phase(&g_app.presentation,
		SDL3_PRESENTATION_HOME);
	if (!g_app.home.feature_asset[0]) {
		sdl3_home_set_feature(&g_app.home,
			sdl3_home_feature_for_seed(SDL_GetTicksNS()));
	}
	while (!selection_complete) {
		if (!sdl3_home_refresh_saves(&g_app.home)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Could not enumerate the save directory");
		}
		sdl3_home_mark_save_damaged(&g_app.home, g_app.failed_save);
		sdl3_home_open(&g_app.home);
		start_home_music(&g_app);
		if (reopen_load && g_app.home.save_count > 0) {
			g_app.home.page = SDL3_HOME_LOAD;
		}
		if (save_deleted) {
			g_app.home.notice = SDL3_HOME_NOTICE_SAVE_DELETED;
		} else if (delete_failed) {
			g_app.home.notice = SDL3_HOME_NOTICE_DELETE_FAILED;
		} else if (save_unavailable) {
			g_app.home.notice = SDL3_HOME_NOTICE_SAVE_UNAVAILABLE;
		}
		save_deleted = false;
		delete_failed = false;
		reopen_load = false;
		sdl3_grid_mark_dirty(&g_app.grid);
		sdl3_frontend_render(&g_app);
		while (!terms_disconnecting &&
				g_app.home.action == SDL3_HOME_ACTION_NONE) {
			if (!SDL_WaitEvent(&event)) {
				sdl3_frontend_log_error(
					"Could not wait for an SDL3 home-screen event");
				terms_disconnecting = 1;
				break;
			}
			sdl3_frontend_dispatch_event(&g_app, &event);
		}
		if (terms_disconnecting ||
				g_app.home.action == SDL3_HOME_ACTION_QUIT) {
			g_app.home.visible = false;
			sdl3_audio_stop_background();
			return false;
		}

		switch (g_app.home.action) {
		case SDL3_HOME_ACTION_CONTINUE:
		case SDL3_HOME_ACTION_LOAD:
			filename = sdl3_home_selected_filename(&g_app.home);
			if (g_app.home.action == SDL3_HOME_ACTION_LOAD && filename &&
					g_app.home.saves[g_app.home.selected_save].damaged) {
				if (!sdl3_load_failure_screen(filename)) return false;
				continue;
			}
			if (!filename ||
					!sdl3_home_selected_save_is_live(&g_app.home)) {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"The selected SDL3 run is no longer available");
				save_unavailable = true;
				continue;
			}
			path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE,
				filename);
			*mode = GAME_LOAD;
			selection_complete = true;
			break;
		case SDL3_HOME_ACTION_DELETE:
			{
				char deleted_filename[256];
				bool deleted;

				filename = sdl3_home_selected_filename(&g_app.home);
				my_strcpy(deleted_filename, filename ? filename : "",
					sizeof(deleted_filename));
				deleted = sdl3_home_delete_selected_save(&g_app.home);
				if (deleted) {
					if (streq(g_app.config.last_save, deleted_filename)) {
						g_app.config.last_save[0] = '\0';
						sdl3_config_save(&g_app.config);
					}
					if (savefile[0] && streq(savefile +
							path_filename_index(savefile), deleted_filename)) {
						savefile[0] = '\0';
					}
					if (streq(g_app.failed_save, deleted_filename)) {
						g_app.failed_save[0] = '\0';
					}
					save_deleted = true;
				} else {
					delete_failed = true;
				}
				reopen_load = true;
				continue;
			}
		case SDL3_HOME_ACTION_MEMORIAL:
			g_app.home.visible = false;
			sdl3_audio_stop_background();
			sdl3_grid_mark_dirty(&g_app.grid);
			sdl3_frontend_render(&g_app);
			show_memorial();
			continue;
		case SDL3_HOME_ACTION_NEW:
			if (!arg_force_name) {
				savefile[0] = '\0';
				arg_name[0] = '\0';
			}
			*mode = GAME_NEW;
			selection_complete = true;
			break;
		default:
			g_app.home.visible = false;
			sdl3_audio_stop_background();
			return false;
		}
	}
	g_app.home.visible = false;
	sdl3_audio_stop_background();
	sdl3_presentation_set_phase(&g_app.presentation,
		SDL3_PRESENTATION_GAME_SETUP);
	sdl3_grid_mark_dirty(&g_app.grid);
	return true;
}

static errr term_xtra_event(int wait)
{
	SDL_Event event;

	sdl3_frontend_render(&g_app);
	if (wait) {
		while (!terms_disconnecting) {
			int result = sdl3_frontend_wait_event(&g_app, &event);

			if (result < 0) {
				sdl3_frontend_log_error("Could not wait for an SDL3 event");
				return 1;
			}
			if (!result) continue;
			if (sdl3_frontend_dispatch_event(&g_app, &event)) return 0;
		}
	} else if (SDL_PollEvent(&event)) {
		sdl3_frontend_dispatch_event(&g_app, &event);
	}

	return 0;
}

static errr term_xtra_flush(void)
{
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_EVENT_WINDOW_EXPOSED:
		case SDL_EVENT_WINDOW_RESIZED:
		case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
		case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		case SDL_EVENT_RENDER_TARGETS_RESET:
		case SDL_EVENT_RENDER_DEVICE_RESET:
		case SDL_EVENT_RENDER_DEVICE_LOST:
			sdl3_frontend_dispatch_event(&g_app, &event);
			break;
		default:
			break;
		}
	}
	return 0;
}

static errr term_xtra_hook(int action, int value)
{
	struct sdl3_pane *pane = active_pane();
	struct sdl3_grid *grid = pane_grid(pane);

	switch (action) {
	case TERM_XTRA_EVENT:
		return term_xtra_event(value);
	case TERM_XTRA_FLUSH:
		return term_xtra_flush();
	case TERM_XTRA_CLEAR:
		sdl3_grid_fill(grid, 0, 0, pane->layout->cols, pane->layout->rows,
			L' ', COLOUR_WHITE);
		return 0;
	case TERM_XTRA_SHAPE:
		if (pane->layout->term_index == SDL3_LAYOUT_MAIN_TERM) {
			g_app.cursor_visible = value != 0;
			sdl3_grid_mark_dirty(&g_app.grid);
		}
		return 0;
	case TERM_XTRA_FRESH:
		/* Present once before the next event wait or timed delay.  Angband may
		 * fresh the main and docked terms back-to-back; presenting each one
		 * separately would block on vertical sync more than once per turn. */
		return 0;
	case TERM_XTRA_DELAY:
		term_xtra_event(0);
		if (value > 0 &&
				sdl3_effects_has_projectile(&g_app.combat_effects)) {
			Uint64 end = SDL_GetTicks() + (Uint64)value;

			while (SDL_GetTicks() < end) {
				Uint64 now = SDL_GetTicks();
				Uint64 remaining = end - now;

				if (sdl3_effects_advance(&g_app.combat_effects, now) &&
						!sdl3_frontend_render_animated_overlay(&g_app)) {
					sdl3_grid_mark_dirty(&g_app.grid);
					sdl3_frontend_render(&g_app);
				}
				SDL_Delay((Uint32)MIN((Uint64)8, remaining));
			}
		} else if (value > 0) {
			SDL_Delay((Uint32)value);
		}
		return 0;
	case TERM_XTRA_REACT:
		sdl3_grid_mark_all_dirty(&g_app.grid);
		sdl3_grid_mark_all_dirty(&g_app.dock_grid);
		return 0;
	default:
		return 0;
	}
}

static errr term_curs_hook(int col, int row)
{
	struct sdl3_pane *pane = active_pane();

	if (pane->layout->term_index == SDL3_LAYOUT_MAIN_TERM) {
		g_app.cursor_col = col;
		g_app.cursor_row = row;
		sdl3_grid_mark_dirty(&g_app.grid);
	}
	return 0;
}

static errr term_wipe_hook(int col, int row, int n)
{
	struct sdl3_pane *pane = active_pane();

	sdl3_grid_wipe(pane_grid(pane), col, row, n);
	return 0;
}

static errr term_text_hook(int col, int row, int n, int attr,
		const wchar_t *text)
{
	struct sdl3_pane *pane = active_pane();

	sdl3_grid_put_text(pane_grid(pane), col, row, n, attr, text);
	return 0;
}

static bool initialize_sdl3(struct sdl3_app *app)
{
	SDL_WindowFlags window_flags =
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		sdl3_frontend_log_error("Could not initialize SDL3");
		return false;
	}
	if (!TTF_Init()) {
		sdl3_frontend_log_error("Could not initialize SDL3_ttf");
		shutdown_sdl3(app);
		return false;
	}
	app->ttf_inited = true;
#ifdef USE_HEADLESS
	if (app->offscreen) {
		app->offscreen_surface = SDL_CreateSurface(
			app->config.window_width, app->config.window_height,
			SDL_PIXELFORMAT_RGBA32);
		if (app->offscreen_surface) {
			app->renderer = SDL_CreateSoftwareRenderer(
				app->offscreen_surface);
		}
		app->fullscreen = false;
		app->window_active = true;
	} else {
#endif
		if (app->config.fullscreen) {
			window_flags |= SDL_WINDOW_FULLSCREEN;
		}
		app->fullscreen = app->config.fullscreen;
		app->window = SDL_CreateWindow(VERSION_TRADEMARK_NAME,
			app->config.window_width, app->config.window_height,
			window_flags);
		if (!app->window) {
			sdl3_frontend_log_error("Could not create the SDL3 window");
			shutdown_sdl3(app);
			return false;
		}
		app->window_active = true;
		if (!SDL_SetWindowMinimumSize(app->window, SDL3_MIN_WINDOW_WIDTH,
				SDL3_MIN_WINDOW_HEIGHT)) {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Could not set the SDL3 minimum window size: %s",
				SDL_GetError());
		}
		app->renderer = SDL_CreateRenderer(app->window, NULL);
#ifdef USE_HEADLESS
	}
#endif
	if (!app->renderer) {
#ifdef USE_HEADLESS
		sdl3_frontend_log_error(app->offscreen ?
			"Could not create the off-screen SDL3 renderer" :
			"Could not create the SDL3 renderer");
#else
		sdl3_frontend_log_error("Could not create the SDL3 renderer");
#endif
		shutdown_sdl3(app);
		return false;
	}
	if (!sdl3_font_prepare_renderer(app->renderer)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not select crisp SDL3 glyph-atlas sampling: %s",
			SDL_GetError());
	}
#ifdef USE_HEADLESS
	if (!app->offscreen && !SDL_SetRenderVSync(app->renderer, 1)) {
#else
	if (!SDL_SetRenderVSync(app->renderer, 1)) {
#endif
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not enable SDL3 vertical sync: %s", SDL_GetError());
	}
#ifdef USE_HEADLESS
	if (!app->offscreen && !SDL_StartTextInput(app->window)) {
#else
	if (!SDL_StartTextInput(app->window)) {
#endif
		sdl3_frontend_log_error("Could not start SDL3 text input");
		shutdown_sdl3(app);
		return false;
	}
	if (!sdl3_grid_init(&app->grid, app->layout.cols, app->layout.rows)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not allocate the SDL3 cell grid");
		shutdown_sdl3(app);
		return false;
	}
	if (!sdl3_grid_init(&app->dock_grid,
			app->layout.panes[SDL3_LAYOUT_MESSAGE_TERM].cols,
			app->layout.panes[SDL3_LAYOUT_MESSAGE_TERM].rows)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not allocate the SDL3 floating-panel grid");
		shutdown_sdl3(app);
		return false;
	}
	app->presenter = sdl3_presenter_create(app->renderer,
			app->config.font[0] ? app->config.font : NULL,
			app->config.map_font[0] ? app->config.map_font : NULL,
			app->layout.cols, app->layout.rows,
			app->config.map_zoom_percent);
	if (!app->presenter) {
		sdl3_frontend_log_error("Could not initialize the SDL3 character compositor");
		shutdown_sdl3(app);
		return false;
	}

	return true;
}

static void remember_font_selection(char *requested, size_t capacity,
		const char *actual)
{
	const char *actual_name;
	const char *requested_name;

	if (!requested || !capacity || !actual || !actual[0]) return;
	actual_name = sdl3_font_filename(actual);
	requested_name = sdl3_font_filename(requested);
	if (!requested[0] ||
			(!streq(requested, actual) &&
			!streq(requested_name, actual_name))) {
		my_strcpy(requested, actual_name, capacity);
	}
}

void sdl3_frontend_remember_selected_fonts(struct sdl3_app *app)
{
	struct sdl3_presenter_info info;

	if (!app) return;
	sdl3_presenter_get_info(app->presenter, &info);
	if (info.interface_font_loaded) {
		remember_font_selection(app->config.font,
			sizeof(app->config.font), info.interface_font_path);
	}
	remember_font_selection(app->config.map_font,
		sizeof(app->config.map_font), info.map_font_path);
}

static void link_terms(struct sdl3_app *app)
{
	int i;

	for (i = 0; i < SDL3_LAYOUT_PANE_COUNT; i++) {
		struct sdl3_pane *pane = &app->panes[i];
		int term_index = app->layout.panes[i].term_index;

		pane->app = app;
		pane->layout = &app->layout.panes[i];
		term_init(&pane->term, pane->layout->cols, pane->layout->rows,
			term_index == SDL3_LAYOUT_MAIN_TERM ? SDL3_KEY_QUEUE_SIZE :
			SDL3_SUBWINDOW_KEY_QUEUE_SIZE);
		pane->term.init_hook = term_init_sdl3;
		pane->term.nuke_hook = term_nuke_sdl3;
		pane->term.xtra_hook = term_xtra_hook;
		pane->term.curs_hook = term_curs_hook;
		pane->term.wipe_hook = term_wipe_hook;
		pane->term.text_hook = term_text_hook;
		/* SDL3 draws the cursor during composition, so request explicit
		 * coordinate and visibility hooks from the term core. */
		pane->term.soft_cursor = false;
		pane->term.complex_input = true;
		pane->term.full_screen_item_selector =
			term_index == SDL3_LAYOUT_MAIN_TERM;
		pane->term.screen_hook = term_index == SDL3_LAYOUT_MAIN_TERM ?
			term_screen_hook : NULL;
		pane->term.never_frosh = true;
		pane->term.data = pane;
		pane->term_inited = true;
		angband_term[term_index] = &pane->term;
	}
	Term_activate(&app->panes[SDL3_LAYOUT_MAIN_TERM].term);
}

void reinit_sdl3(void)
{
	/* New Run deliberately clears savefile before birth.  If birth is then
	 * cancelled, restore the last live run as Home's Continue candidate after
	 * the core has rebuilt its executable-relative directories. */
	if (!savefile[0] && g_app.config.last_save[0]) {
		path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE,
			g_app.config.last_save);
	}
	sdl3_frontend_reinitialize(&g_app);
}

errr init_sdl3(int argc, char **argv)
{
	char requested_theme[SDL3_CONFIG_THEME_CAPACITY];
	bool print_details = false;
	bool smoke_test = false;
	bool memory_test = false;
	int main_cols;
	int main_rows;
	int i;

	if (!init_register_file_parser("home feature",
			&home_feature_parser)) {
		return 1;
	}

#ifdef USE_HEADLESS
	bool offscreen = g_offscreen_requested;

	g_offscreen_requested = false;
#endif
	memset(&g_app, 0, sizeof(g_app));
#ifdef USE_HEADLESS
	g_app.offscreen = offscreen;
#endif
	sdl3_presentation_init(&g_app.presentation);
	create_needed_dirs();
	sdl3_config_init(&g_app.config);
#ifdef USE_HEADLESS
	if (!g_app.offscreen) sdl3_config_load(&g_app.config);
#else
	sdl3_config_load(&g_app.config);
#endif
	if (!arg_name[0]) {
		if (g_app.config.last_save[0]) {
			path_build(savefile, sizeof(savefile), ANGBAND_DIR_SAVE,
				g_app.config.last_save);
		} else {
			savefile[0] = '\0';
		}
	}
	sdl3_weather_reset(&g_app.weather, g_app.config.weather_mode, 1U);
	sdl3_audio_set_settings(&g_app.config.audio);
	/* The saved dock preference becomes active once the game has a live cave.
	 * Keep the title and character-creation screens at the normal main-term
	 * size. */
	sdl3_interface_density_dimensions(g_app.config.interface_density,
		&main_cols, &main_rows);
	sdl3_layout_configure_sized(&g_app.layout, false,
		g_app.config.dock_placement, g_app.config.dock_rows,
		g_app.config.dock_cols, main_cols, main_rows);
	sdl3_home_init(&g_app.home);
	sdl3_pause_menu_init(&g_app.pause_menu);
	sdl3_settings_init(&g_app.settings);
	textui_escape_menu_hook = sdl3_escape_menu;
	textui_startup_menu_hook = sdl3_startup_menu;
	textui_load_failure_hook = sdl3_load_failure_screen;
	textui_store_usable_width_hook = sdl3_store_usable_width;
	textui_fishing_overlay_hook = sdl3_owns_fishing_overlay;
	textui_fishing_overlay_changed_hook = sdl3_fishing_overlay_changed;
	for (i = 1; i < argc; i++) {
		if (streq(argv[i], "-v")) {
			print_details = true;
		} else if (streq(argv[i], "-T")) {
			smoke_test = true;
		} else if (streq(argv[i], "-M")) {
			memory_test = true;
		} else if (streq(argv[i], "-F")) {
			g_app.config.fullscreen = true;
		} else if (streq(argv[i], "-W")) {
			g_app.config.fullscreen = false;
		} else if (streq(argv[i], "-f") && i + 1 < argc) {
			my_strcpy(g_app.config.font, argv[++i],
				sizeof(g_app.config.font));
		} else if (argv[i][0] == '-' && argv[i][1] == 'f' &&
				argv[i][2]) {
			my_strcpy(g_app.config.font, argv[i] + 2,
				sizeof(g_app.config.font));
		} else if (streq(argv[i], "-m") && i + 1 < argc) {
			my_strcpy(g_app.config.map_font, argv[++i],
				sizeof(g_app.config.map_font));
		} else if (argv[i][0] == '-' && argv[i][1] == 'm' &&
				argv[i][2]) {
			my_strcpy(g_app.config.map_font, argv[i] + 2,
				sizeof(g_app.config.map_font));
		} else if (streq(argv[i], "-t") && i + 1 < argc) {
			my_strcpy(g_app.config.theme, argv[++i],
				sizeof(g_app.config.theme));
		} else if (argv[i][0] == '-' && argv[i][1] == 't' &&
				argv[i][2]) {
			my_strcpy(g_app.config.theme, argv[i] + 2,
				sizeof(g_app.config.theme));
		} else if (streq(argv[i], "-a") && i + 1 < argc) {
			enum sdl3_terrain_style style;

			if (sdl3_terrain_style_from_name(argv[++i], &style)) {
				g_app.config.terrain_style = style;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 map style: %s", argv[i]);
			}
		} else if (argv[i][0] == '-' && argv[i][1] == 'a' &&
				argv[i][2]) {
			enum sdl3_terrain_style style;

			if (sdl3_terrain_style_from_name(argv[i] + 2, &style)) {
				g_app.config.terrain_style = style;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 map style: %s", argv[i] + 2);
			}
		} else if (streq(argv[i], "-g") && i + 1 < argc) {
			const char *presentation = argv[++i];

			if (my_stricmp(presentation, "hybrid-32") == 0) {
				g_app.config.tile_mode = true;
				g_app.config.hybrid_tile_size = 32;
			} else if (my_stricmp(presentation, "tiles") == 0 ||
					my_stricmp(presentation, "hybrid") == 0 ||
					my_stricmp(presentation, "hybrid-64") == 0) {
				g_app.config.tile_mode = true;
				g_app.config.hybrid_tile_size = 64;
			} else if (my_stricmp(presentation, "ascii") == 0) {
				g_app.config.tile_mode = false;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 map presentation: %s", presentation);
			}
#ifdef USE_HEADLESS
		} else if (argv[i][0] == '-' && argv[i][1] == 'q' &&
				argv[i][2]) {
			int zoom = atoi(argv[i] + 2);

			if (zoom >= SDL3_ZOOM_MIN && zoom <= SDL3_ZOOM_MAX) {
				g_app.config.map_zoom_percent = zoom;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 map zoom: %s", argv[i] + 2);
			}
		} else if (argv[i][0] == '-' && argv[i][1] == 'z' &&
				argv[i][2]) {
			int width;
			int height;

			if (parse_output_size(argv[i] + 2, &width, &height)) {
				g_app.config.window_width = width;
				g_app.config.window_height = height;
			} else {
				SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
					"Ignoring invalid SDL3 output size: %s", argv[i] + 2);
			}
#endif
		} else {
			SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
				"Ignoring SDL3 option: %s", argv[i]);
		}
	}
	my_strcpy(requested_theme, g_app.config.theme, sizeof(requested_theme));
	g_app.theme = sdl3_theme_by_id(requested_theme);
	if (requested_theme[0] &&
			my_stricmp(requested_theme, g_app.theme->id) != 0) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Unknown SDL3 theme '%s'; using %s", requested_theme,
			g_app.theme->id);
	}
	my_strcpy(g_app.config.theme, g_app.theme->id,
		sizeof(g_app.config.theme));
	g_app.verbose = print_details;

	if (!initialize_sdl3(&g_app)) return 1;
	sdl3_frontend_remember_selected_fonts(&g_app);
	link_terms(&g_app);
	sdl3_frontend_register_game_handlers(&g_app);
	sdl3_frontend_render(&g_app);

	if (print_details) {
		struct sdl3_presenter_info info;

		sdl3_presenter_get_info(g_app.presenter, &info);
		SDL_Log("%s; storage identity %s", buildid, storage_buildid);
		SDL_Log("SDL runtime: %d.%d.%d", SDL_GetVersion() / 1000000,
			(SDL_GetVersion() / 1000) % 1000, SDL_GetVersion() % 1000);
		SDL_Log("Video driver: %s", SDL_GetCurrentVideoDriver());
		SDL_Log("Settings: %s", g_app.config.file_path);
		SDL_Log("Window: %d x %d; %s", g_app.config.window_width,
			g_app.config.window_height,
			g_app.fullscreen ? "fullscreen" : "windowed");
		SDL_Log("Interface theme: %s (%s)", g_app.theme->name,
			g_app.theme->id);
		SDL_Log("Map zoom: x%d.%02d", g_app.config.map_zoom_percent / 100,
			g_app.config.map_zoom_percent % 100);
		SDL_Log("Interface size: %s",
			sdl3_interface_density_name(g_app.config.interface_density));
		SDL_Log("Map style: %s",
			sdl3_terrain_style_name(g_app.config.terrain_style));
		SDL_Log("Map presentation: %s",
			g_app.config.tile_mode ? "Hybrid" : "ASCII");
		SDL_Log("Hybrid detail: %d x %d", g_app.config.hybrid_tile_size,
			g_app.config.hybrid_tile_size);
		SDL_Log("Town weather: %s at %u Hz",
			sdl3_weather_mode_name(g_app.config.weather_mode),
			1000U / SDL3_WEATHER_TICK_MS);
		if (info.interface_font_loaded) {
			SDL_Log("Interface font: %s", info.interface_font_path);
			SDL_Log("Interface font size: %d; cell: %d x %d",
				info.interface_font_point_size, info.cell_width,
				info.cell_height);
		}
		if (info.map_font_path[0]) {
			SDL_Log("Map font: %s", info.map_font_path);
			SDL_Log("Map font size: %d", info.map_font_point_size);
		}
		SDL_Log("Grid cache: %s; initial cell updates: %d",
			info.grid_cache_active ? "render target" : "full redraw",
			info.last_cell_updates);
		SDL_Log("Layout: %d x %d full playfield; %d active term%s",
			g_app.layout.cols, g_app.layout.rows,
			g_app.dock_active ? SDL3_LAYOUT_PANE_COUNT : 1,
			g_app.dock_active ? "s" : "");
		SDL_Log("Render output: %d x %d physical pixels; grid: %d x %d "
			"pixels at %d,%d",
			info.output_width, info.output_height,
			info.cell_width * g_app.layout.cols,
			info.cell_height * g_app.layout.rows,
			info.origin_x, info.origin_y);
	}

	if (memory_test) {
		if (!run_font_memory_stress(&g_app)) {
			quit("SDL3 font-memory stress test failed");
		}
		quit(NULL);
	}

	if (smoke_test) {
		SDL_Event event;
		bool original_dock_visible = g_app.config.dock_visible;
		enum sdl3_dock_placement original_dock_placement =
			g_app.config.dock_placement;
		enum sdl3_dock_placement alternate_dock_placement =
			original_dock_placement == SDL3_DOCK_TOP ?
			SDL3_DOCK_BOTTOM : SDL3_DOCK_TOP;
		int original_dock_rows = g_app.config.dock_rows;
		int original_dock_cols = g_app.config.dock_cols;

		sdl3_presentation_set_phase(&g_app.presentation,
			SDL3_PRESENTATION_WORLD);
		if (!sdl3_frontend_apply_dock_layout(&g_app, true, alternate_dock_placement,
				original_dock_rows, original_dock_cols)) {
			quit("SDL3 alternate dock smoke test failed");
		}
		sdl3_frontend_render(&g_app);
		if (!sdl3_frontend_apply_dock_layout(&g_app, original_dock_visible,
				original_dock_placement, original_dock_rows,
				original_dock_cols)) {
			quit("SDL3 dock restoration smoke test failed");
		}
		sdl3_frontend_render(&g_app);
		if (!sdl3_legal_manifest_valid()) {
			quit("SDL3 legal manifest smoke test failed");
		}
		sdl3_home_open(&g_app.home);
		g_app.home.page = SDL3_HOME_LEGAL_PAGE;
		sdl3_frontend_render(&g_app);
		g_app.home.visible = false;
		sdl3_frontend_render(&g_app);

		SDL_zero(event);
		event.type = SDL_EVENT_RENDER_TARGETS_RESET;
		sdl3_frontend_dispatch_event(&g_app, &event);
		event.type = SDL_EVENT_RENDER_DEVICE_RESET;
		sdl3_frontend_dispatch_event(&g_app, &event);
		if (terms_disconnecting) {
			quit("SDL3 renderer recovery smoke test failed");
		}
		quit(NULL);
	}
	return 0;
}

#ifdef USE_HEADLESS
errr init_sdl3_offscreen(int argc, char **argv)
{
	g_offscreen_requested = true;
	return init_sdl3(argc, argv);
}

bool sdl3_capture_bmp(const char *path)
{
	if (!path || !path[0] || !g_app.offscreen ||
			!g_app.offscreen_surface) {
		return false;
	}
	sdl3_frontend_render(&g_app);
	return SDL_SaveBMP(g_app.offscreen_surface, path);
}

bool sdl3_capture_sequence_begin(const char *directory, unsigned int fps)
{
	if (!directory || !directory[0] || !fps || g_capture_sequence.active ||
			!g_app.offscreen || !g_app.offscreen_surface ||
			!dir_exists(directory)) {
		return false;
	}
	memset(&g_capture_sequence, 0, sizeof(g_capture_sequence));
	my_strcpy(g_capture_sequence.directory, directory,
		sizeof(g_capture_sequence.directory));
	g_capture_sequence.active = true;
	g_capture_sequence.fps = fps;
	g_capture_sequence.next_frame_ms = SDL_GetTicks();
	return true;
}

bool sdl3_capture_sequence_hold(unsigned int duration_ms)
{
	unsigned int frame_target;

	if (!g_capture_sequence.active || g_capture_sequence.failed) return false;
	frame_target = g_capture_sequence.frame_count +
		(unsigned int)(((Uint64)duration_ms * g_capture_sequence.fps +
		999U) / 1000U);
	while (!g_capture_sequence.failed &&
			g_capture_sequence.frame_count < frame_target) {
		Uint64 now = SDL_GetTicks();

		if (now < g_capture_sequence.next_frame_ms) {
			SDL_Delay((Uint32)MIN((Uint64)8,
				g_capture_sequence.next_frame_ms - now));
			continue;
		}
		sdl3_effects_advance(&g_app.combat_effects, now);
		sdl3_frontend_render(&g_app);
	}
	return !g_capture_sequence.failed;
}

bool sdl3_capture_sequence_end(unsigned int *frame_count)
{
	bool okay;

	if (!g_capture_sequence.active) return false;
	if (frame_count) *frame_count = g_capture_sequence.frame_count;
	okay = !g_capture_sequence.failed && g_capture_sequence.frame_count > 0;
	memset(&g_capture_sequence, 0, sizeof(g_capture_sequence));
	return okay;
}

void sdl3_capture_clear_inspection(void)
{
	g_capture_inspection = false;
}

bool sdl3_capture_inspect_closest_monster(void)
{
	struct monster *closest = NULL;
	int closest_distance = INT_MAX;
	int i;

	if (!player || !player->upkeep || !cave ||
			world_spelunk_player_is_active(player)) {
		return false;
	}
	player->upkeep->update |= PU_UPDATE_VIEW | PU_MONSTERS;
	handle_stuff(player);
	for (i = 1; i < cave_monster_max(cave); i++) {
		struct monster *mon = cave_monster(cave, i);
		int candidate_distance;

		if (!mon || !mon->race || mon->hp < 0 ||
				!monster_is_obvious(mon)) {
			continue;
		}
		candidate_distance = distance(player->grid, mon->grid);
		if (!closest || candidate_distance < closest_distance) {
			closest = mon;
			closest_distance = candidate_distance;
		}
	}
	if (!closest) return false;
	g_capture_inspection = true;
	Term_activate(angband_term[0]);
	monster_race_track(player->upkeep, closest->race);
	health_track(player->upkeep, closest);
	/* The capture path has no subsequent input-loop redraw.  Include the
	 * full sidebar alongside the newly tracked monster before taking a frame. */
	player->upkeep->redraw |= PR_BASIC | PR_EXTRA;
	handle_stuff(player);
	(void)Term_set_cursor(true);
	move_cursor_relative(closest->grid.y, closest->grid.x);
	Term_fresh();
	sdl3_frontend_render(&g_app);
	return g_app.map_presenter.inspect_card.active;
}

/* Render the production Home layout without opening an interactive window. */
bool sdl3_capture_home(void)
{
	if (!g_app.offscreen) return false;
	sdl3_home_open(&g_app.home);
	sdl3_grid_mark_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	return g_app.home.page == SDL3_HOME_ROOT;
}

/* Enter through the real Home menu so legal captures use its ordinary state. */
bool sdl3_capture_legal(void)
{
	if (!g_app.offscreen || !sdl3_legal_manifest_valid()) return false;
	sdl3_home_open(&g_app.home);
	g_app.home.selected_row = SDL3_HOME_ABOUT;
	sdl3_home_activate(&g_app.home);
	g_app.home.selected_about_row = SDL3_HOME_ABOUT_LEGAL;
	sdl3_home_activate(&g_app.home);
	sdl3_grid_mark_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	return g_app.home.page == SDL3_HOME_LEGAL_PAGE;
}

bool sdl3_capture_message_position(const char *position)
{
	enum sdl3_dock_placement placement = SDL3_DOCK_BOTTOM;
	if (!g_app.offscreen || !position) return false;
	do {
		if (my_stricmp(position, sdl3_dock_placement_name(placement)) == 0) {
			sdl3_frontend_set_dock_view(&g_app, PW_MESSAGE);
			return sdl3_frontend_apply_dock_layout(&g_app, true, placement,
				g_app.config.dock_rows, g_app.config.dock_cols);
		}
		placement = sdl3_dock_placement_change(placement, 1);
	} while (placement != SDL3_DOCK_BOTTOM);
	return false;
}

/* Exercise the normal settings handlers without an interactive window. */
bool sdl3_capture_settings(const char *action)
{
	if (!g_app.offscreen || !action) return false;
	if (streq(action, "open") || streq(action, "interface") ||
			streq(action, "sound") || streq(action, "display") ||
			streq(action, "hub") || streq(action, "controls")) {
		sdl3_settings_open(&g_app.settings);
		g_app.settings.selected_hub_row = streq(action, "interface") ?
			SDL3_SETTINGS_HUB_INTERFACE : streq(action, "sound") ?
			SDL3_SETTINGS_HUB_SOUND : streq(action, "display") ?
			SDL3_SETTINGS_HUB_DISPLAY : streq(action, "controls") ?
			SDL3_SETTINGS_HUB_CONTROLS : SDL3_SETTINGS_HUB_APPEARANCE;
		if (!streq(action, "hub"))
			(void)sdl3_settings_activate(&g_app.settings);
	} else if (streq(action, "close")) {
		g_app.settings.visible = false;
	} else if (streq(action, "explore") || streq(action, "stairs")) {
		g_app.settings.selected_row = streq(action, "explore") ?
			SDL3_SETTINGS_EXPLORE : SDL3_SETTINGS_AUTO_STAIRS;
		sdl3_frontend_change_selected_setting(&g_app, 1);
	} else if (streq(action, "map") || streq(action, "detail") ||
			streq(action, "warnings")) {
		g_app.settings.selected_row = streq(action, "warnings") ?
			SDL3_SETTINGS_EDGE_WARNINGS : streq(action, "map") ?
			SDL3_SETTINGS_TILE_MODE : SDL3_SETTINGS_HYBRID_DETAIL;
		sdl3_frontend_change_selected_setting(&g_app, 1);
	} else if (streq(action, "density") || streq(action, "font") ||
			streq(action, "stats") || streq(action, "history")) {
		g_app.settings.selected_row = streq(action, "density") ?
			SDL3_SETTINGS_INTERFACE_DENSITY : streq(action, "font") ?
			SDL3_SETTINGS_INTERFACE_FONT : streq(action, "history") ?
			SDL3_SETTINGS_DOCK_SIZE : SDL3_SETTINGS_HUD_STATS;
		sdl3_frontend_change_selected_setting(&g_app, 1);
	} else if (streq(action, "reset")) {
		SDL_Event event;
		SDL_zero(event);
		event.type = SDL_EVENT_RENDER_DEVICE_RESET;
		sdl3_frontend_dispatch_event(&g_app, &event);
		if (terms_disconnecting) return false;
	} else {
		return false;
	}
	sdl3_grid_mark_all_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	return true;
}

bool sdl3_capture_introduction(const char *scene_id)
{
	const struct world_story_scene *scene;
	if (!g_app.offscreen) return false;
	if (!scene_id) {
		g_app.home.visible = false;
		return true;
	}
	scene = world_story_scene_by_id(scene_id);
	if (!scene || !scene->intro_order) return false;
	sdl3_home_open(&g_app.home);
	g_app.home.selected_row = SDL3_HOME_WORLD;
	sdl3_home_activate(&g_app.home);
	g_app.home.introduction.tab = scene->intro_order - 1;
	sdl3_grid_mark_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	return true;
}

/* Select the class-title guide without depending on a particular scene ID. */
bool sdl3_capture_class_titles(void)
{
	if (!g_app.offscreen || !classes) return false;
	for (int tab = 0; tab < world_story_intro_count(); tab++) {
		const struct world_story_scene *scene = world_story_intro_at(tab);
		if (scene->intro_content != WORLD_STORY_INTRO_CLASSES) continue;
		if (!sdl3_capture_introduction(scene->id)) return false;
		sdl3_grid_mark_dirty(&g_app.grid);
		sdl3_frontend_render(&g_app);
		return true;
	}
	return false;
}

/* Read-only shop capture: use generated stock and the production presenter,
 * without opening the interactive purchasing loop or moving the player. */
bool sdl3_capture_store(const char *feature_code)
{
	struct store *store = NULL;
	struct object **stock;
	struct menu menu = { 0 };
	int feature, i;

	if (!g_app.offscreen || !Term || !Term->screen_hook) return false;
	sdl3_store_art_clear(&g_app.store_art);
	if (!feature_code) return true;
	feature = lookup_feat_code(feature_code);
	if (feature < 0 || !stores || !z_info || !player) return false;
	for (i = 0; i < z_info->store_max; i++) {
		if (stores[i].feat == feature) store = &stores[i];
	}
	if (!store || store->relic_broker) return false;
	stock = mem_zalloc(sizeof(*stock) * z_info->store_inven_max);
	store_stock_list(store, stock, z_info->store_inven_max);
	menu.count = store->stock_num;
	if (store->feat != FEAT_HOME && store->owner) {
		char key[sizeof(g_app.store_art.asset_key)];
		sdl3_monster_art_make_key(key, sizeof(key),
			store->owner->art_id, store->art_id);
		sdl3_store_art_show(&g_app.store_art, key);
	}
	ui_store_screen_present(&menu, store, stock, false,
		sdl3_store_usable_width(Term->wid), NULL);
	mem_free(stock);
	sdl3_grid_mark_all_dirty(&g_app.grid);
	sdl3_frontend_render(&g_app);
	return true;
}
#endif

#endif /* USE_SDL3 */
