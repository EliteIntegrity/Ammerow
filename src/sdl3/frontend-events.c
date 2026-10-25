/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/frontend-events.c
 * \brief Raw SDL event dispatch for the SDL3 frontend.
 *
 *
 */

#include "sdl3/frontend-internal.h"

#include "snd-sdl3.h"
#include "player-explore.h"
#include "player-util.h"
#include "ui-help.h"
#include "ui-mode-input.h"
#include "ui-spelunking-survey.h"
#include "sdl3/audio.h"
#include "sdl3/credits.h"
#include "sdl3/fishing.h"
#include "sdl3/input.h"
#include "sdl3/legal.h"
#include "sdl3/settings.h"

void sdl3_frontend_show_field_guide(struct sdl3_app *app)
{
	bool home_was_visible;
	bool pause_was_visible;
	bool settings_was_visible;

	if (!app) return;
	home_was_visible = app->home.visible;
	pause_was_visible = app->pause_menu.visible;
	settings_was_visible = app->settings.visible;
	app->home.visible = false;
	app->pause_menu.visible = false;
	app->settings.visible = false;
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
	do_cmd_help();
	app->home.visible = home_was_visible;
	app->pause_menu.visible = pause_was_visible;
	app->settings.visible = settings_was_visible;
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
}

static void activate_home_selection(struct sdl3_app *app)
{
	sdl3_home_activate(&app->home);
	if (app->home.action == SDL3_HOME_ACTION_FIELD_GUIDE) {
		app->home.action = SDL3_HOME_ACTION_NONE;
		sdl3_frontend_show_field_guide(app);
	}
	if (app->home.action == SDL3_HOME_ACTION_SETTINGS) {
		app->home.action = SDL3_HOME_ACTION_NONE;
		sdl3_frontend_set_settings_visible(app, true);
		return;
	}
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
	sound(MSG_UI_ACCEPT);
}

static void activate_pause_selection(struct sdl3_app *app)
{
	sdl3_pause_menu_activate(&app->pause_menu);
	if (app->pause_menu.action == SDL3_PAUSE_ACTION_SETTINGS) {
		app->pause_menu.action = SDL3_PAUSE_ACTION_NONE;
		sdl3_frontend_set_settings_visible(app, true);
		return;
	}
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
	sound(MSG_UI_ACCEPT);
}

static bool pointer_event_cell(struct sdl3_app *app, const SDL_Event *event,
		int *col, int *row)
{
	SDL_Event converted;
	float x;
	float y;

	if (!app || !event || !col || !row) return false;
	converted = *event;
	if (!SDL_ConvertEventToRenderCoordinates(app->renderer, &converted)) {
		return false;
	}
	if (event->type == SDL_EVENT_MOUSE_MOTION) {
		x = converted.motion.x;
		y = converted.motion.y;
	} else {
		x = converted.button.x;
		y = converted.button.y;
	}
	return sdl3_presenter_point_to_cell(app->presenter, x, y, NULL, col, row);
}

static bool select_overlay_at_pointer(struct sdl3_app *app,
		const SDL_Event *event)
{
	int col;
	int row;

	if (!pointer_event_cell(app, event, &col, &row)) return false;
	if (app->settings.visible) {
		return sdl3_settings_select_at(&app->settings, app->grid.cols,
			app->grid.rows, col, row);
	}
	if (app->pause_menu.visible) {
		return sdl3_pause_menu_select_at(&app->pause_menu, app->grid.cols,
			app->grid.rows, col, row);
	}
	if (app->home.visible) {
		return sdl3_home_select_at(&app->home, app->grid.cols,
			app->grid.rows, col, row);
	}
	return false;
}

