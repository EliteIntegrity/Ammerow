/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-combat.c
 * \brief Small persistent actor and melee adapter for side-view locations.
 *
 *
 */

#include "world-spelunking-combat.h"

#include "game-event.h"
#include "game-world.h"
#include "init.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "option.h"
#include "player-attack.h"
#include "player.h"
#include "player-timed.h"
#include "player-util.h"
#include "project.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-visibility.h"
#include "z-rand.h"
#include "z-util.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

const char *world_spelunk_actor_name(const char *actor_id)
{
	const struct world_spelunk_actor_definition *definition =
		world_spelunk_actor_by_id(actor_id);

	return definition ? definition->name : "unknown creature";
}

static int roll_player_damage(struct player *p, const struct object *weapon)
{
	int dice = weapon ? weapon->dd : 1;
	int sides = weapon ? weapon->ds : 1;
	int damage;

	if (OPT(p, birth_percent_damage)) {
		int die_average = (10 * (sides + 1)) / 2;
		int deadliness = p->state.to_d +
			(weapon ? object_to_dam(weapon) : 0);
		bool extra;

		die_average *= 10;
		apply_deadliness(&die_average, MIN(deadliness, 150));
		sides = (2 * die_average) - 10000;
		extra = randint0(10000) < (sides % 10000);
		sides /= 10000;
		sides += extra ? 1 : 0;
		damage = damroll(dice, MAX(1, sides));
	} else {
		damage = damroll(dice, sides);
		if (weapon) damage += object_to_dam(weapon);
		damage += p->state.to_d;
	}
	return MAX(0, damage);
}

static void prepare_combat_report(struct world_spelunk_combat_report *report,
		const struct world_spelunk_actor *actor)
{
	memset(report, 0, sizeof(*report));
	if (!actor) return;
	{
		const struct world_spelunk_actor_definition *definition =
			world_spelunk_actor_by_id(actor->id);

		report->actor_id = definition ? definition->id : NULL;
	}
	report->x = actor->x;
	report->y = actor->y;
}

static enum world_spelunk_combat_result finish_kill(struct player *p,
		struct world_spelunk_actor *actor,
		struct world_spelunk_combat_report *report)
{
	const struct world_spelunk_actor_definition *definition =
		world_spelunk_actor_by_id(actor->id);

	report->killed = true;
	if (!world_spelunk_runtime_remove_actor(p->spelunking, actor)) {
		return WORLD_SPELUNK_COMBAT_INVALID;
	}
	if (!p->is_dead && definition) player_exp_gain(p, definition->experience);
	return WORLD_SPELUNK_COMBAT_KILLED;
}

static bool apply_projection_damage(struct player *p,
		struct world_spelunk_actor *actor, int damage, int projection_type)
{
	struct world_spelunk_combat_report report;
	const char *name;
	bool visible;

	if (!p || !actor || damage < 0 || projection_type < 0 ||
			projection_type >= z_info->projection_max) {
		return false;
	}
	prepare_combat_report(&report, actor);
	name = world_spelunk_actor_name(report.actor_id);
	visible = world_spelunk_visibility_is_visible(p->spelunking,
		actor->x, actor->y);
	report.hit = true;
	report.damage = damage;
	actor->hp = MAX(0, actor->hp - damage);
	if (visible) {
		if (OPT(p, show_damage) && damage > 0) {
			msgt(projections[projection_type].msgt,
				"The %s strikes the %s (%d).",
				projections[projection_type].desc, name, damage);
		} else if (damage > 0) {
			msgt(projections[projection_type].msgt,
				"The %s strikes the %s.",
				projections[projection_type].desc, name);
		}
	}
	if (damage > 0) {
		event_signal_damage(loc(actor->x, actor->y), damage, visible, false);
	}
	if (actor->hp == 0) {
		report.killed = finish_kill(p, actor, &report) ==
			WORLD_SPELUNK_COMBAT_KILLED;
		if (report.killed && visible) msg("You kill the %s.", name);
	}
	return true;
}

enum world_spelunk_combat_result world_spelunk_player_bump_attack(
		struct player *p, int dx, int dy,
		struct world_spelunk_combat_report *report)
{
	struct world_spelunk_combat_report local;
	struct world_spelunk_actor *actor;
	const struct world_spelunk_actor_definition *definition;
	struct object *weapon;

