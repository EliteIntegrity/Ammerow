/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* Exercise the real map adapter, including synchronous death-message frames.
 * Synthetic sprites keep the tests independent of separately licensed art. */
#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "game-event.h"
#include "game-world.h"
#include "init.h"
#include "mon-make.h"
#include "mon-util.h"
#include "player.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "world-spelunking-runtime.h"
#include "world-entry.h"
#include "world-overworld.h"
#include "target.h"
#include "trap.h"
#include "ui-map.h"
#include "ui-prefs.h"
#include "sdl3/map-presenter.h"
#include "sdl3/tiles.h"
#include "sdl3/zoom.h"

struct fixture {
	struct sdl3_map_presenter presenter;
	struct sdl3_map_presenter_options options;
	struct sdl3_pane_layout pane;
	struct sdl3_grid grid;
	term main_term;
	struct loc subject_grid;
	struct sdl3_tile_registry registry;
	SDL_Surface *surface;
	SDL_Renderer *renderer;
	bool saw_dying;
	bool retained_sprite;
};

static void configure(struct fixture *f)
{
	sdl3_map_presenter_configure(&f->presenter, &f->grid, &f->pane,
		&f->main_term, &f->options);
}

static const struct sdl3_map_art_subject *subject_at(struct fixture *f)
{
	const struct sdl3_map_view *view = &f->presenter.view;
	int col = f->subject_grid.x - view->dungeon_col + view->source_col;
	int row = f->subject_grid.y - view->dungeon_row + view->source_row;
	int i;
	for (i = 0; i < view->art_subject_count; i++) {
		const struct sdl3_map_art_subject *subject = &view->art_subjects[i];
		if (subject->col == col && subject->row == row &&
				subject->priority == SDL3_MAP_ART_MONSTER) return subject;
	}
	return NULL;
}

/* The terminal's normal map-event path dirties its cell grid.  Model that
 * invalidation without opening a window or installing unrelated UI handlers. */
static void map_changed(game_event_type type, game_event_data *data, void *user)
{
	struct fixture *f = user;
	(void)type;
	(void)data;
	sdl3_grid_mark_all_dirty(&f->grid);
}

static bool draw_subject(struct fixture *f)
{
	const struct sdl3_map_art_subject *subject = subject_at(f);
	const struct sdl3_cell *cell;
	SDL_FRect rect = { 0, 0, 64, 64 };
	Uint8 r, g, b, a;
	if (!subject) return false;
	cell = sdl3_grid_cell(f->presenter.view.source, subject->col, subject->row);
	SDL_SetRenderDrawColor(f->renderer, 0, 0, 0, 255);
	SDL_RenderClear(f->renderer);
	if (!sdl3_tiles_draw_subject(&f->registry, f->renderer, subject, cell,
			subject->col, subject->row, &rect, 255)) return false;
	SDL_RenderPresent(f->renderer);
	return SDL_ReadSurfacePixel(f->surface, 32, 32, &r, &g, &b, &a) &&
		r == 0 && g == 255 && b == 0;
}

static void death_message(game_event_type type, game_event_data *data, void *user)
{
	struct fixture *f = user;
	struct monster *mon = square_monster(cave, f->subject_grid);
	(void)type;
	(void)data;
	if (!mon || mon->hp >= 0) return;
	f->saw_dying = true;
	configure(f);
	f->retained_sprite &= draw_subject(f);
}

