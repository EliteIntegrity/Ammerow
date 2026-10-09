/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/frontend-settings.c
 * \brief Settings actions for the SDL3 frontend.
 *
 *
 */

#include "sdl3/frontend-internal.h"

#include "snd-sdl3.h"
#include "sdl3/audio.h"
#include "sdl3/font.h"
#include "sdl3/settings-model.h"
#include "sdl3/terrain.h"
#include "sdl3/theme.h"
#include "sdl3/weather.h"
#include "sdl3/zoom.h"

bool sdl3_frontend_toggle_fullscreen(struct sdl3_app *app)
{
	bool fullscreen;

	if (!app) return false;
	fullscreen = !app->fullscreen;
	if (fullscreen) sdl3_frontend_capture_windowed_size(app);
	if (!SDL_SetWindowFullscreen(app->window, fullscreen)) {
		sdl3_frontend_log_error("Could not change the SDL3 fullscreen state");
		return false;
	}
	app->fullscreen = fullscreen;
	app->config.fullscreen = fullscreen;
	sdl3_grid_mark_dirty(&app->grid);
	return true;
}

void sdl3_frontend_set_settings_visible(struct sdl3_app *app, bool visible)
{
	bool was_visible;
	struct sdl3_presenter_info info;

	if (!app) return;
	was_visible = app->settings.visible;
	if (was_visible == visible) return;
	if (visible) {
		sdl3_settings_open(&app->settings);
	} else {
		app->settings.visible = false;
	}
	sdl3_presenter_get_info(app->presenter, &info);
	if (was_visible && !visible && app->font_cache_needs_compaction &&
			info.interface_font_loaded) {
		char font_path[SDL3_FONT_PATH_CAPACITY];

		my_strcpy(font_path, info.interface_font_path, sizeof(font_path));
		if (sdl3_presenter_set_font(app->presenter, app->renderer,
				font_path)) {
			app->font_cache_needs_compaction = false;
			sdl3_grid_mark_all_dirty(&app->grid);
		}
	}
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
	sound(visible ? MSG_UI_ACCEPT : MSG_UI_CHANGE);
	if (was_visible && !visible) {
		sdl3_frontend_capture_windowed_size(app);
		sdl3_config_save(&app->config);
	}
}

static void change_theme(struct sdl3_app *app, int delta)
{
	int count = sdl3_theme_count();
	int index = sdl3_theme_index(app->theme) + delta;

	while (index < 0) index += count;
	while (index >= count) index -= count;
	app->theme = sdl3_theme_by_index(index);
	my_strcpy(app->config.theme, app->theme->id,
		sizeof(app->config.theme));
	sdl3_presenter_invalidate_grid_cache(app->presenter);
	sdl3_grid_mark_all_dirty(&app->grid);
}

static const char *changed_bundled_font(const char *current, int delta)
{
	int count = sdl3_font_bundled_count();
	int index = sdl3_font_bundled_index(current);

	if (!count) return NULL;
	if (index < 0) index = delta > 0 ? 0 : count - 1;
	else {
		index += delta;
		while (index < 0) index += count;
		while (index >= count) index -= count;
	}
	return sdl3_font_bundled_name(index);
}

static void change_interface_font(struct sdl3_app *app, int delta)
{
	struct sdl3_presenter_info info;
	const char *name;

	sdl3_presenter_get_info(app->presenter, &info);
	name = changed_bundled_font(info.interface_font_path, delta);
	if (name && sdl3_presenter_set_font(app->presenter, app->renderer,
			name)) {
		sdl3_frontend_remember_selected_fonts(app);
		app->font_cache_needs_compaction = false;
		sdl3_grid_mark_dirty(&app->grid);
	}
}

static void change_map_font(struct sdl3_app *app, int delta)
{
	struct sdl3_presenter_info info;
	const char *name;

	sdl3_presenter_get_info(app->presenter, &info);
	name = changed_bundled_font(info.map_font_path, delta);
	if (name && sdl3_presenter_set_map_font(app->presenter, app->renderer,
			name)) {
		sdl3_frontend_remember_selected_fonts(app);
		app->font_cache_needs_compaction = false;
		sdl3_grid_mark_all_dirty(&app->grid);
	}
}

bool sdl3_frontend_change_zoom(struct sdl3_app *app, int direction)
{
	int zoom;

	if (!app) return false;
	zoom = direction == 2 ? SDL3_ZOOM_DEFAULT :
		sdl3_zoom_change(app->config.map_zoom_percent, direction);
	if (zoom == app->config.map_zoom_percent) return false;
	if (!sdl3_presenter_set_map_zoom(app->presenter, app->renderer, zoom)) {
		SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
			"Could not change SDL3 map zoom to %d%%", zoom);
		return false;
	}
	app->config.map_zoom_percent = zoom;
	app->font_cache_needs_compaction = true;
	sdl3_grid_mark_all_dirty(&app->grid);
	if (app->verbose) {
		struct sdl3_presenter_info info;

		sdl3_presenter_get_info(app->presenter, &info);
		SDL_Log("Map zoom changed to x%d.%02d; map font size %d",
			zoom / 100, zoom % 100, info.map_font_point_size);
	}
	return true;
}