	if (!report) report = &local;
	memset(report, 0, sizeof(*report));
	if (!p || !p->upkeep || !world_spelunk_player_is_active(p) || p->is_dead ||
			p->upkeep->energy_use != 0 || dx < -1 || dx > 1 || dy < -1 ||
			dy > 1 || (dx == 0 && dy == 0) ||
			p->spelunking->state.rules.action_energy > INT_MAX) {
		return WORLD_SPELUNK_COMBAT_INVALID;
	}
	actor = world_spelunk_runtime_actor_at_mutable(p->spelunking,
		p->spelunking->state.x + dx, p->spelunking->state.y + dy);
	if (!actor) return WORLD_SPELUNK_COMBAT_NO_TARGET;
	prepare_combat_report(report, actor);
	if (p->spelunking->state.movement != WORLD_SPELUNK_STANDING) {
		return WORLD_SPELUNK_COMBAT_UNSTABLE;
	}
	definition = world_spelunk_actor_by_id(actor->id);
	if (!definition) return WORLD_SPELUNK_COMBAT_INVALID;
	weapon = equipped_item_by_slot_name(p, "weapon");
	p->upkeep->energy_use =
		(int)p->spelunking->state.rules.action_energy;
	report->hit = test_hit(chance_of_melee_hit_base(p, weapon),
		definition->armour);
	if (!report->hit) return WORLD_SPELUNK_COMBAT_TURN;
	report->damage = roll_player_damage(p, weapon);
	actor->hp = MAX(0, actor->hp - report->damage);
	if (actor->hp == 0) return finish_kill(p, actor, report);
	return WORLD_SPELUNK_COMBAT_TURN;
}

enum world_spelunk_combat_result world_spelunk_player_resolve_collision(
		struct player *p, struct world_spelunk_combat_report *report)
{
	struct world_spelunk_combat_report local;
	struct world_spelunk_actor *actor;

	if (!report) report = &local;
	memset(report, 0, sizeof(*report));
	if (!p || !world_spelunk_player_is_active(p)) {
		return WORLD_SPELUNK_COMBAT_INVALID;
	}
	actor = world_spelunk_runtime_actor_at_mutable(p->spelunking,
		p->spelunking->state.x, p->spelunking->state.y);
	if (!actor) return WORLD_SPELUNK_COMBAT_NO_TARGET;
	prepare_combat_report(report, actor);
	report->hit = true;
	report->damage = actor->hp;
	actor->hp = 0;
	return finish_kill(p, actor, report);
}

bool world_spelunk_player_project_ray(struct player *p, int dx, int dy,
		int range, int damage, int projection_type, bool stop_at_first_actor,
		size_t *hits)
{
	struct world_spelunk_runtime *runtime;
	int x;
	int y;
	int previous_x;
	int previous_y;
	int step;

	if (hits) *hits = 0;
	if (!p || !hits || !world_spelunk_player_is_active(p) ||
			dx < -1 || dx > 1 || dy < -1 || dy > 1 ||
			(!dx && !dy) || range <= 0 || damage < 0 ||
			projection_type < 0 || projection_type >= z_info->projection_max) {
		return false;
	}
	runtime = p->spelunking;
	x = runtime->state.x;
	y = runtime->state.y;
	previous_x = x;
	previous_y = y;
	for (step = 0; step < range; step++) {
		struct world_spelunk_actor *actor;
		bool seen;

		x += dx;
		y += dy;
		if (x < 0 || y < 0 || x >= runtime->state.map.width ||
				y >= runtime->state.map.height ||
				runtime->cells[(size_t)y * runtime->state.map.width + x] ==
					WORLD_SPELUNK_ROCK) {
			break;
		}
		seen = world_spelunk_visibility_is_visible(runtime, x, y);
		event_signal_bolt(EVENT_BOLT, projection_type, true, seen,
			!stop_at_first_actor, previous_y, previous_x, y, x);
		previous_x = x;
		previous_y = y;
		actor = world_spelunk_runtime_actor_at_mutable(runtime, x, y);
		if (!actor) continue;
		if (!apply_projection_damage(p, actor, damage, projection_type)) {
			return false;
		}
		(*hits)++;
		if (stop_at_first_actor) break;
	}
	return world_spelunk_runtime_is_valid(runtime);
}

