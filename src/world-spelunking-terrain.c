/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */

/** One-cell excavation, with an explicit conservative structural boundary.
 * Water, load-bearing fixtures and the certified return rail are not a fluid
 * or collapse simulation. Refuse those cuts before spending resources. */
#include "angband.h"
#include "object.h"
#include "player.h"
#include "player-calcs.h"
#include "player-util.h"
#include "player-resource.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-door.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-terrain.h"
#include "world-spelunking-traversal-proof.h"
#include "world-spelunking-visibility.h"

struct cut_plan {
	struct world_spelunk_state player_state;
	struct world_spelunk_action_report fall;
	struct world_spelunk_actor actors[WORLD_SPELUNK_ACTOR_MAX];
	int object_y[WORLD_SPELUNK_OBJECT_MAX];
};

static enum world_spelunk_tile tile(const struct world_spelunk_runtime *r, int x, int y)
{
	if (x < 0 || y < 0 || x >= r->state.map.width || y >= r->state.map.height)
		return WORLD_SPELUNK_ROCK;
	return r->cells[y * r->state.map.width + x];
}

static const struct world_spelunk_material_definition *material(
	const struct world_spelunk_runtime *r, int x, int y)
{
	if (!r || !r->material_tags || x < 0 || y < 0 ||
		x >= r->state.map.width || y >= r->state.map.height) return NULL;
	return world_spelunk_geology_material_by_tag(
		world_spelunk_geology_profile_by_id(r->geology_profile_id),
		r->material_tags[y*r->state.map.width+x]);
}

const char *world_spelunk_rock_name(const struct world_spelunk_runtime *r, int x, int y)
{
	const struct world_spelunk_material_definition *m = material(r, x, y);
	return m ? m->name : "cave rock";
}

int world_spelunk_dig_chance(const struct world_spelunk_runtime *r, int x, int y, int skill)
{
	const struct world_spelunk_material_definition *m = material(r, x, y);
	int threshold = m ? m->dig_threshold : 40;
	int multiplier = m ? m->dig_multiplier : 1;
	/* Clamp before multiplying: even extreme modded skills cannot overflow. */
	return MIN(MAX(0, MIN(skill, threshold + 1600) - threshold) * multiplier, 1600);
}

static bool route_rock(const struct player *p, int x, int y)
{
	const struct world_spelunk_system *s = p->spelunking_system;
	struct world_spelunk_generated_layout g;
	size_t i;
	bool protected = false;
	if (!s) return p->spelunking->layout_revision != 0;
	/* Old authored sections have no certified excavation envelope. */
	if (world_spelunk_reconstruct_node_candidate(s, s->active_node_id,
		p->spelunking->state.rules.action_energy, &g) != WORLD_SPELUNK_RECONSTRUCTION_OK)
		return true;
	if (abs(x-g.entrance_x) <= 1 && abs(y-g.entrance_y) <= 1) protected = true;
	for (i = 0; i < g.witness_step_count && !protected; i++) {
		if (abs(x-g.witness_steps[i].expected_x) <= 1 &&
			abs(y-g.witness_steps[i].expected_y) <= 1) protected = true;
	}
	world_spelunk_generated_layout_dispose(&g);
	return protected;
}

static int landing_y(const struct world_spelunk_runtime *r, int x, int y)
{
	while (y < r->state.map.height-1 &&
		!world_spelunk_map_blocks(&r->state.map, x, y+1) &&
		tile(r, x, y) != WORLD_SPELUNK_WATER) y++;
	return y;
}