static void change_interface_density(struct sdl3_app *app, int delta)
{
	enum sdl3_interface_density previous = app->config.interface_density;
	enum sdl3_interface_density changed =
		sdl3_interface_density_change(previous, delta);

	if (changed == previous) return;
	app->config.interface_density = changed;
	if (!sdl3_frontend_apply_dock_layout(app, app->config.dock_visible,
			app->config.dock_placement, app->config.dock_rows,
			app->config.dock_cols)) {
		app->config.interface_density = previous;
		return;
	}
	sdl3_map_presenter_invalidate_layout(&app->map_presenter);
	if (app->verbose) {
		SDL_Log("Interface size changed to %s",
			sdl3_interface_density_name(changed));
	}
}

static void change_dock_placement(struct sdl3_app *app, int delta)
{
	enum sdl3_dock_placement placement = sdl3_dock_placement_change(
		app->config.dock_placement, delta);
	sdl3_frontend_set_dock_view(app, PW_MESSAGE);
	sdl3_frontend_apply_dock_layout(app, app->config.dock_visible, placement,
		app->config.dock_rows, app->config.dock_cols);
}

static void change_dock_size(struct sdl3_app *app, int delta)
{
	int rows = app->config.dock_rows + delta;

	rows = MAX(SDL3_LAYOUT_MIN_DOCK_ROWS,
		MIN(SDL3_LAYOUT_MAX_DOCK_ROWS, rows));
	if (rows != app->config.dock_rows) {
		sdl3_frontend_apply_dock_layout(app, app->config.dock_visible,
			app->config.dock_placement, rows, app->config.dock_cols);
	}
}

void sdl3_frontend_change_stats_visibility(struct sdl3_app *app)
{
	if (!app) return;
	app->config.hud_stats_visible = !app->config.hud_stats_visible;
	sdl3_grid_mark_dirty(&app->grid);
}

static void change_audio_volume(struct sdl3_app *app, int *volume, int delta)
{
	int changed = sdl3_audio_change_volume(*volume, delta);

	if (changed == *volume) return;
	*volume = changed;
	sdl3_audio_set_settings(&app->config.audio);
}

