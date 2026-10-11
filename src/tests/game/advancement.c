/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "unit-test.h"
#include "test-utils.h"
#include "cave.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "mon-make.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-util.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "savefile.h"
#include "ui-advancement.h"
#include "ui-input.h"
#include "ui-screen.h"
#include "z-quark.h"

static void reset_game(void)
{
	play_again = true;
	wipe_mon_list(cave, player);
	cleanup_angband();
	chunk_list_max = 0;
	init_angband();
	play_again = false;
}

static bool baseline(void)
{
	reset_game();
	return savefile_load("TestAdvanceBase", false);
}

int setup_tests(void **state)
{
	(void)state;
	set_file_paths();
	init_angband();
	if (!player_make_simple(NULL, NULL, "Advancement Tester")) return 1;
	prepare_next_level(player);
	on_new_level();
	return savefile_save("TestAdvanceBase") ? 0 : 1;
}

int teardown_tests(void *state)
{
	(void)state;
	file_delete("TestAdvanceBase");
	file_delete("TestAdvanceSave");
	file_delete("TestAdvanceEdited");
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

static void reach(int level)
{
	int32_t target = player_exp[level - 2] * player->expfact / 100;
	player_exp_gain(player, MAX(0, target - player->exp));
}

static void dismiss_cards(void)
{
	for (int i = 2; i <= PY_MAX_LEVEL; i++) player_advancement_dismiss(player, i);
}

static struct object_kind *kind_for(int tval, const char *name)
{
	return lookup_kind(tval, lookup_sval(tval, name));
}

static int count_kind(const struct object_kind *kind)
{
	int count = 0;
	for (struct object *o = player->gear; o; o = o->next)
		if (o->kind == kind) count += o->number;
	return count;
}

static struct object *give(struct object_kind *kind, int number, const char *note)
{
	struct object *o = object_new();
	object_prep(o, kind, 0, MINIMISE);
	o->number = (uint8_t)number;
	o->known = object_new();
	object_set_base_known(player, o);
	if (note) o->note = quark_add(note);
	inven_carry(player, o, false, false);
	return o;
}

static void fill_pack(void)
{
	int serial = 0;
	struct object_kind *kind = kind_for(TV_FOOD, "Ration of Food");
	while (pack_slots_used(player) < z_info->pack_size) {
		char note[40];
		strnfmt(note, sizeof(note), "Separate parcel %d", serial++);
		give(kind, 1, note);
	}
}

static int test_birth_and_gain(void *unused)
{
	(void)unused;
	require(baseline());
	eq(player->advancement.highest, 1);
	require(player_advancement_valid(&player->advancement));
	eq(player_advancement_next_card(player), 0);
	eq(player_advancement_next_reward(player), 0);
	reach(2);
	eq(player->lev, 2);
	eq(player_advancement_next_card(player), 2);
	eq(player_advancement_next_reward(player), 0);
	player_advancement_dismiss(player, 2);
	eq(player_advancement_next_card(player), 0);
	player_exp_gain(player, 0);
	eq(player_advancement_next_card(player), 0);
	ok;
}

static int test_multilevel_and_defer(void *unused)
{
	int32_t gold;
	(void)unused;
	require(baseline());
	gold = player->au;
	reach(12);
	eq(player->lev, 12);
	eq(player->advancement.highest, 12);
	eq(player->advancement.rewards, 3);
	for (int i = 2; i <= 12; i++) {
		eq(player_advancement_next_card(player), i);
		player_advancement_dismiss(player, i);
	}
	eq(player_advancement_next_card(player), 0);
	eq(player_advancement_next_reward(player), 5);
	eq(player->au, gold);
	require(player_advancement_valid(&player->advancement));
	ok;
}

static int test_gold_once(void *unused)
{
	int32_t gold;
	(void)unused;
	require(baseline());
	reach(5);
	gold = player->au;
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_OK);
	eq(player->au, gold + 60);
	eq(player->advancement.cards[5], 0);
	for (int i = 0; i < 3; i++)
		eq(player_advancement_claim(player, 5, (enum advancement_choice)i), ADVANCEMENT_UNAVAILABLE);
	eq(player->au, gold + 60);
	eq(player_advancement_next_reward(player), 0);
	ok;
}

static int test_potions_once(void *unused)
{
	struct object_kind *kind;
	int before;
	(void)unused;
	require(baseline());
	reach(10);
	kind = kind_for(TV_POTION, "Cure Serious Wounds");
	notnull(kind);
	before = count_kind(kind);
	eq(player_advancement_claim(player, 10, ADVANCEMENT_POTION), ADVANCEMENT_OK);
	eq(count_kind(kind), before + 2);
	for (struct object *o = player->gear; o; o = o->next) {
		if (o->kind != kind) continue;
		require(object_fully_known(o));
		eq(o->origin, ORIGIN_ADVANCEMENT);
	}
	eq(player_advancement_claim(player, 10, ADVANCEMENT_POTION), ADVANCEMENT_UNAVAILABLE);
	eq(count_kind(kind), before + 2);
	eq(player_advancement_next_reward(player), 5);
	ok;
}

