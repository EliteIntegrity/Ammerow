/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-effects.c
 * \brief Effect policy for items used in side-view locations.
 *
 *
 */

#include "cave.h"
#include "effect-handler.h"
#include "effects.h"
#include "game-world.h"
#include "init.h"
#include "message.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-combat.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-visibility.h"
#include "z-rand.h"

#include <limits.h>
#include <stdlib.h>

static bool effect_index_is_player_local(int index)
{
	switch (index) {
	case EF_RANDOM:
	case EF_DAMAGE:
	case EF_HEAL_HP:
	case EF_NOURISH:
	case EF_CRUNCH:
	case EF_CURE:
	case EF_TIMED_SET:
	case EF_TIMED_INC:
	case EF_TIMED_INC_NO_RES:
	case EF_TIMED_DEC:
	case EF_RESTORE_STAT:
	case EF_DRAIN_STAT:
	case EF_LOSE_RANDOM_STAT:
	case EF_GAIN_STAT:
	case EF_RESTORE_EXP:
	case EF_GAIN_EXP:
	case EF_RESTORE_MANA:
	case EF_SELECT:
	case EF_SET_VALUE:
	case EF_CLEAR_VALUE:
	case EF_SCRAMBLE_STATS:
	case EF_UNSCRAMBLE_STATS:
	case EF_SHAPECHANGE:
	case EF_LIGHT_LEVEL:
	case EF_DETECT_ORE:
	case EF_DETECT_GOLD:
	case EF_DETECT_OBJECTS:
	case EF_DETECT_INVISIBLE_MONSTERS:
	case EF_DETECT_VISIBLE_MONSTERS:
	case EF_BREATH:
	case EF_TELEPORT:
	case EF_MAP_AREA:
	case EF_SENSE_GOLD:
	case EF_SENSE_OBJECTS:
	case EF_IDENTIFY:
	case EF_ENCHANT:
	case EF_REMOVE_CURSE:
	case EF_RECHARGE:
	case EF_CURSE_WEAPON:
	case EF_CURSE_ARMOR:
		return true;
	default:
		return false;
	}
}

static bool effect_index_has_side_view_adapter(int index)
{
	if (effect_index_is_player_local(index)) return true;
	switch (index) {
	case EF_BALL:
	case EF_BREATH:
	case EF_ARC:
	case EF_SHORT_BEAM:
	case EF_SWARM:
	case EF_STAR:
	case EF_STAR_BALL:
	case EF_BOLT:
	case EF_BEAM:
	case EF_BOLT_OR_BEAM:
		return true;
	default:
		return false;
	}
}

static const struct player_shape *shape_by_index_without_message(int index)
{
	const struct player_shape *shape;

	for (shape = shapes; shape; shape = shape->next) {
		if (shape->sidx == index) return shape;
	}
	return NULL;
}

static bool effect_chain_is_safe(const struct effect *effect, int depth)
{
	if (!effect || depth > 16) return false;
	for (; effect; effect = effect->next) {
		if (!effect_index_is_player_local(effect->index)) return false;
		if (effect->index == EF_SHAPECHANGE) {
			const struct player_shape *shape =
				shape_by_index_without_message(effect->subtype);

			/* Shape entry effects are an indirect effect chain.  Audit them at
			 * the same boundary so data cannot smuggle a top-down map operation
			 * through an otherwise local potion. */
			if (!shape || (shape->effect &&
					!effect_chain_is_safe(shape->effect, depth + 1))) {
				return false;
			}
		}
	}
	return true;
}

bool world_spelunk_effect_chain_is_player_local(const struct effect *effect)
{
	return effect_chain_is_safe(effect, 0);
}

static bool effect_chain_is_supported(const struct effect *effect, int depth)
{
	if (!effect || depth > 16) return false;
	for (; effect; effect = effect->next) {
		if (!effect_index_has_side_view_adapter(effect->index)) {
			return false;
		}
		if (effect->index == EF_SHAPECHANGE) {
			const struct player_shape *shape =
				shape_by_index_without_message(effect->subtype);

			if (!shape || (shape->effect &&
					!effect_chain_is_supported(shape->effect, depth + 1))) {
				return false;
			}
		}
	}
	return true;
}

bool world_spelunk_effect_chain_is_supported(const struct effect *effect)
{
	/* Recall is intentionally not player-local: it commits an authored
	 * cross-mode transition when its countdown expires.  Permit only the
	 * standalone effect so a data chain cannot run after changing worlds. */
	if (effect && effect->index == EF_RECALL && !effect->next) return true;
	return effect_chain_is_supported(effect, 0);
}

