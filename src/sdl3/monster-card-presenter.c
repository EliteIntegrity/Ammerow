/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/monster-card-presenter.c
 * \brief Lore-safe game-state adapter for SDL3 monster examine cards.
 */

#include "angband.h"

#include "init.h"
#include "mon-blows.h"
#include "mon-lore.h"
#include "mon-make.h"
#include "mon-predicate.h"
#include "mon-spell.h"
#include "mon-util.h"
#include "monster.h"
#include "player-timed.h"
#include "sdl3/map-view.h"
#include "sdl3/monster-card-data.h"
#include "sdl3/monster-card-presenter.h"
#include "ui-display.h"
#include "ui-term.h"

static struct monster *tracked_monster_at_cursor(
		const struct sdl3_map_view *view, struct term *main_term, int cursor_col,
		int cursor_row)
{
	struct monster *mon;
	int mon_col;
	int mon_row;

	if (!view || !main_term || !player || !player->upkeep ||
			player->timed[TMD_IMAGE]) {
		return NULL;
	}
	mon = player->upkeep->health_who;
	if (!mon || mon->hp < 0 || !mon->race || !monster_is_obvious(mon)) {
		return NULL;
	}
	mon_col = view->term_col +
		(mon->grid.x - main_term->offset_x) * tile_width;
	mon_row = view->term_row +
		(mon->grid.y - main_term->offset_y) * tile_height;
	return mon_col == cursor_col && mon_row == cursor_row ? mon : NULL;
}

bool sdl3_monster_card_cursor_tracks(const struct sdl3_map_view *view,
		struct term *main_term, int cursor_col, int cursor_row)
{
	return tracked_monster_at_cursor(view, main_term, cursor_col,
		cursor_row) != NULL;
}

static void list_add(struct sdl3_monster_card_list *list, const char *item)
{
	if (!list || !item || !item[0]) return;
	list->total++;
	if (list->count >= SDL3_MONSTER_CARD_LIST_CAPACITY) return;
	list->items[list->count++] = item;
}

static void format_dice(const random_value *dice, char *text,
		size_t capacity)
{
	char bonus[24];

	if (!text || !capacity) return;
	text[0] = '\0';
	if (!dice) return;
	if (dice->base && dice->dice && dice->sides) {
		strnfmt(text, capacity, "%d+%dd%d", dice->base, dice->dice,
			dice->sides);
	} else if (dice->dice && dice->sides) {
		strnfmt(text, capacity, "%dd%d", dice->dice, dice->sides);
	} else if (dice->base) {
		strnfmt(text, capacity, "%d", dice->base);
	}
	if (dice->m_bonus) {
		strnfmt(bonus, sizeof(bonus), "%sM%d", text[0] ? "+" : "",
			dice->m_bonus);
		my_strcat(text, bonus, capacity);
	}
}

static void format_attack(const struct monster_blow *blow, char *text,
		size_t capacity)
{
	char dice[48];
	const char *method;
	const char *effect;

	if (!text || !capacity) return;
	text[0] = '\0';
	if (!blow || !blow->method) return;
	method = blow->method->desc && blow->method->desc[0] ?
		blow->method->desc : blow->method->name;
	effect = blow->effect && blow->effect->desc ? blow->effect->desc : "";
	format_dice(&blow->dice, dice, sizeof(dice));
	if (effect[0] && dice[0]) {
		strnfmt(text, capacity, "%s to %s (%s)", method, effect, dice);
	} else if (effect[0]) {
		strnfmt(text, capacity, "%s to %s", method, effect);
	} else if (dice[0]) {
		strnfmt(text, capacity, "%s (%s)", method, dice);
	} else {
		my_strcpy(text, method, capacity);
	}
}

static void add_known_flag_type(struct sdl3_monster_card_list *list,
		const bitflag known_flags[RF_SIZE], int type)
{
	bitflag mask[RF_SIZE];
	int flag;

	create_mon_flag_mask(mask, type, RFT_MAX);
	rf_inter(mask, known_flags);
	for (flag = rf_next(mask, FLAG_START); flag;
			flag = rf_next(mask, flag + 1)) {
		list_add(list, describe_race_flag(flag));
	}
}