static int test_scrolls_once(void *unused)
{
	struct object_kind *kind;
	int before;
	(void)unused;
	require(baseline());
	reach(5);
	kind = kind_for(TV_SCROLL, "Seam Step");
	notnull(kind);
	before = count_kind(kind);
	eq(player_advancement_claim(player, 5, ADVANCEMENT_SCROLL), ADVANCEMENT_OK);
	eq(count_kind(kind), before + 3);
	eq(player_advancement_claim(player, 5, ADVANCEMENT_SCROLL), ADVANCEMENT_UNAVAILABLE);
	eq(count_kind(kind), before + 3);
	ok;
}

static int test_full_pack_atomic(void *unused)
{
	struct object_kind *kind;
	bool aware;
	int before;
	(void)unused;
	require(baseline());
	reach(5);
	fill_pack();
	kind = kind_for(TV_POTION, "Cure Serious Wounds");
	aware = kind->aware;
	before = count_kind(kind);
	eq(player_advancement_claim(player, 5, ADVANCEMENT_POTION), ADVANCEMENT_NO_ROOM);
	eq(count_kind(kind), before);
	eq(kind->aware, aware);
	eq(player_advancement_next_reward(player), 5);
	eq(player->advancement.cards[5], 1);
	eq(pack_slots_used(player), z_info->pack_size);
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_OK);
	ok;
}

static int test_partial_stack_atomic(void *unused)
{
	struct object_kind *kind;
	struct object *o;
	int before;
	(void)unused;
	require(baseline());
	reach(5);
	kind = kind_for(TV_POTION, "Cure Serious Wounds");
	o = give(kind, kind->base->max_stack - 1, NULL);
	fill_pack();
	/* The level-10 bundle contains two, but only one fits in this stack. */
	reach(10);
	before = count_kind(kind);
	eq(player_advancement_claim(player, 10, ADVANCEMENT_POTION), ADVANCEMENT_NO_ROOM);
	eq(count_kind(kind), before);
	require(player->advancement.rewards & 2);
	/* One potion does fit, even though no free slot remains. */
	eq(player_advancement_claim(player, 5, ADVANCEMENT_POTION), ADVANCEMENT_OK);
	eq(o->number, kind->base->max_stack);
	ok;
}

static int test_drain_regain(void *unused)
{
	int32_t gold, xp;
	(void)unused;
	require(baseline());
	reach(10);
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_OK);
	eq(player_advancement_claim(player, 10, ADVANCEMENT_GOLD), ADVANCEMENT_OK);
	dismiss_cards();
	gold = player->au;
	xp = player->exp;
	for (int permanent = 0; permanent < 2; permanent++) {
		player_exp_lose(player, xp, permanent != 0);
		eq(player->lev, 1);
		eq(player->advancement.highest, 10);
		player_exp_gain(player, xp);
		eq(player->lev, 10);
		eq(player_advancement_next_card(player), 0);
		eq(player_advancement_next_reward(player), 0);
		eq(player_advancement_claim(player, 10, ADVANCEMENT_GOLD), ADVANCEMENT_UNAVAILABLE);
		eq(player->au, gold);
	}
	reach(11);
	eq(player_advancement_next_card(player), 11);
	ok;
}

static int test_several_partial_stacks(void *unused)
{
	struct object_kind *kind;
	int before;
	(void)unused;
	require(baseline());
	reach(10);
	kind = kind_for(TV_POTION, "Cure Serious Wounds");
	give(kind, kind->base->max_stack - 1, "first parcel");
	give(kind, kind->base->max_stack - 1, "second parcel");
	fill_pack();
	before = count_kind(kind);
	eq(player_advancement_claim(player, 10, ADVANCEMENT_POTION), ADVANCEMENT_OK);
	eq(count_kind(kind), before + 2);
	eq(pack_slots_used(player), z_info->pack_size);
	require(!pack_is_overfull());
	eq(player_advancement_claim(player, 10, ADVANCEMENT_POTION), ADVANCEMENT_UNAVAILABLE);
	ok;
}

