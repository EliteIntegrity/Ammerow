/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/frontend-lifecycle.c
 * \brief Game-event and presentation lifecycle wiring for the SDL3 frontend.
 *
 *
 */

#include "sdl3/frontend-internal.h"

#include "game-world.h"
#include "init.h"
#include "project.h"
#include "savefile.h"
#include "snd-sdl3.h"
#include "store.h"
#include "ui-display.h"
#include "ui-game.h"
#include "ui-object.h"
#include "ui-prefs.h"
#include "sdl3/monster-art.h"
#include "world-spelunking-adapter.h"

static void handle_dock_lifecycle(game_event_type type,
		game_event_data *data, void *user);
static void handle_modal_screen(game_event_type type,
		game_event_data *data, void *user);
static void handle_item_selector(game_event_type type,
		game_event_data *data, void *user);
static void handle_store_art(game_event_type type,
		game_event_data *data, void *user);
static void handle_combat_effect(game_event_type type,
		game_event_data *data, void *user);

void sdl3_frontend_remember_current_run(struct sdl3_app *app)
{
	const char *filename;
	const char *description;

	if (!app || !savefile[0] || !file_exists(savefile)) return;
	filename = savefile + path_filename_index(savefile);
	description = savefile_get_description(savefile);
	if (sdl3_home_save_is_live(filename, description)) {
		my_strcpy(app->config.last_save, filename,
			sizeof(app->config.last_save));
	} else if (streq(app->config.last_save, filename)) {
		app->config.last_save[0] = '\0';
	}
}

bool sdl3_frontend_apply_dock_layout(struct sdl3_app *app, bool visible,
		enum sdl3_dock_placement placement, int rows, int cols)
{
	struct sdl3_layout candidate;
	struct sdl3_pane *main;
	struct sdl3_pane *dock;
	bool active;
	term *old = Term;
	int main_cols;
	int main_rows;
	int i;

	if (!app) return false;
	main = &app->panes[SDL3_LAYOUT_MAIN_TERM];
	dock = &app->panes[SDL3_LAYOUT_MESSAGE_TERM];
	active = sdl3_presentation_dock_active(&app->presentation, visible);
	/* The left side is the transparent player HUD, not the Angband subwindow.
	 * Old version-14 settings are migrated on load; this is a defensive guard
	 * for any in-memory caller. */
	if (placement == SDL3_DOCK_LEFT) placement = SDL3_DOCK_BOTTOM;
	sdl3_interface_density_dimensions(app->config.interface_density,
		&main_cols, &main_rows);
	sdl3_layout_configure_sized(&candidate, active, placement, rows, cols,
		main_cols, main_rows);
	if (!sdl3_grid_resize(&app->grid,
			candidate.panes[SDL3_LAYOUT_MAIN_TERM].cols,
			candidate.panes[SDL3_LAYOUT_MAIN_TERM].rows)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not resize the SDL3 playfield grid");
		return false;
	}
	if (!sdl3_grid_resize(&app->dock_grid,
			candidate.panes[SDL3_LAYOUT_MESSAGE_TERM].cols,
			candidate.panes[SDL3_LAYOUT_MESSAGE_TERM].rows)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not resize the SDL3 message-overlay grid");
		return false;
	}
	app->layout = candidate;
	for (i = 0; i < SDL3_LAYOUT_PANE_COUNT; i++) {
		app->panes[i].layout = &app->layout.panes[i];
	}
	if (!sdl3_presenter_set_grid_size(app->presenter, app->renderer,
			app->layout.cols, app->layout.rows)) {
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"Could not resize the SDL3 presentation layout");
		return false;
	}
	app->config.dock_visible = visible;
	app->config.dock_placement = placement;
	app->config.dock_rows = MAX(SDL3_LAYOUT_MIN_DOCK_ROWS,
		MIN(SDL3_LAYOUT_MAX_DOCK_ROWS, rows));
	app->config.dock_cols = MAX(SDL3_LAYOUT_MIN_DOCK_COLS,
		MIN(SDL3_LAYOUT_MAX_DOCK_COLS, cols));
	app->dock_active = active;
	app->font_cache_needs_compaction = true;
	if (main->term_inited) {
		Term_activate(&main->term);
		if (main->term.wid != main->layout->cols ||
				main->term.hgt != main->layout->rows) {
			Term_resize(main->layout->cols, main->layout->rows);
			Term_redraw();
		}
	}
	if (dock->term_inited) {
		Term_activate(&dock->term);
		if (dock->term.wid != dock->layout->cols ||
				dock->term.hgt != dock->layout->rows) {
			Term_resize(dock->layout->cols, dock->layout->rows);
		}
		if (active) Term_redraw();
	}
	if (old) Term_activate(old);
	sdl3_grid_mark_all_dirty(&app->grid);
	sdl3_grid_mark_all_dirty(&app->dock_grid);
	return true;
}

