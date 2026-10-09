/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/config.c */
/* Exercise SDL3 settings migration, persistence, and zoom bounds. */

#include "unit-test.h"

#include "init.h"
#include "sdl3/config.h"
#include "sdl3/zoom.h"
#include "test-utils.h"

#include <math.h>

static char test_path[SDL3_CONFIG_PATH_CAPACITY];

static void remove_test_files(void)
{
	char alternate[SDL3_CONFIG_PATH_CAPACITY + 8];

	if (file_exists(test_path)) file_delete(test_path);
	strnfmt(alternate, sizeof(alternate), "%s.new", test_path);
	if (file_exists(alternate)) file_delete(alternate);
	strnfmt(alternate, sizeof(alternate), "%s.old", test_path);
	if (file_exists(alternate)) file_delete(alternate);
}

static bool write_test_config(const char *text)
{
	ang_file *file = file_open(test_path, MODE_WRITE, FTYPE_TEXT);
	bool wrote;

	if (!file) return false;
	wrote = file_putf(file, "%s", text);
	return file_close(file) && wrote;
}

static void prepare_config(struct sdl3_config *config)
{
	sdl3_config_init(config);
	my_strcpy(config->file_path, test_path, sizeof(config->file_path));
}

int setup_tests(void **data)
{
	(void)data;
	set_file_paths();
	path_build(test_path, sizeof(test_path), ANGBAND_DIR_USER,
		"sdl3-config-unittest.txt");
	remove_test_files();
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	remove_test_files();
	return 0;
}

static int test_zoom_bounds(void *state)
{
	(void)state;
	eq(sdl3_zoom_clamp(10), SDL3_ZOOM_MIN);
	eq(sdl3_zoom_clamp(250), 250);
	eq(sdl3_zoom_clamp(900), SDL3_ZOOM_MAX);
	eq(sdl3_zoom_change(SDL3_ZOOM_MAX, 1), SDL3_ZOOM_MAX);
	eq(sdl3_zoom_change(SDL3_ZOOM_MIN, -1), SDL3_ZOOM_MIN);
	eq(sdl3_zoom_change(150, 1), 200);
	eq(sdl3_zoom_change(150, -1), 125);
	eq(sdl3_zoom_change(175, 1), 200);
	eq(sdl3_zoom_change(175, -1), 150);
	eq(sdl3_zoom_change(100, -1), 80);
	eq(sdl3_zoom_change(80, -1), 67);
	eq(sdl3_zoom_change(67, -1), 50);
	eq(sdl3_zoom_change(50, 1), 67);
	ok;
}