int setup_tests(void **state)
{
	struct fixture *f = mem_zalloc(sizeof(*f));
	char test_user[1024];
	int x, y;
	*state = f;
	set_file_paths();
	path_build(test_user, sizeof(test_user), ANGBAND_DIR_USER, "map-presenter-test");
	if (!dir_create(test_user)) return 1;
	string_free(ANGBAND_DIR_USER);
	ANGBAND_DIR_USER = string_make(test_user);
	init_angband();
	textui_prefs_init();
	if (!player_make_simple("Quarryhand", "Warrior", "Map Tester")) return 1;
	cave = t_build_arena(80, 120);
	player->cave = cave_new(80, 120);
	for (y = 1; y < 79; y++) {
		for (x = 1; x < 119; x++) {
			struct loc grid = loc(x, y);
			square_set_feat(player->cave, grid, FEAT_FLOOR);
			sqinfo_on(square(cave, grid)->info, SQUARE_SEEN);
			sqinfo_on(square(cave, grid)->info, SQUARE_VIEW);
			sqinfo_on(square(cave, grid)->info, SQUARE_GLOW);
		}
	}
	player->grid = loc(50, 30);
	square_set_mon(cave, player->grid, -1);
	character_dungeon = true;
	term_init(&f->main_term, 100, 40, 128);
	f->main_term.sidebar_mode = SIDEBAR_LEFT;
	f->main_term.offset_x = 20;
	f->main_term.offset_y = 10;
	term_screen = angband_term[0] = &f->main_term;
	Term_activate(&f->main_term);
	tile_width = tile_height = 1;
	f->pane.cols = 100;
	f->pane.rows = 40;
	f->options.zoom_percent = 400;
	f->options.terrain_style = SDL3_TERRAIN_CLASSIC;
	f->options.hud_stats_visible = true;
	f->options.messages_visible = true;
	f->options.message_rows = 6;
	if (!sdl3_grid_init(&f->grid, 100, 40)) return 1;
	event_add_handler(EVENT_MAP, map_changed, f);
	return 0;
}

int teardown_tests(void *state)
{
	struct fixture *f = state;
	event_remove_handler(EVENT_MAP, map_changed, f);
	sdl3_map_presenter_free(&f->presenter);
	sdl3_grid_free(&f->grid);
	term_nuke(&f->main_term);
	term_screen = angband_term[0] = NULL;
	textui_prefs_free();
	cleanup_angband();
	mem_free(f);
	return 0;
}

static void move_player(struct fixture *f, struct loc grid)
{
	square_set_mon(cave, player->grid, 0);
	player->grid = grid;
	square_set_mon(cave, grid, -1);
	sdl3_grid_mark_all_dirty(&f->grid);
}

static void point_cursor(struct fixture *f, struct loc grid)
{
	f->options.cursor_col = col_map[SIDEBAR_LEFT] + grid.x - f->main_term.offset_x;
	f->options.cursor_row = row_top_map[SIDEBAR_LEFT] + grid.y - f->main_term.offset_y;
}

static int test_retained_target_follows_player(void *state)
{
	struct fixture *f = state;
	struct monster *mon = t_add_monster(cave, loc(60, 30), "giant white mouse");
	const int zooms[] = { 100, 200, 400, 800 };
	int i, tile, visible, x;
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	target_set_monster(mon);
	health_track(player->upkeep, mon);
	require(target_is_set());
	point_cursor(f, mon->grid);
	f->options.inspection_visible = false;
	for (tile = 0; tile < 2; tile++) {
		f->options.tile_mode = tile != 0;
		for (visible = 0; visible < 2; visible++) {
			f->options.cursor_visible = visible != 0;
			for (i = 0; i < (int)N_ELEMENTS(zooms); i++) {
				f->options.zoom_percent = zooms[i];
				for (x = 35; x <= 55; x += 5) {
					const struct sdl3_map_view *view = &f->presenter.view;
					move_player(f, loc(x, 30));
					configure(f);
					require(view->active && view->expanded_source);
					eq(view->dungeon_col + view->focus_col, player->grid.x);
					eq(view->dungeon_row + view->focus_row, player->grid.y);
					/* Highlight still points at the retained target. */
					eq(view->dungeon_col + view->cursor_col, mon->grid.x);
					require(!f->presenter.inspect_card.active);
				}
			}
		}
	}
	delete_monster_idx(cave, mon->midx);
	ok;
}

