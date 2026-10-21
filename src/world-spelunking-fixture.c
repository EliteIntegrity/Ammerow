/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
/** Persistent traps and supply chests, independent of the native chunk. */
#include "angband.h"
#include "datafile.h"
#include "message.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player.h"
#include "player-calcs.h"
#include "player-timed.h"
#include "player-util.h"
#include "world-spelunking-door.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-visibility.h"
#include "z-rand.h"

#define FIXTURE_DEFINITION_MAX 32
struct fixture_data {
	size_t count;
	struct world_spelunk_fixture_definition entries[FIXTURE_DEFINITION_MAX];
};
static struct fixture_data data;

const struct world_spelunk_fixture_definition *world_spelunk_fixture_by_id(const char *id)
{
	size_t i;
	for (i = 0; id && i < data.count; i++) {
		if (streq(id, data.entries[i].id)) return &data.entries[i];
	}
	return NULL;
}

static enum parser_error parse_fixture(struct parser *p, bool chest)
{
	struct fixture_data *parsed = parser_priv(p);
	struct world_spelunk_fixture_definition d = { 0 };
	const char *id = parser_getsym(p, "id"), *name = parser_getstr(p, "name");
	size_t i;
	if (!world_id_is_valid(id) || !name[0] || strlen(name) >= sizeof(d.name))
		return PARSE_ERROR_INVALID_VALUE;
	for (i = 0; i < parsed->count; i++) {
		if (streq(id, parsed->entries[i].id)) return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (parsed->count == FIXTURE_DEFINITION_MAX) return PARSE_ERROR_TOO_MANY_ENTRIES;
	my_strcpy(d.id, id, sizeof(d.id));
	my_strcpy(d.name, name, sizeof(d.name));
	d.kind = chest ? SPELUNK_CHEST : SPELUNK_TRAP;
	if (chest) {
		const char *trap = parser_getsym(p, "trap"), *tval = parser_getsym(p, "tval");
		const char *sval = parser_getsym(p, "sval");
		int lock = parser_getint(p, "lock"), count = parser_getint(p, "count");
		if ((!streq(trap, "none") && !world_id_is_valid(trap)) ||
			strlen(tval) >= sizeof(d.tval) || strlen(sval) >= sizeof(d.sval) ||
			lock < 0 || lock > 7 || count < 1 || count > 5) return PARSE_ERROR_INVALID_VALUE;
		if (!streq(trap, "none")) my_strcpy(d.trap_id, trap, sizeof(d.trap_id));
		my_strcpy(d.tval, tval, sizeof(d.tval));
		my_strcpy(d.sval, sval, sizeof(d.sval));
		d.lock_power = lock;
		d.quantity = count;
	} else {
		int search = parser_getint(p, "search"), disarm = parser_getint(p, "disarm");
		int damage = parser_getint(p, "damage");
		if (search < 0 || search > 200 || disarm < 0 || disarm > 200 ||
			damage < 1 || damage > 100) return PARSE_ERROR_INVALID_VALUE;
		d.search_power = search;
		d.disarm_power = disarm;
		d.damage = damage;
	}
	parsed->entries[parsed->count++] = d;
	return PARSE_ERROR_NONE;
}
static enum parser_error parse_trap(struct parser *p) { return parse_fixture(p, false); }
static enum parser_error parse_chest(struct parser *p) { return parse_fixture(p, true); }
static struct parser *init_fixtures(void)
{
	struct parser *p = parser_new();
	parser_setpriv(p, mem_zalloc(sizeof(struct fixture_data)));
	parser_reg(p, "trap sym id int search int disarm int damage str name", parse_trap);
	parser_reg(p, "chest sym id int lock sym trap sym tval int count sym sval str name", parse_chest);
	return p;
}
static errr run_fixtures(struct parser *p) { return parse_file_quit_not_found(p, "spelunking_fixture"); }
static errr finish_fixtures(struct parser *p)
{
	struct fixture_data *parsed = parser_priv(p);
	errr result = parsed->count ? PARSE_ERROR_NONE : PARSE_ERROR_TOO_FEW_ENTRIES;
	if (!result) data = *parsed;
	mem_free(parsed);
	parser_destroy(p);
	return result;
}
static void cleanup_fixtures(void) { memset(&data, 0, sizeof(data)); }
struct file_parser spelunking_fixture_parser = {
	"spelunking_fixture", init_fixtures, run_fixtures, finish_fixtures, cleanup_fixtures
};

static struct object_kind *loot_kind(const struct world_spelunk_fixture_definition *d)
{
	int tval = tval_find_idx(d->tval);
	int sval = tval > 0 ? lookup_sval(tval, d->sval) : -1;
	return sval >= 0 ? lookup_kind(tval, sval) : NULL;
}

bool world_spelunk_fixture_data_validate(void)
{
	size_t i;
	for (i = 0; i < data.count; i++) {
		const struct world_spelunk_fixture_definition *d = &data.entries[i];
		const struct world_spelunk_fixture_definition *trap;
		if (d->kind != SPELUNK_CHEST) continue;
		if (!loot_kind(d)) return false;
		trap = world_spelunk_fixture_by_id(d->trap_id);
		if (d->trap_id[0] && (!trap || trap->kind != SPELUNK_TRAP)) return false;
	}
	return true;
}

const struct world_spelunk_fixture *world_spelunk_fixture_at(
	const struct world_spelunk_runtime *r, int x, int y)
{
	size_t i;
	for (i = 0; r && i < r->fixture_count && i < WORLD_SPELUNK_FIXTURE_MAX; i++) {
		if (r->fixtures[i].x == x && r->fixtures[i].y == y) return &r->fixtures[i];
	}
	return NULL;
}
struct world_spelunk_fixture *world_spelunk_fixture_at_mutable(
	struct world_spelunk_runtime *r, int x, int y)
{
	const struct world_spelunk_fixture *f = world_spelunk_fixture_at(r, x, y);
	return f ? &r->fixtures[f - r->fixtures] : NULL;
}
static bool fixture_site(const struct world_spelunk_runtime *r, int x, int y)
{
	int w = r->state.map.width;
	return x > 0 && y > 0 && x < w - 1 && y < r->state.map.height - 1 &&
		r->cells[y*w+x] == WORLD_SPELUNK_AIR &&
		r->cells[(y+1)*w+x] == WORLD_SPELUNK_ROCK &&
		!world_spelunk_door_at(r, x, y);
}
bool world_spelunk_fixture_add(struct world_spelunk_runtime *r,
	const char *id, int x, int y)
{
	const struct world_spelunk_fixture_definition *d = world_spelunk_fixture_by_id(id);
	struct world_spelunk_fixture *f;
	if (!r || !d || r->fixture_count >= WORLD_SPELUNK_FIXTURE_MAX ||
		!fixture_site(r, x, y) || world_spelunk_fixture_at(r, x, y) ||
		(x == r->state.x && y == r->state.y) ||
		world_spelunk_runtime_actor_at(r, x, y) ||
		world_spelunk_runtime_ground_object_at(r, x, y) ||
		r->pitons[y*r->state.map.width+x] || r->ropes[y*r->state.map.width+x]) return false;
	f = &r->fixtures[r->fixture_count++];
	memset(f, 0, sizeof(*f));
	my_strcpy(f->id, id, sizeof(f->id));
	f->x = x; f->y = y;
	f->state = d->lock_power ? SPELUNK_FIXTURE_LOCKED : SPELUNK_FIXTURE_READY;
	f->armed = d->kind == SPELUNK_TRAP || d->trap_id[0];
	return true;
}

bool world_spelunk_fixtures_valid(const struct world_spelunk_runtime *r)
{
	size_t i, j;
	if (!r || r->fixture_count > WORLD_SPELUNK_FIXTURE_MAX) return false;
	for (i = 0; i < r->fixture_count; i++) {
		const struct world_spelunk_fixture *f = &r->fixtures[i];
		const struct world_spelunk_fixture_definition *d;
		if (!memchr(f->id, 0, sizeof(f->id))) return false;
		d = world_spelunk_fixture_by_id(f->id);
		if (!d || !fixture_site(r, f->x, f->y) || f->state < SPELUNK_FIXTURE_READY ||
			f->state > SPELUNK_FIXTURE_SPENT || f->known > SPELUNK_FIXTURE_SPENT ||
			f->armed > 1 || f->trap_known > SPELUNK_TRAP_SAFE ||
			(f->known && !r->explored[f->y*r->state.map.width+f->x]) ||
			(f->trap_known && !f->known) ||
			(f->state == SPELUNK_FIXTURE_SPENT && f->armed) ||
			(f->state == SPELUNK_FIXTURE_LOCKED && !d->lock_power)) return false;
		if (d->kind == SPELUNK_TRAP &&
			(f->state == SPELUNK_FIXTURE_LOCKED ||
			 f->armed != (f->state == SPELUNK_FIXTURE_READY))) return false;
		if (d->kind == SPELUNK_CHEST && !d->trap_id[0] && f->armed) return false;
		for (j = i + 1; j < r->fixture_count; j++) {
			if (r->fixtures[j].x == f->x && r->fixtures[j].y == f->y) return false;
		}
	}
	return true;
}

static void learn_fixture(struct world_spelunk_runtime *r, struct world_spelunk_fixture *f,
	bool trap)
{
	if (f->state != SPELUNK_FIXTURE_LOCKED || f->known != SPELUNK_FIXTURE_LOCKED)
		f->known = f->state == SPELUNK_FIXTURE_LOCKED ? SPELUNK_FIXTURE_READY : f->state;
	if (trap) f->trap_known = f->armed ? SPELUNK_TRAP_ARMED : SPELUNK_TRAP_SAFE;
	r->explored[f->y*r->state.map.width+f->x] = 1;
}
void world_spelunk_fixtures_observe(struct world_spelunk_runtime *r)
{
	size_t i;
	for (i = 0; i < r->fixture_count; i++) {
		struct world_spelunk_fixture *f = &r->fixtures[i];
		const struct world_spelunk_fixture_definition *d = world_spelunk_fixture_by_id(f->id);
		if (!d || !world_spelunk_visibility_is_visible(r, f->x, f->y)) continue;
		if (d->kind == SPELUNK_CHEST || f->known) {
			learn_fixture(r, f, f->trap_known != SPELUNK_TRAP_UNKNOWN);
		}
	}
}
void world_spelunk_fixture_notice(struct player *p)
{
	struct world_spelunk_runtime *r;
	size_t i;
	if (!p || !(r = p->spelunking) || p->is_dead || p->timed[TMD_BLIND] ||
		p->timed[TMD_CONFUSED] || p->timed[TMD_IMAGE]) return;
	for (i = 0; i < r->fixture_count; i++) {
		struct world_spelunk_fixture *f = &r->fixtures[i];
		const struct world_spelunk_fixture_definition *d = world_spelunk_fixture_by_id(f->id);
		const struct world_spelunk_fixture_definition *trap = d->kind == SPELUNK_TRAP ? d :
			world_spelunk_fixture_by_id(d->trap_id);
		if (!trap || !f->armed || f->trap_known || abs(f->x-r->state.x) > 1 ||
			abs(f->y-r->state.y) > 1 || !world_spelunk_visibility_is_visible(r, f->x, f->y) ||
			p->state.skills[SKILL_SEARCH] < trap->search_power) continue;
		learn_fixture(r, f, true);
		msg(d->kind == SPELUNK_TRAP ? "You notice a %s." : "You notice a trap on the %s.", d->name);
		disturb(p);
		p->upkeep->redraw |= PR_MAP;
	}
}
static void trigger(struct player *p, struct world_spelunk_fixture *f)
{
	const struct world_spelunk_fixture_definition *d = world_spelunk_fixture_by_id(f->id);
	const struct world_spelunk_fixture_definition *trap = d->kind == SPELUNK_TRAP ? d :
		world_spelunk_fixture_by_id(d->trap_id);
	if (!f->armed || !trap || p->is_dead) return;
	/* Consume the mechanism before HP/death processing: no reload/retrigger. */
	f->armed = 0;
	if (d->kind == SPELUNK_TRAP) f->state = SPELUNK_FIXTURE_SPENT;
	learn_fixture(p->spelunking, f, true);
	msgt(MSG_HIT, "The %s strikes you for %u damage!", trap->name, trap->damage);
	disturb(p);
	take_hit(p, trap->damage, trap->name);
	p->upkeep->redraw |= PR_MAP | PR_HP;
}
void world_spelunk_fixture_step(struct player *p)
{
	struct world_spelunk_runtime *r;
	struct world_spelunk_fixture *f;
	const struct world_spelunk_fixture_definition *d;
	if (!p || !(r = p->spelunking) || p->is_dead || !world_spelunk_is_grounded(&r->state)) return;
	f = world_spelunk_fixture_at_mutable(r, r->state.x, r->state.y);
	d = f ? world_spelunk_fixture_by_id(f->id) : NULL;
	if (d && d->kind == SPELUNK_TRAP) trigger(p, f);
}
bool world_spelunk_fixture_detect(struct world_spelunk_runtime *r,
	int vertical, int horizontal, bool traps)
{
	bool found = false;
	size_t i;
	for (i = 0; i < r->fixture_count; i++) {
		struct world_spelunk_fixture *f = &r->fixtures[i];
		const struct world_spelunk_fixture_definition *d = world_spelunk_fixture_by_id(f->id);
		if (abs(f->x-r->state.x) > horizontal || abs(f->y-r->state.y) > vertical) continue;
		if (traps ? !f->armed : d->kind != SPELUNK_CHEST) continue;
		learn_fixture(r, f, traps);
		found = true;
	}
	return found;
}

bool world_spelunk_fixture_interact(struct player *p,
	struct world_spelunk_fixture *f, bool disarm)
{
	struct world_spelunk_runtime *r = p->spelunking;
	const struct world_spelunk_fixture_definition *d = world_spelunk_fixture_by_id(f->id);
	const struct world_spelunk_fixture_definition *trap = d->kind == SPELUNK_TRAP ? d :
		world_spelunk_fixture_by_id(d->trap_id);
	if (disarm) {
		int skill = p->state.skills[SKILL_DISARM_PHYS], chance;
		if (!f->known || f->trap_known != SPELUNK_TRAP_ARMED || !f->armed || !trap) {
			msg("There is no known armed trap there.");
			return false;
		}
		if (p->timed[TMD_BLIND] || p->timed[TMD_CONFUSED] || p->timed[TMD_IMAGE]) skill /= 10;
		chance = MAX(2, skill - trap->disarm_power);
		if (randint0(100) < chance) {
			f->armed = 0;
			if (d->kind == SPELUNK_TRAP) f->state = SPELUNK_FIXTURE_SPENT;
			learn_fixture(r, f, true);
			msgt(MSG_DISARM, "You disarm the %s.", trap->name);
			player_exp_gain(p, 1);
		} else if (randint0(100) < chance) {
			msg("You fail to disarm the %s.", trap->name);
		} else {
			trigger(p, f);
		}
		return true;
	}
	if (d->kind != SPELUNK_CHEST || !f->known || f->state == SPELUNK_FIXTURE_SPENT) {
		msg("There is no closed chest there.");
		return false;
	}
	/* Capacity failure cannot consume the latch, trap, RNG or treasure. */
	if (!world_spelunk_runtime_can_add_ground_object_at(r, f->x, f->y)) {
		msg("There is no room to unpack the chest's contents.");
		return false;
	}
	if (f->state == SPELUNK_FIXTURE_LOCKED) {
		f->known = SPELUNK_FIXTURE_LOCKED;
		if (randint0(100) >= calc_unlocking_chance(p, d->lock_power, p->timed[TMD_BLIND] != 0)) {
			msgt(MSG_LOCKPICK_FAIL, "You fail to pick the %s's lock.", d->name);
			return true;
		}
		f->state = SPELUNK_FIXTURE_READY;
		sound(MSG_LOCKPICK);
	}
	/* A triggered chest trap spends this attempt. A subsequent Open retrieves
	 * the contents, unless the trap killed the player. */
	if (f->armed) { trigger(p, f); return true; }
	{
		struct object *obj = object_new();
		struct randomizer_state rng;
		Rand_state_export(&rng);
		object_prep(obj, loot_kind(d), 0, AVERAGE);
		Rand_state_import(&rng);
		obj->number = d->quantity;
		obj->origin = ORIGIN_CHEST;
		obj->known = object_new();
		object_copy(obj->known, obj);
		obj->known->known = NULL;
		if (!world_spelunk_runtime_add_ground_object(r, obj, f->x, f->y)) {
			struct object *known = obj->known;
			obj->known = NULL;
			object_delete(NULL, NULL, &known);
			object_delete(NULL, NULL, &obj);
			return false;
		}
	}
	f->state = SPELUNK_FIXTURE_SPENT;
	learn_fixture(r, f, true);
	msgt(MSG_OPENDOOR, "You open the %s. Its supplies lie beside the empty chest.", d->name);
	return true;
}