static bool handle_semantic_selection_pointer(struct sdl3_app *app,
		const SDL_Event *event, bool activate)
{
	const struct sdl3_screen_row *row;
	int col;
	int screen_row;
	int index;
	int delta;

	if (app && event && app->screen.active &&
			app->screen.kind == UI_SCREEN_CAVE_SURVEY) {
		/* Selection is a semantic index, not a click on the hidden terminal
		 * map. Hover and blank clicks must not navigate or change focus. */
		if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			if (event->button.button == SDL_BUTTON_RIGHT) {
				Term_keypress(ESCAPE, 0);
			} else if (activate && pointer_event_cell(app, event, &col, &screen_row)) {
				if (sdl3_screen_map_node_at(&app->screen, app->grid.cols,
						app->grid.rows, col, screen_row, &index)) {
					Term_keypress(KC_SURVEY_SELECT_FIRST + index, 0);
				} else if (sdl3_screen_tab_at(&app->screen, app->grid.cols,
						app->grid.rows, col, screen_row, &index)) {
					/* Survey is the last tab; step backward to the clicked tab. */
					for (int i = app->screen.tab_count - 1; i > index; i--)
						Term_keypress(ARROW_LEFT, 0);
				}
			}
		}
		return true;
	}

	if (app && event && app->screen.active && app->screen.kind == UI_SCREEN_FLOOR_PILE) {
		/* No hover-generated commands and no click through to the map.
		 * Look piles have item tags; arrival notices simply close on click. */
		if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
			keycode_t key = ESCAPE;
			if (event->button.button == SDL_BUTTON_LEFT &&
					pointer_event_cell(app, event, &col, &screen_row) &&
					sdl3_screen_row_at(&app->screen, app->grid.cols,
						app->grid.rows, col, screen_row, &index)) {
				key = app->screen.rows[index].tag ? app->screen.rows[index].tag : KC_ENTER;
			}
			Term_keypress(key, 0);
		}
		return true;
	}
	if (!app || !event || !app->screen.active ||
			(app->screen.kind != UI_SCREEN_CHARACTER_CREATION &&
			app->screen.kind != UI_SCREEN_CHARACTER_DOSSIER &&
			app->screen.kind != UI_SCREEN_BIRTH_STATS &&
			app->screen.kind != UI_SCREEN_ACTION_MENU &&
			app->screen.kind != UI_SCREEN_ITEM_ACTIONS &&
			app->screen.kind != UI_SCREEN_ADVANCEMENT &&
			app->screen.kind != UI_SCREEN_OPTIONS)) {
		return false;
	}
	/* These interfaces are presented from semantic rows rather than legacy
	 * terminal coordinates.  Once one owns the pointer, never let a miss fall
	 * through to hidden terminal controls. */
	if ((event->type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
			event->button.button != SDL_BUTTON_LEFT) ||
			!pointer_event_cell(app, event, &col, &screen_row) ||
			!sdl3_screen_row_at(&app->screen, app->grid.cols, app->grid.rows,
				col, screen_row, &index)) {
		return true;
	}
	row = &app->screen.rows[index];
	if (!row->enabled) return true;
	if (app->screen.kind == UI_SCREEN_ACTION_MENU ||
			app->screen.kind == UI_SCREEN_ITEM_ACTIONS ||
			app->screen.kind == UI_SCREEN_ADVANCEMENT) {
		keycode_t keys[SDL3_SCREEN_ROW_CAPACITY];
		int count = sdl3_screen_action_menu_keys(&app->screen, index,
			activate, keys, N_ELEMENTS(keys));
		for (int i = 0; i < count; i++) Term_keypress(keys[i], 0);
		return true;
	}
	if (row->tag) {
		/* A tag alone moves the authoritative controller cursor, which makes
		 * motion provide the same highlight and contextual description as the
		 * keyboard.  A click follows it with acceptance. */
		if (index != app->screen.cursor) Term_keypress(row->tag, 0);
		if (activate) Term_keypress(KC_ENTER, 0);
		return true;
	}
	/* Point allocation has selectable rows but no row activation tags.  Hover
	 * and click move its controller cursor without guessing a stat change. */
	if (app->screen.cursor < 0 || index == app->screen.cursor) return true;
	delta = index - app->screen.cursor;
	while (delta < 0) {
		Term_keypress(ARROW_UP, 0);
		delta++;
	}
	while (delta > 0) {
		Term_keypress(ARROW_DOWN, 0);
		delta--;
	}
	return true;
}