static enum world_spelunk_cut_result plan_cut(struct player *p, int x, int y,
	int stamina_cost, struct cut_plan *plan)
{
	struct world_spelunk_runtime *r;
	struct object *obj;
	size_t i, j, index;
	enum world_spelunk_cut_result result = SPELUNK_CUT_OK;
	if (!p || !plan || stamina_cost < 0 || p->is_dead || !world_spelunk_player_is_active(p)) return SPELUNK_CUT_INVALID;
	r = p->spelunking;
	if (x <= 0 || y <= 0 || x >= r->state.map.width-1 || y >= r->state.map.height-1)
		return SPELUNK_CUT_BOUNDARY;
	if (tile(r,x,y) != WORLD_SPELUNK_ROCK) return SPELUNK_CUT_NOT_ROCK;
	if (tile(r,x-1,y) == WORLD_SPELUNK_WATER || tile(r,x+1,y) == WORLD_SPELUNK_WATER ||
		tile(r,x,y-1) == WORLD_SPELUNK_WATER) return SPELUNK_CUT_WATER;
	if (route_rock(p,x,y)) return SPELUNK_CUT_ROUTE;
	index = (size_t)y*r->state.map.width+x;
	/* Only the cell is borrowed temporarily; all movement is calculated on
	 * copies. No callback, visibility update, RNG or player mutation occurs. */
	r->cells[index] = WORLD_SPELUNK_AIR;
	for (i = 0; i < r->door_count; i++) {
		if (!world_spelunk_door_site(r, r->doors[i].x, r->doors[i].y)) {
			result = SPELUNK_CUT_FIXTURE; goto done;
		}
	}
	for (i = 0; i < r->fixture_count; i++) {
		if (tile(r, r->fixtures[i].x, r->fixtures[i].y+1) != WORLD_SPELUNK_ROCK) {
			result = SPELUNK_CUT_FIXTURE; goto done;
		}
	}
	for (i = 0; i < (size_t)r->state.map.width*r->state.map.height; i++) {
		int px = (int)(i % r->state.map.width), py = (int)(i / r->state.map.width);
		if (r->pitons[i] && tile(r,px-1,py) != WORLD_SPELUNK_ROCK &&
			tile(r,px+1,py) != WORLD_SPELUNK_ROCK && tile(r,px,py-1) != WORLD_SPELUNK_ROCK &&
			tile(r,px,py+1) != WORLD_SPELUNK_ROCK) {
			result = SPELUNK_CUT_ANCHOR; goto done;
		}
	}
	plan->player_state = r->state;
	plan->player_state.stamina = MAX(0, player_stamina_current(p)-stamina_cost);
	if (!world_spelunk_settle_terrain(&plan->player_state, &plan->fall)) {
		result = SPELUNK_CUT_INVALID; goto done;
	}
	memcpy(plan->actors, r->actors, sizeof(plan->actors));
	for (i = 0; i < r->actor_count; i++) {
		struct world_spelunk_actor *a = &plan->actors[i];
		/* Current cave creatures are ground-bound. Only newly unsupported
		 * occupants settle; unrelated legacy placements are left alone. */
		if (a->x == x && a->y == y-1) {
			struct world_spelunk_state body = r->state;
			struct world_spelunk_action_report fall;
			body.x = a->x; body.y = a->y; body.fall_start_y = a->y;
			body.grip_target_x = body.grip_target_y = -1;
			body.map.pitons = body.map.ropes = NULL;
			body.map.infrastructure_stride = 0;
			body.movement = WORLD_SPELUNK_STANDING;
			body.jump_holding = false;
			if (!world_spelunk_settle_terrain(&body, &fall) ||
				tile(r,body.x,body.y) != WORLD_SPELUNK_AIR) {
				result = SPELUNK_CUT_OCCUPIED; goto done;
			}
			a->y = body.y;
			a->hp = fall.damage >= a->hp ? 0 : a->hp-fall.damage;
		}
	}
	for (i = 0; i < r->actor_count; i++) {
		const struct world_spelunk_actor *a = &plan->actors[i];
		if (a->hp == 0) continue;
		if (a->x == plan->player_state.x && a->y == plan->player_state.y) {
			result = SPELUNK_CUT_OCCUPIED; goto done;
		}
		for (j = i+1; j < r->actor_count; j++) {
			if (plan->actors[j].hp && a->x == plan->actors[j].x && a->y == plan->actors[j].y) {
				result = SPELUNK_CUT_OCCUPIED; goto done;
			}
		}
	}
	for (i = 0, obj = r->ground_objects; obj; obj = obj->next, i++) {
		plan->object_y[i] = obj->grid.y;
		if (obj->grid.x == x && obj->grid.y == y-1) {
			plan->object_y[i] = landing_y(r, x, obj->grid.y);
			if (tile(r,x,plan->object_y[i]) != WORLD_SPELUNK_AIR) {
				result = SPELUNK_CUT_OCCUPIED; goto done;
			}
		}
	}
done:
	r->cells[index] = WORLD_SPELUNK_ROCK;
	return result;
}