static void refresh_dock_view(uint32_t flag)
{
	if (!player) return;
	switch (flag) {
	case PW_INVEN: event_signal(EVENT_INVENTORY); break;
	case PW_EQUIP: event_signal(EVENT_EQUIPMENT); break;
	case PW_PLAYER_0:
	case PW_PLAYER_1:
	case PW_PLAYER_2: event_signal(EVENT_HP); break;
	case PW_PLAYER_3: event_signal(EVENT_STATUS); break;
	case PW_MAP: event_signal(EVENT_DUNGEONLEVEL); break;
	case PW_MESSAGE: event_signal(EVENT_STATE); break;
	case PW_OVERHEAD:
		event_signal_point(EVENT_MAP, -1, -1);
		event_signal(EVENT_END);
		break;
	case PW_MONSTER: event_signal(EVENT_MONSTERTARGET); break;
	case PW_OBJECT: event_signal(EVENT_OBJECTTARGET); break;
	case PW_MONLIST: event_signal(EVENT_MONSTERLIST); break;
	case PW_ITEMLIST: event_signal(EVENT_ITEMLIST); break;
	default: break;
	}
}

void sdl3_frontend_set_dock_view(struct sdl3_app *app, uint32_t flag)
{
	uint32_t new_flags[ANGBAND_TERM_MAX];
	int i;

	if (!app || window_flag[SDL3_LAYOUT_MESSAGE_TERM] == flag) return;
	for (i = 0; i < ANGBAND_TERM_MAX; i++) new_flags[i] = window_flag[i];
	new_flags[SDL3_LAYOUT_MESSAGE_TERM] = flag;
	subwindows_set_flags(new_flags, ANGBAND_TERM_MAX);
	refresh_dock_view(flag);
	sdl3_grid_mark_all_dirty(&app->dock_grid);
}

static void handle_dock_lifecycle(game_event_type type,
		game_event_data *data, void *user)
{
	struct sdl3_app *app = user;
	bool was_allowed;
	(void)data;

	if (!app) return;
	was_allowed = app->presentation.phase == SDL3_PRESENTATION_WORLD;
	switch (type) {
	case EVENT_LEAVE_WORLD:
		/* Stores also hide the world view: this is not a shop exit. */
		sdl3_presentation_set_phase(&app->presentation,
			SDL3_PRESENTATION_GAME_SETUP);
		sdl3_effects_clear(&app->combat_effects);
		app->missile_previous_valid = false;
		break;
	case EVENT_LEAVE_GAME:
		sdl3_audio_set_in_store(false);
		app->game_handlers_registered = false;
		app->store_enter_handler_registered = false;
		app->store_leave_handler_registered = false;
		return;
	case EVENT_ENTER_WORLD:
		sdl3_frontend_remember_current_run(app);
		sdl3_config_save(&app->config);
		/* Fall through. */
	case EVENT_NEW_LEVEL_DISPLAY:
		sdl3_audio_set_in_store(false);
		sdl3_store_art_clear(&app->store_art);
		if (!app->store_enter_handler_registered) {
			event_add_handler(EVENT_ENTER_STORE, handle_store_art, app);
			app->store_enter_handler_registered = true;
		}
		if (!character_dungeon || !cave) return;
		sdl3_presentation_set_phase(&app->presentation,
			SDL3_PRESENTATION_WORLD);
		break;
	default:
		return;
	}
	if (was_allowed ==
			(app->presentation.phase == SDL3_PRESENTATION_WORLD)) return;
	if (app->presentation.phase == SDL3_PRESENTATION_WORLD) {
		sdl3_frontend_set_dock_view(app, PW_MESSAGE);
	}
	sdl3_frontend_apply_dock_layout(app, app->config.dock_visible,
		app->config.dock_placement, app->config.dock_rows,
		app->config.dock_cols);
	if (app->verbose) {
		SDL_Log("Dock lifecycle: %s; preference %s; presentation %s",
			app->presentation.phase == SDL3_PRESENTATION_WORLD ?
			"gameplay" : "suppressed",
			app->config.dock_visible ? "enabled" : "disabled",
			app->dock_active ? "visible" : "hidden");
	}
}