void sdl3_frontend_change_selected_setting(struct sdl3_app *app, int delta)
{
	switch (app->settings.selected_row) {
	case SDL3_SETTINGS_THEME:
		change_theme(app, delta);
		break;
	case SDL3_SETTINGS_INTERFACE_FONT:
		change_interface_font(app, delta);
		break;
	case SDL3_SETTINGS_INTERFACE_DENSITY:
		change_interface_density(app, delta);
		break;
	case SDL3_SETTINGS_BIG_STAT_CARDS:
		app->config.big_stat_cards = !app->config.big_stat_cards;
		break;
	case SDL3_SETTINGS_MAP_FONT:
		change_map_font(app, delta);
		break;
	case SDL3_SETTINGS_TILE_MODE:
		app->config.tile_mode = !app->config.tile_mode;
		sdl3_map_presenter_invalidate_terrain(&app->map_presenter);
		sdl3_presenter_invalidate_grid_cache(app->presenter);
		sdl3_grid_mark_all_dirty(&app->grid);
		break;
	case SDL3_SETTINGS_HYBRID_DETAIL:
		app->config.hybrid_tile_size = app->config.hybrid_tile_size == 64 ? 32 : 64;
		sdl3_presenter_invalidate_grid_cache(app->presenter);
		sdl3_grid_mark_all_dirty(&app->grid);
		break;
	case SDL3_SETTINGS_ZOOM:
		sdl3_frontend_change_zoom(app, delta);
		break;
	case SDL3_SETTINGS_ANIMATED_COMBAT:
		app->config.animated_combat = !app->config.animated_combat;
		if (!app->config.animated_combat) {
			sdl3_effects_clear(&app->combat_effects);
			app->missile_previous_valid = false;
		}
		break;
	case SDL3_SETTINGS_TERRAIN:
		app->config.terrain_style = sdl3_terrain_style_change(
			app->config.terrain_style, delta);
		sdl3_map_presenter_invalidate_terrain(&app->map_presenter);
		sdl3_grid_mark_all_dirty(&app->grid);
		break;
	case SDL3_SETTINGS_WEATHER:
		app->config.weather_mode = app->config.weather_mode == SDL3_WEATHER_OFF ?
			SDL3_WEATHER_AUTO : SDL3_WEATHER_OFF;
		app->weather_next_tick_ms = 0;
		break;
	case SDL3_SETTINGS_FULLSCREEN:
		sdl3_frontend_toggle_fullscreen(app);
		break;
	case SDL3_SETTINGS_DOCK_VISIBLE:
		sdl3_frontend_apply_dock_layout(app, !app->config.dock_visible,
			app->config.dock_placement, app->config.dock_rows,
			app->config.dock_cols);
		break;
	case SDL3_SETTINGS_DOCK_PLACEMENT:
		change_dock_placement(app, delta);
		break;
	case SDL3_SETTINGS_DOCK_SIZE:
		change_dock_size(app, delta);
		break;
	case SDL3_SETTINGS_HUD_STATS:
		sdl3_frontend_change_stats_visibility(app);
		break;
	case SDL3_SETTINGS_AUDIO_ENABLED:
		app->config.audio.enabled = !app->config.audio.enabled;
		if (player) {
			player->opts.opt[OPT_use_sound] = app->config.audio.enabled;
			app->audio_player = player;
			app->audio_option_value = app->config.audio.enabled;
		}
		sdl3_audio_set_settings(&app->config.audio);
		break;
	case SDL3_SETTINGS_AUDIO_MUSIC_ENABLED:
		app->config.audio.music_enabled =
			!app->config.audio.music_enabled;
		sdl3_audio_set_settings(&app->config.audio);
		break;
	case SDL3_SETTINGS_AUDIO_MOVEMENT:
		app->config.audio.movement_enabled =
			!app->config.audio.movement_enabled;
		sdl3_audio_set_settings(&app->config.audio);
		break;
	case SDL3_SETTINGS_AUDIO_MASTER:
		change_audio_volume(app, &app->config.audio.master_volume, delta);
		break;
	case SDL3_SETTINGS_AUDIO_MUSIC:
		change_audio_volume(app, &app->config.audio.music_volume, delta);
		break;
	case SDL3_SETTINGS_AUDIO_INTERFACE:
		change_audio_volume(app, &app->config.audio.interface_volume, delta);
		break;
	case SDL3_SETTINGS_AUDIO_GAMEPLAY:
		change_audio_volume(app, &app->config.audio.gameplay_volume, delta);
		break;
	case SDL3_SETTINGS_AUDIO_CREATURE:
		change_audio_volume(app, &app->config.audio.creature_volume, delta);
		break;
	case SDL3_SETTINGS_AUDIO_AMBIENT:
		change_audio_volume(app, &app->config.audio.ambient_volume, delta);
		break;
	default:
		break;
	}
}

void sdl3_frontend_activate_settings_selection(struct sdl3_app *app)
{
	int ui_sound;

	if (!app) return;
	if (sdl3_settings_activate(&app->settings)) {
		ui_sound = MSG_UI_ACCEPT;
	} else {
		sdl3_frontend_change_selected_setting(app, 1);
		ui_sound = MSG_UI_CHANGE;
	}
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
	sound(ui_sound);
}

void sdl3_frontend_handle_settings_key(struct sdl3_app *app,
		const SDL_KeyboardEvent *event)
{
	int ui_sound = -1;

	if (!app || !event || event->repeat) return;
	if (event->key == SDLK_F1 ||
			(event->scancode == SDL_SCANCODE_SLASH &&
			(event->mod & SDL_KMOD_SHIFT))) {
		if (event->scancode == SDL_SCANCODE_SLASH) {
			app->suppress_text_codepoint = '?';
		}
		sdl3_frontend_show_field_guide(app);
		return;
	}
	switch (event->key) {
	case SDLK_ESCAPE:
		if (!sdl3_settings_back(&app->settings)) {
			sdl3_frontend_set_settings_visible(app, false);
			return;
		}
		ui_sound = MSG_UI_CHANGE;
		break;
	case SDLK_UP:
	case SDLK_KP_8:
		sdl3_settings_move(&app->settings, -1);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_DOWN:
	case SDLK_KP_2:
		sdl3_settings_move(&app->settings, 1);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_LEFT:
	case SDLK_KP_4:
		if (app->settings.page != SDL3_SETTINGS_HUB) {
			sdl3_frontend_change_selected_setting(app, -1);
			ui_sound = MSG_UI_CHANGE;
		}
		break;
	case SDLK_RIGHT:
	case SDLK_KP_6:
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
	case SDLK_SPACE:
		sdl3_frontend_activate_settings_selection(app);
		return;
	case SDLK_F11:
		if (!sdl3_frontend_toggle_fullscreen(app)) return;
		ui_sound = MSG_UI_CHANGE;
		break;
	default:
		return;
	}
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
	if (ui_sound >= 0) sound(ui_sound);
}