static int overlay_selection_key(const struct sdl3_app *app)
{
	if (app->settings.visible) {
		return 1000 + app->settings.page * 100 +
			(app->settings.page == SDL3_SETTINGS_HUB ?
			app->settings.selected_hub_row : app->settings.selected_row);
	}
	if (app->pause_menu.visible) {
		return 2000 + app->pause_menu.page * 100 +
			(app->pause_menu.page == SDL3_PAUSE_CONFIRM_ABANDON ?
			(app->pause_menu.abandon_confirmed ? 1 : 0) :
			app->pause_menu.selected_row);
	}
	if (app->home.visible) {
		int selected = app->home.selected_row;

		if (app->home.page == SDL3_HOME_ABOUT_MENU) {
			selected = app->home.selected_about_row;
		} else if (app->home.page == SDL3_HOME_LOAD) {
			selected = app->home.selected_save;
		}
		return 3000 + app->home.page * 100 + selected;
	}
	return -1;
}

void sdl3_frontend_handle_home_key(struct sdl3_app *app,
		const SDL_KeyboardEvent *event)
{
	int document_width;
	int document_rows;
	bool document_page;
	int document_max;
	int ui_sound = -1;

	if (!app || !event) return;
	if (app->home.page == SDL3_HOME_WORLD_PAGE && event->key != SDLK_ESCAPE &&
			event->key != SDLK_F11 && event->key != SDLK_F1) {
		enum ui_world_intro_action action;
		switch (event->key) {
		case SDLK_LEFT: case SDLK_KP_4: action = UI_INTRO_PREVIOUS_TAB; break;
		case SDLK_RIGHT: case SDLK_KP_6: action = UI_INTRO_NEXT_TAB; break;
		case SDLK_TAB:
			action = event->mod & SDL_KMOD_SHIFT ?
				UI_INTRO_PREVIOUS_TAB : UI_INTRO_NEXT_TAB; break;
		case SDLK_UP: case SDLK_KP_8: action = UI_INTRO_UP; break;
		case SDLK_DOWN: case SDLK_KP_2: action = UI_INTRO_DOWN; break;
		case SDLK_PAGEUP: action = UI_INTRO_PAGE_UP; break;
		case SDLK_PAGEDOWN: action = UI_INTRO_PAGE_DOWN; break;
		case SDLK_HOME: action = UI_INTRO_FIRST_LINE; break;
		case SDLK_END: action = UI_INTRO_LAST_LINE; break;
		default: return;
		}
		if (event->repeat && (action == UI_INTRO_PREVIOUS_TAB ||
				action == UI_INTRO_NEXT_TAB)) return;
		ui_world_intro_handle(&app->home.introduction, action,
			app->grid.cols, app->grid.rows);
		sdl3_grid_mark_dirty(&app->grid);
		sdl3_frontend_render(app);
		sound(MSG_UI_NAVIGATE);
		return;
	}
	document_width = SDL_min(88, app->grid.cols - 8);
	document_rows = SDL_max(1, app->grid.rows - 11);
	document_page = app->home.page == SDL3_HOME_CREDITS_PAGE ||
		app->home.page == SDL3_HOME_LEGAL_PAGE;
	document_max = app->home.page == SDL3_HOME_LEGAL_PAGE ?
		sdl3_legal_max_offset(document_width, document_rows) :
		sdl3_credits_max_offset(document_width, document_rows);
	if (event->repeat && (!document_page ||
			!sdl3_input_is_document_scroll_key(event))) return;
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
		sdl3_home_back(&app->home);
		ui_sound = MSG_UI_CHANGE;
		break;
	case SDLK_DELETE:
	case SDLK_D:
		if (app->home.page != SDL3_HOME_LOAD) return;
		sdl3_home_request_delete(&app->home);
		if (event->key == SDLK_D) app->suppress_text_codepoint = 'd';
		ui_sound = MSG_UI_CHANGE;
		break;
	case SDLK_Y:
		if (app->home.page != SDL3_HOME_DELETE_CONFIRM) return;
		sdl3_home_confirm_delete(&app->home);
		ui_sound = MSG_UI_ACCEPT;
		break;
	case SDLK_N:
		if (app->home.page != SDL3_HOME_DELETE_CONFIRM) return;
		sdl3_home_back(&app->home);
		ui_sound = MSG_UI_CHANGE;
		break;
	case SDLK_UP:
	case SDLK_KP_8:
		if (document_page) {
			sdl3_home_scroll_document(&app->home, -1, document_max);
		} else {
			sdl3_home_move(&app->home, -1);
		}
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_DOWN:
	case SDLK_KP_2:
		if (document_page) {
			sdl3_home_scroll_document(&app->home, 1, document_max);
		} else {
			sdl3_home_move(&app->home, 1);
		}
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_PAGEUP:
		if (!document_page) return;
		sdl3_home_scroll_document(&app->home, -document_rows, document_max);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_PAGEDOWN:
		if (!document_page) return;
		sdl3_home_scroll_document(&app->home, document_rows, document_max);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_HOME:
		if (!document_page) return;
		sdl3_home_scroll_document(&app->home, -document_max, document_max);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_END:
		if (!document_page) return;
		sdl3_home_scroll_document(&app->home, document_max, document_max);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		if (app->home.page == SDL3_HOME_DELETE_CONFIRM) return;
		activate_home_selection(app);
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

void sdl3_frontend_handle_pause_key(struct sdl3_app *app,
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
		sdl3_pause_menu_back(&app->pause_menu);
		ui_sound = MSG_UI_CHANGE;
		break;
	case SDLK_UP:
	case SDLK_LEFT:
	case SDLK_KP_8:
	case SDLK_KP_4:
		sdl3_pause_menu_move(&app->pause_menu, -1);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_DOWN:
	case SDLK_RIGHT:
	case SDLK_KP_2:
	case SDLK_KP_6:
		sdl3_pause_menu_move(&app->pause_menu, 1);
		ui_sound = MSG_UI_NAVIGATE;
		break;
	case SDLK_RETURN:
	case SDLK_KP_ENTER:
		activate_pause_selection(app);
		return;
	default:
		return;
	}
	sdl3_grid_mark_dirty(&app->grid);
	sdl3_frontend_render(app);
	if (ui_sound >= 0) sound(ui_sound);
}

