/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-birth-screen.c
 * \brief Semantic presentation adapter for character-creation choices.
 */

#include "angband.h"

#include "player.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "obj-properties.h"
#include "player-resource.h"
#include "ui-birth-screen.h"
#include "ui-display.h"
#include "ui-menu.h"
#include "ui-screen.h"

static void context_skills(char *context, size_t capacity, size_t *end,
		const int race_skills[], const int class_skills[], int hit_die,
		int experience, int infravision)
{
	int skills[SKILL_MAX];
	int i;

	for (i = 0; i < SKILL_MAX; i++) {
		skills[i] = (race_skills ? race_skills[i] : 0) +
			(class_skills ? class_skills[i] : 0);
	}
	strnfcat(context, capacity, end, "Combat  melee %+d  ranged %+d\n",
		skills[SKILL_TO_HIT_MELEE], skills[SKILL_TO_HIT_BOW]);
	strnfcat(context, capacity, end, "Throw   %+d\n",
		skills[SKILL_TO_HIT_THROW]);
	strnfcat(context, capacity, end, "Disarm  physical %+d  arcane %+d\n",
		skills[SKILL_DISARM_PHYS], skills[SKILL_DISARM_MAGIC]);
	strnfcat(context, capacity, end, "Device  %+d   Save %+d   Stealth %+d\n",
		skills[SKILL_DEVICE], skills[SKILL_SAVE], skills[SKILL_STEALTH]);
	strnfcat(context, capacity, end, "Digging %+d   Search %+d\n",
		skills[SKILL_DIGGING], skills[SKILL_SEARCH]);
	strnfcat(context, capacity, end, "Hit die %d   Experience %d%%",
		hit_die, experience);
	if (infravision >= 0) {
		strnfcat(context, capacity, end, "   Infravision %d ft",
			infravision * 10);
	}
	strnfcat(context, capacity, end, "\n");
}

static void context_stats(char *context, size_t capacity, size_t *end,
		const int race_adj[], const int class_adj[])
{
	int half = (STAT_MAX + 1) / 2;
	int i;

	strnfcat(context, capacity, end, "Stat modifiers\n");
	for (i = 0; i < half; i++) {
		int second = i + half;
		int first_adj = (race_adj ? race_adj[i] : 0) +
			(class_adj ? class_adj[i] : 0);

		strnfcat(context, capacity, end, "  %-4s %+d",
			stat_names_reduced[i], first_adj);
		if (second < STAT_MAX) {
			int second_adj = (race_adj ? race_adj[second] : 0) +
				(class_adj ? class_adj[second] : 0);

			strnfcat(context, capacity, end, "        %-4s %+d",
				stat_names_reduced[second], second_adj);
		}
		strnfcat(context, capacity, end, "\n");
	}
}

static void context_abilities(char *context, size_t capacity, size_t *end,
		const bitflag object_flags[], const bitflag player_flags[],
		const struct element_info *elements, bool include_elements)
{
	struct player_ability *ability;
	int shown = 0;

	for (ability = player_abilities; ability && shown < 3;
			ability = ability->next) {
		bool granted = false;

		if (streq(ability->type, "object")) {
			granted = object_flags && of_has(object_flags, ability->index);
		} else if (streq(ability->type, "player")) {
			granted = player_flags && pf_has(player_flags, ability->index);
		} else if (include_elements && elements &&
				streq(ability->type, "element")) {
			granted = elements[ability->index].res_level == ability->value;
		}
		if (!granted) continue;
		if (!shown) strnfcat(context, capacity, end, "Traits\n");
		strnfcat(context, capacity, end, "  %s\n", ability->name);
		shown++;
	}
}

void ui_birth_origin_context(const struct player_race *race,
		char *context, size_t capacity)
{
	size_t end = 0;
	if (!context || !capacity) return;
	context[0] = '\0';
	if (!race) return;
	if (race->lore && race->lore[0]) {
		strnfcat(context, capacity, &end, "%s\n\n", race->lore);
	}
	context_stats(context, capacity, &end, race->r_adj, NULL);
	context_skills(context, capacity, &end, race->r_skills, NULL,
		race->r_mhp, race->r_exp, race->infra);
	context_abilities(context, capacity, &end, race->flags,
		race->pflags, race->el_info, true);
}

