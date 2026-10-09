/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-player.c
 * \brief character screens and dumps
 *
 * Copyright (c) 1997 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */
#include "angband.h"
#include "buildid.h"
#include "game-world.h"
#include "init.h"
#include "obj-curse.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-info.h"
#include "obj-knowledge.h"
#include "obj-util.h"
#include "player.h"
#include "player-calcs.h"
#include "player-quest.h"
#include "player-timed.h"
#include "player-util.h"
#include "store.h"
#include "ui-birth.h"
#include "ui-display.h"
#include "ui-entry.h"
#include "ui-entry-renderers.h"
#include "ui-history.h"
#include "ui-input.h"
#include "ui-menu.h"
#include "ui-object.h"
#include "ui-output.h"
#include "ui-player.h"
#include "ui-story.h"
#include "ui-screen.h"
#include "ui-spelunking-survey.h"
#include "world-objective-data.h"


/**
 * ------------------------------------------------------------------------
 * Panel utilities
 * ------------------------------------------------------------------------ */

/**
 * Panel line type
 */
struct panel_line {
	uint8_t attr;
	const char *label;
	char value[20];
};

/**
 * Panel holder type
 */
struct panel {
	size_t len;
	size_t max;
	struct panel_line *lines;
};

/**
 * Allocate some panel lines
 */
static struct panel *panel_allocate(int n) {
	struct panel *p = mem_zalloc(sizeof *p);

	p->len = 0;
	p->max = n;
	p->lines = mem_zalloc(p->max * sizeof *p->lines);

	return p;
}

/**
 * Free up panel lines
 */
static void panel_free(struct panel *p) {
	assert(p);
	mem_free(p->lines);
	mem_free(p);
}

/**
 * Add a new line to the panel
 */
static void panel_line(struct panel *p, uint8_t attr, const char *label,
		const char *fmt, ...) {
	va_list vp;

	struct panel_line *pl;

	/* Get the next panel line */
	assert(p);
	assert(p->len != p->max);
	pl = &p->lines[p->len++];

	/* Set the basics */
	pl->attr = attr;
	pl->label = label;

	/* Set the value */
	va_start(vp, fmt);
	vstrnfmt(pl->value, sizeof pl->value, fmt, vp);
	va_end(vp);
}

/**
 * Add a spacer line in a panel
 */
static void panel_space(struct panel *p) {
	assert(p);
	assert(p->len != p->max);
	p->len++;
}


/**
 * Cache the layout of the character sheet, currently only for the resistance
 * panel, since it is no longer hardwired.
 */
struct char_sheet_resist {
	struct ui_entry* entry;
	wchar_t label[6];
};
struct char_sheet_config {
	struct ui_entry** stat_mod_entries;
	region res_regions[4];
	struct char_sheet_resist *resists_by_region[4];
	int n_resist_by_region[4];
	int n_stat_mod_entries;
	int res_cols;
	int res_rows;
	int res_nlabel;
};
static struct char_sheet_config *cached_config = NULL;
static void display_resistance_panel(int ipart, struct char_sheet_config* config);


static bool have_valid_char_sheet_config(void)
{
	if (!cached_config) {
		return false;
	}
	if (cached_config->res_cols !=
		cached_config->res_nlabel + 1 + player->body.count) {
		return false;
	}
	return true;
}


static void release_char_sheet_config(void)
{
	int i;

	if (!cached_config) {
		return;
	}
	for (i = 0; i < 4; ++i) {
		mem_free(cached_config->resists_by_region[i]);
	}
	mem_free(cached_config->stat_mod_entries);
	mem_free(cached_config);
	cached_config = 0;
}


static bool check_for_two_categories(const struct ui_entry* entry,
		const void *closure)
{
	const char * const *categories = closure;

	return ui_entry_has_category(entry, categories[0]) &&
		ui_entry_has_category(entry, categories[1]);
}


static void configure_char_sheet(void)
{
	const char* region_categories[] = {
		"resistances",
		"abilities",
		"hindrances",
		"modifiers"
	};
	const char* test_categories[2];
	struct ui_entry_iterator* ui_iter;
	int i, n;

	release_char_sheet_config();

	cached_config = mem_alloc(sizeof(*cached_config));

	test_categories[0] = "CHAR_SCREEN1";
	test_categories[1] = "stat_modifiers";
	ui_iter = initialize_ui_entry_iterator_const(check_for_two_categories,
		test_categories, test_categories[1]);
	n = count_ui_entry_iterator(ui_iter);
	/*
	 * Linked to hardcoded stats display with STAT_MAX entries so only use
	 * that many.
	 */
	if (n > STAT_MAX) {
	    n = STAT_MAX;
	}
	cached_config->n_stat_mod_entries = n;
	cached_config->stat_mod_entries = mem_alloc(n *
		sizeof(*cached_config->stat_mod_entries));
	for (i = 0; i < n; ++i) {
		cached_config->stat_mod_entries[i] =
			advance_ui_entry_iterator(ui_iter);
	}
	release_ui_entry_iterator(ui_iter);

	cached_config->res_nlabel = 6;
	cached_config->res_cols =
		cached_config->res_nlabel + 1 + player->body.count;
	cached_config->res_rows = 0;
	for (i = 0; i < 4; ++i) {
		int j;

		cached_config->res_regions[i].col =
			i * (cached_config->res_cols + 1);
		cached_config->res_regions[i].row = 2 + STAT_MAX;
		cached_config->res_regions[i].width = cached_config->res_cols;

		test_categories[1] = region_categories[i];
		ui_iter = initialize_ui_entry_iterator_const(
			check_for_two_categories, test_categories,
			region_categories[i]);
		n = count_ui_entry_iterator(ui_iter);
		/*
		 * Fit in 24 row display; leave at least one row blank before
		 * prompt on last row.
		 */
		if (n + 2 + cached_config->res_regions[i].row > 22) {
		    n = 20 - cached_config->res_regions[i].row;
		}
		cached_config->n_resist_by_region[i] = n;
		cached_config->resists_by_region[i] = mem_alloc(n * sizeof(*cached_config->resists_by_region[i]));
		for (j = 0; j < n; ++j) {
			struct ui_entry *entry = advance_ui_entry_iterator(ui_iter);

			cached_config->resists_by_region[i][j].entry = entry;
			get_ui_entry_label(entry, cached_config->res_nlabel, true, cached_config->resists_by_region[i][j].label);
			(void) text_mbstowcs(cached_config->resists_by_region[i][j].label + 5, ":", 1);
		}
		release_ui_entry_iterator(ui_iter);

		if (cached_config->res_rows <
			cached_config->n_resist_by_region[i]) {
			cached_config->res_rows =
				cached_config->n_resist_by_region[i];
		}
	}
	for (i = 0; i < 4; ++i) {
		cached_config->res_regions[i].page_rows =
			cached_config->res_rows + 2;
	}
}


/**
 * Returns a "rating" of x depending on y, and sets "attr" to the
 * corresponding "attribute".
 */
static const char *likert(int x, int y, uint8_t *attr)
{
	/* Paranoia */
	if (y <= 0) y = 1;

	/* Negative value */
	if (x < 0) {
		*attr = COLOUR_RED;
		return ("Very Bad");
	}

	/* Analyze the value */
	switch ((x / y))
	{
		case 0:
		case 1:
		{
			*attr = COLOUR_RED;
			return ("Bad");
		}
		case 2:
		{
			*attr = COLOUR_RED;
			return ("Poor");
		}
		case 3:
		case 4:
		{
			*attr = COLOUR_YELLOW;
			return ("Fair");
		}
		case 5:
		{
			*attr = COLOUR_YELLOW;
			return ("Good");
		}
		case 6:
		{
			*attr = COLOUR_YELLOW;
			return ("Very Good");
		}
		case 7:
		case 8:
		{
			*attr = COLOUR_L_GREEN;
			return ("Excellent");
		}
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
		{
			*attr = COLOUR_L_GREEN;
			return ("Superb");
		}
		case 14:
		case 15:
		case 16:
		case 17:
		{
			*attr = COLOUR_L_GREEN;
			return ("Heroic");
		}
		default:
		{
			*attr = COLOUR_L_GREEN;
			return ("Legendary");
		}
	}
}