bool sdl3_frontend_dispatch_event(struct sdl3_app *app,
		const SDL_Event *event)
{
	if (!app || !event) return false;
	/* Steam's default screenshot key must not dismiss an any-key card or
	 * become a game command. Idle presents below let the overlay capture
	 * the current screen, including when Steam consumes the key itself. */
	if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
			event->key.key == SDLK_F12) return false;
	if (!sdl3_input_gate_accept(&app->input_gate, event)) return false;
	/* Consume a deliberate interrupt before shortcuts can open an overlay
	 * and accidentally leave the simulation running behind it. */
	if (player_explore_active(player) &&
			(event->type == SDL_EVENT_KEY_DOWN ||
			 event->type == SDL_EVENT_MOUSE_BUTTON_DOWN)) {
		player_explore_cancel(player, "stopped by you");
		disturb(player);
		sdl3_input_gate_arm(&app->input_gate);
		app->suppress_text_codepoint = 0;
		return false;
	}
	switch (event->type) {
	case SDL_EVENT_QUIT:
		terms_disconnecting = 1;
		return true;
	case SDL_EVENT_KEY_DOWN:
		app->suppress_text_codepoint = 0;
		/* Holding p must not restart exploration past its safety stops. */
		if (event->key.repeat && event->key.key == SDLK_P && player &&
				OPT(player, autoexplore_commands) &&
				OPT(player, autoexplore_continuous) &&
				app->presentation.phase == SDL3_PRESENTATION_WORLD &&
				!app->settings.visible && !app->pause_menu.visible &&
				!app->home.visible) {
			app->suppress_text_codepoint = 'p';
			return true;
		}
		if (event->key.repeat && event->key.key == SDLK_P &&
				app->spelunk_peek_key_down) {
			app->suppress_text_codepoint =
				(event->key.mod & SDL_KMOD_SHIFT) ? 'P' : 'p';
			return true;
		}
		if (!event->key.repeat &&
				sdl3_input_is_settings_shortcut(&event->key)) {
			app->suppress_text_codepoint = 's';
			sdl3_frontend_set_settings_visible(app, !app->settings.visible);
			return false;
		}
		if (!event->key.repeat &&
				(event->key.key == SDLK_F11 ||
				(event->key.key == SDLK_RETURN &&
				(event->key.mod & SDL_KMOD_ALT)))) {
			if (sdl3_frontend_toggle_fullscreen(app)) {
				sound(MSG_UI_CHANGE);
				sdl3_frontend_render(app);
			}
			return false;
		}
		if (app->settings.visible) {
			sdl3_frontend_handle_settings_key(app, &event->key);
			return false;
		}
		if (app->pause_menu.visible) {
			sdl3_frontend_handle_pause_key(app, &event->key);
			return false;
		}
		if (app->home.visible) {
			sdl3_frontend_handle_home_key(app, &event->key);
			return false;
		}
		if (app->presentation.phase == SDL3_PRESENTATION_WORLD) {
			enum sdl3_dock_placement placement =
				sdl3_input_overlay_shortcut(&event->key);

			if (placement < SDL3_DOCK_PLACEMENT_COUNT) {
				app->suppress_text_codepoint = 0;
				if (placement == SDL3_DOCK_LEFT) {
					sdl3_frontend_change_stats_visibility(app);
					sdl3_frontend_render(app);
					sound(MSG_UI_CHANGE);
					sdl3_config_save(&app->config);
				} else {
					bool visible = !(app->config.dock_visible &&
						app->config.dock_placement == placement);

					sdl3_frontend_set_dock_view(app, PW_MESSAGE);
					if (sdl3_frontend_apply_dock_layout(app, visible,
							placement, app->config.dock_rows,
							app->config.dock_cols)) {
						sdl3_frontend_render(app);
						sound(MSG_UI_CHANGE);
						sdl3_config_save(&app->config);
					}
				}
				return false;
			}
		}
		if (!event->key.repeat && event->key.key == SDLK_P &&
				!(event->key.mod &
				(SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) &&
				textui_mode_input_uses_held_peek(player)) {
			app->spelunk_peek_key_down = true;
			app->suppress_text_codepoint =
				(event->key.mod & SDL_KMOD_SHIFT) ? 'P' : 'p';
			Term_keypress(KC_MODE_PEEK_BEGIN, 0);
			return true;
		}
		{
			int action = sdl3_input_zoom_shortcut(&event->key);

			if (action) {
				app->suppress_text_codepoint = action == -1 ? '-' :
					(action == 2 ? '0' :
					((event->key.scancode == SDL_SCANCODE_EQUALS &&
					!(event->key.mod & SDL_KMOD_SHIFT)) ? '=' : '+'));
				if (!sdl3_presentation_map_zoom_active(
						&app->presentation)) return false;
				if (sdl3_frontend_change_zoom(app, action)) {
					sdl3_frontend_render(app);
					sdl3_config_save(&app->config);
				}
				return false;
			}
		}
		{
			uint8_t modifiers;
			keycode_t key = event->key.repeat ?
				sdl3_input_translate_repeated(&event->key, &modifiers) :
				sdl3_input_translate_special(&event->key, &modifiers);

			if (key) {
				if ((sdl3_input_is_keypad_key(event->key.key) ||
						event->key.repeat) && key >= 32 && key <= 126) {
					app->suppress_text_codepoint = key;
				}
				Term_keypress(key, modifiers);
				return true;
			}
		}
		break;
	case SDL_EVENT_KEY_UP:
		if (event->key.key == SDLK_P && app->spelunk_peek_key_down) {
			app->spelunk_peek_key_down = false;
			app->suppress_text_codepoint = 0;
			if (textui_mode_input_uses_held_peek(player)) {
				Term_keypress(KC_MODE_PEEK_END, 0);
			}
			return true;
		}
		break;
	case SDL_EVENT_TEXT_INPUT:
		if (app->settings.visible || app->pause_menu.visible ||
				app->home.visible) {
			app->suppress_text_codepoint = 0;
			return false;
		}
		if (event->text.text && event->text.text[0] >= 32 &&
				event->text.text[0] <= 126 && !event->text.text[1]) {
			uint8_t modifiers =
				sdl3_input_translate_modifiers(SDL_GetModState());
			keycode_t key = (uint8_t)event->text.text[0];

			if (app->suppress_text_codepoint == key ||
					((app->suppress_text_codepoint == 'p' ||
					app->suppress_text_codepoint == 'P') &&
					(key == 'p' || key == 'P'))) {
				app->suppress_text_codepoint = 0;
				return true;
			}
			app->suppress_text_codepoint = 0;
			modifiers &= ~KC_MOD_SHIFT;
			Term_keypress(key, modifiers);
			return true;
		}
		break;
	case SDL_EVENT_MOUSE_MOTION:
		if (app->settings.visible || app->pause_menu.visible ||
				app->home.visible) {
			int before = overlay_selection_key(app);

			if (select_overlay_at_pointer(app, event) &&
					overlay_selection_key(app) != before) {
				sdl3_grid_mark_dirty(&app->grid);
				sdl3_frontend_render(app);
			}
		} else if (handle_semantic_selection_pointer(app, event, false)) {
			return true;
		}
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
		if (app->home.visible && app->home.page == SDL3_HOME_WORLD_PAGE &&
				!app->settings.visible && !app->pause_menu.visible) {
			int col, row;
			if (event->button.button == SDL_BUTTON_LEFT &&
					pointer_event_cell(app, event, &col, &row) &&
					ui_world_intro_select_at(&app->home.introduction,
						app->grid.cols, app->grid.rows, col, row)) {
				sdl3_grid_mark_dirty(&app->grid);
				sdl3_frontend_render(app);
				sound(MSG_UI_NAVIGATE);
			}
			return false;
		}
		if (app->settings.visible || app->pause_menu.visible ||
				app->home.visible) {
			if (event->button.button != SDL_BUTTON_LEFT ||
					!select_overlay_at_pointer(app, event)) {
				return false;
			}
			if (app->settings.visible) {
				sdl3_frontend_activate_settings_selection(app);
			} else if (app->pause_menu.visible) {
				activate_pause_selection(app);
			} else {
				activate_home_selection(app);
			}
			return false;
		}
		if (handle_semantic_selection_pointer(app, event,
				event->button.button == SDL_BUTTON_LEFT)) {
			return true;
		}
		{
			SDL_Event converted = *event;

			if (SDL_ConvertEventToRenderCoordinates(app->renderer,
					&converted)) {
				int composite_col;
				int composite_row;
				int col;
				int row;
				uint8_t mods = sdl3_input_translate_modifiers(
					SDL_GetModState());
				uint8_t button = event->button.button |
					((mods & 0x0f) << 4);

				if (sdl3_presenter_point_to_cell(app->presenter,
						converted.button.x, converted.button.y,
						&app->map_presenter.view, &composite_col,
						&composite_row) &&
						sdl3_layout_main_cell(&app->layout,
							composite_col, composite_row, &col, &row)) {
					Term_mousepress(col, row, button);
					return true;
				}
			}
		}
		break;
	case SDL_EVENT_MOUSE_WHEEL:
		{
			float wheel_y = event->wheel.y;
			int direction;

			if (event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
				wheel_y = -wheel_y;
			}
			if (wheel_y == 0.0f) return false;
			direction = wheel_y > 0.0f ? 1 : -1;
			if (app->screen.active && app->screen.kind == UI_SCREEN_FLOOR_PILE &&
					!app->settings.visible && !app->pause_menu.visible) {
				Term_keypress(direction > 0 ? KC_PGUP : KC_PGDOWN, 0);
				return true;
			}
			if (app->screen.active && app->screen.kind == UI_SCREEN_ITEM_DETAILS &&
					!app->settings.visible && !app->pause_menu.visible) {
				for (int i = 0; i < 3; i++)
					Term_keypress(direction > 0 ? ARROW_UP : ARROW_DOWN, 0);
				return true;
			}
			if (app->home.visible && app->home.page == SDL3_HOME_WORLD_PAGE &&
					!app->settings.visible && !app->pause_menu.visible) {
				struct ui_world_intro_layout layout;
				if (ui_world_intro_measure(&app->home.introduction,
						app->grid.cols, app->grid.rows, &layout)) {
					ui_world_intro_scroll(&app->home.introduction,
						-direction * 3, &layout);
					sdl3_grid_mark_dirty(&app->grid);
					sdl3_frontend_render(app);
				}
				return false;
			}
			if (app->home.visible) {
				bool document_page =
					app->home.page == SDL3_HOME_CREDITS_PAGE ||
					app->home.page == SDL3_HOME_LEGAL_PAGE;
				int width = SDL_min(88, app->grid.cols - 8);
				int rows = SDL_max(1, app->grid.rows - 11);
				int maximum;

				if (!document_page) return false;
				maximum = app->home.page == SDL3_HOME_LEGAL_PAGE ?
					sdl3_legal_max_offset(width, rows) :
					sdl3_credits_max_offset(width, rows);
				sdl3_home_scroll_document(&app->home, -direction * 3,
					maximum);
				sdl3_grid_mark_dirty(&app->grid);
				sdl3_frontend_render(app);
				return false;
			}
			if (app->settings.visible || app->pause_menu.visible ||
					!sdl3_presentation_map_zoom_active(&app->presentation)) {
				return false;
			}
			if (sdl3_frontend_change_zoom(app, direction)) {
				sdl3_frontend_render(app);
				sound(MSG_UI_CHANGE);
				sdl3_config_save(&app->config);
			}
		}
		break;
	case SDL_EVENT_WINDOW_EXPOSED:
	case SDL_EVENT_WINDOW_RESIZED:
	case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
	case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
		sdl3_frontend_capture_windowed_size(app);
		sdl3_presenter_resize(app->presenter, app->renderer);
		sdl3_grid_mark_dirty(&app->grid);
		sdl3_frontend_render(app);
		break;
	case SDL_EVENT_WINDOW_HIDDEN:
	case SDL_EVENT_WINDOW_MINIMIZED:
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		if (app->spelunk_peek_key_down) {
			app->spelunk_peek_key_down = false;
			app->suppress_text_codepoint = 0;
			if (textui_mode_input_uses_held_peek(player)) {
				Term_keypress(KC_MODE_PEEK_END, 0);
			}
		}
		app->window_active = false;
		sdl3_weather_set_visible(&app->weather, false);
		app->weather_next_tick_ms = 0;
		sdl3_audio_set_active(false);
		break;
	case SDL_EVENT_WINDOW_SHOWN:
	case SDL_EVENT_WINDOW_RESTORED:
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		app->window_active = true;
		sdl3_audio_set_active(true);
		sdl3_grid_mark_dirty(&app->grid);
		sdl3_frontend_render(app);
		break;
	case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
		app->fullscreen = true;
		app->config.fullscreen = true;
		break;
	case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
		app->fullscreen = false;
		app->config.fullscreen = false;
		break;
	case SDL_EVENT_RENDER_TARGETS_RESET:
		sdl3_presenter_invalidate_grid_cache(app->presenter);
		sdl3_grid_mark_all_dirty(&app->grid);
		sdl3_frontend_render(app);
		break;
	case SDL_EVENT_RENDER_DEVICE_RESET:
		if (!sdl3_presenter_recreate_renderer_resources(app->presenter,
				app->renderer)) {
			SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
				"Could not recreate SDL3 resources after a device reset");
			terms_disconnecting = 1;
			return true;
		}
		sdl3_grid_mark_all_dirty(&app->grid);
		sdl3_frontend_render(app);
		if (app->verbose) SDL_Log("Renderer resources recreated after reset");
		break;
	case SDL_EVENT_RENDER_DEVICE_LOST:
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
			"The SDL3 render device was lost and cannot be recovered");
		terms_disconnecting = 1;
		return true;
	default:
		break;
	}
	return false;
}