static int test_modal_inspection_owns_focus(void *state)
{
	struct fixture *f = state;
	struct loc cursor = loc(62, 30);
	int visible;
	move_player(f, loc(50, 30));
	point_cursor(f, cursor);
	f->options.zoom_percent = 400;
	for (visible = 0; visible < 2; visible++) {
		f->options.cursor_visible = visible != 0;
		f->options.inspection_visible = true;
		configure(f);
		eq(f->presenter.view.dungeon_col + f->presenter.view.focus_col, cursor.x);
		/* Confirmation/cancellation both release the modal ownership flag. */
		f->options.inspection_visible = false;
		configure(f);
		eq(f->presenter.view.dungeon_col + f->presenter.view.focus_col, player->grid.x);
	}
	ok;
}

static int check_death(struct fixture *f, int detail, const char *note)
{
	struct monster *mon;
	SDL_Surface *art;
	bool fear = false, killed;
	const struct sdl3_map_art_subject *subject;
	const struct sdl3_cell *cell;
	move_player(f, loc(50, 30));
	f->subject_grid = loc(51, 30);
	mon = t_add_monster(cave, f->subject_grid, "giant white mouse");
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	f->options.tile_mode = true;
	f->options.zoom_percent = 400;
	f->options.inspection_visible = false;
	/* Zero HP is still alive in the core combat rules. */
	mon->hp = 0;
	configure(f);
	subject = subject_at(f);
	notnull(subject);
	require(subject->tile_key[0]);
	f->surface = SDL_CreateSurface(64, 64, SDL_PIXELFORMAT_RGBA32);
	art = SDL_CreateSurface(detail, detail, SDL_PIXELFORMAT_RGBA32);
	require(f->surface && art);
	f->renderer = SDL_CreateSoftwareRenderer(f->surface);
	require(f->renderer);
	SDL_FillSurfaceRect(art, NULL, SDL_MapSurfaceRGBA(art, 0, 255, 0, 255));
	f->registry.atlases[0].texture = SDL_CreateTextureFromSurface(f->renderer, art);
	require(f->registry.atlases[0].texture);
	f->registry.atlases[0].width = f->registry.atlases[0].height = detail;
	f->registry.atlases[0].cell_width = f->registry.atlases[0].cell_height = detail;
	f->registry.atlas_count = f->registry.entry_count = 1;
	f->registry.loaded = true;
	my_strcpy(f->registry.entries[0].key, subject->tile_key,
		sizeof(f->registry.entries[0].key));
	require(draw_subject(f));
	/* The death message can render before delete_monster_idx(). */
	f->saw_dying = false;
	f->retained_sprite = true;
	sdl3_grid_mark_clean(&f->grid);
	event_add_handler(EVENT_MESSAGE, death_message, f);
	killed = mon_take_hit(mon, player, 1, &fear, note);
	event_remove_handler(EVENT_MESSAGE, death_message, f);
	/* Release SDL resources even when a regression assertion below fails. */
	sdl3_tiles_free(&f->registry);
	SDL_DestroySurface(art);
	SDL_DestroyRenderer(f->renderer);
	SDL_DestroySurface(f->surface);
	require(killed);
	require(f->saw_dying);
	require(f->retained_sprite);
	null(square_monster(cave, f->subject_grid));
	require(f->grid.dirty);
	configure(f);
	null(subject_at(f));
	cell = sdl3_grid_cell(f->presenter.view.source,
		f->subject_grid.x - f->presenter.view.dungeon_col,
		f->subject_grid.y - f->presenter.view.dungeon_row);
	notnull(cell);
	/* This creature drops nothing: no lingering sprite or monster glyph. */
	eq(cell->codepoint, L'.');
	ok;
}

static int test_death_32(void *state)
{
	return check_death(state, 32, NULL);
}

static int test_death_64(void *state)
{
	return check_death(state, 64, " dies.");
}

static int test_no_hidden_or_hallucinated_portraits(void *state)
{
	struct fixture *f = state;
	struct monster *mon;
	f->subject_grid = loc(51, 30);
	mon = t_add_monster(cave, f->subject_grid, "giant white mouse");
	f->options.tile_mode = true;
	mflag_off(mon->mflag, MFLAG_VISIBLE);
	sdl3_grid_mark_all_dirty(&f->grid);
	configure(f);
	null(subject_at(f));
	mflag_on(mon->mflag, MFLAG_VISIBLE);
	player->timed[TMD_IMAGE] = 1;
	configure(f);
	null(subject_at(f));
	player->timed[TMD_IMAGE] = 0;
	delete_monster_idx(cave, mon->midx);
	ok;
}

