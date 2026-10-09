/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/settings.c
 * \brief Full-screen SDL3 presentation settings pages.
 *
 *
 */

#include "sdl3/settings.h"

#include "sdl3/menu-layout.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

static const char *hub_labels[SDL3_SETTINGS_HUB_ROW_COUNT] = {
	"Appearance",
	"Interface",
	"Sound",
	"Display / Performance"
};

static const char *hub_descriptions[SDL3_SETTINGS_HUB_ROW_COUNT] = {
	"Interface theme, map face, and semantic terrain style",
	"Menu sizing, interface face, transparent HUD and messages",
	"Master and category volumes",
	"Fullscreen, combat animation, and visible weather"
};

static struct sdl3_menu_layout settings_hub_layout(int cols, int rows)
{
	int width = SDL_min(88, cols - 8);
	int left = SDL_max(4, (cols - width) / 2);

	return (struct sdl3_menu_layout) {
		left, SDL_max(9, rows / 3), width, SDL3_SETTINGS_HUB_ROW_COUNT, 3, 2
	};
}

static struct sdl3_menu_layout settings_page_layout(
		enum sdl3_settings_page page, int cols)
{
	int width = SDL_min(88, cols - 8);

	return (struct sdl3_menu_layout) {
		SDL_max(4, (cols - width) / 2), 9, width,
		sdl3_settings_page_row_count(page), 2, 1
	};
}

static void draw_setting_row(const struct sdl3_settings_overlay *overlay,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width, int row,
		enum sdl3_settings_row setting_row, const char *label,
		const char *value)
{
	int value_col = left + SDL_min(28, width / 2);

	if (overlay->selected_row == setting_row) {
		sdl3_ui_draw_selection(renderer, visual, theme, left, row, width);
		sdl3_ui_draw_text(visual, ">", left + 1, row, 1, theme->accent);
	}
	sdl3_ui_draw_text(visual, label, left + 4, row, value_col - left - 6,
		theme->muted);
	sdl3_ui_draw_text(visual, value, value_col, row, left + width - value_col - 2,
		theme->text);
}

static void draw_hub(const struct sdl3_settings_overlay *overlay,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width)
{
	struct sdl3_menu_layout menu = settings_hub_layout(visual->cols,
		visual->rows);
	int i;

	for (i = 0; i < SDL3_SETTINGS_HUB_ROW_COUNT; i++) {
		int row = sdl3_menu_item_row(&menu, i);

		if (overlay->selected_hub_row == i) {
			sdl3_ui_draw_selection(renderer, visual, theme, left, row, width);
			sdl3_ui_draw_text(visual, ">", left + 1, row, 1, theme->accent);
		}
		sdl3_ui_draw_text(visual, hub_labels[i], left + 4, row, 26, theme->text);
		sdl3_ui_draw_text(visual, hub_descriptions[i], left + 4, row + 1,
			width - 6, theme->muted);
	}
}

bool sdl3_settings_select_at(struct sdl3_settings_overlay *overlay, int cols,
		int rows, int col, int row)
{
	struct sdl3_menu_layout menu;
	enum sdl3_settings_row setting_row;
	int item;

	if (!overlay) return false;
	if (overlay->page == SDL3_SETTINGS_HUB) {
		menu = settings_hub_layout(cols, rows);
		item = sdl3_menu_item_at(&menu, col, row);
		if (item < 0) return false;
		overlay->selected_hub_row = item;
		return true;
	}
	menu = settings_page_layout(overlay->page, cols);
	item = sdl3_menu_item_at(&menu, col, row);
	if (item < 0 || !sdl3_settings_page_row_at(overlay->page, item,
			&setting_row)) {
		return false;
	}
	overlay->selected_row = setting_row;
	return true;
}

static const char *page_title(enum sdl3_settings_page page)
{
	switch (page) {
	case SDL3_SETTINGS_APPEARANCE: return "APPEARANCE";
	case SDL3_SETTINGS_INTERFACE: return "INTERFACE";
	case SDL3_SETTINGS_SOUND: return "SOUND";
	case SDL3_SETTINGS_DISPLAY: return "DISPLAY / PERFORMANCE";
	default: return "SETTINGS";
	}
}