/**
 * Equippy chars
 */
static void display_player_equippy(int y, int x)
{
	int i;

	uint8_t a;
	wchar_t c;

	struct object *obj;

	/* Dump equippy chars */
	for (i = 0; i < player->body.count; ++i) {
		/* Object */
		obj = slot_object(player, i);

		/* Get attr/char for display; clear if big tiles or no object */
		if (obj && tile_width == 1 && tile_height == 1) {
			a = object_attr(obj);
			c = object_char(obj);
		} else {
			a = COLOUR_WHITE;
			c = L' ';
		}

		/* Dump */
		Term_putch(x + i, y, a, c);
	}
}


static void display_resistance_panel(int ipart, struct char_sheet_config *config)
{
	int *vals = mem_alloc((player->body.count + 1) * sizeof(*vals));
	int *auxs = mem_alloc((player->body.count + 1) * sizeof(*auxs));
	struct object **equipment =
		mem_alloc(player->body.count * sizeof(*equipment));
	struct cached_object_data **ocaches =
		mem_zalloc(player->body.count * sizeof(*ocaches));
	struct cached_player_data *pcache = NULL;
	struct ui_entry_details render_details;
	int i;
	int j;
	int col = config->res_regions[ipart].col;
	int row = config->res_regions[ipart].row;

	for (i = 0; i < player->body.count; i++) {
		equipment[i] = slot_object(player, i);
	}

	/* Equippy */
	display_player_equippy(row++, col + config->res_nlabel);

	Term_putstr(col, row++, config->res_cols, COLOUR_WHITE, "      abcdefgimnop@");
	render_details.label_position.x = col;
	render_details.value_position.x = col + config->res_nlabel;
	render_details.position_step = loc(1, 0);
	render_details.combined_position = loc(0, 0);
	render_details.vertical_label = false;
	render_details.alternate_color_first = false;
	render_details.show_combined = false;
	for (i = 0; i < config->n_resist_by_region[ipart]; i++, row++) {
		const struct ui_entry *entry = config->resists_by_region[ipart][i].entry;

		for (j = 0; j < player->body.count; j++) {
			compute_ui_entry_values_for_object(entry, equipment[j], player, ocaches + j, vals + j, auxs + j);
		}
		compute_ui_entry_values_for_player(entry, player, &pcache, vals + player->body.count, auxs + player->body.count);

		render_details.label_position.y = row;
		render_details.value_position.y = row;
		render_details.known_rune = is_ui_entry_for_known_rune(entry, player);
		ui_entry_renderer_apply(get_ui_entry_renderer_index(entry), config->resists_by_region[ipart][i].label, config->res_nlabel, vals, auxs, player->body.count + 1, &render_details);
	}

	if (pcache) {
		release_cached_player_data(pcache);
	}
	for (i = 0; i < player->body.count; ++i) {
		if (ocaches[i]) {
			release_cached_object_data(ocaches[i]);
		}
	}
	mem_free(ocaches);
	mem_free(equipment);
	mem_free(auxs);
	mem_free(vals);
}

static void display_player_flag_info(void)
{
	int i;

	for (i = 0; i < 4; i++)
		display_resistance_panel(i, cached_config);
}


/**
 * Special display, part 2b
 */
void display_player_stat_info(void)
{
	int i, row, col;

	char buf[80];


	/* Row */
	row = 2;

	/* Column */
	col = 42;

	/* Print out the labels for the columns */
	c_put_str(COLOUR_WHITE, "  Self", row-1, col+5);
	c_put_str(COLOUR_WHITE, " RB", row-1, col+12);
	c_put_str(COLOUR_WHITE, " CB", row-1, col+16);
	c_put_str(COLOUR_WHITE, " EB", row-1, col+20);
	c_put_str(COLOUR_WHITE, "  Best", row-1, col+24);

	/* Display the stats */
	for (i = 0; i < STAT_MAX; i++) {
		/* Reduced or normal */
		if (player->stat_cur[i] < player->stat_max[i])
			/* Use lowercase stat name */
			put_str(stat_names_reduced[i], row+i, col);
		else
			/* Assume uppercase stat name */
			put_str(stat_names[i], row+i, col);

		/* Indicate natural maximum */
		if (player->stat_max[i] == 18+100)
			put_str("!", row+i, col+3);

		/* Internal "natural" maximum value */
		cnv_stat(player->stat_max[i], buf, sizeof(buf));
		c_put_str(COLOUR_L_GREEN, buf, row+i, col+5);

		/* Race Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->race->r_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+12);

		/* Class Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->class->c_adj[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+16);

		/* Equipment Bonus */
		strnfmt(buf, sizeof(buf), "%+3d", player->state.stat_add[i]);
		c_put_str(COLOUR_L_BLUE, buf, row+i, col+20);

		/* Resulting "modified" maximum value */
		cnv_stat(player->state.stat_top[i], buf, sizeof(buf));
		c_put_str(COLOUR_L_GREEN, buf, row+i, col+24);

		/* Only display stat_use if there has been draining */
		if (player->stat_cur[i] < player->stat_max[i]) {
			cnv_stat(player->state.stat_use[i], buf, sizeof(buf));
			c_put_str(COLOUR_YELLOW, buf, row+i, col+31);
		}
	}
}


/**
 * Special display, part 2c
 *
 * Display stat modifiers from equipment and sustains.  Colors and symbols
 * are configured from ui_entry.txt and ui_entry_renderers.txt.  Other
 * configuration that's possible there (extra characters for each number
 * for instance) aren't well handled here - the assumption is just one digit
 * for each equipment slot.
 */
static void display_player_sust_info(struct char_sheet_config *config)
{
	int *vals = mem_alloc((player->body.count + 1) * sizeof(*vals));
	int *auxs = mem_alloc((player->body.count + 1) * sizeof(*auxs));
	struct object **equipment =
		mem_alloc(player->body.count * sizeof(*equipment));
	struct cached_object_data **ocaches =
		mem_zalloc(player->body.count * sizeof(*ocaches));
	struct cached_player_data *pcache = NULL;
	struct ui_entry_details render_details;
	int i, row, col;

	for (i = 0; i < player->body.count; i++) {
		equipment[i] = slot_object(player, i);
	}

	/* Row */
	row = 2;

	/* Column */
	col = 26;

	/* Header */
	c_put_str(COLOUR_WHITE, "abcdefgimnop@", row - 1, col);

	render_details.label_position.x = col + player->body.count + 5;
	render_details.value_position.x = col;
	render_details.position_step = loc(1, 0);
	render_details.combined_position = loc(0, 0);
	render_details.vertical_label = false;
	render_details.alternate_color_first = false;
	render_details.known_rune = true;
	render_details.show_combined = false;
	for (i = 0; i < config->n_stat_mod_entries; i++) {
		const struct ui_entry *entry = config->stat_mod_entries[i];
		int j;

		for (j = 0; j < player->body.count; j++) {
			compute_ui_entry_values_for_object(entry, equipment[j], player, ocaches + j, vals + j, auxs + j);
		}
		compute_ui_entry_values_for_player(entry, player, &pcache, vals + player->body.count, auxs + player->body.count);
		/* Just use the sustain information for the player column. */
		vals[player->body.count] = 0;

		render_details.label_position.y = row + i;
		render_details.value_position.y = row + i;
		ui_entry_renderer_apply(get_ui_entry_renderer_index(entry), NULL, 0, vals, auxs, player->body.count + 1, &render_details);
	}

	if (pcache) {
		release_cached_player_data(pcache);
	}
	for (i = 0; i < player->body.count; ++i) {
		if (ocaches[i]) {
			release_cached_object_data(ocaches[i]);
		}
	}
	mem_free(ocaches);
	mem_free(equipment);
	mem_free(auxs);
	mem_free(vals);
}