int sdl3_frontend_wait_event(struct sdl3_app *app, SDL_Event *event)
{
	bool fishing_active;
	Uint64 next_present = SDL_GetTicks() + 250;

	if (!app || !event) return -1;
	while (!terms_disconnecting) {
		Uint64 now = SDL_GetTicks();
		Uint64 next_tick = UINT64_MAX;
		bool redraw = false, weather_due, fishing_due = false;
		bool idle_enabled = app->window_active;
		bool idle_present;
		int timeout;
#ifdef USE_HEADLESS
		idle_enabled = idle_enabled && !app->offscreen;
#endif
		idle_present = idle_enabled && now >= next_present;

		/* Never make an already queued key wait for an animation present.
		 * Ordinary movement frames advance weather too, so it keeps moving. */
		if (SDL_PollEvent(event)) return 1;
		if (app->combat_effects.active) {
			if (now >= app->combat_effects.next_frame_ms) {
				redraw = sdl3_effects_advance(&app->combat_effects, now);
			}
			if (app->combat_effects.active)
				next_tick = app->combat_effects.next_frame_ms;
		}
		weather_due = sdl3_weather_tick(&app->weather, now,
			&app->weather_next_tick_ms);
		redraw |= weather_due;
		if (app->weather.visible) {
			next_tick = MIN(next_tick, app->weather_next_tick_ms);
		}
		fishing_active = app->window_active && player &&
			sdl3_fishing_is_active(player->fishing);
		if (fishing_active && !app->fishing_next_tick_ms) {
			app->fishing_next_tick_ms = now + SDL3_FISHING_ANIMATION_MS;
		}
		if (fishing_active && now >= app->fishing_next_tick_ms) {
			redraw = fishing_due = true;
			app->fishing_next_tick_ms = now + SDL3_FISHING_ANIMATION_MS;
		}
		if (fishing_active) {
			next_tick = MIN(next_tick, app->fishing_next_tick_ms);
		} else {
			app->fishing_next_tick_ms = 0;
		}
		if (redraw || idle_present) {
			/* One present for all due overlays; discard missed cosmetic ticks. */
			if (idle_present || !sdl3_frontend_render_animated_overlay(app)) {
				sdl3_grid_mark_dirty(&app->grid);
				sdl3_frontend_render(app);
			}
			now = SDL_GetTicks();
			next_present = now + 250;
			if (weather_due) app->weather_next_tick_ms = now + SDL3_WEATHER_TICK_MS;
			if (fishing_due) app->fishing_next_tick_ms = now + SDL3_FISHING_ANIMATION_MS;
			continue;
		}
		/* A quiet turn-based screen still needs occasional graphics presents
		 * for external overlays/capture. Recompose from the cached layers:
		 * SDL does not retain the window backbuffer after RenderPresent.
		 * Never advance game turns or add idle presents while unfocused. */
		if (idle_enabled)
			next_tick = MIN(next_tick, next_present);
		if (next_tick == UINT64_MAX) return SDL_WaitEvent(event) ? 1 : -1;
		now = SDL_GetTicks();
		timeout = next_tick > now ? (int)MIN((Uint64)INT_MAX, next_tick - now) : 0;
		if (SDL_WaitEventTimeout(event, timeout)) return 1;
	}
	return 0;
}
