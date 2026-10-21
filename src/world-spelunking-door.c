/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
/** Persistent cave fixtures. Terrain stays immutable; doors are an overlay. */
#include "angband.h"
#include "datafile.h"
#include "world-spelunking-door.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-visibility.h"

#define DOOR_DEFINITION_MAX 16
struct door_data {
	size_t count;
	struct world_spelunk_door_definition definitions[DOOR_DEFINITION_MAX];
};
static struct door_data data;

const struct world_spelunk_door_definition *world_spelunk_door_by_id(const char *id)
{
	size_t i;
	for (i = 0; id && i < data.count; i++) {
		if (streq(id, data.definitions[i].id)) return &data.definitions[i];
	}
	return NULL;
}

static enum parser_error parse_door(struct parser *p)
{
	struct door_data *parsed = parser_priv(p);
	const char *id = parser_getsym(p, "id"), *name = parser_getstr(p, "name");
	int lock = parser_getint(p, "lock");
	struct world_spelunk_door_definition *d;
	size_t i;
	if (!world_id_is_valid(id) || !name[0] || strlen(name) >= 80 ||
		lock < 0 || lock > 7) return PARSE_ERROR_INVALID_VALUE;
	for (i = 0; i < parsed->count; i++) {
		if (streq(id, parsed->definitions[i].id)) return PARSE_ERROR_REPEATED_DIRECTIVE;
	}
	if (parsed->count == DOOR_DEFINITION_MAX) return PARSE_ERROR_TOO_MANY_ENTRIES;
	d = &parsed->definitions[parsed->count++];
	my_strcpy(d->id, id, sizeof(d->id));
	my_strcpy(d->name, name, sizeof(d->name));
	d->lock_power = (uint8_t)lock;
	return PARSE_ERROR_NONE;
}

static struct parser *init_doors(void)
{
	struct parser *p = parser_new();
	parser_setpriv(p, mem_zalloc(sizeof(struct door_data)));
	parser_reg(p, "door sym id int lock str name", parse_door);
	return p;
}
static errr run_doors(struct parser *p)
{
	return parse_file_quit_not_found(p, "spelunking_door");
}
static errr finish_doors(struct parser *p)
{
	struct door_data *parsed = parser_priv(p);
	errr result = parsed->count ? PARSE_ERROR_NONE : PARSE_ERROR_TOO_FEW_ENTRIES;
	if (!result) data = *parsed;
	mem_free(parsed);
	parser_destroy(p);
	return result;
}
static void cleanup_doors(void) { memset(&data, 0, sizeof(data)); }
struct file_parser spelunking_door_parser = {
	"spelunking_door", init_doors, run_doors, finish_doors, cleanup_doors
};

const struct world_spelunk_door *world_spelunk_door_at(
	const struct world_spelunk_runtime *r, int x, int y)
{
	size_t i;
	for (i = 0; r && i < r->door_count && i < WORLD_SPELUNK_DOOR_MAX; i++) {
		if (r->doors[i].x == x && r->doors[i].y == y) return &r->doors[i];
	}
	return NULL;
}

struct world_spelunk_door *world_spelunk_door_at_mutable(
	struct world_spelunk_runtime *r, int x, int y)
{
	const struct world_spelunk_door *d = world_spelunk_door_at(r, x, y);
	return d ? &r->doors[d - r->doors] : NULL;
}

bool world_spelunk_door_site(const struct world_spelunk_runtime *r, int x, int y)
{
	int w;
	if (!r || x < 1 || y < 1 || x >= r->state.map.width - 1 ||
		y >= r->state.map.height - 1) return false;
	w = r->state.map.width;
	/* One-cell dry throat with permanent floor and lintel. No supports,
	 * floating doors, water dams, or changes to certified terrain geometry. */
	return r->cells[y*w+x] == WORLD_SPELUNK_AIR &&
		r->cells[(y-1)*w+x] == WORLD_SPELUNK_ROCK &&
		r->cells[(y+1)*w+x] == WORLD_SPELUNK_ROCK &&
		r->cells[y*w+x-1] == WORLD_SPELUNK_AIR &&
		r->cells[y*w+x+1] == WORLD_SPELUNK_AIR &&
		r->cells[(y+1)*w+x-1] == WORLD_SPELUNK_ROCK &&
		r->cells[(y+1)*w+x+1] == WORLD_SPELUNK_ROCK;
}

void world_spelunk_doors_rebuild(struct world_spelunk_runtime *r)
{
	size_t i;
	if (!r || !r->blocked || r->door_count > WORLD_SPELUNK_DOOR_MAX) return;
	memset(r->blocked, 0, (size_t)r->state.map.width * r->state.map.height);
	for (i = 0; i < r->door_count; i++) {
		const struct world_spelunk_door *d = &r->doors[i];
		if (d->x >= 0 && d->y >= 0 && d->x < r->state.map.width &&
			d->y < r->state.map.height) {
			r->blocked[d->y*r->state.map.width+d->x] = d->state != WORLD_SPELUNK_DOOR_OPEN;
		}
	}
}