static void display_panel(const struct panel *p, bool left_adj,
		const region *bounds)
{
	size_t i;
	int col = bounds->col;
	int row = bounds->row;
	int w = bounds->width;
	int offset = 0;

	region_erase(bounds);

	if (left_adj) {
		for (i = 0; i < p->len; i++) {
			struct panel_line *pl = &p->lines[i];

			int len = pl->label ? strlen(pl->label) : 0;
			if (offset < len) offset = len;
		}
		offset += 2;
	}

	for (i = 0; i < p->len; i++, row++) {
		int len;
		struct panel_line *pl = &p->lines[i];

		if (!pl->label)
			continue;

		Term_putstr(col, row, strlen(pl->label), COLOUR_WHITE, pl->label);

		len = strlen(pl->value);
		len = len < w - offset ? len : w - offset - 1;

		if (left_adj)
			Term_putstr(col+offset, row, len, pl->attr, pl->value);
		else
			Term_putstr(col+w-len, row, len, pl->attr, pl->value);
	}
}

static const char *show_title(void)
{
	if (player->wizard)
		return "[=-WIZARD-=]";
	else if (player->total_winner || player->lev > PY_MAX_LEVEL)
		return "***WINNER***";
	else
		return player->class->title[(player->lev - 1) / PY_TITLE_LEVELS];
}

static const char *show_adv_exp(void)
{
	if (player->lev < PY_MAX_LEVEL) {
		static char buffer[30];
		int32_t advance = (player_exp[player->lev - 1]
			* player->expfact / 100L);
		strnfmt(buffer, sizeof(buffer), "%ld", (long)advance);
		return buffer;
	}
	else {
		return "********";
	}
}

static const char *show_depth(void)
{
	static char buffer[13];

	if (player->max_depth == 0) return "Town";

	strnfmt(buffer, sizeof(buffer), "%d' (L%d)",
	        player->max_depth * 50, player->max_depth);
	return buffer;
}

static const char *show_speed(void)
{
	static char buffer[10];
	int tmp = player->state.speed;
	if (player->timed[TMD_FAST]) tmp -= 10;
	if (player->timed[TMD_SLOW]) tmp += 10;
	if (tmp == 110) return "Normal";
	int multiplier = 10 * extract_energy[tmp] / extract_energy[110];
	int int_mul = multiplier / 10;
	int dec_mul = multiplier % 10;
	if (OPT(player, effective_speed))
		strnfmt(buffer, sizeof(buffer), "%d.%dx (%d)", int_mul, dec_mul, tmp - 110);
	else
		strnfmt(buffer, sizeof(buffer), "%d (%d.%dx)", tmp - 110, int_mul, dec_mul);
	return buffer;
}

static uint8_t max_color(int val, int max)
{
	return val < max ? COLOUR_YELLOW : COLOUR_L_GREEN;
}

/**
 * Colours for table items
 */
static const uint8_t colour_table[] =
{
	COLOUR_RED, COLOUR_RED, COLOUR_RED, COLOUR_L_RED, COLOUR_ORANGE,
	COLOUR_YELLOW, COLOUR_YELLOW, COLOUR_GREEN, COLOUR_GREEN, COLOUR_L_GREEN,
	COLOUR_L_BLUE
};


static struct panel *get_panel_topleft(void) {
	struct panel *p = panel_allocate(6);

	panel_line(p, COLOUR_L_BLUE, "Name", "%s", player->full_name);
	panel_line(p, COLOUR_L_BLUE, "Origin",	"%s", player->race->name);
	panel_line(p, COLOUR_L_BLUE, "Class", "%s", player->class->name);
	panel_line(p, COLOUR_L_BLUE, "Title", "%s", show_title());
	panel_line(p, COLOUR_L_BLUE, "HP", "%d/%d", player->chp, player->mhp);
	panel_line(p, COLOUR_L_BLUE, "Focus", "%d/%d", player->csp, player->msp);

	return p;
}

static struct panel *get_panel_midleft(void) {
	struct panel *p = panel_allocate(9);
	int diff = weight_remaining(player);
	uint8_t attr = diff < 0 ? COLOUR_L_RED : COLOUR_L_GREEN;

	panel_line(p, max_color(player->lev, player->max_lev),
			"Level", "%d", player->lev);
	panel_line(p, max_color(player->exp, player->max_exp),
			"Cur Exp", "%d", player->exp);
	panel_line(p, COLOUR_L_GREEN, "Max Exp", "%d", player->max_exp);
	panel_line(p, COLOUR_L_GREEN, "Adv Exp", "%s", show_adv_exp());
	panel_space(p);
	panel_line(p, COLOUR_L_GREEN, "Gold", "%d", player->au);
	panel_line(p, attr, "Burden", "%.1f lb",
			   player->upkeep->total_weight / 10.0F);
	panel_line(p, attr, "Overweight", "%d.%d lb", -diff / 10, abs(diff) % 10);
	panel_line(p, COLOUR_L_GREEN, "Max Depth", "%s", show_depth());

	return p;
}

static struct panel *get_panel_combat(void) {
	struct panel *p = panel_allocate(9);
	struct object *obj;
	int bth, dam, hit;
	int melee_dice = 1, melee_sides = 1;

	/* AC */
	panel_line(p, COLOUR_L_BLUE, "Armor", "[%d,%+d]",
			player->known_state.ac, player->known_state.to_a);

	/* Melee */
	obj = equipped_item_by_slot_name(player, "weapon");
	bth = (player->state.skills[SKILL_TO_HIT_MELEE] * 10) / BTH_PLUS_ADJ;
	dam = player->known_state.to_d;
	hit = player->known_state.to_h;
	if (obj && obj->known) {
		melee_dice = obj->known->dd;
		melee_sides = obj->known->ds;
		dam += object_to_dam(obj->known);
		hit += object_to_hit(obj->known);
	}
	if (player->known_state.bless_wield) {
		hit += 2;
	}

	panel_space(p);
	panel_line(p, COLOUR_L_BLUE, "Melee", "%dd%d,%+d", melee_dice, melee_sides, dam);
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Blows", "%d.%d/turn",
			player->state.num_blows / 100, (player->state.num_blows / 10 % 10));

	/* Ranged */
	obj = equipped_item_by_slot_name(player, "shooting");
	bth = (player->state.skills[SKILL_TO_HIT_BOW] * 10) / BTH_PLUS_ADJ;
	dam = 0;
	hit = player->known_state.to_h;
	if (obj && obj->known) {
		dam += object_to_dam(obj->known);
		hit += object_to_hit(obj->known);
	}

	panel_space(p);
	panel_line(p, COLOUR_L_BLUE, "Shoot to-dam", "%+d", dam);
	panel_line(p, COLOUR_L_BLUE, "To-hit", "%d,%+d", bth / 10, hit);
	panel_line(p, COLOUR_L_BLUE, "Shots", "%d.%d/turn",
			   player->state.num_shots / 10, player->state.num_shots % 10);

	return p;
}

static struct panel *get_panel_skills(void) {
	struct panel *p = panel_allocate(8);

	int skill;
	uint8_t attr;
	const char *desc;
	int depth = cave ? cave->depth : 0;

#define BOUND(x, min, max)		MIN(max, MAX(min, x))

	/* Saving throw */
	skill = BOUND(player->state.skills[SKILL_SAVE], 0, 100);
	panel_line(p, colour_table[skill / 10], "Saving Throw", "%d%%", skill);

	/* Stealth */
	desc = likert(player->state.skills[SKILL_STEALTH], 1, &attr);
	panel_line(p, attr, "Stealth", "%s", desc);

	/* Physical disarming: assume we're disarming a dungeon trap */
	skill = BOUND(player->state.skills[SKILL_DISARM_PHYS] - depth / 5, 2, 100);
	panel_line(p, colour_table[skill / 10], "Disarm - phys.", "%d%%", skill);

	/* Magical disarming */
	skill = BOUND(player->state.skills[SKILL_DISARM_MAGIC] - depth / 5, 2, 100);
	panel_line(p, colour_table[skill / 10], "Disarm - magic", "%d%%", skill);

	/* Magic devices */
	skill = player->state.skills[SKILL_DEVICE];
	panel_line(p, colour_table[skill / 13], "Magic Devices", "%d", skill);

	/* Searching ability */
	skill = BOUND(player->state.skills[SKILL_SEARCH], 0, 100);
	panel_line(p, colour_table[skill / 10], "Searching", "%d%%", skill);

	/* Infravision */
	panel_line(p, COLOUR_L_GREEN, "Infravision", "%d ft",
			player->state.see_infra * 10);

