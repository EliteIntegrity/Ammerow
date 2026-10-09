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
#include "obj-randart.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "player-birth.h"
#include "player-calcs.h"
#include "player-util.h"
#include "relic-broker-data.h"
#include "savefile.h"
#include "store.h"
#include "ui-menu.h"
#include "ui-relic-broker.h"
#include "ui-screen.h"
#include "z-quark.h"

static bool reload_base(void)
{
	play_again = true;
	wipe_mon_list(cave, player);
	cleanup_angband();
	chunk_list_max = 0;
	init_angband();
	play_again = false;
	return savefile_load("TestBrokerBase", false);
}

static bool enter_broker(void)
{
	struct loc grid;
	for (grid.y = 0; grid.y < cave->height; grid.y++) {
		for (grid.x = 0; grid.x < cave->width; grid.x++) {
			if (square(cave, grid)->feat == FEAT_STORE_BROKER) {
				square_set_mon(cave, player->grid, 0);
				player_place(cave, player, grid);
				return true;
			}
		}
	}
	return false;
}
static int available_lot(void)
{
	int i;
	for (i = 0; i < player->broker.count; i++)
		if (relic_broker_lot_available(&player->broker.lots[i])) return i;
	return -1;
}

int setup_tests(void **state)
{
	(void)state;
	set_file_paths();
	init_angband();
	if (!player_make_simple(NULL, NULL, "Broker Tester")) return 1;
	prepare_next_level(player);
	on_new_level();
	return savefile_save("TestBrokerBase") ? 0 : 1;
}
int teardown_tests(void *state)
{
	(void)state;
	file_delete("TestBrokerBase");
	file_delete("TestBrokerBought");
	file_delete("TestBrokerInvalid");
	file_delete("TestBrokerLegacySource");
	file_delete("TestBrokerLegacy");
	wipe_mon_list(cave, player);
	cleanup_angband();
	return 0;
}

static int test_catalogue(void *unused)
{
	struct randomizer_state before, after;
	struct relic_broker_state catalogue;
	int i, b, quest = 0;
	(void)unused;
	require(reload_base());
	require(!player->broker.initialized);
	Rand_state_export(&before);
	require(relic_broker_ensure(player));
	Rand_state_export(&after);
	require(memcmp(&before, &after, sizeof(before)) == 0);
	catalogue = player->broker;
	eq(catalogue.count, 12);
	require(relic_broker_state_valid(&catalogue));
	for (b = 1; b <= relic_broker_band_count(); b++) {
		int available = 0;
		for (i = 0; i < catalogue.count; i++) {
			const struct relic_broker_lot *lot = &catalogue.lots[i];
			const struct relic_broker_band *band = relic_broker_band(lot->band);
			if (lot->artifact) {
				int32_t power = relic_broker_artifact_power(&a_info[lot->artifact]);
				require(power >= band->minimum && power < band->maximum);
				require(!is_artifact_created(&a_info[lot->artifact]));
				if (lot->band == b) available++;
			}
		}
		printf("Appraisal %s: %d concrete offers\n", relic_broker_band(b)->name, available);
		require(available > 0);
	}
	for (i = 1; i < z_info->a_max; i++) {
		struct object_kind *kind = lookup_kind(a_info[i].tval, a_info[i].sval);
		if (kind && kf_has(kind->kind_flags, KF_QUEST_ART)) {
			quest++;
			require(!relic_broker_artifact_eligible(&a_info[i]));
		}
	}
	require(quest > 0);
	memset(&player->broker, 0, sizeof(player->broker));
	require(relic_broker_ensure(player));
	require(memcmp(&catalogue, &player->broker, sizeof(catalogue)) == 0);
	/* Malformed state fails before writing; no OOB or quest purchases. */
	player->broker.lots[1].artifact = player->broker.lots[0].artifact;
	require(!relic_broker_state_valid(&player->broker));
	require(!savefile_save("TestBrokerInvalid"));
	player->broker = catalogue;
	ok;
}