static void handle_store_art(game_event_type type,
		game_event_data *data, void *user)
{
	struct sdl3_app *app = user;
	(void)data;

	if (!app) return;
	if (type == EVENT_ENTER_STORE) {
		struct store *store = cave && player ?
			store_at(cave, player->grid) : NULL;

		sdl3_audio_set_in_store(true);
		app->store_enter_handler_registered = false;
		if (!app->store_leave_handler_registered) {
			event_add_handler(EVENT_LEAVE_STORE, handle_store_art, app);
			app->store_leave_handler_registered = true;
		}
		if (store && store->feat != FEAT_HOME && store->owner) {
			char key[sizeof(app->store_art.asset_key)];

			sdl3_monster_art_make_key(key, sizeof(key),
				store->owner->art_id, store->art_id);
			sdl3_store_art_show(&app->store_art, key);
		} else {
			sdl3_store_art_clear(&app->store_art);
		}
	} else if (type == EVENT_LEAVE_STORE) {
		sdl3_audio_set_in_store(false);
		app->store_leave_handler_registered = false;
		sdl3_store_art_clear(&app->store_art);
	}
	sdl3_grid_mark_all_dirty(&app->grid);
}

static bool adjacent_locations(struct loc first, struct loc second)
{
	return ABS(first.x - second.x) <= 1 && ABS(first.y - second.y) <= 1 &&
		!loc_eq(first, second);
}

static wchar_t bolt_effect_glyph(const game_event_data *data)
{
	int dx = data->bolt.x - data->bolt.ox;
	int dy = data->bolt.y - data->bolt.oy;

	if (!dx && !dy) return L'*';
	if (!dx) return L'|';
	if (!dy) return L'-';
	return (dx < 0) == (dy < 0) ? L'\\' : L'/';
}