	/* Speed */
	skill = player->state.speed;
	if (player->timed[TMD_FAST]) skill -= 10;
	if (player->timed[TMD_SLOW]) skill += 10;
	attr = skill < 110 ? COLOUR_L_UMBER : COLOUR_L_GREEN;
	panel_line(p, attr, "Speed", "%s", show_speed());

	return p;
}

static struct panel *get_panel_misc(void) {
	struct panel *p = panel_allocate(7);
	uint8_t attr = COLOUR_L_BLUE;

	panel_line(p, attr, "Age", "%d", player->age);
	panel_line(p, attr, "Height", "%d'%d\"", player->ht / 12, player->ht % 12);
	panel_line(p, attr, "Weight", "%dst %dlb", player->wt / 14, player->wt % 14);
	panel_line(p, attr, "Turns used:", "");
	panel_line(p, attr, "Game", "%d", turn);
	panel_line(p, attr, "Standard", "%d", player->total_energy / 100);
	panel_line(p, attr, "Resting", "%d", player->resting_turn);

	return p;
}

/**
 * Panels for main character screen
 */
static const struct {
	region bounds;
	bool align_left;
	struct panel *(*panel)(void);
} panels[] =
{
	/*   x  y wid rows */
	{ {  1, 1, 40, 7 }, true,  get_panel_topleft },	/* Name, Class, ... */
	{ { 21, 1, 18, 3 }, false, get_panel_misc },	/* Age, ht, wt, ... */
	{ {  1, 9, 24, 9 }, false, get_panel_midleft },	/* Cur Exp, Max Exp, ... */
	{ { 29, 9, 19, 9 }, false, get_panel_combat },
	{ { 52, 9, 20, 8 }, false, get_panel_skills },
};

#define PLAYER_SCREEN_ROW_CAPACITY 64
#define PLAYER_SCREEN_CONTENT_ROW 8

struct player_screen_storage {
	struct ui_screen_row rows[PLAYER_SCREEN_ROW_CAPACITY];
	char labels[PLAYER_SCREEN_ROW_CAPACITY][80];
	char statuses[PLAYER_SCREEN_ROW_CAPACITY][80];
	char sources[PLAYER_SCREEN_ROW_CAPACITY][160];
};

static const int player_screen_panel_order[] = { 0, 2, 3, 4, 1 };
static const char *const player_screen_groups[] = {
	"Identity", "Progress", "Combat", "Skills", "Record"
};
static const char *const player_screen_labels[5][9] = {
	{ "Name", "Origin", "Class", "Title", "HP", "Focus" },
	{ "Age", "Height", "Weight", NULL, "Game turns", "Standard turns",
		"Resting turns" },
	{ "Level", "Current experience", "Maximum experience",
		"Next level", NULL, "Gold", "Burden", "Weight vs limit", "Max depth" },
	{ "Armor", NULL, "Melee damage", "Melee to-hit", "Melee blows", NULL,
		"Ranged damage", "Ranged to-hit", "Ranged shots" },
	{ "Saving throw", "Stealth", "Physical disarming", "Magic disarming",
		"Magic devices", "Searching", "Infravision", "Speed" }
};

static bool player_screen_value_present(int value)
{
	return value != UI_ENTRY_VALUE_NOT_PRESENT && value != 0;
}

static void player_screen_append_source(char *buffer, size_t length,
		const char *source)
{
	if (!buffer || !length || !source || !source[0]) return;
	if (buffer[0]) my_strcat(buffer, ", ", length);
	my_strcat(buffer, source, length);
}

static void player_screen_entry_label(const struct ui_entry *entry,
		char *buffer, size_t length)
{
	wchar_t wide[80];
	char *dest = buffer;
	size_t remaining = length;
	size_t character_capacity = (size_t)text_wcsz();
	size_t i;

	if (!buffer || !length) return;
	buffer[0] = '\0';
	get_ui_entry_label(entry, N_ELEMENTS(wide) - 1, false, wide);
	for (i = 0; wide[i] && remaining > character_capacity; i++) {
		int written = text_wctomb(dest, wide[i]);

		if (written <= 0 || (size_t)written >= remaining) break;
		dest += written;
		remaining -= written;
	}
	*dest = '\0';
}

static void player_screen_signed_value(char *buffer, size_t length, int value)
{
	if (value == UI_ENTRY_UNKNOWN_VALUE) {
		my_strcpy(buffer, "Unknown", length);
	} else if (value == UI_ENTRY_VALUE_NOT_PRESENT || value == 0) {
		my_strcpy(buffer, "None", length);
	} else {
		strnfmt(buffer, length, "%+d", value);
	}
}

static void player_screen_defence_status(char *buffer, size_t length,
		int group, int value, int auxiliary)
{
	bool temporary = player_screen_value_present(auxiliary);

	buffer[0] = '\0';
	if (value == UI_ENTRY_UNKNOWN_VALUE ||
			auxiliary == UI_ENTRY_UNKNOWN_VALUE) {
		my_strcpy(buffer, "Unknown", length);
		return;
	}
	if (value == UI_ENTRY_VALUE_NOT_PRESENT) value = 0;
	if (auxiliary == UI_ENTRY_VALUE_NOT_PRESENT) auxiliary = 0;
	temporary = auxiliary != 0;
	if (group == -1) {
		player_screen_signed_value(buffer, length, value);
		if (temporary) my_strcat(buffer, ", sustained", length);
		return;
	}
	if (group == 0) {
		if (value == 3) {
			my_strcpy(buffer, "Immune", length);
		} else if (value == UI_ENTRY_RESIST0_RES_VUL) {
			my_strcpy(buffer, "Mixed; no net resistance", length);
		} else if (value > 0 && value < UI_ENTRY_RESIST0_RES_VUL) {
			my_strcpy(buffer, "Resistant", length);
		} else if (value < 0) {
			my_strcpy(buffer, "Vulnerable", length);
		} else if (temporary) {
			my_strcpy(buffer, auxiliary == 3 ?
				"Temporary immunity" :
				(auxiliary < 0 ? "Temporary vulnerability" :
				"Temporary resistance"), length);
		} else {
			my_strcpy(buffer, "None", length);
		}
		if (temporary && value != 0 &&
				value != UI_ENTRY_VALUE_NOT_PRESENT) {
			my_strcat(buffer, ", temporary effect", length);
		}
		return;
	}
	if (group == 1) {
		my_strcpy(buffer, value > 0 ? "Active" :
			(temporary ? "Temporarily active" : "No"), length);
		return;
	}
	if (group == 2) {
		my_strcpy(buffer, value > 0 ? "Present" :
			(value < 0 ? "Suppressed" :
			(temporary ? "Temporary" : "None")), length);
		return;
	}
	player_screen_signed_value(buffer, length, value);
	if (temporary) {
		char suffix[32];

		strnfmt(suffix, sizeof(suffix), " (temporary %+d)", auxiliary);
		my_strcat(buffer, suffix, length);
	}
}

static uint8_t player_screen_defence_attr(int group, int value,
		int auxiliary)
{
	if (value == UI_ENTRY_UNKNOWN_VALUE ||
			auxiliary == UI_ENTRY_UNKNOWN_VALUE) return COLOUR_SLATE;
	if (value == UI_ENTRY_VALUE_NOT_PRESENT) value = 0;
	if (auxiliary == UI_ENTRY_VALUE_NOT_PRESENT) auxiliary = 0;
	if (group == 2) return value > 0 || auxiliary > 0 ?
		COLOUR_L_RED : COLOUR_L_GREEN;
	if (value == UI_ENTRY_RESIST0_RES_VUL) return COLOUR_YELLOW;
	if (value < 0 || auxiliary < 0) return COLOUR_L_RED;
	if (value == 3) return COLOUR_L_BLUE;
	if (value > 0 || auxiliary > 0) return COLOUR_L_GREEN;
	return COLOUR_WHITE;
}

static void player_screen_add_heading(struct player_screen_storage *storage,
		int *count, const char *heading)
{
	struct ui_screen_row *row;

	if (*count >= PLAYER_SCREEN_ROW_CAPACITY) return;
	row = &storage->rows[*count];
	my_strcpy(storage->labels[*count], heading,
		sizeof(storage->labels[*count]));
	row->label = storage->labels[*count];
	row->attr = COLOUR_L_BLUE;
	row->enabled = false;
	(*count)++;
}