static int test_purchase_and_save(void *unused)
{
	struct object *received = NULL;
	struct relic_broker_state catalogue;
	int lot, artifact, price;
	(void)unused;
	require(reload_base());
	require(relic_broker_ensure(player));
	lot = available_lot();
	require(lot >= 0);
	eq(relic_broker_buy(player, lot, &received), RELIC_BROKER_WRONG_STORE);
	require(enter_broker());
	price = player->broker.lots[lot].price;
	artifact = player->broker.lots[lot].artifact;
	player->au = price - 1;
	eq(relic_broker_buy(player, lot, &received), RELIC_BROKER_NO_GOLD);
	null(received);
	eq(player->au, price - 1);
	require(!player->broker.lots[lot].sold);
	mark_artifact_created(&a_info[artifact], true);
	player->au = price;
	eq(relic_broker_buy(player, lot, &received), RELIC_BROKER_UNAVAILABLE);
	eq(player->au, price);
	mark_artifact_created(&a_info[artifact], false);
	/* Use the production command queue for the successful purchase. */
	eq(cmdq_push(CMD_BUY_RELIC), 0);
	cmd_set_arg_number(cmdq_peek(), "lot", lot);
	require(cmdq_pop(CTX_STORE));
	for (received = player->gear; received; received = received->next)
		if (received->artifact == &a_info[artifact]) break;
	notnull(received);
	require(object_fully_known(received));
	require(received->artifact->text && received->artifact->text[0]);
	eq(player->au, 0);
	require(is_artifact_created(&a_info[artifact]));
	require(is_artifact_seen(&a_info[artifact]));
	require(player->broker.lots[lot].sold);
	catalogue = player->broker;
	require(savefile_save("TestBrokerBought"));
	require(reload_base());
	require(savefile_load("TestBrokerBought", false));
	require(memcmp(&catalogue, &player->broker, sizeof(catalogue)) == 0);
	player->au = price;
	eq(relic_broker_buy(player, lot, NULL), RELIC_BROKER_UNAVAILABLE);
	eq(player->au, price);
	require(relic_broker_ensure(player));
	require(memcmp(&catalogue, &player->broker, sizeof(catalogue)) == 0);
	ok;
}

static int test_full_pack(void *unused)
{
	int lot, price, serial = 0;
	(void)unused;
	require(reload_base());
	require(enter_broker());
	require(relic_broker_ensure(player));
	lot = available_lot();
	require(lot >= 0);
	price = player->broker.lots[lot].price;
	player->au = price;
	while (pack_slots_used(player) < z_info->pack_size) {
		struct object *obj = object_new();
		struct object_kind *kind = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Ration of Food"));
		char note[40];
		notnull(kind);
		object_prep(obj, kind, 0, AVERAGE);
		obj->known = object_new();
		object_set_base_known(player, obj);
		strnfmt(note, sizeof(note), "Separate parcel %d", serial++);
		obj->note = quark_add(note);
		inven_carry(player, obj, false, false);
	}
	eq(relic_broker_buy(player, lot, NULL), RELIC_BROKER_NO_ROOM);
	eq(player->au, price);
	require(!player->broker.lots[lot].sold);
	require(!is_artifact_created(&a_info[player->broker.lots[lot].artifact]));
	ok;
}

/* Remove just the optional block from a current valid file, preserving all
 * checksums of retained blocks. This models a pre-broker save, not a migration
 * helper shipped in the game. */
static bool without_broker_block(void)
{
	ang_file *in = file_open("TestBrokerLegacySource", MODE_READ, FTYPE_SAVE);
	ang_file *out = file_open("TestBrokerLegacy", MODE_WRITE, FTYPE_SAVE);
	char header[28];
	bool result = in && out;
	if (result) result = file_read(in, header, 8) == 8 && file_write(out, header, 8);
	while (result) {
		uint32_t size;
		char *payload;
		int got = file_read(in, header, sizeof(header));
		if (!got) break;
		if (got != sizeof(header)) { result = false; break; }
		size = (uint8_t)header[20] | ((uint32_t)(uint8_t)header[21] << 8) |
			((uint32_t)(uint8_t)header[22] << 16) | ((uint32_t)(uint8_t)header[23] << 24);
		if (size > 16 * 1024 * 1024) { result = false; break; }
		size = (size + 3) & ~3U;
		payload = mem_alloc(MAX(size, 1));
		result = file_read(in, payload, size) == (int)size;
		if (result && strncmp(header, "relic broker", 16)) {
			result = file_write(out, header, sizeof(header)) && file_write(out, payload, size);
		}
		mem_free(payload);
	}
	if (in) file_close(in);
	if (out) file_close(out);
	return result;
}

static int test_old_save_and_missing_shop(void *unused)
{
	struct loc grid;
	bool saved;
	(void)unused;
	require(reload_base());
	require(enter_broker());
	grid = player->grid;
	square_set_feat(cave, grid, FEAT_FLOOR);
	square_set_feat(player->cave, grid, FEAT_FLOOR);
	/* The new feature/store is append-only; old files had one fewer store. */
	eq(f_info[FEAT_STORE_BROKER].shopnum, z_info->store_max);
	z_info->store_max--;
	saved = savefile_save("TestBrokerLegacySource");
	z_info->store_max++;
	require(saved);
	require(without_broker_block());
	require(reload_base());
	require(savefile_load("TestBrokerLegacy", false));
	require(!player->broker.initialized);
	require(enter_broker());
	require(store_at(cave, player->grid)->owner != NULL);
	require(relic_broker_ensure(player));
	eq(player->broker.count, 12);
	ok;
}