static void format_context(enum ui_birth_screen_stage stage, int choice,
		char *context, size_t capacity)
{
	size_t end = 0;

	context[0] = '\0';
	if (stage == UI_BIRTH_SCREEN_RACE) {
		struct player_race *race = player_id2race(choice);

		ui_birth_origin_context(race, context, capacity);
	} else if (stage == UI_BIRTH_SCREEN_CLASS) {
		struct player_class *class = player_id2class(choice);
		const struct player_race *race = player->race;

		if (!class || !race) return;
		context_stats(context, capacity, &end, race->r_adj, class->c_adj);
		context_skills(context, capacity, &end, race->r_skills,
			class->c_skills, race->r_mhp + class->c_mhp,
			race->r_exp + class->c_exp, -1);
		if (class->magic.total_spells) {
			strnfcat(context, capacity, &end, "Magic  available\n");
		}
		context_abilities(context, capacity, &end, class->flags,
			class->pflags, NULL, false);
	} else if (choice == 0) {
		strnfcat(context, capacity, &end,
			"Shape your attributes directly.\n\n"
			"Recommended for a deliberate first character.\n"
			"Every point and trade-off is visible before you continue.");
	} else {
		strnfcat(context, capacity, &end,
			"Roll a traditional random set of attributes.\n\n"
			"You may reroll or retrieve the previous result before accepting.");
	}
}

void ui_birth_screen_present(struct menu *menu,
		enum ui_birth_screen_stage stage, const char *const *items,
		int item_count, const char *hint)
{
	struct ui_screen screen = { 0 };
	struct ui_screen_row rows[64];
	char context[1024];
	const char *title;
	int count;
	int i;

	if (!Term || !Term->screen_hook || !menu || !items || item_count <= 0) {
		return;
	}
	count = MIN(item_count, (int)N_ELEMENTS(rows));
	if (menu->cursor < 0 || menu->cursor >= count) return;
	memset(rows, 0, sizeof(rows));
	for (i = 0; i < count; i++) {
		rows[i].label = items[i];
		rows[i].tag = menu->selections && menu->selections[i] ?
			menu->selections[i] : 0;
		rows[i].attr = COLOUR_WHITE;
		rows[i].enabled = true;
	}
	format_context(stage, menu->cursor, context, sizeof(context));
	if (stage == UI_BIRTH_SCREEN_RACE) {
		title = "Choose your origin";
	} else if (stage == UI_BIRTH_SCREEN_CLASS) {
		title = "Choose your class";
	} else {
		title = "Choose attribute generation";
	}
	screen.kind = UI_SCREEN_CHARACTER_CREATION;
	screen.title = title;
	screen.subtitle = hint;
	screen.help =
		"Up/Down moves   Enter selects   = birth options   * random   @ finish randomly   Escape goes back";
	screen.context_title = items[menu->cursor];
	screen.context = context;
	screen.content_col = 4;
	screen.content_row = 8;
	screen.content_cols = MAX(72, Term->wid - 8);
	screen.content_rows = MAX(1, Term->hgt - screen.content_row - 3);
	screen.cursor = menu->cursor;
	screen.row_offset = menu->top;
	screen.row_count = count;
	screen.rows = rows;
	Term->screen_hook(&screen);
}

static bool can_present(void)
{
	return Term && Term->screen_hook && player;
}

static void set_common_screen(struct ui_screen *screen, const char *title,
		const char *subtitle, const char *help,
		const struct ui_screen_row *rows, int row_count, int cursor,
		const char *context_title, const char *context)
{
	memset(screen, 0, sizeof(*screen));
	screen->kind = UI_SCREEN_CHARACTER_CREATION;
	screen->title = title;
	screen->subtitle = subtitle;
	screen->help = help;
	screen->context_title = context_title;
	screen->context = context;
	screen->content_col = 4;
	screen->content_row = 8;
	screen->content_cols = MAX(72, Term->wid - 8);
	screen->content_rows = MAX(1, Term->hgt - screen->content_row - 3);
	screen->cursor = cursor;
	screen->row_count = row_count;
	screen->rows = rows;
}

static void append_identity(char *context, size_t capacity, size_t *end)
{
	const char *race_name = player->race ? player->race->name : "Unchosen";
	const char *class_name = player->class ? player->class->name : "Unchosen";

	strnfcat(context, capacity, end, "%s %s\n", race_name, class_name);
	if (player->mhp > 0) {
		strnfcat(context, capacity, end, "Hit points %d\n", player->mhp);
	}
	strnfcat(context, capacity, end, "Stamina %d\n",
		player_stamina_maximum(player));
}

