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
#include "target.h"
#include "ui-map.h"
#include "ui-prefs.h"
#include "sdl3/map-presenter.h"
#include "sdl3/tiles.h"

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

const char *suite_name = "sdl3/map-presenter";
struct test tests[] = {
	{ "retained target follows player", test_retained_target_follows_player },
	{ "modal inspection owns focus", test_modal_inspection_owns_focus },
	{ "lethal melee retains 32px sprite until removal", test_death_32 },
	{ "lethal missile retains 64px sprite until removal", test_death_64 },
	{ "hidden and hallucinated subjects have no portrait", test_no_hidden_or_hallucinated_portraits },
	{ NULL, NULL }
};
