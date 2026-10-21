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
#include "game-input.h"
#include "mon-summon.h"
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
#include "world-spelunking-actor-effects.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-door.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-terrain.h"
#include "world-spelunking-passage.h"
#include "world-spelunking-transition.h"
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
	case EF_DETECT_TRAPS:
	case EF_DETECT_DOORS:
	case EF_DETECT_STAIRS:
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
	case EF_LINE:
	case EF_ALTER:
	case EF_BOLT_STATUS:
	case EF_BOLT_STATUS_DAM:
	case EF_BOLT_AWARE:
	case EF_PROJECT_LOS:
	case EF_PROJECT_LOS_AWARE:
	case EF_DETECT_LIVING_MONSTERS:
	case EF_DETECT_FEARFUL_MONSTERS:
	case EF_DETECT_EVIL:
	case EF_DETECT_SOUL:
	case EF_PROBE:
	case EF_WAKE:
	case EF_SUMMON:
		return true;
	default:
		return false;
	}
}

static bool is_actor_projection(int index)
{
	switch (index) {
	case EF_BALL: case EF_BREATH: case EF_ARC: case EF_SHORT_BEAM:
	case EF_SWARM: case EF_STAR: case EF_STAR_BALL: case EF_BOLT:
	case EF_BEAM: case EF_BOLT_OR_BEAM: case EF_LINE: case EF_ALTER:
	case EF_BOLT_STATUS: case EF_BOLT_STATUS_DAM: case EF_BOLT_AWARE:
	case EF_PROJECT_LOS: case EF_PROJECT_LOS_AWARE: return true;
	default: return false;
	}
}

static bool summon_type_supported(int type)
{
	return type >= 0 && (type == summon_name_to_idx("ANY") || type == summon_name_to_idx("MONSTER") ||
		type == summon_name_to_idx("MONSTERS") || type == summon_name_to_idx("ANIMAL"));
}