static void player_screen_add_defence(struct player_screen_storage *storage,
		int *count, const struct ui_entry *entry, int group)
{
	int values[32];
	int auxiliaries[32];
	struct object *equipment[32];
	struct cached_object_data *object_cache[32] = { 0 };
	struct cached_player_data *player_cache = NULL;
	struct ui_entry_combiner_funcs combiner;
	int combined = UI_ENTRY_VALUE_NOT_PRESENT;
	int combined_aux = UI_ENTRY_VALUE_NOT_PRESENT;
	int nvalue = MIN((int)player->body.count, 31) + 1;
	struct ui_screen_row *row;
	int i;

	if (*count >= PLAYER_SCREEN_ROW_CAPACITY) return;
	for (i = 0; i < nvalue - 1; i++) {
		equipment[i] = slot_object(player, i);
		compute_ui_entry_values_for_object(entry, equipment[i], player,
			&object_cache[i], &values[i], &auxiliaries[i]);
	}
	compute_ui_entry_values_for_player(entry, player, &player_cache,
		&values[nvalue - 1], &auxiliaries[nvalue - 1]);
	if (!ui_entry_combiner_get_funcs(get_ui_entry_combiner_index(entry),
			&combiner)) {
		combiner.vec_func(nvalue, values, auxiliaries, &combined,
			&combined_aux);
	}
	row = &storage->rows[*count];
	player_screen_entry_label(entry, storage->labels[*count],
		sizeof(storage->labels[*count]));
	player_screen_defence_status(storage->statuses[*count],
		sizeof(storage->statuses[*count]), group, combined, combined_aux);
	storage->sources[*count][0] = '\0';
	for (i = 0; i < nvalue - 1; i++) {
		if (player_screen_value_present(values[i]) ||
				player_screen_value_present(auxiliaries[i])) {
			player_screen_append_source(storage->sources[*count],
				sizeof(storage->sources[*count]),
				player->body.slots[i].name);
		}
	}
	if (player_screen_value_present(values[nvalue - 1])) {
		player_screen_append_source(storage->sources[*count],
			sizeof(storage->sources[*count]), "innate");
	}
	if (player_screen_value_present(auxiliaries[nvalue - 1])) {
		player_screen_append_source(storage->sources[*count],
			sizeof(storage->sources[*count]), "temporary");
	}
	row->label = storage->labels[*count];
	row->prefix = storage->statuses[*count];
	row->detail = storage->sources[*count];
	row->attr = player_screen_defence_attr(group, combined, combined_aux);
	row->enabled = true;
	(*count)++;
	if (player_cache) release_cached_player_data(player_cache);
	for (i = 0; i < nvalue - 1; i++) {
		if (object_cache[i]) release_cached_object_data(object_cache[i]);
	}
}

static int present_player_defences(int row_offset)
{
	static const char *const headings[] = {
		"Resistances", "Abilities", "Hindrances", "Modifiers"
	};
	struct player_screen_storage storage = { 0 };
	struct ui_screen screen = { 0 };
	struct ui_screen_tab tabs[4] = {
		{ "Overview", 'h', false },
		{ "Defences", 'h', true },
		{ "Quests", 'h', false },
		{ "Cave Survey", 'h', false }
	};
	char subtitle[160];
	int count = 0;
	int i;

	if (!Term || !Term->screen_hook || !player) return 0;
	if (!have_valid_char_sheet_config()) configure_char_sheet();
	player_screen_add_heading(&storage, &count, "Attribute modifiers");
	for (i = 0; i < cached_config->n_stat_mod_entries; i++) {
		player_screen_add_defence(&storage, &count,
			cached_config->stat_mod_entries[i], -1);
	}
	for (i = 0; i < 4; i++) {
		int j;

		player_screen_add_heading(&storage, &count, headings[i]);
		for (j = 0; j < cached_config->n_resist_by_region[i]; j++) {
			player_screen_add_defence(&storage, &count,
				cached_config->resists_by_region[i][j].entry, i);
		}
	}
	strnfmt(subtitle, sizeof(subtitle),
		"%s %s   Level %d   Overall result and known sources",
		player->race->name, player->class->name, player->lev);
	screen.kind = UI_SCREEN_CHARACTER_DOSSIER;
	screen.title = "Character dossier";
	screen.subtitle = subtitle;
	screen.help =
		"Up/Down or 8/2 scroll   H or Left/Right changes page   C renames   F exports   Escape returns";
	screen.content_col = 4;
	screen.content_row = PLAYER_SCREEN_CONTENT_ROW;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = -1;
	screen.row_offset = MIN(MAX(0, row_offset),
		MAX(0, count - screen.content_rows));
	screen.row_count = count;
	screen.rows = storage.rows;
	screen.tab_count = 4;
	screen.tabs = tabs;
	Term->screen_hook(&screen);
	return MAX(0, count - screen.content_rows);
}

static int present_player_overview(int row_offset)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[PLAYER_SCREEN_ROW_CAPACITY];
	struct ui_screen_tab tabs[4] = {
		{ "Overview", 'h', true },
		{ "Defences", 'h', false },
		{ "Quests", 'h', false },
		{ "Cave Survey", 'h', false }
	};
	struct panel *owned_panels[N_ELEMENTS(panels)];
	char subtitle[160];
	int count = 0;
	size_t i;

	if (!Term || !Term->screen_hook || !player) return 0;
	memset(rows, 0, sizeof(rows));
	memset(owned_panels, 0, sizeof(owned_panels));
	for (i = 0; i < N_ELEMENTS(player_screen_panel_order); i++) {
		int panel_index = player_screen_panel_order[i];
		struct panel *panel = panels[panel_index].panel();
		size_t line;

		owned_panels[panel_index] = panel;
		if (count >= PLAYER_SCREEN_ROW_CAPACITY) break;
		rows[count].label = player_screen_groups[i];
		rows[count].attr = COLOUR_L_BLUE;
		rows[count].enabled = false;
		count++;
		for (line = 0; line < panel->len &&
				line < N_ELEMENTS(player_screen_labels[panel_index]) &&
				count < PLAYER_SCREEN_ROW_CAPACITY; line++) {
			struct panel_line *source = &panel->lines[line];
			struct ui_screen_row *row;
			const char *label = player_screen_labels[panel_index][line];

			if (!label) continue;
			row = &rows[count++];
			row->label = label;
			row->detail = source->value;
			row->attr = source->attr;
			row->enabled = true;
		}
	}
	strnfmt(subtitle, sizeof(subtitle), "%s %s   Level %d   %s",
		player->race->name, player->class->name, player->lev, show_title());
	screen.kind = UI_SCREEN_CHARACTER_DOSSIER;
	screen.title = "Character dossier";
	screen.subtitle = subtitle;
	screen.help =
		"Up/Down or 8/2 scroll   H or Left/Right changes page   C renames   F exports   Escape returns";
	screen.context_title = "History";
	screen.context = player->history;
	screen.content_col = 4;
	screen.content_row = PLAYER_SCREEN_CONTENT_ROW;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = -1;
	screen.row_offset = MIN(MAX(0, row_offset),
		MAX(0, count - screen.content_rows));
	screen.row_count = count;
	screen.rows = rows;
	screen.tab_count = 4;
	screen.tabs = tabs;
	Term->screen_hook(&screen);
	for (i = 0; i < N_ELEMENTS(owned_panels); i++) {
		panel_free(owned_panels[i]);
	}
	return MAX(0, count - screen.content_rows);
}