static int test_save_pending_and_claimed(void *unused)
{
	struct player_advancement before;
	int32_t gold;
	(void)unused;
	require(baseline());
	reach(10);
	dismiss_cards();
	player->advancement.cards[10] = 1;
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_OK);
	before = player->advancement;
	gold = player->au;
	require(savefile_save("TestAdvanceSave"));
	require(baseline());
	require(savefile_load("TestAdvanceSave", false));
	require(memcmp(&before, &player->advancement, sizeof(before)) == 0);
	eq(player->au, gold);
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_UNAVAILABLE);
	eq(player_advancement_claim(player, 10, ADVANCEMENT_GOLD), ADVANCEMENT_OK);
	dismiss_cards();
	require(savefile_save("TestAdvanceSave"));
	require(baseline());
	require(savefile_load("TestAdvanceSave", false));
	eq(player->au, gold + 120);
	eq(player_advancement_claim(player, 10, ADVANCEMENT_SCROLL), ADVANCEMENT_UNAVAILABLE);
	eq(player_advancement_next_card(player), 0);
	eq(player_advancement_next_reward(player), 0);
	ok;
}

/* Test-only save editor: remove, duplicate, or corrupt ONLY the advancement
 * block. An omitted block is the exact format used by pre-feature saves. */
static bool edit_block(int mode)
{
	ang_file *in = file_open("TestAdvanceSave", MODE_READ, FTYPE_SAVE);
	ang_file *out;
	char header[28];
	bool result, found = false;
	file_delete("TestAdvanceEdited");
	out = file_open("TestAdvanceEdited", MODE_WRITE, FTYPE_SAVE);
	result = in && out;
	if (result) result = file_read(in, header, 8) == 8 && file_write(out, header, 8);
	while (result) {
		uint32_t size;
		char *payload;
		bool target;
		int got = file_read(in, header, sizeof(header));
		if (!got) break;
		if (got != sizeof(header)) { result = false; break; }
		size = (uint8_t)header[20] | ((uint32_t)(uint8_t)header[21] << 8) |
			((uint32_t)(uint8_t)header[22] << 16) | ((uint32_t)(uint8_t)header[23] << 24);
		if (size > 16 * 1024 * 1024) { result = false; break; }
		size = (size + 3) & ~3U;
		payload = mem_alloc(MAX(size, 1));
		result = file_read(in, payload, size) == (int)size;
		target = !strncmp(header, "advancement", 16);
		if (target) found = true;
		if (result && (!target || mode != 0)) {
			if (target && mode == 2 && size) payload[0] = 0; /* Invalid highest. */
			result = file_write(out, header, sizeof(header)) && file_write(out, payload, size);
			if (result && target && mode == 1)
				result = file_write(out, header, sizeof(header)) && file_write(out, payload, size);
		}
		mem_free(payload);
	}
	if (in) file_close(in);
	if (out) file_close(out);
	return result && found;
}

static int test_legacy_no_retrospective_rewards(void *unused)
{
	(void)unused;
	require(baseline());
	reach(17);
	player_exp_lose(player, player->exp - player_exp[10] * player->expfact / 100, false);
	eq(player->lev, 12);
	eq(player->max_lev, 17);
	require(savefile_save("TestAdvanceSave"));
	require(edit_block(0));
	require(baseline());
	require(savefile_load("TestAdvanceEdited", false));
	eq(player->advancement.highest, 17);
	eq(player_advancement_next_card(player), 0);
	eq(player_advancement_next_reward(player), 0);
	reach(17);
	eq(player_advancement_next_card(player), 0);
	reach(20);
	eq(player_advancement_next_card(player), 18);
	eq(player_advancement_next_reward(player), 20);
	eq(player->advancement.rewards, 8);
	ok;
}

static int test_invalid_and_duplicate_save_blocks(void *unused)
{
	(void)unused;
	require(baseline());
	reach(5);
	require(savefile_save("TestAdvanceSave"));
	for (int mode = 1; mode <= 2; mode++) {
		require(edit_block(mode));
		require(baseline());
		require(!savefile_load("TestAdvanceEdited", false));
	}
	require(baseline());
	player->advancement.rewards = 1; /* Not earned at level 1. */
	require(!savefile_save("TestAdvanceSave"));
	require(baseline());
	require(savefile_load("TestAdvanceSave", false));
	eq(player->advancement.highest, 5); /* Failed save didn't overwrite it. */
	ok;
}

static int test_invalid_claims(void *unused)
{
	(void)unused;
	require(baseline());
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_UNAVAILABLE);
	reach(5);
	eq(player_advancement_claim(player, 4, ADVANCEMENT_GOLD), ADVANCEMENT_UNAVAILABLE);
	eq(player_advancement_claim(player, 55, ADVANCEMENT_GOLD), ADVANCEMENT_UNAVAILABLE);
	eq(player_advancement_claim(player, 5, (enum advancement_choice)3), ADVANCEMENT_UNAVAILABLE);
	player->is_dead = true;
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_UNAVAILABLE);
	player->is_dead = false;
	player->au = INT32_MAX;
	eq(player_advancement_claim(player, 5, ADVANCEMENT_GOLD), ADVANCEMENT_NO_ROOM);
	eq(player_advancement_next_reward(player), 5);
	player->advancement.cards[6] = 1;
	require(!player_advancement_valid(&player->advancement));
	player->advancement.cards[6] = 0;
	player->advancement.cards[2] = 2;
	require(!player_advancement_valid(&player->advancement));
	ok;
}