static int test_edge_warning_policy(void *state)
{
	/* Small isolated player: warning queries must not mutate game resources. */
	struct player p = { 0 };
	int16_t timed[TMD_MAX] = { 0 };
	struct world_spelunk_runtime runtime = { 0 };
	enum world_spelunk_tile cells[9] = { WORLD_SPELUNK_AIR };
	(void)state;
	p.timed = timed;
	p.mhp = p.chp = 100;
	p.resources.initialized = true;
	p.resources.stamina.maximum = p.resources.stamina.current = 100;
	p.resources.air.maximum = 6;
	p.resources.air.current = 1;
	p.spelunking = &runtime;
	runtime.state.map = (struct world_spelunk_map){ cells, 3, 3, 3 };
	runtime.state.x = runtime.state.y = 1;
	timed[TMD_FOOD] = (int16_t)PY_FOOD_FULL;
	eq(sdl3_map_player_warning(&p, false), SDL3_EDGE_WARNING_NONE);
	timed[TMD_POISONED] = 50;
	eq(sdl3_map_player_warning(&p, false), SDL3_EDGE_WARNING_NONE);
	timed[TMD_FOOD] = (int16_t)PY_FOOD_FAINT;
	eq(sdl3_map_player_warning(&p, false), SDL3_EDGE_WARNING_NONE);
	timed[TMD_FOOD]--;
	eq(sdl3_map_player_warning(&p, false), SDL3_EDGE_WARNING_HUNGER);
	p.chp = 25;
	eq(sdl3_map_player_warning(&p, false), SDL3_EDGE_WARNING_HEALTH);
	cells[4] = WORLD_SPELUNK_WATER;
	runtime.state.movement = WORLD_SPELUNK_SWIMMING;
	/* Surface air above the head prevents the air warning. */
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_HEALTH);
	cells[1] = WORLD_SPELUNK_WATER;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_AIR);
	p.chp = 10;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_HEALTH);
	p.chp = 100;
	p.resources.air.current = 6;
	p.resources.stamina.current = 20;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_STAMINA);
	/* Neither depleted resources nor a cached cave imply danger on land. */
	timed[TMD_FOOD] = (int16_t)PY_FOOD_FULL;
	eq(sdl3_map_player_warning(&p, false), SDL3_EDGE_WARNING_NONE);
	cells[1] = cells[4] = WORLD_SPELUNK_AIR;
	runtime.state.movement = WORLD_SPELUNK_STANDING;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_NONE);
	runtime.state.movement = WORLD_SPELUNK_CLIMBING;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_STAMINA);
	cells[7] = WORLD_SPELUNK_ROCK;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_NONE);
	cells[7] = WORLD_SPELUNK_AIR;
	p.resources.stamina.current = 21;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_NONE);
	p.chp = 1;
	p.is_dead = true;
	eq(sdl3_map_player_warning(&p, true), SDL3_EDGE_WARNING_NONE);
	eq(sdl3_map_player_warning(NULL, false), SDL3_EDGE_WARNING_NONE);
	ok;
}

static int test_edge_warning_toggle_and_recovery(void *state)
{
	struct fixture *f = state;
	int16_t hp = player->chp, max_hp = player->mhp;
	player->mhp = 100;
	player->chp = 20;
	f->options.edge_warnings = true;
	f->options.animated_combat = false;
	configure(f);
	eq(f->presenter.view.edge_warning, SDL3_EDGE_WARNING_HEALTH);
	f->options.edge_warnings = false;
	configure(f);
	eq(f->presenter.view.edge_warning, SDL3_EDGE_WARNING_NONE);
	f->options.edge_warnings = true;
	f->options.settings_visible = true;
	configure(f);
	eq(f->presenter.view.edge_warning, SDL3_EDGE_WARNING_NONE);
	f->options.settings_visible = false;
	player->chp = 100;
	configure(f);
	eq(f->presenter.view.edge_warning, SDL3_EDGE_WARNING_NONE);
	player->chp = hp;
	player->mhp = max_hp;
	ok;
}

