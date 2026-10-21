/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "angband.h"
#include "obj-desc.h"
#include "obj-ignore.h"
#include "obj-util.h"
#include "player.h"
#include "player-timed.h"
#include "ui-input.h"
#include "ui-nearby-screen.h"
#include "ui-spelunking-list.h"
#include "ui-term.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-actor-effects.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-visibility.h"

static bool nearby_cell_is_known(const struct world_spelunk_runtime *r, int x, int y)
{
	return world_spelunk_visibility_is_explored(r, x, y) &&
		(world_spelunk_visibility_is_visible(r, x, y) ||
		 world_spelunk_visibility_is_detected(r, x, y));
}

static void position(struct ui_spelunking_list_entry *e,
	const struct world_spelunk_runtime *r, int x, int y)
{
	e->dx = x - r->state.x;
	e->dy = y - r->state.y;
	e->distance = abs(e->dx) + abs(e->dy);
	e->detected = !world_spelunk_visibility_is_visible(r, x, y);
	if (!e->distance) my_strcpy(e->offset, "underfoot", sizeof(e->offset));
	else if (!e->dy) strnfmt(e->offset, sizeof(e->offset), "%d %s",
		abs(e->dx), e->dx < 0 ? "left" : "right");
	else if (!e->dx) strnfmt(e->offset, sizeof(e->offset), "%d %s",
		abs(e->dy), e->dy < 0 ? "above" : "below");
	else strnfmt(e->offset, sizeof(e->offset), "%d %s, %d %s",
		abs(e->dy), e->dy < 0 ? "above" : "below",
		abs(e->dx), e->dx < 0 ? "left" : "right");
}

static int compare(const void *a, const void *b)
{
	const struct ui_spelunking_list_entry *x = a, *y = b;
	if (x->detected != y->detected) return x->detected ? 1 : -1;
	if (x->distance != y->distance) return x->distance - y->distance;
	if (x->dy != y->dy) return x->dy - y->dy;
	if (x->dx != y->dx) return x->dx - y->dx;
	return strcmp(x->label, y->label);
}

size_t textui_spelunking_collect_nearby(const struct player *p, bool objects,
	struct ui_spelunking_list_entry *entries, size_t capacity)
{
	const struct world_spelunk_runtime *r;
	size_t n = 0, i;
	if (!entries || !capacity || !world_spelunk_player_is_active(p) ||
		p->timed[TMD_IMAGE]) return 0;
	r = p->spelunking;
	if (objects) {
		const struct object *obj;
		for (obj = r->ground_objects; obj && n < capacity; obj = obj->next) {
			struct ui_spelunking_list_entry *e;
			const struct object_kind *kind = obj->known->kind;
			if (!nearby_cell_is_known(r, obj->grid.x, obj->grid.y) ||
				ignore_item_ok(p, obj)) continue;
			e = &entries[n++];
			memset(e, 0, sizeof(*e));
			object_desc(e->label, sizeof(e->label), obj,
				ODESC_PREFIX | ODESC_FULL | ODESC_NOSEEN, p);
			/* Match cave-map definition glyphs, independent of terminal prefs.
			 * An unfamiliar flavour must not reveal the identified kind. */
			e->glyph = kind->flavor && !kind->aware ? kind->flavor->d_char : kind->d_char;
			e->attr = kind->flavor && !kind->aware ? kind->flavor->d_attr : kind->d_attr;
			position(e, r, obj->grid.x, obj->grid.y);
		}
	} else {
		for (i = 0; i < r->actor_count && n < capacity; i++) {
			const struct world_spelunk_actor *actor = &r->actors[i];
			const struct world_spelunk_actor_definition *def;
			struct ui_spelunking_list_entry *e;
			if (!nearby_cell_is_known(r, actor->x, actor->y)) continue;
			def = world_spelunk_actor_by_id(actor->id);
			if (!def) continue;
			e = &entries[n++];
			memset(e, 0, sizeof(*e));
			my_strcpy(e->label, def->name, sizeof(e->label));
			e->glyph = def->glyph;
			e->attr = def->attr;
			position(e, r, actor->x, actor->y);
			if (!e->detected && !p->timed[TMD_BLIND]) {
				char status[128];
				world_spelunk_actor_status_text(actor,status,sizeof(status));
				if (status[0]) {
					my_strcat(e->label," (",sizeof(e->label));
					my_strcat(e->label,status,sizeof(e->label));
					my_strcat(e->label,")",sizeof(e->label));
				}
			}
		}
	}
	qsort(entries, n, sizeof(*entries), compare);
	return n;
}