bool world_spelunk_doors_valid(const struct world_spelunk_runtime *r)
{
	size_t i, j;
	if (!r || !r->blocked || !r->explored || !r->pitons || !r->ropes ||
		r->state.map.blocked != r->blocked ||
		r->door_count > WORLD_SPELUNK_DOOR_MAX) return false;
	for (i = 0; i < r->door_count; i++) {
		const struct world_spelunk_door *d = &r->doors[i];
		const struct world_spelunk_door_definition *def;
		if (!memchr(d->id, 0, sizeof(d->id))) return false;
		def = world_spelunk_door_by_id(d->id);
		if (!def || !world_spelunk_door_site(r, d->x, d->y) ||
			d->state < WORLD_SPELUNK_DOOR_CLOSED || d->state > WORLD_SPELUNK_DOOR_LOCKED ||
			d->known > WORLD_SPELUNK_DOOR_LOCKED ||
			(d->known && !r->explored[d->y*r->state.map.width+d->x]) ||
			(d->state == WORLD_SPELUNK_DOOR_LOCKED && !def->lock_power) ||
			r->pitons[d->y*r->state.map.width+d->x] ||
			r->ropes[d->y*r->state.map.width+d->x]) return false;
		if (d->state != WORLD_SPELUNK_DOOR_OPEN &&
			((r->state.x == d->x && r->state.y == d->y) ||
			 world_spelunk_runtime_actor_at(r, d->x, d->y) ||
			 world_spelunk_runtime_ground_object_at(r, d->x, d->y))) return false;
		for (j = i + 1; j < r->door_count; j++) {
			if (r->doors[j].x == d->x && r->doors[j].y == d->y) return false;
		}
	}
	for (i = 0; i < (size_t)r->state.map.width * r->state.map.height; i++) {
		const struct world_spelunk_door *d = world_spelunk_door_at(r,
			(int)(i % r->state.map.width), (int)(i / r->state.map.width));
		if (r->blocked[i] != (d && d->state != WORLD_SPELUNK_DOOR_OPEN)) return false;
	}
	return true;
}

bool world_spelunk_door_add(struct world_spelunk_runtime *r,
	const char *id, int x, int y)
{
	const struct world_spelunk_door_definition *def = world_spelunk_door_by_id(id);
	struct world_spelunk_door *d;
	if (!r || !def || r->door_count >= WORLD_SPELUNK_DOOR_MAX ||
		!world_spelunk_door_site(r, x, y) || world_spelunk_door_at(r, x, y) ||
		world_spelunk_fixture_at(r, x, y) ||
		(r->state.x == x && r->state.y == y) ||
		world_spelunk_runtime_actor_at(r, x, y) ||
		world_spelunk_runtime_ground_object_at(r, x, y) ||
		r->pitons[y*r->state.map.width+x] || r->ropes[y*r->state.map.width+x]) return false;
	d = &r->doors[r->door_count++];
	memset(d, 0, sizeof(*d));
	my_strcpy(d->id, id, sizeof(d->id));
	d->x = x;
	d->y = y;
	d->state = def->lock_power ? WORLD_SPELUNK_DOOR_LOCKED : WORLD_SPELUNK_DOOR_CLOSED;
	world_spelunk_doors_rebuild(r);
	return true;
}

void world_spelunk_doors_observe(struct world_spelunk_runtime *r)
{
	size_t i;
	for (i = 0; i < r->door_count; i++) {
		struct world_spelunk_door *d = &r->doors[i];
		if (!world_spelunk_visibility_is_terrain_visible(r, d->x, d->y)) continue;
		if (d->state == WORLD_SPELUNK_DOOR_OPEN) d->known = WORLD_SPELUNK_DOOR_OPEN;
		else if (d->known != WORLD_SPELUNK_DOOR_LOCKED) d->known = WORLD_SPELUNK_DOOR_CLOSED;
	}
}

bool world_spelunk_door_set_open(struct world_spelunk_runtime *r,
	int x, int y, bool open)
{
	struct world_spelunk_door *d = world_spelunk_door_at_mutable(r, x, y);
	if (!d || (open && d->state == WORLD_SPELUNK_DOOR_OPEN) ||
		(!open && (d->state != WORLD_SPELUNK_DOOR_OPEN ||
		(r->state.x == x && r->state.y == y) ||
		world_spelunk_runtime_actor_at(r, x, y) ||
		world_spelunk_runtime_ground_object_at(r, x, y)))) return false;
	d->state = open ? WORLD_SPELUNK_DOOR_OPEN : WORLD_SPELUNK_DOOR_CLOSED;
	world_spelunk_doors_rebuild(r);
	world_spelunk_visibility_follow_player(r);
	return true;
}