int ui_player_present_objectives(int *row_offset, int cursor,
		bool briefing, const char *briefing_subtitle)
{
	struct player_screen_storage storage = { 0 };
	struct ui_screen screen = { 0 };
	struct ui_screen_tab tabs[4] = {
		{ "Overview", 'h', false },
		{ "Defences", 'h', false },
		{ "Quests", 'h', true },
		{ "Cave Survey", 'h', false }
	};
	const char *context_titles[PLAYER_SCREEN_ROW_CAPACITY] = { 0 };
	const char *contexts[PLAYER_SCREEN_ROW_CAPACITY] = { 0 };
	char briefing_context[1024] = "";
	int count = 0;
	int i;
	int maximum_offset;

	if (!Term || !Term->screen_hook || !player || !row_offset) return 0;
	if (!briefing) {
		for (i = 0; i < z_info->quest_max &&
				count < PLAYER_SCREEN_ROW_CAPACITY; i++) {
			const struct quest *definition = &quests[i];
			const struct quest *progress = &player->quests[i];
			struct ui_screen_row *row = &storage.rows[count];
			bool complete = progress->level == 0;

			my_strcpy(storage.labels[count], definition->name,
				sizeof(storage.labels[count]));
			my_strcpy(storage.statuses[count], complete ? "COMPLETE" : "MAIN",
				sizeof(storage.statuses[count]));
			strnfmt(storage.sources[count], sizeof(storage.sources[count]),
				"Meridian %u — %s", definition->level,
				definition->reward ? definition->reward : "Required");
			row->label = storage.labels[count];
			row->prefix = storage.statuses[count];
			row->detail = storage.sources[count];
			row->attr = complete ? COLOUR_L_GREEN : COLOUR_L_BLUE;
			row->enabled = true;
			context_titles[count] = definition->name;
			contexts[count] = definition->description ?
				definition->description : "No description recorded.";
			count++;
		}
	}
	for (i = 0; i < world_objective_count() &&
			count < PLAYER_SCREEN_ROW_CAPACITY; i++) {
		const struct world_objective_definition *definition =
			world_objective_by_index(i);
		struct world_objective_progress progress;
		struct ui_screen_row *row;

		if (!definition || !world_objective_progress(player, definition,
				&progress)) continue;
		row = &storage.rows[count];
		my_strcpy(storage.labels[count], definition->title,
			sizeof(storage.labels[count]));
		my_strcpy(storage.statuses[count], progress.complete ?
			"COMPLETE" : "OPTIONAL", sizeof(storage.statuses[count]));
		strnfmt(storage.sources[count], sizeof(storage.sources[count]),
			"%u/%u — %s", progress.current, progress.target,
			definition->reward);
		row->label = storage.labels[count];
		row->prefix = storage.statuses[count];
		row->detail = storage.sources[count];
		row->attr = progress.complete ? COLOUR_L_GREEN : COLOUR_YELLOW;
		row->enabled = true;
		context_titles[count] = definition->title;
		contexts[count] = definition->description;
		if (briefing) {
			if (briefing_context[0]) {
				my_strcat(briefing_context, "\n\n",
					sizeof(briefing_context));
			}
			my_strcat(briefing_context, definition->title,
				sizeof(briefing_context));
			my_strcat(briefing_context, ": ", sizeof(briefing_context));
			my_strcat(briefing_context, definition->description,
				sizeof(briefing_context));
		}
		count++;
	}
	cursor = briefing || count == 0 ? -1 : MIN(MAX(0, cursor), count - 1);
	screen.kind = UI_SCREEN_CHARACTER_DOSSIER;
	screen.title = briefing ? "Optional quests" : "Quest log";
	screen.subtitle = briefing ? (briefing_subtitle ? briefing_subtitle :
		"Optional preparations for this expedition") :
		"Fishing, spelunking, and village progress";
	screen.help = briefing ?
		"Press any key to enter the shop   Reopen with Shift+C, then Quests" :
		"Up/Down selects a quest   Left/Right changes page   Escape returns";
	if (briefing) {
		screen.context_title = "Objectives";
		screen.context = briefing_context;
	} else if (cursor >= 0) {
		screen.context_title = context_titles[cursor];
		screen.context = contexts[cursor];
	}
	screen.content_col = 4;
	screen.content_row = PLAYER_SCREEN_CONTENT_ROW;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = cursor;
	maximum_offset = MAX(0, count - screen.content_rows);
	*row_offset = MIN(MAX(0, *row_offset), maximum_offset);
	if (cursor >= 0 && cursor < *row_offset) *row_offset = cursor;
	if (cursor >= *row_offset + screen.content_rows) {
		*row_offset = cursor - screen.content_rows + 1;
	}
	screen.row_offset = *row_offset;
	screen.row_count = count;
	screen.rows = storage.rows;
	screen.tab_count = 4;
	screen.tabs = tabs;
	Term->screen_hook(&screen);
	return count;
}