bool world_spelunk_player_project_ball(struct player *p, int dx, int dy,
		int range, int radius, int damage, int projection_type, size_t *hits)
{
	struct world_spelunk_runtime *runtime;
	int centre_x;
	int centre_y;
	int previous_x;
	int previous_y;
	int step;
	size_t i = 0;

	if (hits) *hits = 0;
	if (!p || !hits || !world_spelunk_player_is_active(p) ||
			dx < -1 || dx > 1 || dy < -1 || dy > 1 || (!dx && !dy) ||
			range <= 0 || radius < 0 || damage < 0 || projection_type < 0 ||
			projection_type >= z_info->projection_max) {
		return false;
	}
	runtime = p->spelunking;
	centre_x = runtime->state.x;
	centre_y = runtime->state.y;
	previous_x = centre_x;
	previous_y = centre_y;
	for (step = 0; step < range; step++) {
		int next_x = centre_x + dx;
		int next_y = centre_y + dy;
		bool seen;

		if (next_x < 0 || next_y < 0 ||
				next_x >= runtime->state.map.width ||
				next_y >= runtime->state.map.height ||
				runtime->cells[(size_t)next_y * runtime->state.map.width +
					next_x] == WORLD_SPELUNK_ROCK) {
			break;
		}
		centre_x = next_x;
		centre_y = next_y;
		seen = world_spelunk_visibility_is_visible(runtime, centre_x, centre_y);
		event_signal_bolt(EVENT_BOLT, projection_type, true, seen, false,
			previous_y, previous_x, centre_y, centre_x);
		previous_x = centre_x;
		previous_y = centre_y;
		if (world_spelunk_runtime_actor_at(runtime, centre_x, centre_y)) break;
	}
	while (i < runtime->actor_count) {
		struct world_spelunk_actor *actor = &runtime->actors[i];
		int blast_distance = distance(loc(centre_x, centre_y),
			loc(actor->x, actor->y));

		if (blast_distance <= radius &&
				world_spelunk_visibility_line_is_clear(runtime, centre_x,
					centre_y, actor->x, actor->y)) {
			int actor_damage = (damage + blast_distance) /
				(blast_distance + 1);
			size_t count_before = runtime->actor_count;

			if (!apply_projection_damage(p, actor, actor_damage,
					projection_type)) {
				return false;
			}
			(*hits)++;
			if (runtime->actor_count < count_before) continue;
		}
		i++;
	}
	return world_spelunk_runtime_is_valid(runtime);
}

/**
 * Build a terrain-aware path using the same integer line principle as
 * Angband's native projectile path, but with the side-view runtime as the
 * terrain and actor owner.  Rock is never included in the returned path.
 */
static size_t projectile_path(const struct world_spelunk_runtime *runtime,
		struct loc target, bool through_target, int range, bool stop_at_actor,
		struct loc *path, size_t capacity)
{
	struct loc start;
	int x;
	int y;
	int delta_x;
	int delta_y;
	int step_x;
	int step_y;
	int error;
	size_t count = 0;

	if (!runtime || !path || !capacity || range <= 0) return 0;
	start = loc(runtime->state.x, runtime->state.y);
	if (loc_eq(start, target)) return 0;
	x = start.x;
	y = start.y;
	delta_x = abs(target.x - x);
	delta_y = -abs(target.y - y);
	step_x = x < target.x ? 1 : -1;
	step_y = y < target.y ? 1 : -1;
	error = delta_x + delta_y;

	while (count < capacity) {
		int doubled = error * 2;
		size_t index;

		if (doubled >= delta_y) {
			error += delta_y;
			x += step_x;
		}
		if (doubled <= delta_x) {
			error += delta_x;
			y += step_y;
		}
		if (x < 0 || y < 0 || x >= runtime->state.map.width ||
				y >= runtime->state.map.height ||
				distance(start, loc(x, y)) > range) {
			break;
		}
		index = (size_t)y * runtime->state.map.width + x;
		if (runtime->cells[index] == WORLD_SPELUNK_ROCK) break;
		path[count++] = loc(x, y);
		if ((stop_at_actor && world_spelunk_runtime_actor_at(runtime, x, y)) ||
				(!through_target && x == target.x && y == target.y)) {
			break;
		}
	}
	return count;
}

bool world_spelunk_closest_visible_actor(const struct player *p, int range,
		struct loc *target)
{
	const struct world_spelunk_runtime *runtime;
	int closest_distance = INT_MAX;
	size_t closest_index = SIZE_MAX;
	size_t i;