static void handle_combat_effect(game_event_type type,
		game_event_data *data, void *user)
{
	struct sdl3_app *app = user;
	Uint64 now;
	Uint64 delay;

	if (!app || !data || !app->window_active || !player ||
			!app->config.animated_combat) return;
	now = SDL_GetTicks();
	delay = (Uint64)MAX(1, player->opts.delay_factor);
	if (type == EVENT_MELEE) {
		if (!data->melee.visible) return;
		sdl3_effects_start_melee(&app->combat_effects, now,
			data->melee.source, data->melee.target, data->melee.hit);
		app->missile_previous_valid = false;
	} else if (type == EVENT_DAMAGE) {
		if (!data->damage.visible || data->damage.amount <= 0) return;
		sdl3_effects_start_damage(&app->combat_effects, now,
			data->damage.target, data->damage.amount,
			data->damage.target_is_player);
	} else if (type == EVENT_MISSILE) {
		struct loc target;
		struct loc source;
		struct loc player_source;

		if (!data->missile.seen || !data->missile.obj) return;
		target = loc(data->missile.x, data->missile.y);
		player_source = world_spelunk_player_is_active(player) ?
			loc(player->spelunking->state.x, player->spelunking->state.y) :
			player->grid;
		if (adjacent_locations(player_source, target)) {
			source = player_source;
		} else if (app->missile_previous_valid &&
				adjacent_locations(app->missile_previous, target)) {
			source = app->missile_previous;
		} else {
			source = target;
		}
		sdl3_effects_start_projectile(&app->combat_effects,
			SDL3_EFFECT_MISSILE, now, delay, source, target,
			object_char(data->missile.obj), object_attr(data->missile.obj));
		app->missile_previous = target;
		app->missile_previous_valid = true;
	} else if (type == EVENT_BOLT) {
		int attr;

		if (!data->bolt.seen) return;
		attr = projections && data->bolt.proj_type >= 0 &&
			data->bolt.proj_type < z_info->projection_max ?
			projections[data->bolt.proj_type].color : COLOUR_WHITE;
		sdl3_effects_start_projectile(&app->combat_effects, SDL3_EFFECT_BOLT,
			now, delay, loc(data->bolt.ox, data->bolt.oy),
			loc(data->bolt.x, data->bolt.y), bolt_effect_glyph(data),
			(uint8_t)(attr % MAX_COLORS));
		app->missile_previous_valid = false;
	} else {
		return;
	}
	sdl3_grid_mark_dirty(&app->grid);
}

static void handle_item_selector(game_event_type type,
		game_event_data *data, void *user)
{
	struct sdl3_app *app = user;
	(void)data;

	if (!app) return;
	if (type == EVENT_ENTER_ITEM_SELECTOR) {
		bool entering = !sdl3_presentation_has_layer(&app->presentation,
			SDL3_PRESENTATION_ITEM_SELECTOR);

		sdl3_presentation_set_layer(&app->presentation,
			SDL3_PRESENTATION_ITEM_SELECTOR, true);
		if (entering) {
			sdl3_frontend_apply_dock_layout(app, app->config.dock_visible,
				app->config.dock_placement, app->config.dock_rows,
				app->config.dock_cols);
		}
		if (Term == angband_term[0]) Term_clear();
	} else if (type == EVENT_LEAVE_ITEM_SELECTOR &&
			sdl3_presentation_has_layer(&app->presentation,
				SDL3_PRESENTATION_ITEM_SELECTOR)) {
		sdl3_presentation_set_layer(&app->presentation,
			SDL3_PRESENTATION_ITEM_SELECTOR, false);
		sdl3_presentation_set_layer(&app->presentation,
			SDL3_PRESENTATION_SEMANTIC_SCREEN, false);
		sdl3_screen_clear(&app->screen);
		sdl3_frontend_apply_dock_layout(app, app->config.dock_visible,
			app->config.dock_placement, app->config.dock_rows,
			app->config.dock_cols);
	}
}

static void handle_modal_screen(game_event_type type,
		game_event_data *data, void *user)
{
	struct sdl3_app *app = user;
	bool active;
	(void)data;

	if (!app) return;
	active = type == EVENT_ENTER_MODAL;
	if (active == sdl3_presentation_has_layer(&app->presentation,
			SDL3_PRESENTATION_CLASSIC_MODAL)) return;
	sdl3_presentation_set_layer(&app->presentation,
		SDL3_PRESENTATION_CLASSIC_MODAL, active);
	sdl3_frontend_apply_dock_layout(app, app->config.dock_visible,
		app->config.dock_placement, app->config.dock_rows,
		app->config.dock_cols);
}

