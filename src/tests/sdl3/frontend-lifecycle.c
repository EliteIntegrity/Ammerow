/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */

/* Exercise real Home/reset and inventory controllers with real SDL3 event
 * handlers. Only rendering, audio output and settings writes are replaced;
 * this suite needs neither a visible window nor the private artwork. */
#include "unit-test.h"
#include "test-utils.h"
#include "sdl3/frontend-internal.h"
#include "cmd-core.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "savefile.h"
#include "snd-sdl3.h"
#include "ui-display.h"
#include "ui-game.h"
#include "ui-init.h"
#include "ui-input.h"
#include "ui-knowledge.h"
#include "ui-object.h"
#include "ui-output.h"
#include "ui-prefs.h"

static struct sdl3_app app;
static const keycode_t *keys;
static int key_count, key_position, home_returns, reinits, failed_loads;
static int settings_writes, audio_enters, audio_leaves;
static bool in_store;
static char baseline[1024], broken_save[1024];

bool sdl3_presenter_set_grid_size(struct sdl3_presenter *presenter,
		SDL_Renderer *renderer, int cols, int rows)
{
	(void)presenter; (void)renderer; (void)cols; (void)rows;
	return true;
}

void sdl3_map_presenter_reset(struct sdl3_map_presenter *presenter)
{
	(void)presenter;
}

bool sdl3_config_save(const struct sdl3_config *config)
{
	(void)config;
	settings_writes++;
	return true;
}

void sdl3_audio_set_in_store(bool inside)
{
	in_store = inside;
	if (inside) audio_enters++;
	else audio_leaves++;
}

void sdl3_monster_art_make_key(char *destination, size_t capacity,
		const char *art_id, const char *base_art_id)
{
	(void)destination; (void)capacity; (void)art_id; (void)base_art_id;
	quit("Unexpected shop portrait lookup in lifecycle fixture");
}

static void capture(const struct ui_screen *screen)
{
	sdl3_screen_present(&app.screen, &app.screen_parent, screen);
	sdl3_presentation_set_layer(&app.presentation,
		SDL3_PRESENTATION_SEMANTIC_SCREEN, app.screen.active);
}

static errr input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) {
		if (key_position >= key_count) quit("Unexpected lifecycle test input");
		Term_keypress(keys[key_position++], 0);
	}
	return 0;
}

static void feed(const keycode_t *sequence, int count)
{
	Term_flush();
	inkey_next = NULL;
	keys = sequence;
	key_count = count;
	key_position = 0;
}

static bool home(enum game_mode_type *mode)
{
	(void)mode;
	home_returns++;
	return false;
}

static bool load_failure(const char *filename)
{
	(void)filename;
	failed_loads++;
	return true;
}

static void reinitialize(void)
{
	reinits++;
	sdl3_frontend_reinitialize(&app);
}

/* Start each case with freshly registered handlers even if the previous case
 * failed. The early-return tests exercise the unassisted production reset
 * through play_game(), rather than calling this helper after the abort. */
static void reset_run(void)
{
	if (screen_save_depth == 0) event_signal(EVENT_LEAVE_WORLD);
	sdl3_frontend_unregister_game_handlers(&app);
	play_again = true;
	cleanup_angband();
	init_display();
	init_angband();
	sdl3_frontend_reinitialize(&app);
	textui_init();
	play_again = false;
	terms_disconnecting = 0;
	savefile[0] = '\0';
	Term_flush();
}

int setup_tests(void **state)
{
	term *terminal = &app.panes[SDL3_LAYOUT_MAIN_TERM].term;
	char path[1024];
	ang_file *file;
	*state = NULL;
	set_file_paths();
	path_build(path, sizeof(path), ANGBAND_DIR_USER, "frontend-lifecycle-test");
	if (!dir_create(path)) return 1;
	string_free(ANGBAND_DIR_USER);
	ANGBAND_DIR_USER = string_make(path);
	path_build(baseline, sizeof(baseline), path, "baseline");
	path_build(broken_save, sizeof(broken_save), path, "broken-save");
	file = file_open(broken_save, MODE_WRITE, FTYPE_SAVE);
	if (!file) return 1;
	file_putf(file, "Deliberately invalid save for the lifecycle regression.\n");
	file_close(file);

	/* Make a valid run before installing birth UI handlers. */
	init_angband();
	if (!player_make_simple("Nearlander", "Warrior", "Lifecycle Tester")) return 1;
	prepare_next_level(player);
	on_new_level();
	if (!savefile_save(baseline)) return 1;
	play_again = true;
	cleanup_angband();
	play_again = false;

	SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
	if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
	app.window = SDL_CreateWindow("Lifecycle test", 16, 16, SDL_WINDOW_HIDDEN);
	if (!app.window) return 1;
	app.config.interface_density = SDL3_INTERFACE_STANDARD;
	app.config.dock_visible = true;
	app.config.dock_rows = 4;
	app.config.dock_cols = 32;
	app.config.hud_stats_visible = true;
	if (!sdl3_grid_init(&app.grid, 100, 30) ||
			!sdl3_grid_init(&app.dock_grid, 100, 4)) return 1;
	term_init(terminal, 100, 30, 128);
	terminal->screen_hook = capture;
	terminal->xtra_hook = input;
	app.panes[SDL3_LAYOUT_MAIN_TERM].term_inited = true;
	term_screen = angband_term[0] = terminal;
	Term_activate(terminal);
	init_display();
	init_angband();
	textui_init();
	sdl3_frontend_register_game_handlers(&app);
	textui_startup_menu_hook = home;
	textui_load_failure_hook = load_failure;
	reinit_hook = reinitialize;
	return 0;
}