static void append_birth_effects(char *context, size_t capacity, size_t *end)
{
	int capacity_weight = weight_remaining(player) + player->upkeep->total_weight;
	strnfcat(context, capacity, end,
		"Focus %d   Carry %d.%d lb\nBlows %d.%02d/turn   Gold %ld\n",
		player->msp, capacity_weight / 10, capacity_weight % 10,
		player->state.num_blows / 100, player->state.num_blows % 100,
		(long)player->au_birth);
}

static bool compact_attributes(void)
{
	return Term->wid - 8 < 100;
}

static void append_attribute_identity(char *context, size_t capacity, size_t *end)
{
	if (compact_attributes()) {
		int weight = weight_remaining(player) + player->upkeep->total_weight;
		strnfcat(context, capacity, end,
			"%s %s\nHP %d   Stamina %d   Focus %d\n"
			"Carry %d.%d lb   Blows %d.%02d/turn   Gold %ld\n",
			player->race->name, player->class->name, player->mhp,
			player_stamina_maximum(player), player->msp, weight / 10, weight % 10,
			player->state.num_blows / 100, player->state.num_blows % 100,
			(long)player->au_birth);
	} else {
		append_identity(context, capacity, end);
		append_birth_effects(context, capacity, end);
	}
}

static void attribute_label(char *label, size_t capacity, int stat)
{
	char base[16], final[16];
	cnv_stat(player->stat_cur[stat], base, sizeof(base));
	cnv_stat(player->state.stat_use[stat], final, sizeof(final));
	strnfmt(label, capacity, "%-4s %6s  %+3d  %+3d  %6s",
		stat_names_reduced[stat], base,
		player->race ? player->race->r_adj[stat] : 0,
		player->class ? player->class->c_adj[stat] : 0, final);
}

void ui_birth_screen_present_points(int selected, const int spent[],
		const int increase[], int remaining, const int buysell_allowed[])
{
	struct ui_screen screen;
	struct ui_screen_row rows[STAT_MAX];
	char labels[STAT_MAX][96];
	char context[1536];
	char context_title[96];
	char subtitle[128];
	size_t end = 0;
	int total = remaining;
	int minimum, maximum;
	struct obj_property *property;
	int i;

	if (!can_present() || !spent || !increase || !buysell_allowed) return;
	selected = MIN(MAX(0, selected), STAT_MAX - 1);
	memset(rows, 0, sizeof(rows));
	for (i = 0; i < STAT_MAX; i++) {
		attribute_label(labels[i], sizeof(labels[i]), i);
		my_strcat(labels[i], format("  cost %d", spent[i]), sizeof(labels[i]));
		rows[i].label = labels[i];
		rows[i].attr = COLOUR_WHITE;
		rows[i].enabled = true;
		total += spent[i];
	}
	context[0] = '\0';
	append_attribute_identity(context, sizeof(context), &end);
	player_birth_stat_range(false, &minimum, &maximum);
	if (compact_attributes()) {
		strnfcat(context, sizeof(context), &end,
			"Base %d-%d   Points remaining %d/%d", minimum, maximum, remaining, total);
		if (player->stat_cur[selected] < maximum)
			strnfcat(context, sizeof(context), &end, "   Next costs %d", increase[selected]);
		strnfcat(context, sizeof(context), &end, "\nLeft: %s   Right: %s\n",
			(buysell_allowed[selected] & 1) ? "decrease" : "at minimum",
			(buysell_allowed[selected] & 2) ? "increase" : "at affordable maximum");
	} else {
		strnfcat(context, sizeof(context), &end,
			"\nBase range %d-%d\nPoints remaining %d of %d\n",
			minimum, maximum, remaining, total);
		if (player->stat_cur[selected] < maximum) {
			strnfcat(context, sizeof(context), &end, "Next increase costs %d\n",
				increase[selected]);
		}
		strnfcat(context, sizeof(context), &end, "%s%s",
			(buysell_allowed[selected] & 2) ? "Right: increase\n" :
				"At maximum affordable value\n",
			(buysell_allowed[selected] & 1) ? "Left: decrease\n" :
				"At minimum value\n");
	}
	strnfmt(context_title, sizeof(context_title), "Adjust %s",
		stat_names_reduced[selected]);
	property = lookup_obj_property(OBJ_PROPERTY_STAT, selected);
	if (property && property->desc) {
		strnfcat(context, sizeof(context), &end, "%s%s\n",
			compact_attributes() ? "" : "\n", property->desc);
	}
	strnfcat(context, sizeof(context), &end,
		compact_attributes() ? "Effects change at thresholds, not every point." :
		"\nEffects have thresholds: not every point changes every derived value.");
	/* The budget is essential, even when the lower context is clipped. */
	strnfmt(subtitle, sizeof(subtitle),
		"Points left: %d / %d  |  Base / Origin / Class / Final / Spent",
		remaining, total);
	set_common_screen(&screen, "Allocate attributes", subtitle,
		"Up/Down selects  Left/Right adjusts  R resets  Enter accepts  Esc back",
		rows, STAT_MAX, selected, context_title, context);
	screen.kind = UI_SCREEN_BIRTH_STATS;
	if (compact_attributes()) screen.content_row = 6;
	Term->screen_hook(&screen);
}