void sdl3_frontend_register_game_handlers(struct sdl3_app *app)
{
	if (!app || app->game_handlers_registered) return;
	event_add_handler(EVENT_ENTER_WORLD, handle_dock_lifecycle, app);
	event_add_handler(EVENT_LEAVE_WORLD, handle_dock_lifecycle, app);
	event_add_handler(EVENT_LEAVE_GAME, handle_dock_lifecycle, app);
	event_add_handler(EVENT_NEW_LEVEL_DISPLAY, handle_dock_lifecycle, app);
	event_add_handler(EVENT_ENTER_MODAL, handle_modal_screen, app);
	event_add_handler(EVENT_LEAVE_MODAL, handle_modal_screen, app);
	event_add_handler(EVENT_ENTER_ITEM_SELECTOR, handle_item_selector, app);
	event_add_handler(EVENT_LEAVE_ITEM_SELECTOR, handle_item_selector, app);
	event_add_handler(EVENT_MELEE, handle_combat_effect, app);
	event_add_handler(EVENT_DAMAGE, handle_combat_effect, app);
	event_add_handler(EVENT_MISSILE, handle_combat_effect, app);
	event_add_handler(EVENT_BOLT, handle_combat_effect, app);
	event_add_handler(EVENT_ENTER_STORE, handle_store_art, app);
	app->store_enter_handler_registered = true;
	app->game_handlers_registered = true;
}

void sdl3_frontend_unregister_game_handlers(struct sdl3_app *app)
{
	if (!app) return;
	if (app->game_handlers_registered) {
		event_remove_handler(EVENT_ENTER_WORLD, handle_dock_lifecycle, app);
		event_remove_handler(EVENT_LEAVE_WORLD, handle_dock_lifecycle, app);
		event_remove_handler(EVENT_LEAVE_GAME, handle_dock_lifecycle, app);
		event_remove_handler(EVENT_NEW_LEVEL_DISPLAY,
			handle_dock_lifecycle, app);
		event_remove_handler(EVENT_ENTER_MODAL, handle_modal_screen, app);
		event_remove_handler(EVENT_LEAVE_MODAL, handle_modal_screen, app);
		event_remove_handler(EVENT_ENTER_ITEM_SELECTOR,
			handle_item_selector, app);
		event_remove_handler(EVENT_LEAVE_ITEM_SELECTOR,
			handle_item_selector, app);
		event_remove_handler(EVENT_MELEE, handle_combat_effect, app);
		event_remove_handler(EVENT_DAMAGE, handle_combat_effect, app);
		event_remove_handler(EVENT_MISSILE, handle_combat_effect, app);
		event_remove_handler(EVENT_BOLT, handle_combat_effect, app);
		app->game_handlers_registered = false;
	}
	if (app->store_enter_handler_registered) {
		event_remove_handler(EVENT_ENTER_STORE, handle_store_art, app);
		app->store_enter_handler_registered = false;
	}
	if (app->store_leave_handler_registered) {
		event_remove_handler(EVENT_LEAVE_STORE, handle_store_art, app);
		app->store_leave_handler_registered = false;
	}
}

void sdl3_frontend_reinitialize(struct sdl3_app *app)
{
	bool restored_handlers;

	if (!app || !app->window) return;
	restored_handlers = !app->game_handlers_registered;
	sdl3_audio_set_in_store(false);
	sdl3_presentation_set_phase(&app->presentation,
		SDL3_PRESENTATION_GAME_SETUP);
	sdl3_screen_clear(&app->screen);
	sdl3_effects_clear(&app->combat_effects);
	app->missile_previous_valid = false;
	app->pause_menu.visible = false;
	sdl3_weather_set_visible(&app->weather, false);
	sdl3_store_art_clear(&app->store_art);
	app->weather_next_tick_ms = 0;
	app->dock_active = false;
	app->audio_player = NULL;
	sdl3_map_presenter_reset(&app->map_presenter);
	sdl3_frontend_register_game_handlers(app);
	sdl3_frontend_apply_dock_layout(app, app->config.dock_visible,
		app->config.dock_placement, app->config.dock_rows,
		app->config.dock_cols);
	if (app->verbose && restored_handlers) {
		SDL_Log("SDL3 frontend restored for in-process New Game; dock "
			"preference %s", app->config.dock_visible ? "enabled" :
			"disabled");
	}
}
