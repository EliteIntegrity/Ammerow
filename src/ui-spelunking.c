/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-spelunking.c
 * \brief Native terminal presentation for side-view spelunking.
 *
 *
 */

#include "angband.h"

#include "game-world.h"
#include "obj-desc.h"
#include "player-resource.h"
#include "ui-input.h"
#include "ui-spelunking.h"
#include "ui-spelunking-appearance.h"
#include "ui-target.h"
#include "ui-term.h"
#include "world-fishing-site.h"
#include "world-fishing.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-breath.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-system.h"
#include "world-spelunking-view.h"

/* Reused storage keeps every redraw allocation-free on old hardware. */
static uint8_t view_cells[WORLD_SPELUNK_CELL_MAX];

struct spelunking_look_state {
	const struct world_spelunk_runtime *runtime;
	bool active;
	int x;
	int y;
	int origin_col;
	int origin_row;
	int cols;
	int rows;
	int source_x;
	int source_y;
};

static struct spelunking_look_state look_state;

/** Resolve the active section's authored player-facing name. */
static const char *current_section_name(const struct player *p)
{
	const struct world_spelunk_system_node *node;
	const struct world_spelunk_recipe_definition *recipe;

	if (!p || !p->spelunking_system ||
			p->spelunking_system->graph_version !=
				WORLD_SPELUNK_SYSTEM_GRAPH_VERSION) {
		return NULL;
	}
	node = world_spelunk_system_node_by_id(p->spelunking_system,
		p->spelunking_system->active_node_id);
	if (!node || !node->recipe_id[0]) return NULL;
	recipe = world_spelunk_recipe_definition_by_id(node->recipe_id);
	return recipe && recipe->name && recipe->name[0] ? recipe->name : NULL;
}

static const char *movement_name(enum world_spelunk_movement movement)
{
	switch (movement) {
	case WORLD_SPELUNK_STANDING:
		return "Standing";
	case WORLD_SPELUNK_JUMP_APEX:
		return "Jumping";
	case WORLD_SPELUNK_FALLING:
		return "Falling";
	case WORLD_SPELUNK_CLIMBING:
		return "Climbing";
	case WORLD_SPELUNK_HANGING:
		return "Hanging";
	case WORLD_SPELUNK_SWIMMING:
		return "Swimming";
	default:
		return "Unknown";
	}
}

bool textui_spelunking_status(const struct player *p,
		struct ui_spelunking_status *status)
{
	const struct level *level;
	const char *section;
	const struct world_spelunk_state *state;
	struct world_fishing_site site;

	if (!status) return false;
	memset(status, 0, sizeof(*status));
	if (!world_spelunk_player_is_active(p)) return false;
	state = &p->spelunking->state;
	level = world_player_level(p);
	section = current_section_name(p);
	strnfmt(status->location, sizeof(status->location), "%s%s%s",
		level && level->name ? level->name : "Cave",
		section ? " - " : "", section ? section : "");
	strnfmt(status->activity, sizeof(status->activity), "%s%s",
		p->spelunking->peeking ? "Peek / " : "", movement_name(state->movement));
	if (state->movement == WORLD_SPELUNK_CLIMBING ||
			state->movement == WORLD_SPELUNK_HANGING) {
		bool rope = state->movement == WORLD_SPELUNK_HANGING;
		strnfmt(status->effort, sizeof(status->effort),
			"%s stamina: Up %d / Side %d / Down %d",
			rope ? "Rope" : "Climb",
			rope ? state->rules.rope_up_stamina_cost : state->rules.grip_up_stamina_cost,
			rope ? state->rules.rope_lateral_stamina_cost : state->rules.grip_lateral_stamina_cost,
			rope ? state->rules.rope_down_stamina_cost : state->rules.grip_down_stamina_cost);
	}
	status->hp = p->chp;
	status->max_hp = p->mhp;
	status->stamina = player_stamina_current(p);
	status->max_stamina = player_stamina_maximum(p);
	world_spelunk_player_air_status(p, &status->air, &status->max_air);
	status->can_fish = world_fishing_site_query(p, NULL, &site) == WORLD_FISHING_SITE_OK;
	if (p->fishing && p->fishing->active) {
		my_strcpy(status->activity, "Fishing", sizeof(status->activity));
		status->can_fish = false;
	}
	status->active = true;
	return true;
}

bool textui_spelunking_look_location(const struct player *p, int *x, int *y)
{
	if (!p || !x || !y || !look_state.active ||
			look_state.runtime != p->spelunking) {
		return false;
	}
	*x = look_state.x;
	*y = look_state.y;
	return true;
}

bool textui_spelunking_look_view(const struct player *p, int *origin_col,
		int *origin_row, int *cols, int *rows, int *source_x, int *source_y)
{
	if (!p || !origin_col || !origin_row || !cols || !rows || !source_x ||
			!source_y || !look_state.active ||
			look_state.runtime != p->spelunking || look_state.cols <= 0 ||
			look_state.rows <= 0) {
		return false;
	}
	*origin_col = look_state.origin_col;
	*origin_row = look_state.origin_row;
	*cols = look_state.cols;
	*rows = look_state.rows;
	*source_x = look_state.source_x;
	*source_y = look_state.source_y;
	return true;
}