bool world_spelunk_item_effect_is_safe(const struct effect *effect)
{
	return world_spelunk_effect_chain_is_supported(effect);
}

bool world_spelunk_effect_origin_is_player_owned(
		const effect_handler_context_t *context)
{
	if (!context) return false;
	if (context->origin.what == SRC_PLAYER) return true;
	return context->origin.what == SRC_OBJECT &&
		context->origin.which.object &&
		object_is_carried(player, context->origin.which.object);
}

static bool in_detection_area(const struct world_spelunk_runtime *runtime,
		const effect_handler_context_t *context, int x, int y)
{
	int x_reach = context->x > 0 ? context->x : runtime->state.map.width;
	int y_reach = context->y > 0 ? context->y : runtime->state.map.height;

	return abs(x - runtime->state.x) <= x_reach &&
		abs(y - runtime->state.y) <= y_reach;
}

static bool detect_objects(effect_handler_context_t *context, bool gold,
		bool sense)
{
	struct world_spelunk_runtime *runtime = player->spelunking;
	struct object *obj;
	bool found = false;

	for (obj = runtime->ground_objects; obj; obj = obj->next) {
		if (!in_detection_area(runtime, context, obj->grid.x, obj->grid.y) ||
				tval_is_money(obj) != gold) {
			continue;
		}
		found = true;
		(void)world_spelunk_visibility_detect_cell(runtime, obj->grid.x,
			obj->grid.y);
	}
	if (found) {
		if (sense) {
			msg(gold ? "You sense the presence of gold!" :
				"You sense the presence of objects!");
		} else {
			msg(gold ? "You detect the presence of gold!" :
				"You detect the presence of objects!");
		}
	} else if (context->aware) {
		if (sense) {
			msg(gold ? "You sense no gold." : "You sense no objects.");
		} else {
			msg(gold ? "You detect no gold." : "You detect no objects.");
		}
	}
	context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_ITEMLIST;
	return true;
}