static int test_full_tile_village_materials_and_gates(void *state)
{
	struct chunk *actual = cave_new(66, 198), *known = cave_new(66, 198);
	struct world_overworld_layout saved_layout = player->overworld_layout;
	struct loc edges[] = { loc(99, 0), loc(197, 33), loc(99, 65), loc(0, 33) };
	struct loc inside[] = { loc(99, 1), loc(196, 33), loc(99, 64), loc(1, 33) };
	struct loc shop = loc(50, 30), bank = loc(50, 1);
	int i;
	(void)state;
	/* Use the supported legacy layout: village at centre, all four exits. */
	require(world_overworld_layout_set(player, WORLD_OVERWORLD_VERSION, 3, 3, 9, 0));
	require(world_overworld_rebuild_routes(player));
	require(world_chunk_set_location(actual, "core.hub", false));
	require(world_chunk_set_location(known, "core.hub", false));
	square_set_feat(actual, shop, FEAT_PERM);
	square_set_feat(actual, bank, FEAT_GRANITE);
	require(streq(sdl3_terrain_full_tile_for_grid(actual, known, shop, FEAT_PERM),
		"village-masonry"));
	require(streq(sdl3_terrain_full_tile_for_grid(actual, known, bank, FEAT_GRANITE),
		"village-rock-bank"));
	require(streq(sdl3_terrain_full_tile_for_grid(actual, known, bank, FEAT_MERIDIAN_STONE),
		"meridian-marker-stone"));
	/* Unknown/mimicked cells cannot leak the underlying feature. */
	null(sdl3_terrain_full_tile_for_grid(actual, known, shop, FEAT_NONE));
	null(sdl3_terrain_full_tile_for_grid(actual, known, shop, FEAT_FLOOR));
	for (i = 0; i < 4; i++) {
		int delta;
		require(world_entry_is_edge_exit_grid(actual, inside[i]));
		for (delta = -3; delta <= 3; delta++) {
			struct loc edge = edges[i], gate = inside[i];
			if (i % 2) { edge.y += delta; gate.y += delta; }
			else { edge.x += delta; gate.x += delta; }
			square_set_feat(actual, edge, FEAT_PERM);
			square_set_feat(actual, gate, FEAT_FLOOR);
			/* Actual knowledge alone must not create a visible exit. */
			require(!streq(square_apparent_name(known, gate),
				"region crossing (step here to travel)"));
			require(streq(sdl3_terrain_full_tile_for_grid(actual, known, edge, FEAT_PERM),
				"village-rock-bank"));
			struct grid_data data = { .f_idx = FEAT_PERM };
			eq(sdl3_terrain_ascii_kind(&data,
				sdl3_terrain_full_tile_for_grid(actual, known, edge, FEAT_PERM)),
				SDL3_ASCII_TERRAIN_WALL);
			square_set_feat(known, gate, FEAT_FLOOR);
			eq(world_entry_is_edge_exit_grid(actual, gate), ABS(delta) <= 2);
			require(!world_entry_is_edge_exit_grid(actual, edge));
			eq(streq(square_apparent_name(known, gate),
				"region crossing (step here to travel)"), ABS(delta) <= 2);
			if (ABS(delta) <= 2) {
				require(streq(square_apparent_look_prefix(known, gate), "a "));
				require(streq(square_apparent_look_in_preposition(known, gate), "at "));
			}
			require(streq(sdl3_terrain_full_tile_for_grid(actual, known, edge, FEAT_PERM),
				ABS(delta) <= 2 ? "travel-opening" : "village-rock-bank"));
			eq(sdl3_terrain_ascii_kind(&data,
				sdl3_terrain_full_tile_for_grid(actual, known, edge, FEAT_PERM)),
				ABS(delta) <= 2 ? SDL3_ASCII_TERRAIN_OPENING : SDL3_ASCII_TERRAIN_WALL);
			eq(square(actual, edge)->feat, FEAT_PERM);
			eq(square(actual, gate)->feat, FEAT_FLOOR);
		}
	}
	/* The fast rejection cannot discard a trigger or mark safe arrival cells. */
	for (int y = 0; y < actual->height; y++) {
		for (int x = 0; x < actual->width; x++) {
			bool expected = ((y == 1 || y == actual->height - 2) && ABS(x - 99) <= 2) ||
				((x == 1 || x == actual->width - 2) && ABS(y - 33) <= 2);
			eq(world_entry_is_edge_exit_grid(actual, loc(x, y)), expected);
		}
	}
	require(!world_entry_is_edge_exit_grid(actual, loc(-1, 1)));
	require(!world_entry_is_edge_exit_grid(actual, loc(99, actual->height)));
	require(!world_entry_is_edge_exit_grid(NULL, loc(99, 1)));
	/* The victory profile changes colours, not the village material identity. */
	player->total_winner = true;
	require(streq(sdl3_terrain_full_tile_for_grid(actual, known, shop, FEAT_PERM),
		"village-masonry"));
	player->total_winner = false;
	actual->depth = 1;
	require(world_chunk_set_location(actual, "core.main.001", false));
	null(sdl3_terrain_full_tile_for_grid(actual, known, shop, FEAT_PERM));
	null(sdl3_terrain_full_tile_for_grid(actual, known, bank, FEAT_GRANITE));
	cave_free(known);
	cave_free(actual);
	player->overworld_layout = saved_layout;
	require(world_overworld_rebuild_routes(player));
	ok;
}