void ui_birth_screen_present_roll(bool previous_available)
{
	struct ui_screen screen;
	struct ui_screen_row rows[STAT_MAX];
	char labels[STAT_MAX][96];
	char context[1024];
	size_t end = 0;
	int previous[STAT_MAX];
	int minimum, maximum;
	int i;

	if (!can_present()) return;
	previous_available = previous_available && player_birth_previous_stats(previous);
	memset(rows, 0, sizeof(rows));
	for (i = 0; i < STAT_MAX; i++) {
		attribute_label(labels[i], sizeof(labels[i]), i);
		rows[i].label = labels[i];
		rows[i].attr = COLOUR_WHITE;
		rows[i].enabled = true;
		if (previous_available) {
			char value[16];
			int change = player->state.stat_use[i] - previous[i];
			cnv_stat(previous[i], value, sizeof(value));
			my_strcat(labels[i], format("  prev %s %s", value,
				change > 0 ? "+" : change < 0 ? "-" : "="), sizeof(labels[i]));
			rows[i].attr = change > 0 ? COLOUR_L_GREEN :
				change < 0 ? COLOUR_L_RED : COLOUR_WHITE;
		}
	}
	context[0] = '\0';
	append_attribute_identity(context, sizeof(context), &end);
	player_birth_stat_range(true, &minimum, &maximum);
	strnfcat(context, sizeof(context), &end,
		"\nBase roll range %d-%d\nEach roll is a complete candidate.\n",
		minimum, maximum);
	if (previous_available) {
		strnfcat(context, sizeof(context), &end,
			"The immediately previous roll is still available with P.\n");
	}
	set_common_screen(&screen, "Review rolled attributes",
		"Columns: Roll / Origin / Class / Final / Previous (+ higher, - lower)",
		previous_available ?
			"R rerolls   P restores previous   Enter accepts   Escape goes back" :
			"R rerolls   Enter accepts   Escape goes back",
		rows, STAT_MAX, -1, "Traditional roller", context);
	screen.kind = UI_SCREEN_BIRTH_STATS;
	if (compact_attributes()) screen.content_row = 6;
	Term->screen_hook(&screen);
}

static void text_with_cursor(const char *source, size_t cursor, bool show_cursor,
		char *destination, size_t capacity)
{
	const char *position;
	size_t prefix;

	if (!source) source = "";
	if (!show_cursor) {
		my_strcpy(destination, source, capacity);
		return;
	}
	position = source;
	while (cursor > 0 && *position) {
		position++;
		while (*position && ((uint8_t)*position & 0xc0) == 0x80) position++;
		cursor--;
	}
	prefix = position - source;
	if (prefix >= capacity) prefix = capacity - 1;
	memcpy(destination, source, prefix);
	destination[prefix] = '\0';
	my_strcat(destination, "|", capacity);
	my_strcat(destination, position, capacity);
}