static bool detect_actors(effect_handler_context_t *context, bool invisible)
{
	struct world_spelunk_runtime *runtime = player->spelunking;
	bool found = false;
	size_t i;

	/* Side-view actor definitions currently have no invisible trait.  Keeping
	 * that fact here makes DETECT_INVISIBLE fail honestly and leaves one clear
	 * extension point if actor traits become data-driven later. */
	if (!invisible) {
		for (i = 0; i < runtime->actor_count; i++) {
			const struct world_spelunk_actor *actor = &runtime->actors[i];

			if (!in_detection_area(runtime, context, actor->x, actor->y)) {
				continue;
			}
			found = true;
			(void)world_spelunk_visibility_detect_cell(runtime, actor->x,
				actor->y);
		}
	}
	if (found) {
		msg(invisible ? "You sense the presence of invisible creatures!" :
			"You sense the presence of monsters!");
	} else if (context->aware) {
		msg(invisible ? "You sense no invisible creatures." :
			"You sense no monsters.");
	}
	context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool teleport_destination_is_valid(
		const struct world_spelunk_runtime *runtime, int x, int y)
{
	return x >= 0 && y >= 0 && x < runtime->state.map.width &&
		y < runtime->state.map.height &&
		runtime->cells[(size_t)y * runtime->state.map.width + x] !=
			WORLD_SPELUNK_ROCK &&
		!world_spelunk_runtime_actor_at(runtime, x, y);
}

static int side_view_distance(int x0, int y0, int x1, int y1)
{
	int dx = abs(x1 - x0);
	int dy = abs(y1 - y0);

	/* Match Angband's octagonal distance closely without depending on a
	 * top-down chunk: one diagonal step plus half the remaining axis. */
	return dx > dy ? dx + dy / 2 : dy + dx / 2;
}

static bool teleport_player(effect_handler_context_t *context)
{
	struct world_spelunk_runtime *runtime = player->spelunking;
	struct world_spelunk_state before = runtime->state;
	int wanted = context->value.base +
		damroll(context->value.dice, context->value.sides);
	int percentage = context->value.m_bonus;
	int best_score = INT_MAX;
	int choices = 0;
	int chosen_x = runtime->state.x;
	int chosen_y = runtime->state.y;
	int x;
	int y;

	context->ident = true;
	if (player_of_has(player, OF_NO_TELEPORT)) {
		equip_learn_flag(player, OF_NO_TELEPORT);
		msg("Teleportation forbidden!");
		return true;
	}
	if (percentage) {
		int vertical = MAX(runtime->state.y,
			runtime->state.map.height - 1 - runtime->state.y);
		int horizontal = MAX(runtime->state.x,
			runtime->state.map.width - 1 - runtime->state.x);

		wanted = MAX(vertical, horizontal) * percentage / 100;
	}
	if (wanted > 1) {
		int variation = wanted / 4;

		wanted += one_in_(2) ? -randint0(variation + 1) :
			randint0(variation + 1);
	}
	for (y = 0; y < runtime->state.map.height; y++) {
		for (x = 0; x < runtime->state.map.width; x++) {
			int distance;
			int score;

			if ((x == before.x && y == before.y) ||
					!teleport_destination_is_valid(runtime, x, y)) {
				continue;
			}
			distance = side_view_distance(before.x, before.y, x, y);
			score = abs(distance - wanted);
			if (score < best_score) {
				best_score = score;
				choices = 1;
				chosen_x = x;
				chosen_y = y;
			} else if (score == best_score && one_in_(++choices)) {
				chosen_x = x;
				chosen_y = y;
			}
		}
	}
	if (!choices) {
		msg("Space twists, but finds nowhere to take you.");
		return true;
	}
	runtime->state.x = chosen_x;
	runtime->state.y = chosen_y;
	runtime->state.fall_start_y = chosen_y;
	runtime->state.grip_target_x = -1;
	runtime->state.grip_target_y = -1;
	runtime->state.jump_holding = false;
	if (runtime->cells[(size_t)chosen_y * runtime->state.map.width +
			chosen_x] == WORLD_SPELUNK_WATER) {
		runtime->state.movement = WORLD_SPELUNK_SWIMMING;
	} else if (world_spelunk_is_grounded(&runtime->state)) {
		runtime->state.movement = WORLD_SPELUNK_STANDING;
	} else {
		/* Deliberately do not search for safe support.  A blink over a chasm
		 * leaves the player falling; ordinary cave physics owns the result. */
		runtime->state.movement = WORLD_SPELUNK_FALLING;
	}
	if (!world_spelunk_runtime_is_valid(runtime)) {
		runtime->state = before;
		return false;
	}
	world_spelunk_visibility_follow_player(runtime);
	player->upkeep->redraw |= PR_MAP | PR_STATUS;
	return true;
}

static bool breathe(effect_handler_context_t *context)
{
	int damage = effect_calculate_value(context, false);
	int range = context->radius ? context->radius : z_info->max_range;
	int dx;
	int dy;
	size_t hits = 0;

	if (context->dir < 1 || context->dir > 9 || context->dir == 5) {
		return false;
	}
	dx = ddgrid[context->dir].x;
	dy = ddgrid[context->dir].y;
	msgt(projections[context->subtype].msgt, "You breathe %s.",
		projections[context->subtype].desc);
	if (!world_spelunk_player_project_ray(player, dx, dy, range, damage,
			context->subtype, false, &hits)) {
		return false;
	}
	context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool aimed_damage_ray(effect_handler_context_t *context, bool beam)
{
	int damage;
	int dx;
	int dy;
	size_t hits = 0;

	if (context->dir < 1 || context->dir > 9 || context->dir == DIR_TARGET) {
		return false;
	}
	dx = ddgrid[context->dir].x;
	dy = ddgrid[context->dir].y;
	if (!dx && !dy) return false;
	damage = effect_calculate_value(context, true);
	if (!world_spelunk_player_project_ray(player, dx, dy, z_info->max_range,
			damage, context->subtype, !beam, &hits)) {
		return false;
	}
	if (!player->timed[TMD_BLIND]) context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool aimed_damage_ball_value(effect_handler_context_t *context,
		int damage)
{
	int radius;
	int dx;
	int dy;
	size_t hits = 0;

	if (context->dir < 1 || context->dir > 9 || context->dir == DIR_TARGET) {
		return false;
	}
	dx = ddgrid[context->dir].x;
	dy = ddgrid[context->dir].y;
	if (!dx && !dy) return false;
	radius = context->radius ? context->radius : 2;
	if (!world_spelunk_player_project_ball(player, dx, dy, z_info->max_range,
			radius, damage, context->subtype, &hits)) {
		return false;
	}
	if (!player->timed[TMD_BLIND]) context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool aimed_damage_ball(effect_handler_context_t *context)
{
	return aimed_damage_ball_value(context,
		effect_calculate_value(context, true));
}

static bool aimed_damage_arc(effect_handler_context_t *context)
{
	int damage;
	int range = context->radius ? context->radius : z_info->max_range;
	int dx;
	int dy;
	size_t hits = 0;

	if (context->dir < 1 || context->dir > 9 || context->dir == DIR_TARGET) {
		return false;
	}
	dx = ddgrid[context->dir].x;
	dy = ddgrid[context->dir].y;
	if (!dx && !dy) return false;
	damage = effect_calculate_value(context, context->effect == EF_ARC);
	if (!world_spelunk_player_project_ray(player, dx, dy, range, damage,
			context->subtype, false, &hits)) {
		return false;
	}
	context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool radial_damage(effect_handler_context_t *context, bool balls)
{
	static const int directions[] = { 1, 2, 3, 4, 6, 7, 8, 9 };
	int damage = effect_calculate_value(context, true);
	int i;

	for (i = 0; i < (int)N_ELEMENTS(directions); i++) {
		int dx = ddgrid[directions[i]].x;
		int dy = ddgrid[directions[i]].y;
		size_t hits = 0;
		bool projected;

		if (balls) {
			int radius = context->radius ? context->radius : 2;

			projected = world_spelunk_player_project_ball(player, dx, dy,
				z_info->max_range, radius, damage, context->subtype, &hits);
		} else {
			projected = world_spelunk_player_project_ray(player, dx, dy,
				z_info->max_range, damage, context->subtype, false, &hits);
		}
		if (!projected) return false;
	}
	if (!player->timed[TMD_BLIND]) context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

enum world_spelunk_effect_dispatch world_spelunk_effect_dispatch(
		effect_handler_context_t *context)
{
	if (!world_spelunk_effect_origin_is_player_owned(context) ||
			!world_spelunk_player_is_active(player)) {
		return WORLD_SPELUNK_EFFECT_UNHANDLED;
	}
	switch (context->effect) {
	case EF_BOLT:
		return aimed_damage_ray(context, false) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_BEAM:
		return aimed_damage_ray(context, true) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_BOLT_OR_BEAM:
		return aimed_damage_ray(context,
			randint0(100) < context->beam + context->other) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_BALL:
		return aimed_damage_ball(context) ? WORLD_SPELUNK_EFFECT_COMPLETED :
			WORLD_SPELUNK_EFFECT_FAILED;
	case EF_LIGHT_LEVEL:
		if (context->value.base) {
			msg("An image of your surroundings forms in your mind...");
		}
		world_spelunk_visibility_reveal_layout(player->spelunking);
		context->ident = true;
		player->upkeep->redraw |= PR_MAP;
		return WORLD_SPELUNK_EFFECT_COMPLETED;
	case EF_DETECT_ORE:
		if (context->aware) msg("You sense no buried treasure.");
		context->ident = true;
		return WORLD_SPELUNK_EFFECT_COMPLETED;
	case EF_DETECT_GOLD:
		return detect_objects(context, true, false) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_DETECT_OBJECTS:
		return detect_objects(context, false, false) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_SENSE_GOLD:
		return detect_objects(context, true, true) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_SENSE_OBJECTS:
		return detect_objects(context, false, true) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_DETECT_INVISIBLE_MONSTERS:
		return detect_actors(context, true) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_DETECT_VISIBLE_MONSTERS:
		return detect_actors(context, false) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_TELEPORT:
		return teleport_player(context) ? WORLD_SPELUNK_EFFECT_COMPLETED :
			WORLD_SPELUNK_EFFECT_FAILED;
	case EF_MAP_AREA:
		world_spelunk_visibility_reveal_area(player->spelunking,
			context->y ? context->y : context->value.dice,
			context->x ? context->x : context->value.sides);
		context->ident = true;
		player->upkeep->redraw |= PR_MAP;
		return WORLD_SPELUNK_EFFECT_COMPLETED;
	case EF_BREATH:
		return breathe(context) ? WORLD_SPELUNK_EFFECT_COMPLETED :
			WORLD_SPELUNK_EFFECT_FAILED;
	case EF_ARC:
	case EF_SHORT_BEAM:
		return aimed_damage_arc(context) ? WORLD_SPELUNK_EFFECT_COMPLETED :
			WORLD_SPELUNK_EFFECT_FAILED;
	case EF_SWARM: {
		int count = context->value.m_bonus;
		int damage = effect_calculate_value(context, true);

		while (count-- > 0) {
			if (!aimed_damage_ball_value(context, damage)) {
				return WORLD_SPELUNK_EFFECT_FAILED;
			}
		}
		return WORLD_SPELUNK_EFFECT_COMPLETED;
	}
	case EF_STAR:
		return radial_damage(context, false) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_STAR_BALL:
		return radial_damage(context, true) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	default:
		return WORLD_SPELUNK_EFFECT_UNHANDLED;
	}
}