static int present_player_cave_survey(int *cursor)
{
	struct ui_spelunk_survey_model model = { 0 };
	struct ui_screen screen = { 0 };
	struct ui_screen_row empty_row = {
		"No cave survey recorded", NULL,
		"Enter and explore a cave to begin charting its sections.",
		0, COLOUR_SLATE, 0, false, 0, false
	};
	struct ui_screen_tab tabs[4] = {
		{ "Overview", 'h', false },
		{ "Defences", 'h', false },
		{ "Quests", 'h', false },
		{ "Cave Survey", 'h', true }
	};
	bool have_survey;

	if (!Term || !Term->screen_hook || !player || !cursor) return 0;
	have_survey = player->spelunking_system &&
		ui_spelunk_survey_build(player->spelunking_system, &model);
	if (have_survey) {
		*cursor = *cursor < 0 ? model.current_node :
			MIN(*cursor, model.node_count - 1);
	} else {
		*cursor = -1;
	}
	screen.kind = have_survey ? UI_SCREEN_CAVE_SURVEY :
		UI_SCREEN_CHARACTER_DOSSIER;
	screen.title = "Cave survey";
	screen.subtitle = have_survey ?
		"Only discovered sections and passages are shown." :
		"No connected cave has been charted yet.";
	screen.help = have_survey ?
		"Up/Down selects a section   Left/Right changes page   Escape returns" :
		"Left/Right changes page   Escape returns";
	screen.content_col = 4;
	screen.content_row = PLAYER_SCREEN_CONTENT_ROW;
	screen.content_cols = MAX(40, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = *cursor;
	screen.row_count = have_survey ? model.node_count : 1;
	screen.rows = have_survey ? model.rows : &empty_row;
	screen.tab_count = 4;
	screen.tabs = tabs;
	if (have_survey) {
		screen.map_node_count = model.node_count;
		screen.map_nodes = model.nodes;
		screen.map_edge_count = model.edge_count;
		screen.map_edges = model.edges;
	}
	Term->screen_hook(&screen);
	return have_survey ? model.node_count : 0;
}

void ui_player_present_fishing_quest_briefing(const char *subtitle, bool wait_for_ack)
{
	char body[4096] = "";
	int i;
	if (!player) return;
	for (i = 0; i < world_objective_count(); i++) {
		const struct world_objective_definition *objective =
			world_objective_by_index(i);
		my_strcat(body, objective->title, sizeof(body));
		my_strcat(body, "\n", sizeof(body));
		my_strcat(body, objective->description, sizeof(body));
		my_strcat(body, "\n\n", sizeof(body));
	}
	ui_story_show("Work for willing hands", subtitle, body,
		"These quests are optional. Press Shift+C > Quests at any time "
		"to read each objective, its reward and your progress.", wait_for_ack);
}

static void display_player_objectives_terminal(void)
{
	int i;
	int row = 4;

	clear_from(0);
	Term_putstr(2, 1, -1, COLOUR_L_BLUE, "Quest log");
	for (i = 0; i < z_info->quest_max; i++) {
		const struct quest *definition = &quests[i];
		const char *description = definition->description ?
			definition->description : "No description recorded.";
		const char *reward = definition->reward ? definition->reward :
			"No reward recorded.";
		bool complete = player->quests[i].level == 0;
		char line[160];

		strnfmt(line, sizeof(line), "%s  [Meridian %u]  %s",
			definition->name, definition->level,
			complete ? "COMPLETE" : "MAIN");
		Term_putstr(4, row++, -1,
			complete ? COLOUR_L_GREEN : COLOUR_L_BLUE, line);
		Term_putstr(6, row++, MIN(70, (int)strlen(description)),
			COLOUR_WHITE, description);
		Term_putstr(6, row++, MIN(70, (int)strlen(reward)), COLOUR_SLATE,
			reward);
		row++;
	}
	for (i = 0; i < world_objective_count(); i++) {
		const struct world_objective_definition *definition =
			world_objective_by_index(i);
		struct world_objective_progress progress;
		char line[160];

		if (!definition || !world_objective_progress(player, definition,
				&progress)) continue;
		strnfmt(line, sizeof(line), "%s  [%u/%u]  %s", definition->title,
			progress.current, progress.target,
			progress.complete ? "COMPLETE" : "OPTIONAL");
		Term_putstr(4, row++, -1,
			progress.complete ? COLOUR_L_GREEN : COLOUR_YELLOW, line);
		Term_putstr(6, row++, MIN(70,
			(int)strlen(definition->description)), COLOUR_WHITE,
			definition->description);
		Term_putstr(6, row++, MIN(70,
			(int)strlen(definition->reward)), COLOUR_SLATE,
			definition->reward);
		row++;
	}
}

static void display_player_cave_survey_terminal(int cursor)
{
	struct ui_spelunk_survey_model model = { 0 };
	bool have_survey = player && player->spelunking_system &&
		ui_spelunk_survey_build(player->spelunking_system, &model);
	int i;

	clear_from(0);
	Term_putstr(2, 1, -1, COLOUR_L_BLUE, "Cave survey");
	if (!have_survey) {
		Term_putstr(4, 4, -1, COLOUR_SLATE,
			"No cave survey recorded. Explore a cave to begin charting it.");
		return;
	}
	cursor = cursor < 0 ? model.current_node :
		MIN(cursor, model.node_count - 1);
	for (i = 0; i < model.node_count && i < 16; i++) {
		char line[256];

		strnfmt(line, sizeof(line), "%s%s  %s", i == cursor ? "> " : "  ",
			model.rows[i].label, model.rows[i].detail);
		Term_putstr(4, 4 + i, MIN(75, (int)strlen(line)),
			i == cursor ? COLOUR_L_BLUE : model.rows[i].attr, line);
	}
}

void display_player_xtra_info(void)
{
	size_t i;
	for (i = 0; i < N_ELEMENTS(panels); i++) {
		struct panel *p = panels[i].panel();
		display_panel(p, panels[i].align_left, &panels[i].bounds);
		panel_free(p);
	}

	/* Indent output by 1 character, and wrap at column 72 */
	text_out_wrap = 72;
	text_out_indent = 1;

	/* History */
	Term_gotoxy(text_out_indent, 19);
	text_out_to_screen(COLOUR_WHITE, player->history);

	/* Reset text_out() vars */
	text_out_wrap = 0;
	text_out_indent = 0;

	return;
}

/**
 * Display the character on the screen (two different modes)
 *
 * The top two lines, and the bottom line (or two) are left blank.
 *
 * Mode 0 = standard display with skills/history
 * Mode 1 = special display with equipment flags
 */
void display_player(int mode)
{
	if (!have_valid_char_sheet_config()) {
		configure_char_sheet();
	}

	/* Erase screen */
	clear_from(0);

	/* When not playing, do not display in subwindows */
	if (Term != angband_term[0] && !player->upkeep->playing) return;

	/* Stat info */
	display_player_stat_info();

	if (mode) {
		struct panel *p = panels[0].panel();
		display_panel(p, panels[0].align_left, &panels[0].bounds);
		panel_free(p);

		/* Stat/Sustain flags */
		display_player_sust_info(cached_config);

		/* Other flags */
		display_player_flag_info();
	} else {
		/* Extra info */
		display_player_xtra_info();
	}
}


/**
 * Write a character dump
 */
void write_character_dump(ang_file *fff)
{
	int i, x, y, ylim;

	int a;
	wchar_t c;

	struct store *home = &stores[f_info[FEAT_HOME].shopnum - 1];
	struct object **home_list = mem_zalloc(sizeof(struct object *) *
										   z_info->store_inven_max);
	char o_name[80];

	int n;
	char *buf, *p;

	if (!have_valid_char_sheet_config()) {
		configure_char_sheet();
	}

	n = 80;
	if (n < 2 * cached_config->res_cols + 1) {
		n = 2 * cached_config->res_cols + 1;
	}
	buf = mem_alloc(text_wcsz() * n + 1);

	/* Begin dump */
	file_putf(fff, "  [%s Character Dump]\n\n", buildid);

	/* Display player basics */
	display_player(0);

	/* Dump part of the screen */
	for (y = 1; y < 23; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 79; x++) {
			/* Get the attr/char */
			(void)(Term_what(x, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Display player resistances etc */
	display_player(1);

	/* Print a header */
	file_putf(fff, "%-20s%s\n", "Resistances", "Abilities");

	/* Dump part of the screen */
	ylim = ((cached_config->n_resist_by_region[0] >
		cached_config->n_resist_by_region[1]) ?
		cached_config->n_resist_by_region[0] :
		cached_config->n_resist_by_region[1]) +
		cached_config->res_regions[0].row + 2;
	for (y = cached_config->res_regions[0].row + 2; y < ylim; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 2 * cached_config->res_cols + 1; x++) {
			/* Get the attr/char */
			(void)(Term_what(x, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Skip a line */
	file_putf(fff, "\n");

	/* Print a header */
	file_putf(fff, "%-20s%s\n", "Hindrances", "Modifiers");

	/* Dump part of the screen */
	ylim = ((cached_config->n_resist_by_region[2] >
		cached_config->n_resist_by_region[3]) ?
		cached_config->n_resist_by_region[2] :
		cached_config->n_resist_by_region[3]) +
		cached_config->res_regions[0].row + 2;
	for (y = cached_config->res_regions[0].row + 2; y < ylim; y++) {
		p = buf;
		/* Dump each row */
		for (x = 0; x < 2 * cached_config->res_cols + 1; x++) {
			/* Get the attr/char */
			(void)(Term_what(x + 2 * cached_config->res_cols + 2, y, &a, &c));

			/* Dump it */
			n = text_wctomb(p, c);
			if (n > 0) {
				p += n;
			} else {
				*p++ = ' ';
			}
		}

		/* Back up over spaces */
		while ((p > buf) && (p[-1] == ' ')) --p;

		/* Terminate */
		*p = '\0';

		/* End the row */
		file_putf(fff, "%s\n", buf);
	}

	/* Skip some lines */
	file_putf(fff, "\n\n");


	/* If dead, dump last messages -- Prfnoff */
	if (player->is_dead) {
		i = messages_num();
		if (i > 15) i = 15;
		file_putf(fff, "  [Last Messages]\n\n");
		while (i-- > 0)
		{
			file_putf(fff, "> %s\n", message_str((int16_t)i));
		}
		if (streq(player->died_from, "Retiring")) {
			file_putf(fff, "\nRetired.\n\n");
		} else {
			file_putf(fff, "\nKilled by %s.\n\n",
				player->died_from);
		}
	}


	/* Dump the equipment */
	file_putf(fff, "  [Character Equipment]\n\n");
	for (i = 0; i < player->body.count; i++) {
		struct object *obj = slot_object(player, i);
		if (!obj) continue;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the inventory */
	file_putf(fff, "\n\n  [Character Inventory]\n\n");
	for (i = 0; i < z_info->pack_size; i++) {
		struct object *obj = player->upkeep->inven[i];
		if (!obj) break;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the quiver */
	file_putf(fff, "\n\n  [Character Quiver]\n\n");
	for (i = 0; i < z_info->quiver_size; i++) {
		struct object *obj = player->upkeep->quiver[i];
		if (!obj) continue;

		object_desc(o_name, sizeof(o_name), obj,
			ODESC_PREFIX | ODESC_FULL, player);
		file_putf(fff, "%c) %s\n", gear_to_label(player, obj), o_name);
		object_info_chardump(fff, obj, 5, 72);
	}
	file_putf(fff, "\n\n");

	/* Dump the Home -- if anything there */
	store_stock_list(home, home_list, z_info->store_inven_max);
	if (home->stock_num) {
		/* Header */
		file_putf(fff, "  [Home Inventory]\n\n");

		/* Dump all available items */
		for (i = 0; i < z_info->store_inven_max; i++) {
			struct object *obj = home_list[i];
			if (!obj) break;
			object_desc(o_name, sizeof(o_name), obj,
				ODESC_PREFIX | ODESC_FULL, player);
			file_putf(fff, "%c) %s\n", I2A(i), o_name);

			object_info_chardump(fff, obj, 5, 72);
		}

		/* Add an empty line */
		file_putf(fff, "\n\n");
	}

	/* Dump character history */
	dump_history(fff);
	file_putf(fff, "\n\n");

	/* Dump options */
	file_putf(fff, "  [Options]\n\n");

	/* Dump options */
	for (i = 0; i < OP_MAX; i++) {
		int opt;
		const char *title = "";
		switch (i) {
			case OP_INTERFACE: title = "User interface"; break;
			case OP_BIRTH: title = "Birth"; break;
		    default: continue;
		}

		file_putf(fff, "  [%s]\n\n", title);
		for (opt = 0; opt < OPT_MAX; opt++) {
			const char *desc;
			size_t u8len;

			if (option_type(opt) != i) continue;
			desc = option_desc(opt);
			u8len = utf8_strlen(desc);
			if (u8len < 45) {
				file_putf(fff, "%s%*s", desc,
					(int)(45 - u8len), " ");
			} else {
				file_putf(fff, "%s", desc);
			}
			file_putf(fff, ": %s (%s)\n",
			        player->opts.opt[opt] ? "yes" : "no ",
			        option_name(opt));
		}

		/* Skip some lines */
		file_putf(fff, "\n");
	}

	/*
	 * Display the randart seed, if applicable.  Use the same format as is
	 * used when constructing the randart file name.
	 */
	if (OPT(player, birth_randarts)) {
		file_putf(fff, "  [Randart seed]\n\n");
		file_putf(fff, "%08lx\n\n", (unsigned long)seed_randart);
	}

	mem_free(home_list);
	mem_free(buf);
}

/**
 * Save the lore to a file in the user directory.
 *
 * \param path is the path to the filename
 *
 * \returns true on success, false otherwise.
 */
bool dump_save(const char *path)
{
	if (text_lines_to_file(path, write_character_dump)) {
		msg("Failed to create file %s.new", path);
		return false;
	}

	return true;
}



/**
 * Change name
 */
int ui_player_character_page_step(int page, int direction)
{
	page = MIN(MAX(0, page), UI_PLAYER_CHARACTER_PAGE_COUNT - 1);
	return (page + (direction < 0 ? UI_PLAYER_CHARACTER_PAGE_COUNT - 1 : 1)) %
		UI_PLAYER_CHARACTER_PAGE_COUNT;
}

static void change_character_page(int *page, int direction)
{
	int next;

	if (!page) return;
	next = ui_player_character_page_step(*page, direction);
	if (next == *page) return;
	*page = next;
	sound(MSG_UI_CHANGE);
}

void do_cmd_change_name(void)
{
	ui_event ke;
	int mode = UI_PLAYER_CHARACTER_OVERVIEW;
	int overview_offset = 0;
	int overview_max_offset = 0;
	int defences_offset = 0;
	int defences_max_offset = 0;
	int objectives_offset = 0;
	int objectives_cursor = 0;
	int objectives_count = 0;
	int survey_cursor = -1;
	int survey_count = 0;

	const char *p;

	bool more = true;

	/* Prompt */
	p = "['c' to change name, 'f' to file, 'h' to change mode, or ESC]";

	/* Save screen */
	screen_save();

	/* Forever */
	while (more) {
		if (Term->screen_hook) {
			if (mode == UI_PLAYER_CHARACTER_OVERVIEW) {
				overview_max_offset =
					present_player_overview(overview_offset);
				overview_offset = MIN(overview_offset,
					overview_max_offset);
			} else if (mode == UI_PLAYER_CHARACTER_DEFENCES) {
				defences_max_offset =
					present_player_defences(defences_offset);
				defences_offset = MIN(defences_offset,
					defences_max_offset);
			} else if (mode == UI_PLAYER_CHARACTER_QUESTS) {
				objectives_count = ui_player_present_objectives(
					&objectives_offset, objectives_cursor, false, NULL);
				if (objectives_count > 0) {
					objectives_cursor = MIN(objectives_cursor,
						objectives_count - 1);
				}
			} else {
				survey_count = present_player_cave_survey(&survey_cursor);
				if (survey_count > 0) {
					survey_cursor = MIN(survey_cursor, survey_count - 1);
				}
			}
		} else {
			/* Terminal frontends retain the inherited character sheets.  SDL3's
			 * semantic dossier is the sole presenter there: drawing both models
			 * caused fragments from the legacy labels to prefix the new rows. */
			if (mode == UI_PLAYER_CHARACTER_QUESTS) {
				display_player_objectives_terminal();
			} else if (mode == UI_PLAYER_CHARACTER_CAVE_SURVEY) {
				display_player_cave_survey_terminal(survey_cursor);
			} else {
				display_player(mode);
			}
			Term_putstr(2, 23, -1, COLOUR_WHITE, p);
		}

		/* Query */
		ke = inkey_m();

		if (ke.type == EVT_KBRD) {
			switch (ke.key.code) {
				case ESCAPE: more = false; break;
				case 'c': {
					if (Term->screen_hook) Term->screen_hook(NULL);
					if(arg_force_name)
						msg("You are not allowed to change your name!");
					else {
					char namebuf[32] = "";

					/* Set player name */
					if (get_character_name(namebuf, sizeof namebuf))
						my_strcpy(player->full_name, namebuf,
								  sizeof(player->full_name));
					}

					break;
				}

				case 'f': {
					char buf[1024];
					char fname[80];
					if (Term->screen_hook) Term->screen_hook(NULL);

					/* Get the filesystem-safe name and append .txt */
					player_safe_name(fname, sizeof(fname), player->full_name, false);
					my_strcat(fname, ".txt", sizeof(fname));

					if (get_file(fname, buf, sizeof buf)) {
						if (dump_save(buf))
							msg("Character dump successful.");
						else
							msg("Character dump failed!");
					}
					break;
				}
				
				case 'h':
				case ' ':
				case 'l':
				case ARROW_RIGHT:
					change_character_page(&mode, 1);
					break;

				case ARROW_LEFT:
					change_character_page(&mode, -1);
					break;

				case ARROW_UP:
				case '8':
				case 'k':
					if (mode == UI_PLAYER_CHARACTER_OVERVIEW) {
						overview_offset = MAX(0, overview_offset - 1);
					} else if (mode == UI_PLAYER_CHARACTER_DEFENCES) {
						defences_offset = MAX(0, defences_offset - 1);
					} else if (mode == UI_PLAYER_CHARACTER_QUESTS) {
						objectives_cursor = MAX(0, objectives_cursor - 1);
					} else {
						survey_cursor = MAX(0, survey_cursor - 1);
					}
					break;

				case ARROW_DOWN:
				case '2':
				case 'j':
					if (mode == UI_PLAYER_CHARACTER_OVERVIEW) {
						overview_offset = MIN(overview_offset + 1,
							overview_max_offset);
					} else if (mode == UI_PLAYER_CHARACTER_DEFENCES) {
						defences_offset = MIN(defences_offset + 1,
							defences_max_offset);
					} else if (mode == UI_PLAYER_CHARACTER_QUESTS &&
							objectives_count > 0) {
						objectives_cursor = MIN(objectives_cursor + 1,
							objectives_count - 1);
					} else if (mode == UI_PLAYER_CHARACTER_CAVE_SURVEY &&
							survey_count > 0) {
						survey_cursor = MIN(survey_cursor + 1,
							survey_count - 1);
					}
					break;
			}
		} else if (ke.type == EVT_MOUSE) {
			if (ke.mouse.button == 1) {
				/* Flip through the screens */			
				change_character_page(&mode, 1);
			} else if (ke.mouse.button == 2) {
				/* exit the screen */
				more = false;
			} else {
				/* Flip backwards through the screens */			
				change_character_page(&mode, -1);
			}
		}

		/* Flush messages */
		event_signal(EVENT_MESSAGE_FLUSH);
	}
	if (Term->screen_hook) Term->screen_hook(NULL);

	/* Load screen */
	screen_load();
}


static void init_ui_player(void)
{
	/* Nothing to do; lazy initialization. */
}


static void cleanup_ui_player(void)
{
	release_char_sheet_config();
}


struct init_module ui_player_module = {
	.name = "ui-player",
	.init = init_ui_player,
	.cleanup = cleanup_ui_player
};