	if (!p || !target || range <= 0 || !world_spelunk_player_is_active(p)) {
		return false;
	}
	runtime = p->spelunking;
	for (i = 0; i < runtime->actor_count; i++) {
		const struct world_spelunk_actor *actor = &runtime->actors[i];
		struct loc path[WORLD_SPELUNK_PROJECTILE_PATH_MAX];
		struct loc actor_grid = loc(actor->x, actor->y);
		size_t path_length;
		int actor_distance;

		if (!world_spelunk_visibility_is_visible(runtime, actor->x, actor->y)) {
			continue;
		}
		actor_distance = distance(loc(runtime->state.x, runtime->state.y),
			actor_grid);
		if (actor_distance > range || actor_distance > closest_distance) continue;
		path_length = projectile_path(runtime, actor_grid, false, range, true,
			path, N_ELEMENTS(path));
		if (!path_length || !loc_eq(path[path_length - 1], actor_grid) ||
				world_spelunk_runtime_actor_at(runtime, actor->x, actor->y) !=
				actor) {
			continue;
		}
		if (actor_distance < closest_distance || closest_index == SIZE_MAX) {
			closest_distance = actor_distance;
			closest_index = i;
		}
	}
	if (closest_index == SIZE_MAX) return false;
	*target = loc(runtime->actors[closest_index].x,
		runtime->actors[closest_index].y);
	return true;
}

enum world_spelunk_projectile_result world_spelunk_player_projectile(
		struct player *p, struct object *missile, struct object *launcher,
		struct loc target, bool through_target, int range, int shots,
		struct world_spelunk_projectile_report *report)
{
	struct world_spelunk_projectile_report local;
	struct world_spelunk_runtime *runtime;
	int pierce;
	size_t i;

	if (!report) report = &local;
	memset(report, 0, sizeof(*report));
	if (!p || !p->upkeep || !missile || shots <= 0 || range <= 0 ||
			!world_spelunk_player_is_active(p) || p->is_dead ||
			p->upkeep->energy_use != 0) {
		return WORLD_SPELUNK_PROJECTILE_INVALID;
	}
	runtime = p->spelunking;
	report->landing_x = runtime->state.x;
	report->landing_y = runtime->state.y;
	if (runtime->state.movement != WORLD_SPELUNK_STANDING) {
		return WORLD_SPELUNK_PROJECTILE_UNSTABLE;
	}
	report->path_length = projectile_path(runtime, target, through_target,
		range, false, report->path, N_ELEMENTS(report->path));
	if (report->path_length) {
		report->landing_x = report->path[report->path_length - 1].x;
		report->landing_y = report->path[report->path_length - 1].y;
	}
	report->recoverable = runtime->cells[(size_t)report->landing_y *
		runtime->state.map.width + report->landing_x] == WORLD_SPELUNK_AIR;
	/* An actor may stop the shot before the provisional endpoint.  Reserve one
	 * local-object slot up front so a surviving missile can always be committed
	 * transactionally at any open impact cell. */
	if (world_spelunk_runtime_ground_object_count(runtime) >=
			WORLD_SPELUNK_OBJECT_MAX) {
		return WORLD_SPELUNK_PROJECTILE_NO_STORAGE;
	}

	p->upkeep->energy_use = z_info->move_energy * 10 / shots;
	pierce = p->timed[TMD_POWERSHOT] && tval_is_sharp_missile(missile) ?
		p->state.ammo_mult : 1;
	for (i = 0; i < report->path_length && pierce > 0; i++) {
		struct loc grid = report->path[i];
		struct world_spelunk_actor *actor =
			world_spelunk_runtime_actor_at_mutable(runtime, grid.x, grid.y);
		const struct world_spelunk_actor_definition *definition;
		struct world_spelunk_projectile_hit *hit;

		if (!actor) continue;
		definition = world_spelunk_actor_by_id(actor->id);
		if (!definition || report->hit_count >= N_ELEMENTS(report->hits)) {
			return WORLD_SPELUNK_PROJECTILE_INVALID;
		}
		hit = &report->hits[report->hit_count++];
		hit->actor_id = definition->id;
		hit->x = grid.x;
		hit->y = grid.y;
		hit->visible = world_spelunk_visibility_is_visible(runtime,
			grid.x, grid.y);
		hit->hit = player_resolve_ranged_attack_against_armour(p, missile,
			launcher, definition->armour, distance(loc(runtime->state.x,
			runtime->state.y), grid), hit->visible, &hit->damage,
			&hit->message_type);
		if (hit->hit) {
			report->hit_target = true;
			missile_learn_on_ranged_attack(p, missile);
			equip_learn_on_ranged_attack(p);
			actor->hp = MAX(0, actor->hp - hit->damage);
			if (actor->hp == 0) {
				struct world_spelunk_combat_report kill_report;

				prepare_combat_report(&kill_report, actor);
				kill_report.hit = true;
				kill_report.damage = hit->damage;
				hit->killed = finish_kill(p, actor, &kill_report) ==
					WORLD_SPELUNK_COMBAT_KILLED;
			}
		}
		pierce--;
		if (!pierce) {
			report->path_length = i + 1;
			report->landing_x = grid.x;
			report->landing_y = grid.y;
			report->recoverable = true;
		}
	}
	if (p->timed[TMD_POWERSHOT]) {
		player_clear_timed(p, TMD_POWERSHOT, true, false);
	}
	return world_spelunk_runtime_is_valid(runtime) ?
		WORLD_SPELUNK_PROJECTILE_TURN : WORLD_SPELUNK_PROJECTILE_INVALID;
}