const char *suite_name = "sdl3/map-presenter";
static int test_ascii_wall_semantics(void *state)
{
	struct grid_data data = { .f_idx = FEAT_GRANITE };
	struct object_kind kind = { 0 };
	struct trap trap = { 0 };
	(void)state;
	eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_WALL);
	data.f_idx = FEAT_MAGMA;
	eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_WALL);
	data.f_idx = FEAT_PERM;
	eq(sdl3_terrain_ascii_kind(&data, "travel-opening"), SDL3_ASCII_TERRAIN_OPENING);
	/* Every foreground subject, even one using the same symbol, is excluded. */
	data.m_idx = 1;
	eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_NORMAL);
	data.m_idx = 0; data.first_kind = &kind;
	eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_NORMAL);
	data.first_kind = NULL; data.trap = &trap;
	eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_NORMAL);
	data.trap = NULL; data.hallucinate = true;
	eq(sdl3_terrain_ascii_kind(&data, "travel-opening"), SDL3_ASCII_TERRAIN_NORMAL);
	data.hallucinate = false; data.is_player = true;
	eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_NORMAL);
	data.is_player = false; data.surface = GRID_SURFACE_TREE;
	eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_NORMAL);
	data.surface = GRID_SURFACE_EXIT_NORTH;
	eq(sdl3_terrain_ascii_kind(&data, "travel-opening"), SDL3_ASCII_TERRAIN_NORMAL);
	data.surface = GRID_SURFACE_NONE;
	int unchanged[] = { FEAT_NONE, FEAT_FLOOR, FEAT_LAVA, FEAT_CLOSED,
		FEAT_OPEN, FEAT_MORE, FEAT_LESS, FEAT_RUBBLE, FEAT_PASS_RUBBLE };
	for (size_t i = 0; i < N_ELEMENTS(unchanged); i++) {
		data.f_idx = unchanged[i];
		eq(sdl3_terrain_ascii_kind(&data, NULL), SDL3_ASCII_TERRAIN_NORMAL);
	}
	ok;
}