/* Both the live controller and headless preview use this same layout. */
static int present_list(const struct player *p, bool objects,
		const struct ui_spelunking_list_entry *entries, int total, int *first)
{
	struct ui_nearby_screen_entry rows[UI_NEARBY_SCREEN_CAPACITY] = {{0}};
	char subtitle[180];
	const char *title = objects ? "Cave Items" : "Cave Creatures";
	const char *help = "Up/Down scroll   PgUp/PgDn page   Escape returns (no time passes)";
	int width, height, page, count, i;
	Term_get_size(&width, &height);
	page = MIN(UI_NEARBY_SCREEN_CAPACITY, MAX(1, height - 10));
	*first = MIN(MAX(0, *first), MAX(0, total - page));
	count = MIN(page, total - *first);
	if (p->timed[TMD_IMAGE]) my_strcpy(subtitle,
		"You cannot distinguish nearby occupants while hallucinating.", sizeof(subtitle));
	else if (!total) my_strcpy(subtitle, objects ?
		"No non-ignored items are currently in sight or detected." :
		"No creatures are currently in sight or detected.", sizeof(subtitle));
	else strnfmt(subtitle, sizeof(subtitle),
		"Current section | Showing %d-%d of %d | Offsets are tiles, not safe routes",
		*first + 1, *first + count, total);
	for (i = 0; i < count; i++) {
		const struct ui_spelunking_list_entry *e = &entries[*first + i];
		rows[i].label = e->label;
		rows[i].prefix = e->detected ? "DETECTED" : "IN SIGHT";
		rows[i].detail = e->offset;
		rows[i].glyph = e->glyph;
		rows[i].text_attr = e->detected ? COLOUR_SLATE : COLOUR_WHITE;
		rows[i].glyph_attr = e->attr;
		rows[i].has_glyph_attr = true;
	}
	if (Term->screen_hook) ui_nearby_screen_present(title, subtitle, help, rows, count, -1);
	else {
		Term_clear();
		Term_putstr(0, 0, width, COLOUR_YELLOW, title);
		Term_putstr(0, 2, width, COLOUR_WHITE, subtitle);
		for (i = 0; i < count; i++) {
			char line[256];
			strnfmt(line, sizeof(line), "%s: %s (%s)", rows[i].prefix, rows[i].label, rows[i].detail);
			Term_putch(0, i + 4, rows[i].glyph_attr, rows[i].glyph);
			Term_putstr(2, i + 4, MAX(0, width - 2), rows[i].text_attr, line);
		}
		Term_putstr(0, height - 1, width, COLOUR_L_BLUE, help);
	}
	Term_fresh();
	return page;
}

bool textui_spelunking_list_present(const struct player *p, bool objects)
{
	struct ui_spelunking_list_entry *entries;
	size_t capacity = objects ? WORLD_SPELUNK_OBJECT_MAX : WORLD_SPELUNK_ACTOR_MAX;
	int total, first = 0;
	if (!Term || !world_spelunk_player_is_active(p)) return false;
	entries = mem_zalloc(capacity * sizeof(*entries));
	total = (int)textui_spelunking_collect_nearby(p, objects, entries, capacity);
	present_list(p, objects, entries, total, &first);
	mem_free(entries);
	return true;
}

void textui_spelunking_list(const struct player *p, bool objects)
{
	struct ui_spelunking_list_entry *entries;
	size_t capacity = objects ? WORLD_SPELUNK_OBJECT_MAX : WORLD_SPELUNK_ACTOR_MAX;
	int first = 0, total;
	bool done = false;
	if (!Term || !world_spelunk_player_is_active(p)) return;
	entries = mem_zalloc(capacity * sizeof(*entries));
	total = (int)textui_spelunking_collect_nearby(p, objects, entries, capacity);
	while (!done) {
		int page = present_list(p, objects, entries, total, &first);
		ui_event event;
		event = inkey_m();
		if (event.type == EVT_RESIZE) continue;
		if (event.type == EVT_MOUSE) {
			if (event.mouse.button == 4) first = MAX(0, first - 1);
			else if (event.mouse.button == 5) first = MIN(MAX(0, total - page), first + 1);
			else if (event.mouse.button == 2 || event.mouse.button == 3) done = true;
		} else if (event.type == EVT_KBRD) {
			switch (event.key.code) {
			case ARROW_UP: first = MAX(0, first - 1); break;
			case ARROW_DOWN: first = MIN(MAX(0, total - page), first + 1); break;
			case KC_PGUP: first = MAX(0, first - page); break;
			case KC_PGDOWN: case ' ': first = MIN(MAX(0, total - page), first + page); break;
			case ESCAPE: case 'q': case '[': case ']': case KC_ENTER: done = true; break;
			default: break;
			}
		}
	}
	if (Term->screen_hook) Term->screen_hook(NULL);
	mem_free(entries);
}
