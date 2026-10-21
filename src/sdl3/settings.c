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
#include "option.h"

#include "sdl3/menu-layout.h"
#include "sdl3/render-internal.h"
#include "sdl3/theme.h"
#include "sdl3/ui-draw.h"

static const char *hub_labels[SDL3_SETTINGS_HUB_ROW_COUNT] = {
	"Appearance",
	"Interface",
	"Sound",
	"Display / Performance",
	"Controls & Convenience"
};

static const char *hub_descriptions[SDL3_SETTINGS_HUB_ROW_COUNT] = {
	"Interface theme, map face, and semantic terrain style",
	"Menu sizing, interface face, transparent HUD and messages",
	"Master and category volumes",
	"Fullscreen, combat animation, and visible weather",
	"Exploration, stair navigation, pickup and gameplay options"
};

static void draw_setting_row(const struct sdl3_settings_overlay *overlay,
		SDL_Renderer *renderer, struct sdl3_visual *visual,
		const struct sdl3_theme *theme,
		const struct sdl3_menu_layout *menu,
		enum sdl3_settings_row setting_row, const char *label,
		const char *value)
{
	int left = menu->col, width = menu->width;
	int row = -1;
	int value_col = left + SDL_min(28, width / 2);
	for (int i = 0; i < menu->count; i++) {
		enum sdl3_settings_row candidate;
		if (sdl3_settings_page_row_at(overlay->page, sdl3_settings_appearance_mode(overlay), i, &candidate) &&
				candidate == setting_row) row = sdl3_menu_item_row(menu, i);
	}
	if (row < 0) return;

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
	struct sdl3_menu_layout menu = sdl3_settings_layout(SDL3_SETTINGS_HUB,
		sdl3_settings_appearance_mode(overlay), visual->cols, visual->rows).menu;
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

static const char *page_title(enum sdl3_settings_page page)
{
	switch (page) {
	case SDL3_SETTINGS_APPEARANCE: return "APPEARANCE";
	case SDL3_SETTINGS_INTERFACE: return "INTERFACE";
	case SDL3_SETTINGS_SOUND: return "SOUND";
	case SDL3_SETTINGS_DISPLAY: return "DISPLAY / PERFORMANCE";
	case SDL3_SETTINGS_CONTROLS: return "CONTROLS & CONVENIENCE";
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
		bool hud_stats_visible, bool big_stat_cards, bool animated_combat, bool edge_warnings,
		enum sdl3_tile_mode tile_mode, int hybrid_tile_size, bool ascii_square, bool ascii_enhanced, bool ascii_floors,
		enum sdl3_dock_placement dock_placement, int dock_rows, int dock_cols,
		const struct sdl3_audio_settings *audio,
		const struct player_options *game_options, bool playing)
{
	char ambient[16];
	char creature[16];
	char gameplay[16];
	char interface_volume[16];
	char master[16];
	char music[16];
	char dock_size[24];
	char zoom[24];
	struct sdl3_settings_layout layout = sdl3_settings_layout(overlay->page,
		sdl3_settings_appearance_mode(overlay), visual->cols, visual->rows);
	const struct sdl3_menu_layout *menu = &layout.menu;

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
	case SDL3_SETTINGS_CONTROLS:
		if (!game_options) break;
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_EXPLORE, "Explore mode (p)",
			!game_options->opt[OPT_autoexplore_commands] ? "Off" :
			game_options->opt[OPT_autoexplore_continuous] ?
			"Continuous (Ammerow)" : "Traditional (Angband)");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_AUTO_STAIRS, "Walk to stairs (< / >)",
			game_options->opt[OPT_auto_stairs] ? "On" : "Off");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_PICKUP_MATCHING, "Pick up matching items",
			game_options->opt[OPT_pickup_inven] ? "On" : "Off");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_PICKUP_ALL, "Pick up all items",
			game_options->opt[OPT_pickup_always] ? "On (except ignored)" : "Off");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_ITEM_RULES, "Item ignoring...", playing ? "Open" : "During a run");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_ALL_OPTIONS, "All gameplay options", playing ? "Open (= in game)" : "During a run");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_SAVE_GAME_DEFAULTS, "Save gameplay defaults", "For future characters");
		sdl3_ui_draw_text(visual,
			"p explores; < / > walk to stairs. Surface < opens the world map.",
			left + 4, layout.note_row, width - 6, theme->muted);
		sdl3_ui_draw_text(visual,
			"Continuous stops for monsters/new loot. Combat stays manual.",
			left + 4, layout.note_row + 1, width - 6, theme->muted);
		sdl3_ui_draw_text(visual,
			playing ? "Changes affect this run. Save defaults for future runs." :
			"Before starting a run, save your choices as defaults.",
			left + 4, layout.note_row + 2, width - 6, theme->muted);
		break;
	case SDL3_SETTINGS_APPEARANCE:
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_THEME, "Interface theme", theme->name);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_MAP_FONT, "Map font (game)",
			map_font_name && map_font_name[0] ? map_font_name :
			"Fallback glyphs");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_TILE_MODE, "Map presentation",
			sdl3_tile_mode_name(tile_mode));
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_HYBRID_DETAIL,
			tile_mode == SDL3_TILE_MODE_FULL ? "Full Tile detail" : "Hybrid detail",
			hybrid_tile_size == 32 ? "32 x 32" : "64 x 64");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_TERRAIN, "ASCII terrain",
			sdl3_terrain_style_name(terrain_style));
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_ASCII_GRID, "ASCII grid",
			ascii_square ? "Square" : "Classic");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_ASCII_ENHANCED, "  Enhanced Square",
			ascii_enhanced ? "On" : "Off");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_ASCII_FLOORS, "  Keep floors ASCII",
			ascii_floors ? "On" : "Off");
		sdl3_ui_draw_text(visual,
			overlay->selected_row == SDL3_SETTINGS_ASCII_ENHANCED ?
			"Widen wall glyphs by 20%; other glyphs stay unchanged." :
			tile_mode == SDL3_TILE_MODE_FULL ?
			"ASCII floors: ground only. Water and features stay tiled." :
			tile_mode == SDL3_TILE_MODE_HYBRID ?
			"Hybrid: sprites on ASCII terrain. ASCII grid applies in ASCII mode." :
			"Classic uses rectangular cells; Square keeps similar cell area.",
			left + 4, layout.note_row, width - 6, theme->muted);
		break;
	case SDL3_SETTINGS_INTERFACE:
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_INTERFACE_FONT, "Interface font (menus)",
			interface_font_name && interface_font_name[0] ?
			interface_font_name : "Fallback glyphs");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_INTERFACE_DENSITY, "Interface size",
			sdl3_interface_density_name(interface_density));
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_BIG_STAT_CARDS, "Big stat cards",
			big_stat_cards ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_DOCK_VISIBLE, "Messages",
			dock_visible ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_DOCK_PLACEMENT, "Message position",
			sdl3_dock_placement_name(dock_placement));
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_DOCK_SIZE, "Message history", dock_size);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_HUD_STATS, "Player stats",
			hud_stats_visible ? "Visible" : "Hidden");
		sdl3_ui_draw_text(visual,
			"Ctrl+1 top msg   Ctrl+2 stats   Ctrl+3 bottom msg",
			left + 4, layout.note_row, width - 6, theme->muted);
		break;
	case SDL3_SETTINGS_SOUND:
		if (!audio) break;
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_AUDIO_ENABLED, "Sound",
			audio->enabled ? "Enabled" : "Muted");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_MUSIC_ENABLED, "Home music",
			audio->music_enabled ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_MOVEMENT, "Movement FX",
			audio->movement_enabled ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_MASTER, "Master", master);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_MUSIC, "Music", music);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_INTERFACE, "Interface",
			interface_volume);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_GAMEPLAY, "Gameplay", gameplay);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_CREATURE, "Creatures", creature);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_AUDIO_AMBIENT, "Ambient", ambient);
		break;
	case SDL3_SETTINGS_DISPLAY:
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_FULLSCREEN, "Display",
			fullscreen ? "Desktop fullscreen" : "Windowed");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_ZOOM, "Map zoom", zoom);
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_ANIMATED_COMBAT, "Animated combat",
			animated_combat ? "Enabled" : "Disabled");
		draw_setting_row(overlay, renderer, visual, theme, menu, SDL3_SETTINGS_WEATHER, "Visible weather",
			weather_mode == SDL3_WEATHER_OFF ? "Off" : "On");
		draw_setting_row(overlay, renderer, visual, theme, menu,
			SDL3_SETTINGS_EDGE_WARNINGS, "Warning border",
			edge_warnings ? "Subtle (static)" : "Off");
		sdl3_ui_draw_text(visual,
			"Map zoom affects only the play area; interface size is separate.",
			left + 4, layout.note_row, width - 6, theme->muted);
		sdl3_ui_draw_text(visual,
			"Weather is automatic. Hide it here; mute audio under Sound.",
			left + 4, layout.note_row + 1, width - 6, theme->muted);
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
		bool animated_combat, bool edge_warnings, enum sdl3_tile_mode tile_mode, int hybrid_tile_size, bool ascii_square, bool ascii_enhanced, bool ascii_floors,
		enum sdl3_dock_placement dock_placement, int dock_rows, int dock_cols,
		const struct sdl3_audio_settings *audio,
		const struct player_options *game_options, bool playing)
{
	int width;
	int left;
	struct sdl3_settings_layout layout;

	if (!overlay || !overlay->visible || !renderer || !visual || !theme) {
		return;
	}
	sdl3_ui_fill_output(renderer, visual,
		(SDL_Color){ 0, 0, 0, SDL_ALPHA_OPAQUE });
	layout = sdl3_settings_layout(overlay->page, sdl3_settings_appearance_mode(overlay),
		visual->cols, visual->rows);
	width = layout.menu.width;
	left = layout.menu.col;
	sdl3_ui_draw_text(visual, "SETTINGS", left, 3, width, theme->title);
	sdl3_ui_draw_text(visual, overlay->page == SDL3_SETTINGS_HUB ?
		"Choose a category" : page_title(overlay->page), left, 5, width,
		theme->accent);
	if (overlay->page == SDL3_SETTINGS_HUB) {
		draw_hub(overlay, renderer, visual, theme, left, width);
		sdl3_ui_draw_text(visual,
			"Click or Enter opens   Escape closes   F1 / ? field guide", left,
			layout.footer_row, width, theme->muted);
	} else {
		draw_page(overlay, renderer, visual, theme, left, width,
			interface_font_name, map_font_name, interface_density,
			zoom_percent, terrain_style, weather_mode, fullscreen, dock_visible,
			hud_stats_visible, big_stat_cards, animated_combat, edge_warnings, tile_mode, hybrid_tile_size, ascii_square, ascii_enhanced, ascii_floors,
			dock_placement,
			dock_rows, dock_cols, audio, game_options, playing);
		sdl3_ui_draw_text(visual,
			"Click/Enter changes   Arrows select/change   Esc back   F1/? guide",
			left, layout.footer_row, width, theme->muted);
	}
}