static int test_square_source_extent(void *state)
{
	struct fixture *f = state;
	f->options.cell_width = 16;
	f->options.cell_height = 30;
	f->options.zoom_percent = 200;
	f->options.ascii_enhanced = true;
	for (int tile = 0; tile < 2; tile++) {
		int cols, rows;
		f->options.tile_mode = tile != 0;
		f->options.ascii_square = false;
		configure(f);
		cols = f->presenter.view.source_cols;
		require(!f->presenter.view.ascii_enhanced);
		rows = f->presenter.view.source_rows;
		f->options.ascii_square = true;
		configure(f);
		const struct sdl3_map_view *view = &f->presenter.view;
		eq(view->ascii_square, tile == 0);
		eq(view->ascii_enhanced, tile == 0);
		if (tile) {
			eq(view->source_cols, cols);
			eq(view->source_rows, rows);
		} else {
			eq(view->source_cols, MIN(cave->width,
				sdl3_zoom_square_visible_cells(view->cols, 16, 44)));
			eq(view->source_rows, MIN(cave->height,
				sdl3_zoom_square_visible_cells(view->rows, 30, 44)));
		}
	}
	f->options.ascii_square = false;
	f->options.ascii_enhanced = false;
	ok;
}

static int test_trap_tiles_and_shop_labels(void *state)
{
	struct trap_kind kind = { .name = "gas trap" };
	struct trap trap = { .kind = &kind };
	struct grid_data data = { .f_idx = FEAT_FLOOR, .trap = &trap };
	struct object_kind object = { 0 };
	(void)state;
	null(sdl3_terrain_trap_tile(&data));
	trf_on(trap.flags, TRF_VISIBLE);
	require(streq(sdl3_terrain_trap_tile(&data), "feature:gas-trap"));
	kind.desc = "sleep gas";
	require(streq(sdl3_terrain_trap_tile(&data), "feature:gas-trap"));
	data.hallucinate = true;
	null(sdl3_terrain_trap_tile(&data));
	data.hallucinate = false; data.is_player = true;
	null(sdl3_terrain_trap_tile(&data));
	data.is_player = false; data.m_idx = 1;
	null(sdl3_terrain_trap_tile(&data));
	data.m_idx = 0; data.first_kind = &object;
	null(sdl3_terrain_trap_tile(&data));
	data.first_kind = NULL; data.unseen_object = true;
	null(sdl3_terrain_trap_tile(&data));
	data.unseen_object = false; data.multiple_objects = true;
	null(sdl3_terrain_trap_tile(&data));
	kind.name = "web"; trf_on(trap.flags, TRF_WEB);
	require(streq(sdl3_terrain_trap_tile(&data), "feature:web"));
	data.multiple_objects = false;
	/* All existing public trap categories have bindings; door locks do not. */
	for (int i = 1; i < z_info->trap_max; i++) {
		trap.kind = &trap_info[i];
		trf_wipe(trap.flags);
		trf_on(trap.flags, TRF_VISIBLE);
		if (streq(trap.kind->name, "door lock")) {
			null(sdl3_terrain_trap_tile(&data));
		} else {
			notnull(sdl3_terrain_trap_tile(&data));
		}
	}
	for (int i = FEAT_NONE; i < FEAT_MAX; i++) {
		eq(sdl3_terrain_glyph_policy(i), tf_has(f_info[i].flags, TF_SHOP) ?
			SDL3_TILE_GLYPH_PRESERVE : SDL3_TILE_GLYPH_AUTO);
	}
	ok;
}

struct test tests[] = {
	{ "known trap categories, foreground precedence and shop labels", test_trap_tiles_and_shop_labels },
	{ "ASCII wall styling excludes subjects, floors and markers", test_ascii_wall_semantics },
	{ "square source extent and Hybrid independence", test_square_source_extent },
	{ "Full Tile village materials and known four-way gates", test_full_tile_village_materials_and_gates },
	{ "warning priority and environmental context", test_edge_warning_policy },
	{ "warning toggle, independent animation setting and recovery", test_edge_warning_toggle_and_recovery },
	{ "retained target follows player", test_retained_target_follows_player },
	{ "modal inspection owns focus", test_modal_inspection_owns_focus },
	{ "lethal melee retains 32px sprite until removal", test_death_32 },
	{ "lethal missile retains 64px sprite until removal", test_death_64 },
	{ "hidden and hallucinated subjects have no portrait", test_no_hidden_or_hallucinated_portraits },
	{ NULL, NULL }
};