static void draw_page(const struct sdl3_settings_overlay *overlay,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, int left, int width,
		const char *interface_font_name, const char *map_font_name,
		enum sdl3_interface_density interface_density, int zoom_percent,
		enum sdl3_terrain_style terrain_style,
		enum sdl3_weather_mode weather_mode, bool fullscreen, bool dock_visible,
		bool hud_stats_visible, bool big_stat_cards, bool animated_combat,
		bool tile_mode, int hybrid_tile_size,
		enum sdl3_dock_placement dock_placement, int dock_rows, int dock_cols,
		const struct sdl3_audio_settings *audio)
{
	char ambient[16];
	char creature[16];
	char gameplay[16];
	char interface_volume[16];
	char master[16];
	char music[16];
	char dock_size[24];
	char zoom[24];
	int row = 9;

	strnfmt(zoom, sizeof(zoom), "x%d.%02d", zoom_percent / 100,
		zoom_percent % 100);
	strnfmt(dock_size, sizeof(dock_size), "%d lines", dock_rows);
	(void)dock_cols;
	if (audio) {
		strnfmt(master, sizeof(master), "%d%%", audio->master_volume);
		strnfmt(interface_volume, sizeof(interface_volume), "%d%%",
			audio->interface_volume);
		strnfmt(gameplay, sizeof(gameplay), "%d%%",
			audio->gameplay_volume);
		strnfmt(creature, sizeof(creature), "%d%%",
			audio->creature_volume);
		strnfmt(ambient, sizeof(ambient), "%d%%", audio->ambient_volume);
		strnfmt(music, sizeof(music), "%d%%", audio->music_volume);
	}

	switch (overlay->page) {
	case SDL3_SETTINGS_APPEARANCE:
		draw_setting_row(overlay, renderer, visual, theme, left, width, row,
			SDL3_SETTINGS_THEME, "Interface theme", theme->name);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 2, SDL3_SETTINGS_MAP_FONT, "Map font (game)",
			map_font_name && map_font_name[0] ? map_font_name :
			"Fallback glyphs");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 4, SDL3_SETTINGS_TILE_MODE, "Map presentation",
			tile_mode ? "Hybrid" : "ASCII");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 6, SDL3_SETTINGS_HYBRID_DETAIL, "Hybrid detail",
			hybrid_tile_size == 32 ? "32 x 32" : "64 x 64");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 8, SDL3_SETTINGS_TERRAIN, "ASCII terrain",
			sdl3_terrain_style_name(terrain_style));
		sdl3_ui_draw_text(visual,
			"Hybrid: portrait sprites, ASCII terrain. Cards stay ASCII art.",
			left + 4, row + 11, width - 6, theme->muted);
		break;
	case SDL3_SETTINGS_INTERFACE:
		draw_setting_row(overlay, renderer, visual, theme, left, width, row,
			SDL3_SETTINGS_INTERFACE_FONT, "Interface font (menus)",
			interface_font_name && interface_font_name[0] ?
			interface_font_name : "Fallback glyphs");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 2, SDL3_SETTINGS_INTERFACE_DENSITY, "Interface size",
			sdl3_interface_density_name(interface_density));
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 4, SDL3_SETTINGS_BIG_STAT_CARDS, "Big stat cards",
			big_stat_cards ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 6, SDL3_SETTINGS_DOCK_VISIBLE, "Messages",
			dock_visible ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 8, SDL3_SETTINGS_DOCK_PLACEMENT, "Message position",
			sdl3_dock_placement_name(dock_placement));
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 10, SDL3_SETTINGS_DOCK_SIZE, "Message history", dock_size);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 12, SDL3_SETTINGS_HUD_STATS, "Player stats",
			hud_stats_visible ? "Visible" : "Hidden");
		sdl3_ui_draw_text(visual,
			"Ctrl+1 top msg   Ctrl+2 stats   Ctrl+3 bottom msg",
			left + 4, row + 13, width - 6, theme->muted);
		break;
	case SDL3_SETTINGS_SOUND:
		if (!audio) break;
		draw_setting_row(overlay, renderer, visual, theme, left, width, row,
			SDL3_SETTINGS_AUDIO_ENABLED, "Sound",
			audio->enabled ? "Enabled" : "Muted");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 2, SDL3_SETTINGS_AUDIO_MUSIC_ENABLED, "Home music",
			audio->music_enabled ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 4, SDL3_SETTINGS_AUDIO_MOVEMENT, "Movement FX",
			audio->movement_enabled ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 6, SDL3_SETTINGS_AUDIO_MASTER, "Master", master);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 8, SDL3_SETTINGS_AUDIO_MUSIC, "Music", music);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 10, SDL3_SETTINGS_AUDIO_INTERFACE, "Interface",
			interface_volume);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 12, SDL3_SETTINGS_AUDIO_GAMEPLAY, "Gameplay", gameplay);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 14, SDL3_SETTINGS_AUDIO_CREATURE, "Creatures", creature);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 16, SDL3_SETTINGS_AUDIO_AMBIENT, "Ambient", ambient);
		break;
	case SDL3_SETTINGS_DISPLAY:
		draw_setting_row(overlay, renderer, visual, theme, left, width, row,
			SDL3_SETTINGS_FULLSCREEN, "Display",
			fullscreen ? "Desktop fullscreen" : "Windowed");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 2, SDL3_SETTINGS_ZOOM, "Map zoom", zoom);
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 4, SDL3_SETTINGS_ANIMATED_COMBAT, "Animated combat",
			animated_combat ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, left, width,
			row + 6, SDL3_SETTINGS_WEATHER, "Visible weather",
			weather_mode == SDL3_WEATHER_OFF ? "Off" : "On");
		sdl3_ui_draw_text(visual,
			"Map zoom affects only the play area; interface size is separate.",
			left + 4, row + 9, width - 6, theme->muted);
		sdl3_ui_draw_text(visual,
			"Weather is automatic. Hide its visuals here; mute audio under Sound.",
			left + 4, row + 11, width - 6, theme->muted);
		break;
	default:
		break;
	}
}

