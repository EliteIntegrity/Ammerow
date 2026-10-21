/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */

/** Conservative, top-down continuous exploration. Schedules ordinary paid
 * moves, never combat, rest, doors, disarming, digging or transitions.
 * Planning and loot comparisons use player knowledge, not hidden data. */
#include "angband.h"
#include "cave.h"
#include "cmd-core.h"
#include "cmds.h"
#include "game-world.h"
#include "init.h"
#include "monster.h"
#include "obj-ignore.h"
#include "obj-util.h"
#include "player-explore.h"
#include "player-timed.h"
#include "player-util.h"
#include "world-entry.h"
#include "world-turn.h"

struct explored_loot {
	const struct object_kind *kind;
	struct loc grid;
	int number;
};

struct player_exploration {
	struct chunk *known;
	int width, height, moves;
	int32_t starting_gold;
	struct loc last;
	uint8_t *visited;
	struct explored_loot *loot;
	int loot_count;
	int *queue;
	int *directions;
};

bool player_explore_active(const struct player *p)
{
	return p && p->upkeep && p->upkeep->exploration;
}

bool player_explore_new_loot(const struct player *p)
{
	const struct player_exploration *e;
	struct loc g;
	if (!player_explore_active(p)) return false;
	e = p->upkeep->exploration;
	if (p->cave != e->known) return false;
	for (g.y = 0; g.y < e->height; g.y++) {
		for (g.x = 0; g.x < e->width; g.x++) {
			const struct object *obj;
			for (obj = square_object(p->cave, g); obj; obj = obj->next) {
				const struct explored_loot *old;
				if (!obj->kind || ignore_known_item_ok(p, obj)) continue;
				if (obj->oidx >= e->loot_count) return true;
				old = &e->loot[obj->oidx];
				/* Picking up some of an acknowledged stack is not discovery. */
				if (old->kind != obj->kind || !loc_eq(old->grid, g) ||
						obj->number > old->number) return true;
			}
		}
	}
	return false;
}

static const char *monster_stop_reason(const struct monster *mon)
{
	/* VIEW includes telepathy in line of sight, not just optical vision.
	 * Neither flag guarantees the creature fits on a zoomed/panned screen. */
	return monster_is_in_view(mon) ?
		"a creature is in view (seen or sensed; check the monster list)" :
		"a creature is detected beyond normal sight (check the monster list)";
}

static const struct monster *perceived_monster(void)
{
	if (!cave) return NULL;
	for (int i = 1; i < cave_monster_max(cave); i++) {
		const struct monster *mon = cave_monster(cave, i);
		if (mon->race && monster_is_obvious(mon)) return mon;
	}
	return NULL;
}

static const char *unsafe_player(const struct player *p)
{
	const struct monster *mon;
	if (p->is_dead || p->upkeep->generate_level ||
			world_turn_context_for_player(p) != WORLD_TURN_CONTEXT_TOP_DOWN)
		return "location changed";
	mon = perceived_monster();
	if (mon) return monster_stop_reason(mon);
	if (p->timed[TMD_CONFUSED] || p->timed[TMD_BLIND] ||
			p->timed[TMD_IMAGE] || p->timed[TMD_PARALYZED] ||
			p->timed[TMD_BLOODLUST] || p->timed[TMD_COMMAND] ||
			p->timed[TMD_AFRAID] || p->timed[TMD_STUN])
		return "your condition needs attention";
	if (p->timed[TMD_POISONED] || p->timed[TMD_CUT])
		return "ongoing damage needs attention";
	if (p->chp <= p->mhp / 4) return "health is low";
	if (p->timed[TMD_FOOD] < PY_FOOD_HUNGRY) return "you are hungry";
	if (no_light(p)) return "you have no usable light";
	if (square_iswebbed(cave, p->grid)) return "a web needs attention";
	return NULL;
}

/* Also used before a queued pickup: a world turn may have revealed danger
 * after the previous step, before the next exploration command runs. */
bool player_explore_check(struct player *p)
{
	const char *reason;
	if (!player_explore_active(p)) return true;
	reason = unsafe_player(p);
	if (!reason && player_explore_new_loot(p)) reason = "new loot discovered";
	if (!reason) return true;
	player_explore_cancel(p, reason);
	disturb(p);
	return false;
}

void player_explore_free(struct player *p)
{
	struct player_exploration *e;
	if (!player_explore_active(p)) return;
	e = p->upkeep->exploration;
	p->upkeep->exploration = NULL;
	mem_free(e->visited);
	mem_free(e->loot);
	mem_free(e->queue);
	mem_free(e->directions);
	mem_free(e);
}

void player_explore_cancel(struct player *p, const char *reason)
{
	struct player_exploration *e;
	int moves;
	int32_t gold;
	if (!player_explore_active(p)) return;
	e = p->upkeep->exploration;
	moves = e->moves + (loc_eq(p->grid, e->last) ? 0 : 1);
	gold = p->au - e->starting_gold;
	/* A generic disturbance is not evidence that a particular currently
	 * perceived creature, item or condition caused it. Callers with a known
	 * cause supply that cause before disturbing. */
	player_explore_free(p);
	msg("Explore stopped: %s. %d %s; %ld gold collected.",
		reason ? reason : "interrupted", moves,
		moves == 1 ? "step" : "steps", (long)MAX(0, gold));
}

void player_explore_notice_monster(struct player *p, const struct monster *mon)
{
	if (!player_explore_active(p) || !mon || !mon->race ||
			!monster_is_obvious(mon)) return;
	player_explore_cancel(p, monster_stop_reason(mon));
	disturb(p);
}