int teardown_tests(void *state)
{
	(void)state;
	sdl3_frontend_unregister_game_handlers(&app);
	textui_startup_menu_hook = NULL;
	textui_load_failure_hook = NULL;
	reinit_hook = NULL;
	sdl3_screen_clear(&app.screen);
	sdl3_screen_clear(&app.screen_parent);
	sdl3_grid_free(&app.grid);
	sdl3_grid_free(&app.dock_grid);
	term_nuke(Term);
	term_screen = angband_term[0] = NULL;
	Term = NULL;
	file_delete(baseline);
	file_delete(broken_save);
	textui_cleanup();
	cleanup_angband();
	SDL_DestroyWindow(app.window);
	SDL_Quit();
	return 0;
}

/* Closing any nested inventory path must clear the frontend's semantic screen
 * as well as the core modal depth. The old test fixture cleared it itself,
 * hiding the missing production EVENT_LEAVE_ITEM_SELECTOR handler. */
static int check_inventory_after_return(void)
{
	static const keycode_t simple[] = { ESCAPE };
	static const keycode_t inspect[] = { KC_ENTER, 'I', ESCAPE, ESCAPE, ESCAPE };
	static const keycode_t tabs[] = { ARROW_RIGHT, ARROW_LEFT, ESCAPE };
	static const keycode_t equipment[] = {
		ARROW_RIGHT, KC_ENTER, 'I', ESCAPE, ESCAPE, ESCAPE };
	static const keycode_t actions[] = {
		KC_ENTER, ESCAPE, ARROW_DOWN, KC_ENTER, ESCAPE, ESCAPE };
	static const struct { const keycode_t *keys; int count; } cases[] = {
		{ simple, N_ELEMENTS(simple) }, { inspect, N_ELEMENTS(inspect) },
		{ tabs, N_ELEMENTS(tabs) }, { equipment, N_ELEMENTS(equipment) },
		{ actions, N_ELEMENTS(actions) }
	};
	int i, before;
	terms_disconnecting = 0;
	require(savefile_load(baseline, false));
	before = settings_writes;
	event_signal(EVENT_LEAVE_INIT);
	event_signal(EVENT_ENTER_WORLD);
	eq(settings_writes, before + 1);
	calc_inventory(player);
	eq(screen_save_depth, 0);
	for (i = 0; i < (int)N_ELEMENTS(cases); i++) {
		feed(cases[i].keys, cases[i].count);
		do_cmd_inven();
		eq(key_position, key_count);
		eq(screen_save_depth, 0);
		require(!app.screen.active);
		require(!app.screen_parent.active);
		eq(app.presentation.layers, 0);
		require(app.dock_active);
	}
	event_signal(EVENT_LEAVE_WORLD);
	eq(screen_save_depth, 1);
	return 0;
}

static int test_cancelled_birth(void *state)
{
	static const keycode_t cancel[] = { ESCAPE };
	int before = reinits, returns = home_returns;
	(void)state;
	reset_run();
	feed(cancel, N_ELEMENTS(cancel));
	play_game(GAME_NEW);
	eq(key_position, 1);
	eq(reinits, before + 1);
	eq(home_returns, returns + 1);
	require(check_inventory_after_return() == 0);
	ok;
}

static int test_failed_load(void *state)
{
	int before = reinits, returns = home_returns, failures = failed_loads;
	(void)state;
	reset_run();
	my_strcpy(savefile, broken_save, sizeof(savefile));
	play_game(GAME_LOAD);
	eq(failed_loads, failures + 1);
	eq(reinits, before + 1);
	eq(home_returns, returns + 1);
	require(check_inventory_after_return() == 0);
	ok;
}

static int test_startup_and_repeated_reinit(void *state)
{
	int i, before;
	(void)state;
	reset_run();
	/* No event-registry clear between these calls: catch duplicate installs. */
	for (i = 0; i < 3; i++) sdl3_frontend_reinitialize(&app);
	for (i = 0; i < 3; i++) {
		before = audio_enters;
		event_signal(EVENT_ENTER_STORE);
		eq(audio_enters, before + 1);
		require(in_store);
		event_remove_handler_type(EVENT_ENTER_STORE);
		before = audio_leaves;
		event_signal(EVENT_LEAVE_STORE);
		eq(audio_leaves, before + 1);
		require(!in_store);
		event_remove_handler_type(EVENT_LEAVE_STORE);
		event_signal(EVENT_NEW_LEVEL_DISPLAY);
	}
	require(check_inventory_after_return() == 0);
	ok;
}

static int test_completed_run_reset(void *state)
{
	(void)state;
	reset_run();
	event_signal(EVENT_LEAVE_GAME);
	reset_run();
	require(check_inventory_after_return() == 0);
	ok;
}

const char *suite_name = "sdl3/frontend-lifecycle";
struct test tests[] = {
	{ "cancelled-birth", test_cancelled_birth },
	{ "failed-load", test_failed_load },
	{ "startup-and-repeated-reinit", test_startup_and_repeated_reinit },
	{ "completed-run-reset", test_completed_run_reset },
	{ NULL, NULL }
};