static int test_fullscreen_default_and_saved_choice(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	remove_test_files();
	prepare_config(&config);
	require(config.fullscreen);
	require(!sdl3_config_load(&config));
	require(config.fullscreen);
	/* Existing windowed preferences, including old configs, still win. */
	require(write_test_config("version=3\nfullscreen=0\n"));
	require(sdl3_config_load(&config));
	require(!config.fullscreen);
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(!loaded.fullscreen);
	/* An absent or invalid setting retains the new default. */
	require(write_test_config("version=19\nfullscreen=invalid\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(loaded.fullscreen);
	loaded.fullscreen = true;
	require(sdl3_config_save(&loaded));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(config.fullscreen);
	ok;
}

static int test_version_three_migration(void *state)
{
	struct sdl3_config config;
	(void)state;

	require(write_test_config(
		"version=3\n"
		"window_width=1200\n"
		"window_height=800\n"
		"fullscreen=0\n"
		"dock_visible=1\n"
		"dock_rows=7\n"
		"font=IBMPlexMono-Regular.ttf\n"
		"theme=ember\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.window_width, 1200);
	eq(config.dock_rows, 7);
	eq(config.dock_cols, SDL3_LAYOUT_DEFAULT_DOCK_COLS);
	eq(config.dock_placement, SDL3_DOCK_BOTTOM);
	eq(config.map_zoom_percent, SDL3_ZOOM_DEFAULT);
	require(streq(config.map_font, "IBMPlexMono-Regular.ttf"));
	ok;
}

static int test_font_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config(
		"version=8\n"
		"font=IBMPlexMono-Regular.ttf\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(streq(config.font, "IBMPlexMono-Regular.ttf"));
	require(streq(config.map_font, "IBMPlexMono-Regular.ttf"));

	my_strcpy(config.font, "RobotoMono-SemiBold.ttf", sizeof(config.font));
	my_strcpy(config.map_font, "CozetteVector.ttf",
		sizeof(config.map_font));
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(streq(loaded.font, "RobotoMono-SemiBold.ttf"));
	require(streq(loaded.map_font, "CozetteVector.ttf"));
	require(!loaded.last_save[0]);
	ok;
}

static int test_last_save_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=15\ntheme=midnight\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(!config.last_save[0]);
	my_strcpy(config.last_save, "Ranger", sizeof(config.last_save));
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(streq(loaded.last_save, "Ranger"));
	ok;
}

static int test_zoom_camera_mapping(void *state)
{
	float center;
	(void)state;

	center = sdl3_zoom_camera_center(13, 66, 45, 2112.0f, 128.0f);
	require(fabsf(center - 45.5f) < 0.001f);
	require(fabsf(sdl3_zoom_camera_center(13, 66, 13, 2112.0f,
		128.0f) - 21.25f) < 0.001f);
	require(fabsf(sdl3_zoom_camera_center(13, 66, 78, 2112.0f,
		128.0f) - 70.75f) < 0.001f);
	eq(sdl3_zoom_cell_from_pixel(1472.0f, 416.0f, 2112.0f, 128.0f,
		center), 45);
	eq(sdl3_zoom_cell_from_pixel(1408.0f, 416.0f, 2112.0f, 128.0f,
		center), 45);
	eq(sdl3_zoom_cell_from_pixel(1536.0f, 416.0f, 2112.0f, 128.0f,
		center), 46);
	ok;
}

static int test_zoom_camera_round_trip(void *state)
{
	static const int viewport_widths[] = { 640, 1280, 2112 };
	static const int zoom_levels[] = { 50, 67, 80, 125, 200, 400, 800 };
	static const int focus_cells[] = { 13, 45, 78 };
	int viewport_index;
	int zoom_index;
	int focus_index;
	(void)state;

	for (viewport_index = 0;
			viewport_index < (int)N_ELEMENTS(viewport_widths);
			viewport_index++) {
		for (zoom_index = 0; zoom_index < (int)N_ELEMENTS(zoom_levels);
				zoom_index++) {
			float cell_width = (32.0f * zoom_levels[zoom_index]) / 100.0f;

			for (focus_index = 0;
					focus_index < (int)N_ELEMENTS(focus_cells);
					focus_index++) {
				float center = sdl3_zoom_camera_center(13, 66,
					focus_cells[focus_index],
					(float)viewport_widths[viewport_index], cell_width);
				int col;

				for (col = 13; col < 79; col++) {
					float pixel = 416.0f + viewport_widths[viewport_index] *
						0.5f + (col - center + 0.5f) * cell_width;

					if (pixel < 416.0f || pixel >= 416.0f +
							viewport_widths[viewport_index]) {
						continue;
					}
					eq(sdl3_zoom_cell_from_pixel(pixel, 416.0f,
						(float)viewport_widths[viewport_index], cell_width,
						center), col);
				}
			}
		}
	}
	ok;
}

static int test_zoom_panel_shift_stability(void *state)
{
	const int first_col = 13;
	const int screen_cols = 66;
	const int player_x = 100;
	const int target_x = player_x + 2;
	const int margin = sdl3_zoom_panel_margin(screen_cols, 400);
	const int old_offset = player_x - (screen_cols - margin);
	const int new_offset = player_x - screen_cols / 2;
	const float viewport_width = 2112.0f;
	const float cell_width = 128.0f;
	float old_focus;
	float new_focus;
	float old_target_offset;
	float new_target_offset;
	(void)state;

	eq(sdl3_zoom_panel_margin(screen_cols, 100), 3);
	eq(sdl3_zoom_panel_margin(screen_cols, 125), 27);
	eq(sdl3_zoom_panel_margin(screen_cols, 200), 17);
	eq(margin, 9);
	eq(sdl3_zoom_panel_margin(screen_cols, 800), 5);
	eq(sdl3_zoom_panel_margin(22, 400), 4);

	old_focus = sdl3_zoom_camera_center(first_col, screen_cols,
		first_col + player_x - old_offset, viewport_width, cell_width);
	new_focus = sdl3_zoom_camera_center(first_col, screen_cols,
		first_col + player_x - new_offset, viewport_width, cell_width);
	old_target_offset = first_col + target_x - old_offset - old_focus;
	new_target_offset = first_col + target_x - new_offset - new_focus;
	require(fabsf(old_target_offset - new_target_offset) < 0.001f);
	ok;
}

static int test_zoom_validation(void *state)
{
	struct sdl3_config config;
	(void)state;

	require(write_test_config("version=5\nmap_zoom_percent=400\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.map_zoom_percent, 400);
	require(write_test_config("version=9\nmap_zoom_percent=50\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.map_zoom_percent, 50);

	require(write_test_config("version=5\nmap_zoom_percent=801\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.map_zoom_percent, SDL3_ZOOM_DEFAULT);

	require(write_test_config("version=4\nzoom_percent=70\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.map_zoom_percent, SDL3_ZOOM_DEFAULT);
	ok;
}

static int test_zoom_round_trip(void *state)
{
	struct sdl3_config saved;
	struct sdl3_config loaded;
	(void)state;

	prepare_config(&saved);
	saved.map_zoom_percent = 600;
	require(sdl3_config_save(&saved));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.map_zoom_percent, 600);
	ok;
}

static int test_audio_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=5\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(config.audio.enabled);
	require(config.audio.movement_enabled);
	require(config.audio.music_enabled);
	eq(config.audio.master_volume, 70);
	eq(config.audio.ambient_volume, 0);
	eq(config.audio.music_volume, 50);
	eq(config.audio.shop_ambient_percent, 25);
	require(write_test_config("version=22\naudio_shop_ambient_percent=101\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.audio.shop_ambient_percent, 25);
	require(write_test_config("version=22\naudio_shop_ambient_percent=-1\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.audio.shop_ambient_percent, 25);

	/* Version 6 briefly shipped the sci-fi ambience at 35 percent. */
	require(write_test_config("version=6\naudio_ambient=35\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.audio.ambient_volume, 0);

	config.audio.enabled = false;
	config.audio.movement_enabled = false;
	config.audio.music_enabled = false;
	config.audio.master_volume = 80;
	config.audio.interface_volume = 50;
	config.audio.gameplay_volume = 60;
	config.audio.creature_volume = 40;
	config.audio.ambient_volume = 20;
	config.audio.shop_ambient_percent = 15;
	config.audio.music_volume = 30;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(!loaded.audio.enabled);
	require(!loaded.audio.movement_enabled);
	require(!loaded.audio.music_enabled);
	eq(loaded.audio.master_volume, 80);
	eq(loaded.audio.interface_volume, 50);
	eq(loaded.audio.gameplay_volume, 60);
	eq(loaded.audio.creature_volume, 40);
	eq(loaded.audio.ambient_volume, 20);
	eq(loaded.audio.shop_ambient_percent, 15);
	eq(loaded.audio.music_volume, 30);
	ok;
}

static int test_dock_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=7\ndock_rows=9\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.dock_rows, 9);
	eq(config.dock_cols, SDL3_LAYOUT_DEFAULT_DOCK_COLS);
	eq(config.dock_placement, SDL3_DOCK_BOTTOM);

	/* Value one became an opaque left subwindow in version 14.  Version 15
	 * restores it as independent player-stat visibility while keeping the
	 * single message history at the bottom. */
	require(write_test_config(
		"version=13\n"
		"dock_visible=0\n"
		"dock_placement=1\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.dock_placement, SDL3_DOCK_BOTTOM);
	require(!config.hud_stats_visible);

	config.dock_placement = SDL3_DOCK_TOP;
	config.dock_visible = true;
	config.hud_stats_visible = true;
	config.dock_rows = 9;
	config.dock_cols = 41;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.dock_rows, 9);
	eq(loaded.dock_cols, 41);
	eq(loaded.dock_placement, SDL3_DOCK_TOP);
	require(loaded.dock_visible);
	require(loaded.hud_stats_visible);
	ok;
}

static int test_top_right_message_setting(void *state)
{
	struct sdl3_config config, loaded;
	(void)state;

	/* This appends a value without changing the settings format or defaults. */
	require(write_test_config("version=21\ndock_placement=3\ndock_rows=4\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.dock_placement, SDL3_DOCK_TOP_RIGHT);
	eq(config.dock_rows, 4);
	config.dock_visible = false;
	config.hud_stats_visible = false;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.dock_placement, SDL3_DOCK_TOP_RIGHT);
	eq(loaded.dock_rows, 4);
	require(!loaded.dock_visible);
	require(!loaded.hud_stats_visible);

	require(write_test_config("version=21\ndock_placement=99\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.dock_placement, SDL3_DOCK_BOTTOM);
	ok;
}

static int test_terrain_style_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=9\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.terrain_style, SDL3_TERRAIN_NATURAL);

	require(write_test_config(
		"version=11\n"
		"terrain_decoration=0\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.terrain_style, SDL3_TERRAIN_CLASSIC);

	config.terrain_style = SDL3_TERRAIN_NATURAL;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.terrain_style, SDL3_TERRAIN_NATURAL);

	require(write_test_config(
		"version=12\n"
		"terrain_style=2\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.terrain_style, SDL3_TERRAIN_NATURAL);

	require(write_test_config(
		"version=12\n"
		"terrain_style=99\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.terrain_style, SDL3_TERRAIN_NATURAL);
	ok;
}

static int test_weather_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=12\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.weather_mode, SDL3_WEATHER_AUTO);

	config.weather_mode = SDL3_WEATHER_SNOW;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.weather_mode, SDL3_WEATHER_AUTO);
	config.weather_mode = SDL3_WEATHER_OFF;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.weather_mode, SDL3_WEATHER_OFF);

	require(write_test_config(
		"version=13\n"
		"weather_mode=99\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.weather_mode, SDL3_WEATHER_AUTO);
	ok;
}

static int test_interface_density_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=10\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	eq(config.interface_density, SDL3_INTERFACE_DENSITY_DEFAULT);

	config.interface_density = SDL3_INTERFACE_COMPACT;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.interface_density, SDL3_INTERFACE_COMPACT);

	require(write_test_config(
		"version=11\n"
		"interface_density=99\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.interface_density, SDL3_INTERFACE_DENSITY_DEFAULT);
	ok;
}

static int test_big_stat_cards_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=16\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(config.big_stat_cards);

	config.big_stat_cards = false;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(!loaded.big_stat_cards);

	require(write_test_config(
		"version=17\n"
		"big_stat_cards=2\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(loaded.big_stat_cards);
	ok;
}

static int test_animated_combat_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=17\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(config.animated_combat);

	config.animated_combat = false;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(!loaded.animated_combat);

	require(write_test_config(
		"version=18\n"
		"animated_combat=2\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(loaded.animated_combat);
	ok;
}

static int test_tile_mode_migration_and_round_trip(void *state)
{
	struct sdl3_config config;
	struct sdl3_config loaded;
	(void)state;

	require(write_test_config("version=18\n"));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(!config.tile_mode);
	eq(config.hybrid_tile_size, 64);

	config.tile_mode = true;
	config.hybrid_tile_size = 32;
	require(sdl3_config_save(&config));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(loaded.tile_mode);
	eq(loaded.hybrid_tile_size, 32);

	/* The parked experimental key is ignored; normal settings must survive. */
	require(write_test_config(
		"version=21\ntile_mode=1\ntile_art_size=64\nfullscreen=0\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(loaded.tile_mode);
	require(!loaded.fullscreen);
	eq(loaded.hybrid_tile_size, 64);
	require(sdl3_config_save(&loaded));
	prepare_config(&config);
	require(sdl3_config_load(&config));
	require(config.tile_mode);
	require(!config.fullscreen);

	require(write_test_config(
		"version=19\n"
		"tile_mode=2\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	require(!loaded.tile_mode);
	require(write_test_config("version=22\ntile_mode=1\nhybrid_tile_size=48\n"));
	prepare_config(&loaded);
	require(sdl3_config_load(&loaded));
	eq(loaded.hybrid_tile_size, 64);
	require(loaded.tile_mode);
	ok;
}

const char *suite_name = "sdl3/config";
struct test tests[] = {
	{ "fullscreen default and saved choice",
		test_fullscreen_default_and_saved_choice },
	{ "zoom bounds", test_zoom_bounds },
	{ "zoom camera mapping", test_zoom_camera_mapping },
	{ "zoom camera round trip", test_zoom_camera_round_trip },
	{ "zoom panel shift stability", test_zoom_panel_shift_stability },
	{ "version 3 migration", test_version_three_migration },
	{ "font migration and round trip", test_font_migration_and_round_trip },
	{ "last save migration and round trip",
		test_last_save_migration_and_round_trip },
	{ "zoom validation", test_zoom_validation },
	{ "zoom round trip", test_zoom_round_trip },
	{ "audio migration and round trip", test_audio_migration_and_round_trip },
	{ "dock migration and round trip", test_dock_migration_and_round_trip },
	{ "top right message setting", test_top_right_message_setting },
	{ "terrain style migration and round trip",
		test_terrain_style_migration_and_round_trip },
	{ "weather migration and round trip",
		test_weather_migration_and_round_trip },
	{ "interface density migration and round trip",
		test_interface_density_migration_and_round_trip },
	{ "big stat cards migration and round trip",
		test_big_stat_cards_migration_and_round_trip },
	{ "animated combat migration and round trip",
		test_animated_combat_migration_and_round_trip },
	{ "tile mode migration and round trip",
		test_tile_mode_migration_and_round_trip },
	{ NULL, NULL },
};