bool textui_spelunking_describe(const struct player *p, int x, int y,
		char *description, size_t capacity)
{
	struct world_spelunk_inspect inspect;
	const char *memory;

	if (!description || !capacity) return false;
	description[0] = '\0';
	if (!world_spelunk_player_is_active(p) ||
			!world_spelunk_view_inspect_system(p->spelunking_system,
				p->spelunking, x, y, &inspect)) {
		return false;
	}
	memory = inspect.knowledge == WORLD_SPELUNK_INSPECT_REMEMBERED ?
		"You remember " : "";
	switch (inspect.kind) {
	case WORLD_SPELUNK_INSPECT_VOID:
		my_strcpy(description, "Beyond the cave boundary.", capacity);
		break;
	case WORLD_SPELUNK_INSPECT_UNKNOWN:
		my_strcpy(description, "Unexplored darkness.", capacity);
		break;
	case WORLD_SPELUNK_INSPECT_AIR:
		if (inspect.decoration) {
			strnfmt(description, capacity, "%s%s.", memory,
				inspect.decoration->name);
		} else {
			strnfmt(description, capacity, "%sopen cave space.", memory);
		}
		break;
	case WORLD_SPELUNK_INSPECT_STALACTITE:
		my_strcpy(description, "A stalactite hanging from the cave ceiling.",
			capacity);
		break;
	case WORLD_SPELUNK_INSPECT_STALAGMITE:
		my_strcpy(description, "A stalagmite rising from the cave floor.",
			capacity);
		break;
	case WORLD_SPELUNK_INSPECT_ROCK:
		if (inspect.decoration) {
			strnfmt(description, capacity, "%s%s.", memory,
				inspect.decoration->name);
		} else if (inspect.material) {
			strnfmt(description, capacity, "%s%s rock.", memory,
				inspect.material->name);
		} else {
			strnfmt(description, capacity, "%scave rock.", memory);
		}
		break;
	case WORLD_SPELUNK_INSPECT_WATER:
		strnfmt(description, capacity, "%scave water.", memory);
		break;
	case WORLD_SPELUNK_INSPECT_EXIT:
		strnfmt(description, capacity, "%sthe way out of the cave.", memory);
		break;
	case WORLD_SPELUNK_INSPECT_PASSAGE_UP:
		strnfmt(description, capacity,
			"%san ascending passage to the previous cave section.", memory);
		break;
	case WORLD_SPELUNK_INSPECT_PASSAGE_DOWN:
		strnfmt(description, capacity,
			"%sa descending passage into deeper caves.", memory);
		break;
	case WORLD_SPELUNK_INSPECT_ROPE:
		strnfmt(description, capacity, "%sa secured rope.", memory);
		break;
	case WORLD_SPELUNK_INSPECT_PITON:
		strnfmt(description, capacity, "%sa hammered piton.", memory);
		break;
	case WORLD_SPELUNK_INSPECT_OBJECT:
		if (inspect.object) {
			char name[160];

			object_desc(name, sizeof(name), inspect.object,
				ODESC_PREFIX | ODESC_FULL, p);
			strnfmt(description, capacity, "%s %s.",
				inspect.knowledge == WORLD_SPELUNK_INSPECT_DETECTED ?
				"You sense" : "You see", name);
		}
		break;
	case WORLD_SPELUNK_INSPECT_ACTOR:
		if (inspect.actor && inspect.actor_definition) {
			strnfmt(description, capacity, "%s the %s (%d/%d HP).",
				inspect.knowledge == WORLD_SPELUNK_INSPECT_DETECTED ?
				"You sense" : "You see", inspect.actor_definition->name,
				inspect.actor->hp, inspect.actor->max_hp);
		}
		break;
	case WORLD_SPELUNK_INSPECT_GRIP:
		my_strcpy(description, "Your selected handhold.", capacity);
		break;
	case WORLD_SPELUNK_INSPECT_PLAYER:
		strnfmt(description, capacity, "You are here, %s.",
			movement_name(p->spelunking->state.movement));
		break;
	default:
		return false;
	}
	if (!description[0]) return false;
	description[0] = (char)toupper((unsigned char)description[0]);
	return true;
}