static int test_cap_and_catalogue(void *unused)
{
	int32_t gold;
	(void)unused;
	require(baseline());
	reach(50);
	eq(player->lev, 50);
	eq(player->advancement.highest, 50);
	eq(player->advancement.rewards, 1023);
	gold = player->au;
	for (int i = 5; i <= 50; i += 5) {
		const struct advancement_offer *o = player_advancement_offer(i);
		notnull(o);
		notnull(kind_for(TV_POTION, o->potion));
		notnull(kind_for(TV_SCROLL, o->scroll));
		require(o->potions > 0 && o->potions <= 3);
		require(o->scrolls > 0 && o->scrolls <= 4);
		eq(player_advancement_next_reward(player), i);
		eq(player_advancement_claim(player, i, ADVANCEMENT_GOLD), ADVANCEMENT_OK);
	}
	eq(player->au, gold + 6280);
	dismiss_cards();
	player_exp_gain(player, 100);
	eq(player_advancement_next_card(player), 0);
	eq(player_advancement_next_reward(player), 0);
	ok;
}

static bool card_valid;
static int screen_count, sound_count, input_mode;
static void count_sound(game_event_type type, game_event_data *data, void *user)
{
	(void)type; (void)user;
	if (data->message.type == MSG_LEVEL) sound_count++;
}

static void capture_card(const struct ui_screen *screen)
{
	if (!screen) return;
	screen_count++;
	card_valid = screen->kind == UI_SCREEN_ADVANCEMENT && screen->modal &&
		screen->row_count == 4 && screen->cursor == 3 &&
		strstr(screen->rows[0].label, "Cure Serious Wounds") &&
		strstr(screen->context, "Information > Level rewards");
	if (input_mode == 1) Term_keypress(KC_ENTER, 0); /* Safe default. */
	else if (input_mode == 2) Term_keypress(ESCAPE, 0);
}

static int test_card_and_safe_default(void *unused)
{
	term test_term;
	term *previous = Term, *previous_screen = term_screen;
	int32_t gold;
	(void)unused;
	require(baseline());
	reach(5);
	dismiss_cards();
	player->advancement.cards[5] = 1;
	gold = player->au;
	OPT(player, use_sound) = true;
	term_init(&test_term, 80, 24, 64);
	Term_activate(&test_term);
	term_screen = &test_term;
	test_term.screen_hook = capture_card;
	event_add_handler(EVENT_SOUND, count_sound, NULL);
	sound_count = screen_count = 0;
	input_mode = 1;
	ui_advancement_event(EVENT_ADVANCEMENT, NULL, NULL);
	input_mode = 2;
	do_cmd_level_rewards();
	event_remove_handler(EVENT_SOUND, count_sound, NULL);
	Term_activate(previous);
	term_screen = previous_screen;
	term_nuke(&test_term);
	require(card_valid);
	eq(screen_count, 2);
	eq(sound_count, 1); /* Reopening a deferred choice doesn't repeat the cue. */
	eq(player->au, gold);
	eq(player_advancement_next_card(player), 0);
	eq(player_advancement_next_reward(player), 5);
	ok;
}

const char *suite_name = "game/advancement";
struct test tests[] = {
	{ "birth and first level", test_birth_and_gain },
	{ "multiple levels and deferral", test_multilevel_and_defer },
	{ "gold exactly once", test_gold_once },
	{ "potions exactly once", test_potions_once },
	{ "scrolls exactly once", test_scrolls_once },
	{ "full backpack is atomic", test_full_pack_atomic },
	{ "partial stack is atomic", test_partial_stack_atomic },
	{ "reward fits several partial stacks", test_several_partial_stacks },
	{ "drain and regain cannot farm rewards", test_drain_regain },
	{ "save pending and claimed rewards", test_save_pending_and_claimed },
	{ "legacy saves have no retrospective rewards", test_legacy_no_retrospective_rewards },
	{ "invalid and duplicate save blocks", test_invalid_and_duplicate_save_blocks },
	{ "invalid claims and state", test_invalid_claims },
	{ "cap and curated catalogue", test_cap_and_catalogue },
	{ "card sound and safe default", test_card_and_safe_default },
	{ NULL, NULL }
};