static bool snapshot_safe;
static int last_cursor;
static bool saw_confirmation;
static void capture_screen(const struct ui_screen *screen)
{
	int i, a;
	if (!screen) return;
	if (streq(screen->title, "Confirm sealed purchase")) {
		saw_confirmation = true;
		return;
	}
	last_cursor = screen->cursor;
	snapshot_safe = screen->kind == UI_SCREEN_STORE && screen->row_count == 12;
	for (i = 0; i < screen->row_count; i++) {
		for (a = 1; a < z_info->a_max; a++) {
			if (a_info[a].name && a_info[a].name[0] &&
				strstr(screen->rows[i].label, a_info[a].name)) snapshot_safe = false;
		}
	}
}

static int test_knowledge_safe_screen(void *unused)
{
	term test_term;
	term *previous = Term;
	struct store *store;
	(void)unused;
	require(reload_base());
	require(enter_broker());
	require(relic_broker_ensure(player));
	store = store_at(cave, player->grid);
	term_init(&test_term, 120, 36, 64);
	Term_activate(&test_term);
	test_term.screen_hook = capture_screen;
	ui_relic_broker_present(store, 0, 0);
	Term_activate(previous);
	term_nuke(&test_term);
	require(snapshot_safe);
	ok;
}

static int test_menu_cancel_and_mouse_miss(void *unused)
{
	term test_term;
	term *previous = Term;
	term *previous_screen = term_screen;
	struct store *store;
	int lot, price;
	(void)unused;
	require(reload_base());
	require(enter_broker());
	require(relic_broker_ensure(player));
	lot = available_lot();
	eq(lot, 0);
	price = player->broker.lots[lot].price;
	player->au = price;
	store = store_at(cave, player->grid);
	term_init(&test_term, 120, 36, 64);
	term_screen = &test_term;
	Term_activate(&test_term);
	test_term.screen_hook = capture_screen;
	/* Empty-space click is ignored; normal down/up navigation then Escape. */
	Term_mousepress(119, 30, 1);
	Term_keypress(ARROW_DOWN, 0);
	Term_keypress(ARROW_UP, 0);
	Term_keypress(ESCAPE, 0);
	ui_relic_broker(store);
	eq(last_cursor, 0);
	/* Held Enter selects the lot then cancels the default-No confirmation. */
	Term_keypress('\r', 0);
	Term_keypress('\r', 0);
	Term_keypress(ESCAPE, 0);
	ui_relic_broker(store);
	Term_activate(previous);
	term_screen = previous_screen;
	term_nuke(&test_term);
	require(saw_confirmation);
	eq(player->au, price);
	require(!player->broker.lots[lot].sold);
	ok;
}

static int test_appraisal_sample(void *unused)
{
	int seed, b, i;
	int low[RELIC_BROKER_MAX_BANDS], high[RELIC_BROKER_MAX_BANDS] = { 0 };
	(void)unused;
	require(reload_base());
	for (b = 0; b < RELIC_BROKER_MAX_BANDS; b++) low[b] = INT_MAX;
	for (seed = 0; seed < 16; seed++) {
		int counts[RELIC_BROKER_MAX_BANDS] = { 0 };
		seed_randart = 20260913U + (uint32_t)seed;
		do_randart(seed_randart, false);
		for (i = 1; i < z_info->a_max; i++) {
			int32_t power = relic_broker_artifact_power(&a_info[i]);
			for (b = 1; b <= relic_broker_band_count(); b++) {
				const struct relic_broker_band *band = relic_broker_band(b);
				if (power >= band->minimum && power < band->maximum) counts[b-1]++;
			}
		}
		for (b = 0; b < relic_broker_band_count(); b++) {
			low[b] = MIN(low[b], counts[b]);
			high[b] = MAX(high[b], counts[b]);
		}
	}
	for (b = 0; b < relic_broker_band_count(); b++) {
		printf("16-seed %s candidate range: %d..%d\n", relic_broker_band(b+1)->name, low[b], high[b]);
		require(low[b] >= (int)relic_broker_band(b+1)->quantity);
	}
	ok;
}

const char *suite_name = "game/relic-broker";
struct test tests[] = {
	{ "catalogue and exclusions", test_catalogue },
	{ "purchase and save", test_purchase_and_save },
	{ "full pack is atomic", test_full_pack },
	{ "old save and missing shop", test_old_save_and_missing_shop },
	{ "knowledge-safe screen", test_knowledge_safe_screen },
	{ "menu cancellation and pointer miss", test_menu_cancel_and_mouse_miss },
	{ "appraisal sample", test_appraisal_sample },
	{ NULL, NULL }
};