static bool subtype_supported(const struct effect *e)
{
	if (is_actor_projection(e->index)) return world_spelunk_actor_projection_supported(e->subtype);
	if (e->index == EF_SUMMON) return summon_type_supported(e->subtype);
	return true;
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
		if (!subtype_supported(effect)) return false;
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
		/* Wall removal can move or kill the caster. It must not be followed
		 * by another effect running against stale coordinates/state. */
		if ((effect->index == EF_BOLT || effect->index == EF_BEAM ||
			effect->index == EF_BOLT_OR_BEAM || effect->index == EF_BALL ||
			effect->index == EF_BREATH || effect->index == EF_ARC ||
			effect->index == EF_SHORT_BEAM || effect->index == EF_SWARM ||
			effect->index == EF_STAR || effect->index == EF_STAR_BALL) &&
			effect->subtype == PROJ_KILL_WALL) return false;
		if (!effect_index_has_side_view_adapter(effect->index)) {
			return false;
		}
		if (!subtype_supported(effect)) return false;
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
	if (effect && !effect->next && effect->subtype == PROJ_KILL_WALL &&
		(effect->index == EF_LINE || effect->index == EF_SHORT_BEAM)) return true;
	/* Recall is intentionally not player-local: it commits an authored
	 * cross-mode transition when its countdown expires.  Permit only the
	 * standalone effect so a data chain cannot run after changing worlds. */
	if (effect && effect->index == EF_RECALL && !effect->next) return true;
	/* Banishment has a prompt and self-damage; do not allow later effects to
	 * run after cancellation or death. */
	if (effect && !effect->next && (effect->index == EF_BANISH || effect->index == EF_MASS_BANISH)) return true;
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

static bool detect_fixtures(effect_handler_context_t *context)
{
	struct world_spelunk_runtime *r = player->spelunking;
	struct loc grid;
	enum world_spelunk_passage_direction direction;
	size_t i = 0;
	bool found = false;
	const char *subject;
	if (context->effect == EF_DETECT_TRAPS) {
		found = world_spelunk_fixture_detect(r,
			context->y > 0 ? context->y : r->state.map.height,
			context->x > 0 ? context->x : r->state.map.width, true);
		subject = "cave traps";
	} else if (context->effect == EF_DETECT_DOORS) {
		for (i = 0; i < r->door_count; i++) {
			struct world_spelunk_door *d = &r->doors[i];
			if (!in_detection_area(r, context, d->x, d->y)) continue;
			/* Detection finds a door's outline, not its concealed lock. */
			if (d->known != WORLD_SPELUNK_DOOR_LOCKED || d->state != WORLD_SPELUNK_DOOR_LOCKED)
				d->known = d->state == WORLD_SPELUNK_DOOR_OPEN ? WORLD_SPELUNK_DOOR_OPEN : WORLD_SPELUNK_DOOR_CLOSED;
			r->explored[d->y*r->state.map.width+d->x] = 1;
			found = true;
		}
		subject = "cave doors";
	} else {
		/* Existing endpoints only: no generation, travel or hidden-node reveal. */
		while (world_spelunking_exit_grid_at(player->spelunking_system, r, i++, &grid)) {
			if (!in_detection_area(r, context, grid.x, grid.y)) continue;
			r->explored[grid.y*r->state.map.width+grid.x] = 1;
			found = true;
		}
		i = 0;
		while (world_spelunk_system_passage_grid_at(player->spelunking_system, r, i++, &grid, &direction)) {
			if (!in_detection_area(r, context, grid.x, grid.y)) continue;
			r->explored[grid.y*r->state.map.width+grid.x] = 1;
			found = true;
		}
		subject = "cave passages";
	}
	msg(found ? "You sense the presence of %s!" : "You sense no %s.", subject);
	context->ident = true;
	player->upkeep->redraw |= PR_MAP;
	return true;
}

static bool detect_objects(effect_handler_context_t *context, bool gold,
		bool sense)
{
	struct world_spelunk_runtime *runtime = player->spelunking;
	struct object *obj;
	bool found = false;
	if (!gold) {
		found = world_spelunk_fixture_detect(runtime,
			context->y > 0 ? context->y : runtime->state.map.height,
			context->x > 0 ? context->x : runtime->state.map.width, false);
	}

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

static bool detect_actors(effect_handler_context_t *context)
{
	struct world_spelunk_runtime *runtime = player->spelunking;
	bool found = false;
	size_t i;

	/* Invisibility remains excluded from the authored vocabulary until its
	 * perception and movement rules exist. All supported filters are explicit. */
	if (context->effect != EF_DETECT_INVISIBLE_MONSTERS) {
		for (i = 0; i < runtime->actor_count; i++) {
			const struct world_spelunk_actor *actor = &runtime->actors[i];
			const struct world_spelunk_actor_definition *d = world_spelunk_actor_by_id(actor->id);
			if (!d) continue;
			if (context->effect == EF_DETECT_LIVING_MONSTERS && !world_spelunk_actor_is_living(d)) continue;
			if (context->effect == EF_DETECT_FEARFUL_MONSTERS && rf_has(d->flags,RF_NO_FEAR)) continue;
			if (context->effect == EF_DETECT_EVIL && !rf_has(d->flags,RF_EVIL)) continue;
			if (context->effect == EF_DETECT_SOUL && !rf_has(d->flags,RF_SPIRIT)) continue;

			if (!in_detection_area(runtime, context, actor->x, actor->y)) {
				continue;
			}
			found = true;
			(void)world_spelunk_visibility_detect_cell(runtime, actor->x,
				actor->y);
		}
	}
	if (found) {
		msg("You sense creatures matching the detection.");
	} else if (context->aware) {
		msg("You sense no creatures matching the detection.");
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
		!world_spelunk_map_blocks(&runtime->state.map, x, y) &&
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
	world_spelunk_fixture_step(player);
	world_spelunk_fixture_notice(player);
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

static bool affect_visible_actors(effect_handler_context_t *context)
{
	struct world_spelunk_runtime *r = player->spelunking;
	size_t i;
	int power = effect_calculate_value(context, true);
	bool found = false;
	for (i = r->actor_count; i > 0; i--) {
		struct world_spelunk_actor *a = &r->actors[i-1];
		const struct world_spelunk_actor_definition *d = world_spelunk_actor_by_id(a->id);
		if (!world_spelunk_visibility_is_visible(r,a->x,a->y)) continue;
		if (context->effect == EF_PROBE &&
			(player->timed[TMD_BLIND] || player->timed[TMD_IMAGE])) continue;
		found = true;
		if (context->effect == EF_PROBE) {
			char status[128];
			char traits[768];
			world_spelunk_actor_status_text(a,status,sizeof(status));
			msg("%s: %d/%d HP, armour %d, speed %d%s%s.", d->name, a->hp,a->max_hp,
				d->armour,d->speed,status[0] ? "; " : "",status);
			if (d->description) msg("%s", d->description);
			world_spelunk_actor_describe_traits(d,traits,sizeof(traits));
			msg("Traits: %s.", traits);
		} else if (context->effect == EF_WAKE) {
			a->status[WORLD_SPELUNK_STATUS_SLEEP] = 0;
		} else if (!world_spelunk_actor_project(player,a,power,context->subtype)) return false;
	}
	if (!found && context->effect == EF_PROBE) msg("No cave creatures are in sight to probe.");
	context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool summon_actors(effect_handler_context_t *context)
{
	struct world_spelunk_runtime *r = player->spelunking;
	int count = MIN(WORLD_SPELUNK_ACTOR_MAX, MAX(0,effect_calculate_value(context,false)));
	int spawned = 0, attempt;
	if (!summon_type_supported(context->subtype)) return false;
	for (attempt = 0; attempt < count && r->actor_count < WORLD_SPELUNK_ACTOR_MAX; attempt++) {
		const struct world_spelunk_actor_definition *d = NULL;
		int choices = 0, cells = 0, tx = 0, ty = 0, i, x, y;
		for (i = 0; i < world_spelunk_actor_definition_count(); i++) {
			const struct world_spelunk_actor_definition *candidate = world_spelunk_actor_by_index(i);
			if (context->subtype == summon_name_to_idx("ANIMAL") && !rf_has(candidate->flags,RF_ANIMAL)) continue;
			if (randint0(++choices) == 0) d = candidate;
		}
		if (!d) break;
		for (y = r->state.y-2; y <= r->state.y+2; y++) for (x = r->state.x-2; x <= r->state.x+2; x++) {
			if (!world_spelunk_actor_destination(r,x,y) ||
				!world_spelunk_visibility_line_is_clear(r,r->state.x,r->state.y,x,y)) continue;
			if (randint0(++cells) == 0) { tx = x; ty = y; }
		}
		if (!cells || !world_spelunk_runtime_add_actor(r,d->id,tx,ty)) break;
		r->actors[r->actor_count-1].reward = 0;
		spawned++;
	}
	msg(spawned ? "Hostile cave creatures answer the summons." : "Nothing answers the summons.");
	context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool banish_actors(effect_handler_context_t *context)
{
	struct world_spelunk_runtime *r = player->spelunking;
	char symbol = 0;
	int strain = 0, radius = context->radius ? context->radius : z_info->max_sight;
	size_t i;
	bool mass = context->effect == EF_MASS_BANISH;
	if (!mass && !get_com("Choose a cave creature symbol to banish: ", &symbol)) return false;
	for (i = r->actor_count; i > 0; i--) {
		struct world_spelunk_actor *a = &r->actors[i-1];
		const struct world_spelunk_actor_definition *d = world_spelunk_actor_by_id(a->id);
		if (mass ? distance(loc(r->state.x,r->state.y),loc(a->x,a->y)) > radius : d->glyph != (wchar_t)(unsigned char)symbol) continue;
		r->detected[a->y*r->state.map.width+a->x] = 0;
		(void)world_spelunk_runtime_remove_actor(r,a);
		strain += randint1(mass ? 3 : 4);
	}
	/* No kill XP and no loot. Preserve the shared spell's physical strain. */
	if (strain) take_hit(player,player_apply_damage_reduction(player,strain),"the strain of banishment");
	msg(strain ? "The banishment strains you." : "No cave creatures match the banishment.");
	context->ident = true;
	player->upkeep->redraw |= PR_MAP | PR_MONLIST;
	return true;
}

static bool cut_rock_ray(effect_handler_context_t *context)
{
	struct world_spelunk_runtime *r = player->spelunking;
	int x = r->state.x, y = r->state.y, step;
	int range = z_info->max_range;
	if (context->dir < 1 || context->dir > 9 || context->dir == DIR_TARGET) return false;
	if (context->effect == EF_SHORT_BEAM) {
		range = context->radius + ((context->origin.what == SRC_PLAYER && context->other > 0) ?
			player->lev/context->other : 0);
	}
	for (step = 0; step < range; step++) {
		x += ddgrid[context->dir].x;
		y += ddgrid[context->dir].y;
		if (x < 0 || y < 0 || x >= r->state.map.width || y >= r->state.map.height) break;
		if (r->cells[y*r->state.map.width+x] == WORLD_SPELUNK_ROCK) {
			enum world_spelunk_cut_result result = world_spelunk_cut_rock(player,x,y);
			msg("%s", result == SPELUNK_CUT_OK ? "The cave rock crumbles away." : world_spelunk_cut_reason(result));
			context->ident = true;
			return true;
		}
		if (world_spelunk_map_blocks(&r->state.map,x,y)) break;
	}
	msg("The spell finds no exposed cave rock in that direction.");
	context->ident = true;
	return true;
}

enum world_spelunk_effect_dispatch world_spelunk_effect_dispatch(
		effect_handler_context_t *context)
{
	if (!world_spelunk_effect_origin_is_player_owned(context) ||
			!world_spelunk_player_is_active(player)) {
		return WORLD_SPELUNK_EFFECT_UNHANDLED;
	}
	if (context->subtype == PROJ_KILL_WALL &&
		(context->effect == EF_LINE || context->effect == EF_SHORT_BEAM)) {
		return cut_rock_ray(context) ? WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	}
	switch (context->effect) {
	case EF_PROJECT_LOS: case EF_PROJECT_LOS_AWARE: case EF_PROBE: case EF_WAKE:
		return affect_visible_actors(context) ? WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_SUMMON:
		return summon_actors(context) ? WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_BANISH: case EF_MASS_BANISH:
		return banish_actors(context) ? WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_DETECT_TRAPS:
	case EF_DETECT_DOORS:
	case EF_DETECT_STAIRS:
		return detect_fixtures(context) ? WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_BOLT:
	case EF_BOLT_STATUS:
	case EF_BOLT_STATUS_DAM:
	case EF_BOLT_AWARE:
		return aimed_damage_ray(context, false) ?
			WORLD_SPELUNK_EFFECT_COMPLETED : WORLD_SPELUNK_EFFECT_FAILED;
	case EF_BEAM:
	case EF_LINE:
	case EF_ALTER:
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
	case EF_DETECT_VISIBLE_MONSTERS:
	case EF_DETECT_LIVING_MONSTERS:
	case EF_DETECT_FEARFUL_MONSTERS:
	case EF_DETECT_EVIL:
	case EF_DETECT_SOUL:
		return detect_actors(context) ?
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