enum world_spelunk_cut_result world_spelunk_cut_check(struct player *p, int x, int y,
	int stamina_cost)
{
	struct cut_plan plan;
	return plan_cut(p,x,y,stamina_cost,&plan);
}

enum world_spelunk_cut_result world_spelunk_cut_rock(struct player *p, int x, int y)
{
	struct cut_plan plan;
	enum world_spelunk_cut_result result = plan_cut(p,x,y,0,&plan);
	struct world_spelunk_runtime *r;
	struct object *obj;
	size_t i, index;
	if (result != SPELUNK_CUT_OK) return result;
	r = p->spelunking;
	index = (size_t)y*r->state.map.width+x;
	r->cells[index] = WORLD_SPELUNK_AIR;
	if (r->material_tags) r->material_tags[index] = WORLD_SPELUNK_GEOLOGY_TAG_NONE;
	world_spelunk_runtime_prune_decorations(r);
	r->state = plan.player_state;
	memcpy(r->actors, plan.actors, sizeof(r->actors));
	/* Reverse removal keeps the remaining planned coordinates intact. Falls
	 * are environmental deaths, not attacks with fabricated combat rewards. */
	for (i = r->actor_count; i > 0; i--) {
		if (!r->actors[i-1].hp) world_spelunk_runtime_remove_actor(r, &r->actors[i-1]);
	}
	for (i = 0, obj = r->ground_objects; obj; obj = obj->next, i++) {
		obj->grid.y = plan.object_y[i];
		obj->known->grid = obj->grid;
	}
	world_spelunk_visibility_follow_player(r);
	p->upkeep->redraw |= PR_MAP | PR_STATUS | PR_ITEMLIST | PR_MONLIST;
	if (plan.fall.fall_tiles > 0) msg("The cut gives way beneath you. You fall %d tile%s!",
		plan.fall.fall_tiles, PLURAL(plan.fall.fall_tiles));
	if (plan.fall.damage > 0) take_hit(p, MIN(plan.fall.damage, p->chp+1), "a fall while tunnelling");
	if (!p->is_dead) {
		world_spelunk_fixture_step(p);
		world_spelunk_fixture_notice(p);
	}
	event_signal_point(EVENT_MAP, -1, -1);
	return SPELUNK_CUT_OK;
}

const char *world_spelunk_cut_reason(enum world_spelunk_cut_result result)
{
	switch (result) {
	case SPELUNK_CUT_NOT_ROCK: return "There is no cave rock there to tunnel.";
	case SPELUNK_CUT_BOUNDARY: return "The outer bedrock cannot be excavated.";
	case SPELUNK_CUT_WATER: return "That rock retains a pool. You cannot safely drain it.";
	case SPELUNK_CUT_FIXTURE: return "That rock supports a cave fixture. Leave its foundation intact.";
	case SPELUNK_CUT_ANCHOR: return "That cut would loosen a piton and its rope anchor.";
	case SPELUNK_CUT_ROUTE: return "That rock secures the cave's return route. Leave it intact.";
	case SPELUNK_CUT_OCCUPIED: return "Something above would have no safe landing after that cut.";
	default: return "You cannot safely excavate that rock now.";
	}
}