void ui_birth_screen_present_name(const char *name, size_t cursor,
		bool first_time)
{
	struct ui_screen screen;
	struct ui_screen_row row = { 0 };
	char marked[PLAYER_NAME_LEN + 4];
	char label[PLAYER_NAME_LEN + 16];
	char context[384];
	size_t end = 0;

	if (!can_present()) return;
	text_with_cursor(name, cursor, true, marked, sizeof(marked));
	strnfmt(label, sizeof(label), "Name   %s", marked);
	row.label = label;
	row.attr = COLOUR_WHITE;
	row.enabled = true;
	context[0] = '\0';
	append_identity(context, sizeof(context), &end);
	strnfcat(context, sizeof(context), &end,
		"\nThis name also identifies the saved run.\n"
		"Leave it blank and press Enter for a random name.\n");
	if (first_time && name && name[0]) {
		strnfcat(context, sizeof(context), &end,
			"Typing replaces the suggested name; an arrow key keeps it.\n");
	}
	set_common_screen(&screen, "Name your character",
		"Choose the name that will appear in the world and on the Load Run screen.",
		"Type to edit   Left/Right moves   * randomises   Enter accepts   Escape goes back",
		&row, 1, 0, "Identity", context);
	Term->screen_hook(&screen);
}

static int split_history(const char *source, size_t cursor, bool editing,
		char lines[][96], struct ui_screen_row rows[], int capacity,
		int *selected)
{
	char marked[512];
	const char *position;
	int count = 0;

	text_with_cursor(source, cursor, editing, marked, sizeof(marked));
	position = marked;
	*selected = -1;
	while (*position && count < capacity) {
		size_t length = MIN((size_t)72, strlen(position));
		size_t i;

		if (position[length] && position[length] != '\n') {
			size_t break_at = length;

			while (break_at > 36 && position[break_at] != ' ') break_at--;
			if (break_at > 36) length = break_at;
		}
		for (i = 0; i < length && position[i] != '\n'; i++) {
			lines[count][i] = position[i];
			if (position[i] == '|') *selected = count;
		}
		lines[count][i] = '\0';
		rows[count].label = lines[count];
		rows[count].attr = COLOUR_WHITE;
		rows[count].enabled = true;
		position += i;
		while (*position == ' ' || *position == '\n') position++;
		count++;
	}
	if (!count) {
		my_strcpy(lines[0], editing ? "|" : "(No history)", sizeof(lines[0]));
		rows[0].label = lines[0];
		rows[0].attr = COLOUR_WHITE;
		rows[0].enabled = true;
		count = 1;
		if (editing) *selected = 0;
	}
	return count;
}

void ui_birth_screen_present_history(const char *history, size_t cursor,
		bool editing)
{
	struct ui_screen screen;
	struct ui_screen_row rows[8] = { 0 };
	char lines[8][96];
	int selected;
	int count;

	if (!can_present()) return;
	count = split_history(history, cursor, editing, lines, rows,
		N_ELEMENTS(rows), &selected);
	set_common_screen(&screen, editing ? "Edit your history" :
			"Your character's history",
		editing ? "Rewrite the generated background, or keep it as a seed for the journey." :
			"This short background is flavour, not a restriction on how you play.",
		editing ?
			"Type to edit   Arrows move   Enter accepts   Escape cancels" :
			"Y or Enter accepts   N edits   Escape returns to naming",
		rows, count, editing ? selected : -1, NULL, NULL);
	Term->screen_hook(&screen);
}

void ui_birth_screen_present_confirm(void)
{
	struct ui_screen screen;
	struct ui_screen_row rows[STAT_MAX];
	char labels[STAT_MAX][48];
	char context[1024];
	size_t end = 0;
	int i;

	if (!can_present()) return;
	memset(rows, 0, sizeof(rows));
	for (i = 0; i < STAT_MAX; i++) {
		char value[16];

		cnv_stat(player->state.stat_use[i], value, sizeof(value));
		strnfmt(labels[i], sizeof(labels[i]), "%-4s  %s",
			stat_names_reduced[i], value);
		rows[i].label = labels[i];
		rows[i].attr = COLOUR_WHITE;
		rows[i].enabled = true;
	}
	context[0] = '\0';
	append_identity(context, sizeof(context), &end);
	strnfcat(context, sizeof(context), &end, "\nHistory\n%s",
		player->history[0] ? player->history : "No history recorded.");
	set_common_screen(&screen, player->full_name[0] ? player->full_name :
			"Confirm your character",
		"One final review before the journey begins.",
		"Enter begins   Escape revisits history   S starts character creation over",
		rows, STAT_MAX, -1, "Ready to enter Ammerow", context);
	Term->screen_hook(&screen);
}