void sdl3_monster_card_configure(struct sdl3_inspect_card *card,
		const struct sdl3_map_view *view, struct term *main_term, int cursor_col,
		int cursor_row, bool cursor_visible, bool settings_visible, bool big)
{
	struct monster *mon;
	const struct monster_lore *lore;
	struct sdl3_monster_card_knowledge knowledge = { 0 };
	struct sdl3_monster_card_facts facts = { 0 };
	bitflag known_flags[RF_SIZE];
	bitflag breath_spells[RSF_SIZE];
	char attack_text[SDL3_MONSTER_CARD_LIST_CAPACITY][96] = { { 0 } };
	char power_text[SDL3_MONSTER_CARD_LIST_CAPACITY][96] = { { 0 } };
	char trait_text[SDL3_MONSTER_CARD_LIST_CAPACITY][96] = { { 0 } };
	char health[96];
	int i;

	if (!card) return;
	memset(card, 0, sizeof(*card));
	if (!view || !view->active || settings_visible || !main_term ||
			!cursor_visible) {
		return;
	}
	mon = tracked_monster_at_cursor(view, main_term, cursor_col, cursor_row);
	if (!mon) return;

	card->active = true;
	card->big = big;
	card->glyph = mon->race->d_char;
	card->attr = mon->race->d_attr;
	card->emphasis = rf_has(mon->race->flags, RF_UNIQUE);
	card->cursor_col = cursor_col;
	card->cursor_row = cursor_row;
	my_strcpy(card->name, mon->race->name, sizeof(card->name));
	sdl3_ascii_art_make_key(card->asset_key, sizeof(card->asset_key),
		SDL3_ASCII_ART_MONSTER, mon->race->art_id,
		mon->race->base ? mon->race->base->art_id : NULL);
	if (mon->race->base) {
		my_strcpy(card->subtitle,
			mon->race->base->text ? mon->race->base->text :
			mon->race->base->name, sizeof(card->subtitle));
	}
	look_mon_desc(health, sizeof(health), mon->midx);
	strnfmt(card->status, sizeof(card->status), "Health: %s", health);
	lore = get_lore(mon->race);
	if (lore) {
		int flag;
		int spell;
		int specific_drops = 0;

		monster_flags_known(mon->race, lore, known_flags);
		knowledge.armour_known = lore->armour_known;
		knowledge.armour = mon->race->ac;
		knowledge.average_life = mon->race->avg_hp;
		knowledge.all_known = lore->all_known;
		knowledge.relative_speed = mon->race->speed - 110;
		knowledge.never_moves = rf_has(known_flags, RF_NEVER_MOVE);
		knowledge.drop_known = lore->drop_known;
		if (knowledge.drop_known) {
			knowledge.maximum_drops = mon_create_drop_count(mon->race, true,
				false, &specific_drops);
			knowledge.specific_drops = specific_drops;
			if (rf_has(known_flags, RF_ONLY_ITEM)) {
				knowledge.drop_kind = SDL3_MONSTER_DROP_OBJECTS;
			} else if (rf_has(known_flags, RF_ONLY_GOLD)) {
				knowledge.drop_kind = SDL3_MONSTER_DROP_TREASURE;
			}
			if (rf_has(known_flags, RF_DROP_GREAT)) {
				knowledge.drop_quality = SDL3_MONSTER_DROP_EXCEPTIONAL;
			} else if (rf_has(known_flags, RF_DROP_GOOD)) {
				knowledge.drop_quality = SDL3_MONSTER_DROP_GOOD;
			}
		}
		knowledge.no_physical_attacks =
			rf_has(mon->race->flags, RF_NEVER_BLOW) &&
			rf_has(lore->flags, RF_NEVER_BLOW);
		if (mon->race->blow && lore->blow_known) {
			for (i = 0; i < z_info->mon_blows_max; i++) {
				if (mon->race->blow[i].method && lore->blow_known[i]) {
					if (knowledge.attacks.count <
							SDL3_MONSTER_CARD_LIST_CAPACITY) {
						format_attack(&mon->race->blow[i],
							attack_text[knowledge.attacks.count],
							sizeof(attack_text[0]));
						list_add(&knowledge.attacks,
							attack_text[knowledge.attacks.count]);
					} else {
						knowledge.attacks.total++;
					}
				}
			}
		}
		create_mon_spell_mask(breath_spells, RST_BREATH, RST_NONE);
		for (spell = rsf_next(lore->spell_flags, FLAG_START); spell;
				spell = rsf_next(lore->spell_flags, spell + 1)) {
			const char *description = mon_spell_lore_description(spell,
				mon->race);

			if (knowledge.powers.count < SDL3_MONSTER_CARD_LIST_CAPACITY &&
					rsf_has(breath_spells, spell)) {
				strnfmt(power_text[knowledge.powers.count],
					sizeof(power_text[0]), "breathe %s", description);
				description = power_text[knowledge.powers.count];
			}
			list_add(&knowledge.powers, description);
		}
		add_known_flag_type(&knowledge.weaknesses, known_flags, RFT_VULN);
		add_known_flag_type(&knowledge.weaknesses, known_flags, RFT_VULN_I);
		add_known_flag_type(&knowledge.resistances, known_flags, RFT_RES);
		add_known_flag_type(&knowledge.traits, known_flags, RFT_ALTER);
		add_known_flag_type(&knowledge.traits, known_flags, RFT_DET);
		{
			bitflag protections[RF_SIZE];

			create_mon_flag_mask(protections, RFT_PROT, RFT_MAX);
			rf_inter(protections, known_flags);
			for (flag = rf_next(protections, FLAG_START); flag;
					flag = rf_next(protections, flag + 1)) {
				if (knowledge.traits.count <
						SDL3_MONSTER_CARD_LIST_CAPACITY) {
					int index = knowledge.traits.count;

					strnfmt(trait_text[index], sizeof(trait_text[0]),
						"cannot be %s", describe_race_flag(flag));
					list_add(&knowledge.traits, trait_text[index]);
				} else {
					knowledge.traits.total++;
				}
			}
		}
		if (rf_has(known_flags, RF_UNAWARE)) {
			list_add(&knowledge.traits, "disguised");
		}
		if (rf_has(known_flags, RF_MULTIPLY)) {
			list_add(&knowledge.traits, "breeds explosively");
		}
		if (rf_has(known_flags, RF_REGENERATE)) {
			list_add(&knowledge.traits, "regenerates");
		}
		if (rf_has(known_flags, RF_SMART)) {
			list_add(&knowledge.traits, "intelligent spellcaster");
		}
		if (mon->race->light) {
			if (knowledge.traits.count < SDL3_MONSTER_CARD_LIST_CAPACITY) {
				int index = knowledge.traits.count;

				my_strcpy(trait_text[index], mon->race->light > 0 ?
					"illuminated" : "shrouded in darkness",
					sizeof(trait_text[0]));
				list_add(&knowledge.traits, trait_text[index]);
			} else {
				knowledge.traits.total++;
			}
		}
	}
	sdl3_monster_card_format_knowledge(&knowledge, &facts);
	my_strcpy(card->details[0], facts.defense, sizeof(card->details[0]));
	my_strcpy(card->details[1], facts.profile, sizeof(card->details[1]));
	my_strcpy(card->details[2], facts.attacks, sizeof(card->details[2]));
	my_strcpy(card->details[3], facts.powers, sizeof(card->details[3]));
	my_strcpy(card->details[4], facts.elements, sizeof(card->details[4]));
	my_strcpy(card->details[5], facts.traits, sizeof(card->details[5]));
	my_strcpy(card->flavor, mon->race->text ? mon->race->text : "",
		sizeof(card->flavor));
	my_strcpy(card->footer, "r: full recall", sizeof(card->footer));
}