void sdl3_settings_draw(const struct sdl3_settings_overlay *overlay,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme, const char *interface_font_name,
		const char *map_font_name,
		enum sdl3_interface_density interface_density,
		int zoom_percent, enum sdl3_terrain_style terrain_style,
		enum sdl3_weather_mode weather_mode, bool fullscreen,
		bool dock_visible, bool hud_stats_visible, bool big_stat_cards,
		bool animated_combat, bool tile_mode, int hybrid_tile_size,
		enum sdl3_dock_placement dock_placement, int dock_rows, int dock_cols,
		const struct sdl3_audio_settings *audio)
{
	int width;
	int left;

	if (!overlay || !overlay->visible || !renderer || !visual || !theme) {
		return;
	}
	sdl3_ui_fill_output(renderer, visual,
		(SDL_Color){ 0, 0, 0, SDL_ALPHA_OPAQUE });
	width = SDL_min(88, visual->cols - 8);
	left = SDL_max(4, (visual->cols - width) / 2);
	sdl3_ui_draw_text(visual, "SETTINGS", left, 3, width, theme->title);
	sdl3_ui_draw_text(visual, overlay->page == SDL3_SETTINGS_HUB ?
		"Choose a category" : page_title(overlay->page), left, 5, width,
		theme->accent);
	if (overlay->page == SDL3_SETTINGS_HUB) {
		draw_hub(overlay, renderer, visual, theme, left, width);
		sdl3_ui_draw_text(visual,
			"Click or Enter opens   Escape closes   F1 / ? field guide", left,
			visual->rows - 3, width, theme->muted);
	} else {
		draw_page(overlay, renderer, visual, theme, left, width,
			interface_font_name, map_font_name, interface_density,
			zoom_percent, terrain_style, weather_mode, fullscreen, dock_visible,
			hud_stats_visible, big_stat_cards, animated_combat, tile_mode, hybrid_tile_size,
			dock_placement,
			dock_rows, dock_cols, audio);
		sdl3_ui_draw_text(visual,
			"Click cycles   Arrows select/change   Escape categories   F1 / ? manual",
			left, visual->rows - 3, width, theme->muted);
	}
}