static void stop(struct player *p, const char *reason)
{
	player_explore_cancel(p, reason);
	disturb(p);
}

/* No relaxed fallback through traps/unknown terrain, doors or rubble. */
static bool traversable(const struct player *p, struct loc g)
{
	struct chunk *known = p->cave;
	if (!square_in_bounds_fully(known, g) || !square_isknown(known, g) ||
			!square_ispassable(known, g) || square_isdamaging(known, g) ||
			square_isvisibletrap(known, g) || square_isshop(known, g) ||
			square_isstairs(known, g) || square_iswebbed(known, g) ||
			square_iswater(known, g)) return false;
	/* Authored exits are queried only for a remembered cell. */
	return !world_entry_is_edge_exit_grid(cave, g) &&
		!world_entry_is_overworld_gate_grid(cave, g) &&
		!world_entry_cross_mode_route_here(cave, g);
}

static bool frontier(const struct player *p, struct loc g)
{
	for (int i = 0; i < 8; i++) {
		struct loc n = loc_sum(g, ddgrid[ddd[i]]);
		if (square_in_bounds_fully(p->cave, n) && !square_isknown(p->cave, n))
			return true;
	}
	return false;
}

/* BFS matches ordinary eight-way walking. Visited frontiers cannot lure a
 * session back and forth when the remaining darkness is behind solid rock. */
static int next_direction(struct player *p)
{
	struct player_exploration *e = p->upkeep->exploration;
	int count = e->width * e->height, head = 0, tail = 0;
	int origin = p->grid.y * e->width + p->grid.x;
	for (int i = 0; i < count; i++) e->directions[i] = -1;
	e->queue[tail++] = origin;
	e->directions[origin] = 0;
	e->visited[origin] = 1;
	while (head < tail) {
		int cell = e->queue[head++];
		struct loc g = loc(cell % e->width, cell / e->width);
		if (!e->visited[cell] && frontier(p, g)) return e->directions[cell];
		for (int i = 0; i < 8; i++) {
			int dir = ddd[i];
			struct loc n = loc_sum(g, ddgrid[dir]);
			int index;
			if (!traversable(p, n)) continue;
			index = n.y * e->width + n.x;
			if (e->directions[index] != -1) continue;
			e->directions[index] = cell == origin ? dir : e->directions[cell];
			e->queue[tail++] = index;
		}
	}
	return 0;
}

void player_explore_command(struct command *cmd)
{
	struct player *p = player;
	struct player_exploration *e;
	const char *reason;
	int dir, count;
	struct loc g;
	if (cmd->background_command && !player_explore_active(p)) return;
	if (!OPT(p, autoexplore_commands) || !OPT(p, autoexplore_continuous)) {
		if (player_explore_active(p)) stop(p, "exploration setting changed");
		return;
	}
	reason = unsafe_player(p);
	if (reason) {
		if (player_explore_active(p)) stop(p, reason);
		else msg("Cannot explore: %s.", reason);
		return;
	}
	if (!player_explore_active(p)) {
		/* A deliberate restart acknowledges all currently known loot. */
		e = mem_zalloc(sizeof(*e));
		e->known = p->cave;
		e->width = p->cave->width;
		e->height = p->cave->height;
		e->last = p->grid;
		e->starting_gold = p->au;
		count = e->width * e->height;
		e->visited = mem_zalloc(count * sizeof(*e->visited));
		e->loot_count = p->cave->obj_max;
		e->loot = mem_zalloc(e->loot_count * sizeof(*e->loot));
		e->queue = mem_alloc(count * sizeof(*e->queue));
		e->directions = mem_alloc(count * sizeof(*e->directions));
		for (g.y = 0; g.y < e->height; g.y++) {
			for (g.x = 0; g.x < e->width; g.x++) {
				const struct object *obj;
				for (obj = square_object(p->cave, g); obj; obj = obj->next) {
					if (!obj->kind || ignore_known_item_ok(p, obj)) continue;
					assert(obj->oidx < e->loot_count);
					e->loot[obj->oidx].kind = obj->kind;
					e->loot[obj->oidx].grid = g;
					e->loot[obj->oidx].number = obj->number;
				}
			}
		}
		p->upkeep->exploration = e;
		p->upkeep->running = 1; /* Normal interrupt polling and flushing. */
		p->upkeep->running_firststep = true;
		msg("Exploring... any key or click stops. Combat remains manual.");
	}
	e = p->upkeep->exploration;
	if (e->known != p->cave || p->cave->width != e->width ||
			p->cave->height != e->height) {
		stop(p, "location changed");
		return;
	}
	if (player_explore_new_loot(p)) {
		stop(p, "new loot discovered");
		return;
	}
	if (e->moves >= e->width * e->height * 8) {
		stop(p, "step limit reached");
		return;
	}
	dir = next_direction(p);
	if (!dir) {
		stop(p, "no further reachable unexplored ground (doors and exits stay manual)");
		return;
	}
	g = loc_sum(p->grid, ddgrid[dir]);
	if (world_entry_edge_route_for_step(cave, p->grid, g)) {
		stop(p, "map exit ahead");
		return;
	}
	p->upkeep->energy_use = energy_per_move(p);
	move_player(dir, false);
	/* Movement's view/search/trap handling can disturb and free the session. */
	if (!player_explore_active(p)) return;
	e = p->upkeep->exploration;
	if (loc_eq(e->last, p->grid)) {
		stop(p, "route blocked");
		return;
	}
	e->moves++;
	e->last = p->grid;
	if (!player_explore_check(p)) return;
	if (cmdq_push(CMD_EXPLORE)) {
		stop(p, "command queue is full");
		return;
	}
	cmdq_peek()->background_command = 1;
}