static bool actor_can_step(const struct world_spelunk_runtime *runtime,
		int x, int y)
{
	const struct world_spelunk_map *map = &runtime->state.map;

	return x >= 0 && x < map->width && y >= 0 && y + 1 < map->height &&
		runtime->cells[(size_t)y * map->width + x] == WORLD_SPELUNK_AIR &&
		runtime->cells[(size_t)(y + 1) * map->width + x] ==
			WORLD_SPELUNK_ROCK &&
		!world_spelunk_runtime_actor_at(runtime, x, y);
}

bool world_spelunk_process_actors(struct player *p,
		struct world_spelunk_actor_report *reports, size_t capacity,
		size_t *written)
{
	struct world_spelunk_runtime *runtime;
	size_t i;
	size_t count = 0;

	if (written) *written = 0;
	if (!p || !reports || !written ||
			!world_spelunk_player_is_active(p) ||
			capacity < p->spelunking->actor_count) {
		return false;
	}
	runtime = p->spelunking;
	if (!world_spelunk_runtime_is_valid(runtime)) return false;
	memset(reports, 0, capacity * sizeof(*reports));
	for (i = 0; i < runtime->actor_count && !p->is_dead; i++) {
		struct world_spelunk_actor *actor = &runtime->actors[i];
		const struct world_spelunk_actor_definition *definition =
			world_spelunk_actor_by_id(actor->id);
		struct world_spelunk_actor_report *report;
		uint32_t action_energy = runtime->state.rules.action_energy;
		int gained_energy;
		int dx = runtime->state.x - actor->x;
		int dy = runtime->state.y - actor->y;

		if (!definition) return false;
		gained_energy = turn_energy(definition->speed);
		if (gained_energy <= 0) return false;
		/* Side-view actors participate in Angband's shared energy clock.  Cap at
		 * one ready action while out of sight rather than overflowing or banking
		 * an arbitrary burst of attacks for later. */
		if ((uint32_t)gained_energy >= action_energy - actor->energy) {
			actor->energy = action_energy;
		} else {
			actor->energy += (uint32_t)gained_energy;
		}
		if (actor->energy < action_energy ||
				!world_spelunk_visibility_is_visible(runtime,
					actor->x, actor->y)) {
			continue;
		}
		actor->energy -= action_energy;
		report = &reports[count++];
		report->actor_id = definition->id;
		report->from_x = actor->x;
		report->from_y = actor->y;
		report->to_x = actor->x;
		report->to_y = actor->y;
		if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
			report->event = test_hit(definition->to_hit,
				p->state.ac + p->state.to_a) ?
				WORLD_SPELUNK_ACTOR_EVENT_HIT :
				WORLD_SPELUNK_ACTOR_EVENT_MISSED;
			if (report->event == WORLD_SPELUNK_ACTOR_EVENT_HIT) {
				report->damage = damroll(definition->damage_dice,
					definition->damage_sides);
				take_hit(p, report->damage, definition->death_cause);
			}
		} else if (dy == 0) {
			int step = dx > 0 ? 1 : -1;
			int next_x = actor->x + step;

			if (next_x != runtime->state.x &&
					actor_can_step(runtime, next_x, actor->y)) {
				actor->x = next_x;
				report->to_x = next_x;
				report->event = WORLD_SPELUNK_ACTOR_EVENT_MOVED;
			}
		}
		if (report->event == WORLD_SPELUNK_ACTOR_EVENT_NONE) count--;
	}
	*written = count;
	return world_spelunk_runtime_is_valid(runtime);
}