bool textui_spelunking_present(const struct player *p, bool clear)
{
	struct world_spelunk_view view;
	struct ui_spelunking_status hud;
	char status[384];
	int width;
	int height;
	int available_cols;
	int available_rows;
	int map_col;
	int map_row;
	int origin_col;
	int origin_row;
	int status_row;
	int col;
	int row;
	int look_x;
	int look_y;
	bool looking;

	if (!Term || !world_spelunk_player_is_active(p)) return false;
	Term_get_size(&width, &height);
	if (width <= 0 || height < 5) return false;
	map_col = COL_MAP;
	map_row = ROW_MAP;
	status_row = Term->sidebar_mode == SIDEBAR_TOP ? 3 : height - 1;
	available_cols = width - map_col;
	available_rows = Term->sidebar_mode == SIDEBAR_TOP ?
		height - map_row : status_row - map_row;
	looking = textui_spelunking_look_location(p, &look_x, &look_y);
	if (available_cols <= 0 || available_rows <= 0 ||
			!(looking ? world_spelunk_view_capture_at_system(
				p->spelunking_system, p->spelunking,
				look_x, look_y, available_cols, available_rows, view_cells,
				N_ELEMENTS(view_cells), &view) :
			world_spelunk_view_capture_system(p->spelunking_system,
				p->spelunking, available_cols,
				available_rows, view_cells, N_ELEMENTS(view_cells), &view))) {
		return false;
	}
	if (clear) Term_clear();
	for (row = map_row; row < map_row + available_rows; row++) {
		Term_erase(map_col, row, available_cols);
	}
	Term_erase(map_col, status_row, available_cols);
	origin_col = map_col + (available_cols - view.cols) / 2;
	origin_row = map_row + (available_rows - view.rows) / 2;
	if (looking) {
		look_state.origin_col = origin_col;
		look_state.origin_row = origin_row;
		look_state.cols = view.cols;
		look_state.rows = view.rows;
		look_state.source_x = view.source_x;
		look_state.source_y = view.source_y;
	}
	for (row = 0; row < view.rows; row++) {
		for (col = 0; col < view.cols; col++) {
			int attr;
			wchar_t glyph;

			spelunking_view_cell_appearance(&view, col, row, &attr, &glyph);
			if (glyph != L' ') {
				Term_putch(origin_col + col, origin_row + row, attr, glyph);
			}
		}
	}
	textui_spelunking_status(p, &hud);
	strnfmt(status, sizeof(status),
		"%s  HP %d/%d  Stamina %d/%d  Air %d/%d  %s  %s%s",
		hud.activity, hud.hp, hud.max_hp, hud.stamina, hud.max_stamina,
		hud.air, hud.max_air, hud.location, hud.effort,
		hud.can_fish ? "  Ctrl-C Fish" : "");
	Term_putstr(map_col, status_row,
		MIN(available_cols, (int)strlen(status)), COLOUR_WHITE, status);
	if (looking) {
		char description[256];
		int cursor_col = origin_col + look_x - view.source_x;
		int cursor_row = origin_row + look_y - view.source_y;

		Term_erase(0, 0, width);
		if (textui_spelunking_describe(p, look_x, look_y, description,
				sizeof(description))) {
			Term_putstr(0, 0, MIN(width, (int)strlen(description)),
				COLOUR_WHITE, description);
		}
		Term_gotoxy(cursor_col, cursor_row);
		Term_set_cursor(true);
	} else {
		Term_set_cursor(false);
	}
	return true;
}

void textui_spelunking_look(struct player *p)
{
	bool done = false;

	if (!world_spelunk_player_is_active(p) || !Term) return;
	memset(&look_state, 0, sizeof(look_state));
	look_state.runtime = p->spelunking;
	look_state.active = true;
	look_state.x = p->spelunking->state.x;
	look_state.y = p->spelunking->state.y;
	while (!done && world_spelunk_player_is_active(p) &&
			look_state.runtime == p->spelunking) {
		ui_event input;

		if (!textui_spelunking_present(p, false)) break;
		Term_fresh();
		input = inkey_m();
		if (input.type == EVT_RESIZE) {
			Term_clear();
			continue;
		}
		if (input.type == EVT_MOUSE) {
			if (input.mouse.button == 2 || input.mouse.button == 3) {
				done = true;
			} else if (input.mouse.button == 1 &&
					input.mouse.x >= look_state.origin_col &&
					input.mouse.x < look_state.origin_col + look_state.cols &&
					input.mouse.y >= look_state.origin_row &&
					input.mouse.y < look_state.origin_row + look_state.rows) {
				look_state.x = look_state.source_x +
					input.mouse.x - look_state.origin_col;
				look_state.y = look_state.source_y +
					input.mouse.y - look_state.origin_row;
			}
			continue;
		}
		if (input.type == EVT_KBRD) {
			int direction;

			if (event_is_key(input, ESCAPE) || event_is_key(input, 'q') ||
					event_is_key(input, KC_ENTER) || event_is_key(input, '5')) {
				done = true;
				continue;
			}
			direction = target_dir_allow(input.key, false, true);
			if (direction == ESCAPE) {
				done = true;
			} else if (direction) {
				look_state.x = MAX(0, MIN(p->spelunking->state.map.width - 1,
					look_state.x + ddx[direction]));
				look_state.y = MAX(0, MIN(p->spelunking->state.map.height - 1,
					look_state.y + ddy[direction]));
			} else {
				bell();
			}
		}
	}
	memset(&look_state, 0, sizeof(look_state));
	Term_erase(0, 0, Term->wid);
	(void)textui_spelunking_present(p, false);
	Term_fresh();
}
