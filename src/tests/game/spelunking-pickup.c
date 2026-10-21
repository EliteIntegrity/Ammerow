/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "spelunking-integration-fixture.h"
#include "cmds.h"
#include "obj-ignore.h"
#include "world-spelunking-local.h"
#include "obj-knowledge.h"
#include "ui-object.h"
#include "ui-game.h"
#include "ui-context.h"
#include "ui-screen.h"
#include "ui-term.h"
#include "ui-spelunking-list.h"
#include "game-input.h"

static bool cavern(void)
{
	enum world_spelunk_tile cells[80];
	int x, y;
	reset_before_load();
	if (!savefile_load("Test1", false)) return false;
	cmdq_flush();
	for (y = 0; y < 8; y++) for (x = 0; x < 10; x++)
		cells[y * 10 + x] = !x || x == 9 || !y || y >= 6 ?
			WORLD_SPELUNK_ROCK : WORLD_SPELUNK_AIR;
	if (!install_spelunking_runtime(cells, 10, 8, 2, 5, 90)) return false;
	((struct level *)world_player_level(player))->mode = WORLD_MODE_SPELUNKING;
	my_strcpy(player->world_location.entry, "stair.main", sizeof(player->world_location.entry));
	player->spelunking->actor_roster_initialized = true;
	player->upkeep->energy_use = 0;
	player->upkeep->notice = 0;
	OPT(player, pickup_always) = false;
	OPT(player, pickup_inven) = false;
	player->unignoring = false;
	world_spelunk_visibility_follow_player(player->spelunking);
	return world_spelunk_player_is_active(player);
}

static struct object *ground(int tval, const char *name, int count, int x)
{
	struct object *obj = make_test_object(lookup_kind(tval, lookup_sval(tval, name)), count);
	if (!obj || !world_spelunk_runtime_add_ground_object(player->spelunking, obj, x, 5))
		return NULL;
	return obj;
}

static bool pickup(void)
{
	struct chunk *native = cave;
	bool result;
	player->upkeep->energy_use = 0;
	if (cmdq_push(CMD_AUTOPICKUP)) return false;
	cave = NULL;
	result = cmdq_pop(CTX_GAME);
	cave = native;
	return result;
}

static int test_policy(void *unused)
{
	struct object *obj;
	struct object_kind *kind;
	int before;
	(void)unused;
	require(cavern()); obj = ground(TV_FOOD, "Ration of Food", 4, 2); notnull(obj);
	kind = obj->kind; before = carried_kind_count(player, kind);
	require(pickup()); eq(obj->number, 4); eq(player->upkeep->energy_use, 0);
	obj->note = quark_add("!g =g");
	require(pickup()); eq(obj->number, 4);
	obj->note = quark_add("=g");
	require(pickup()); eq(carried_kind_count(player, kind), before + 4);
	eq(player->upkeep->energy_use, 100);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 0);
	require(pickup()); eq(player->upkeep->energy_use, 0);
	obj = ground(TV_FOOD, "Ration of Food", 1, 2); notnull(obj);
	obj->note = quark_add("!g"); OPT(player, pickup_always) = true;
	require(pickup()); eq(carried_kind_count(player, kind), before + 5);
	ok;
}

static int test_stack_cap_and_neighbour(void *unused)
{
	struct object *obj, *far, *carried;
	struct object_kind *kind;
	int before;
	char note[24];
	(void)unused;
	require(cavern()); obj = ground(TV_FOOD, "Ration of Food", 9, 2); notnull(obj);
	kind = obj->kind; before = carried_kind_count(player, kind);
	carried = carried_kind_object(player, kind); notnull(carried);
	strnfmt(note, sizeof(note), "=g%d", before + 2);
	carried->note = quark_add(note); OPT(player, pickup_inven) = true;
	far = ground(TV_FOOD, "Ration of Food", 5, 3); notnull(far);
	require(pickup()); eq(obj->number, 7); eq(obj->known->number, 7);
	eq(carried_kind_count(player, kind), before + 2); eq(far->number, 5);
	require(pickup()); eq(obj->number, 7); eq(player->upkeep->energy_use, 0);
	carried->note = quark_add("!g"); require(pickup()); eq(obj->number, 7);
	require(world_spelunk_runtime_local_is_valid(player->spelunking));
	ok;
}

static int test_gold_and_mixed_batch(void *unused)
{
	struct object *obj;
	int before;
	(void)unused;
	require(cavern());
	obj = make_gold(5, "any"); notnull(obj);
	obj->known = object_new(); object_copy(obj->known, obj); obj->known->known = NULL;
	before = player->au; obj->pval = 37;
	require(world_spelunk_runtime_add_ground_object(player->spelunking, obj, 2, 5));
	require(pickup()); eq(player->au, before + 37); eq(player->upkeep->energy_use, 0);
	require(pickup()); eq(player->au, before + 37);
	notnull(ground(TV_FOOD, "Ration of Food", 1, 2));
	notnull(ground(TV_POTION, "Cure Light Wounds", 2, 2));
	OPT(player, pickup_always) = true;
	require(pickup()); eq(player->upkeep->energy_use, 100);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 0);
	ok;
}

static int test_ignore_view_and_recover(void *unused)
{
	struct object *hidden, *wanted, *items[8];
	struct world_spelunk_inspect inspect;
	struct world_spelunk_local_report report;
	struct chunk *native;
	int n;
	(void)unused;
	require(cavern()); hidden = ground(TV_FOOD, "Ration of Food", 2, 3); notnull(hidden);
	wanted = ground(TV_POTION, "Cure Light Wounds", 1, 3); notnull(wanted);
	hidden->known->notice |= OBJ_NOTICE_IGNORE;
	require(world_spelunk_view_inspect(player->spelunking, 3, 5, &inspect));
	require(inspect.object == wanted);
	wanted->known->notice |= OBJ_NOTICE_IGNORE;
	require(world_spelunk_view_inspect(player->spelunking, 3, 5, &inspect)); null(inspect.object);
	player->spelunking->state.x = 3;
	native = cave; cave = NULL;
	n = scan_floor(items, 8, player, OFLOOR_VISIBLE, NULL);
	cave = native; eq(n, 0);
	eq(world_spelunk_player_apply_local(player, WORLD_SPELUNK_LOCAL_PICKUP, &report),
		WORLD_SPELUNK_LOCAL_NOTHING_HERE);
	textui_cmd_toggle_ignore(); require(player->unignoring);
	player->spelunking->state.x = 2;
	require(world_spelunk_view_inspect(player->spelunking, 3, 5, &inspect));
	require(inspect.object == hidden);
	player->spelunking->state.x = 3;
	eq(scan_floor(items, 8, player, OFLOOR_VISIBLE, NULL), 2);
	eq(world_spelunk_player_apply_local(player, WORLD_SPELUNK_LOCAL_PICKUP, &report),
		WORLD_SPELUNK_LOCAL_TURN);
	eq(report.affected, 2); eq(world_spelunk_runtime_ground_object_count(player->spelunking), 0);
	ok;
}

static int test_kind_ignore_knowledge(void *unused)
{
	struct object *obj;
	(void)unused;
	require(cavern()); obj = ground(TV_POTION, "Cure Light Wounds", 2, 2); notnull(obj);
	obj->kind->aware = false;
	object_ignore_flavor_of(obj); require(kind_is_ignored_unaware(obj->kind));
	OPT(player, pickup_always) = true;
	require(pickup()); eq(obj->number, 2); require(!obj->kind->aware);
	obj->kind->aware = true; require(!ignore_item_ok(player, obj));
	object_ignore_flavor_of(obj); require(kind_is_ignored_aware(obj->kind));
	require(pickup()); eq(obj->number, 2);
	/* Existing !k protection still defeats type/quality ignoring. */
	obj->note = quark_add("!k"); require(!ignore_item_ok(player, obj));
	require(pickup()); eq(world_spelunk_runtime_ground_object_count(player->spelunking), 0);
	ok;
}

static int test_arrival_and_stale_queue(void *unused)
{
	struct object *obj;
	struct chunk *native;
	(void)unused;
	require(cavern()); obj = ground(TV_FOOD, "Ration of Food", 2, 3); notnull(obj);
	OPT(player, pickup_always) = true;
	native = cave; cave = NULL;
	cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, 1, 0);
	cmdq_pop(CTX_GAME);
	cave = native; eq(player->spelunking->state.x, 3);
	eq(obj->number, 2); /* Movement and handling are distinct actions. */
	player->upkeep->energy_use = 0; require(cmdq_pop(CTX_GAME));
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 0);
	eq(player->upkeep->energy_use, 100); require(!cmdq_pop(CTX_GAME));
	notnull(ground(TV_FOOD, "Ration of Food", 2, 4));
	player->upkeep->energy_use = 0;
	cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, 1, 0); require(cmdq_pop(CTX_GAME));
	player->spelunking->state.x = 3; player->upkeep->energy_use = 0;
	require(cmdq_pop(CTX_GAME)); eq(player->upkeep->energy_use, 0);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	/* A different section can share the same world location and coordinates. */
	cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, 1, 0); require(cmdq_pop(CTX_GAME));
	require(player_spelunking_ensure_system(player));
	player->upkeep->energy_use = 0;
	require(cmdq_pop(CTX_GAME)); eq(player->upkeep->energy_use, 0);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	/* Nor may a queued cave arrival become a native-grid pickup after recall. */
	player->spelunking->state.x = 3;
	cmdq_push_spelunk_action(WORLD_SPELUNK_COMMAND_MOVE, 1, 0); require(cmdq_pop(CTX_GAME));
	((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
	player->upkeep->energy_use = 0;
	require(cmdq_pop(CTX_GAME)); eq(player->upkeep->energy_use, 0);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 1);
	ok;
}

static int test_ignore_drop(void *unused)
{
	struct object *obj;
	struct chunk *native;
	(void)unused;
	require(cavern());
	obj = carried_kind_object(player, lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Ration of Food")));
	notnull(obj); obj->known->notice |= OBJ_NOTICE_IGNORE;
	native = cave; cave = NULL;
	player->upkeep->notice |= PN_IGNORE;
	process_player();
	cave = native;
	require(!object_is_carried(player, obj));
	require(world_spelunk_runtime_ground_object_at(player->spelunking, 2, 5) == obj);
	eq(player->upkeep->energy_use, 100);
	OPT(player, pickup_always) = true; require(pickup());
	require(!object_is_carried(player, obj)); eq(player->upkeep->energy_use, 0);
	ignore_drop(player); require(!cmdq_pop(CTX_GAME));
	ok;
}

static int test_protected_drop_and_capacity(void *unused)
{
	struct object *obj;
	int i;
	(void)unused;
	require(cavern());
	obj = carried_kind_object(player, lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Ration of Food")));
	notnull(obj); obj->known->notice |= OBJ_NOTICE_IGNORE;
	obj->note = quark_add("!d"); ignore_drop(player); require(!cmdq_pop(CTX_GAME));
	obj->note = quark_add("!*"); ignore_drop(player); require(!cmdq_pop(CTX_GAME));
	obj->note = 0;
	player->spelunking->cells[5 * 10 + 2] = WORLD_SPELUNK_WATER;
	ignore_drop(player); require(!cmdq_pop(CTX_GAME)); require(object_is_carried(player, obj));
	player->spelunking->state.movement = WORLD_SPELUNK_SWIMMING;
	require(world_spelunk_player_is_active(player));
	ignore_drop(player); require(!cmdq_pop(CTX_GAME)); require(object_is_carried(player, obj));
	player->spelunking->cells[5 * 10 + 2] = WORLD_SPELUNK_AIR;
	player->spelunking->state.movement = WORLD_SPELUNK_STANDING;
	for (i = 0; i < WORLD_SPELUNK_OBJECT_MAX; i++)
		notnull(ground(TV_FOOD, "Ration of Food", 1, 3));
	ignore_drop(player); require(!cmdq_pop(CTX_GAME)); require(object_is_carried(player, obj));
	ok;
}

static int test_save_roundtrip(void *unused)
{
	struct object *obj, *hidden;
	const struct level *level;
	const struct world_entry *entry;
	struct world_spelunking_entry_report report;
	int x, y;
	(void)unused;
	reset_before_load(); require(savefile_load("Test1", false)); cmdq_flush();
	level = level_by_id("core.cave.east.001");
	entry = world_entry_by_id(level, "shaft.spelunk"); notnull(entry);
	cave->depth = player->cave->depth = level->danger;
	require(world_chunk_set_location(cave, level->id, false));
	require(world_chunk_set_location(player->cave, level->id, true));
	require(world_player_set_location(player, level->id, entry->id));
	square_set_mon(cave, player->grid, 0);
	square_set_feat(cave, entry->grid, FEAT_HOLE);
	square_set_feat(player->cave, entry->grid, FEAT_FLOOR);
	player_place(cave, player, entry->grid);
	eq(world_spelunking_entry_execute(player, cave, "core.route.spelunk.east.in", &report), WORLD_SPELUNKING_ENTRY_OK);
	x = player->spelunking->state.x; y = player->spelunking->state.y;
	OPT(player, pickup_always) = false; OPT(player, pickup_inven) = false;
	obj = make_test_object(lookup_kind(TV_POTION, lookup_sval(TV_POTION, "Cure Light Wounds")), 6);
	notnull(obj);
	obj->note = quark_add("=g2");
	require(world_spelunk_runtime_add_ground_object(player->spelunking, obj, x, y));
	hidden = make_test_object(lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Ration of Food")), 3); notnull(hidden);
	hidden->known->notice |= OBJ_NOTICE_IGNORE;
	require(world_spelunk_runtime_add_ground_object(player->spelunking, hidden, x, y));
	require(pickup()); eq(obj->number, 4);
	player->upkeep->energy_use = 0;
	require(savefile_save("TestSpelunk")); reset_before_load();
	require(savefile_load("TestSpelunk", false)); require(world_spelunk_player_is_active(player));
	hidden = player->spelunking->ground_objects;
	while (hidden && !(hidden->known->notice & OBJ_NOTICE_IGNORE)) hidden = hidden->next;
	notnull(hidden);
	require(ignore_item_ok(player, hidden)); eq(hidden->number, 3);
	obj = player->spelunking->ground_objects;
	while (obj && (!tval_is_potion(obj) || obj->grid.x != x || obj->grid.y != y)) obj = obj->next;
	notnull(obj); eq(obj->number, 4); require(pickup()); eq(obj->number, 4);
	eq(carried_kind_count(player, lookup_kind(TV_POTION, lookup_sval(TV_POTION, "Cure Light Wounds"))), 2);
	require(savefile_save("TestSpelunk"));
	ok;
}

static int test_ignore_menu_cancel_and_confirm(void *unused)
{
	term terminal, *previous = Term, *previous_screen = term_screen;
	struct object *obj;
	struct chunk *native;
	(void)unused;
	require(cavern()); obj = ground(TV_FOOD, "Ration of Food", 1, 2); notnull(obj);
	term_init(&terminal, 80, 24, 64); Term_activate(&terminal); term_screen = &terminal;
	native = cave; cave = NULL;
	Term_keypress(ESCAPE, 0); textui_cmd_ignore_menu(obj);
	cave = native; require(!(obj->known->notice & OBJ_NOTICE_IGNORE));
	eq(player->upkeep->notice & PN_IGNORE, 0);
	cave = NULL;
	Term_keypress(KC_ENTER, 0); textui_cmd_ignore_menu(obj);
	cave = native; require(obj->known->notice & OBJ_NOTICE_IGNORE);
	cave = NULL;
	Term_keypress(KC_ENTER, 0); textui_cmd_ignore_menu(obj);
	cave = native;
	Term_activate(previous); term_screen = previous_screen; term_nuke(&terminal);
	require(!(obj->known->notice & OBJ_NOTICE_IGNORE)); eq(player->upkeep->energy_use, 0);
	ok;
}

static int test_selected_pickup_and_dead(void *unused)
{
	struct object *one, *two, *far;
	struct command cmd = { .code = CMD_PICKUP };
	(void)unused;
	require(cavern()); one = ground(TV_FOOD, "Ration of Food", 1, 2); notnull(one);
	two = ground(TV_FOOD, "Ration of Food", 2, 2); notnull(two);
	far = ground(TV_FOOD, "Ration of Food", 3, 3); notnull(far);
	cmd_set_arg_item(&cmd, "item", far); do_cmd_pickup(&cmd);
	eq(player->upkeep->energy_use, 0); eq(far->number, 3);
	cmd_set_arg_item(&cmd, "item", two); do_cmd_pickup(&cmd);
	eq(world_spelunk_runtime_ground_object_count(player->spelunking), 2);
	eq(one->number, 1); eq(far->number, 3);
	player->is_dead = true; OPT(player, pickup_always) = true; require(pickup());
	eq(player->upkeep->energy_use, 0); eq(one->number, 1);
	player->is_dead = false;
	ok;
}

static int test_full_pack_partial_stack(void *unused)
{
	struct object *obj, *carried;
	struct object_kind *ration;
	int i = 0, capacity;
	(void)unused;
	require(cavern());
	ration = lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Ration of Food"));
	carried = carried_kind_object(player, ration); notnull(carried);
	capacity = ration->base->max_stack;
	player->upkeep->total_weight += (capacity - 2 - carried->number) * object_weight_one(carried);
	carried->number = carried->known->number = (uint8_t)(capacity - 2);
	while (pack_slots_used(player) < z_info->pack_size) {
		char note[32];
		obj = make_test_object(lookup_kind(TV_SWORD, lookup_sval(TV_SWORD, "Dagger")), 1);
		notnull(obj); strnfmt(note, sizeof(note), "test-pack-%d", i++);
		obj->note = quark_add(note); inven_carry(player, obj, false, false);
	}
	obj = ground(TV_FOOD, "Ration of Food", 8, 2); notnull(obj);
	OPT(player, pickup_always) = true;
	require(pickup()); eq(obj->number, 6); eq(obj->known->number, 6);
	eq(carried_kind_count(player, ration), capacity);
	require(pickup()); eq(obj->number, 6); eq(player->upkeep->energy_use, 0);
	eq(pack_slots_used(player), z_info->pack_size);
	require(world_spelunk_runtime_local_is_valid(player->spelunking));
	ok;
}

static int test_equipped_ignore_confirmation(void *unused)
{
	struct object *obj;
	bool (*previous_check)(const char *) = get_check_hook;
	(void)unused;
	require(cavern()); obj = equipped_item_by_slot_name(player, "weapon"); notnull(obj);
	obj->known->notice |= OBJ_NOTICE_IGNORE;
	of_on(obj->flags, OF_STICKY);
	ignore_drop(player); require(!cmdq_pop(CTX_GAME)); require(object_is_carried(player, obj));
	of_off(obj->flags, OF_STICKY);
	/* No confirmation hook means decline; !d prevents repeated prompts. */
	get_check_hook = NULL; ignore_drop(player); get_check_hook = previous_check;
	require(check_for_inscrip(obj, "!d")); require(!cmdq_pop(CTX_GAME));
	ignore_drop(player); require(!cmdq_pop(CTX_GAME));
	require(object_is_equipped(player->body, obj));
	ok;
}

static errr unexpected_input(int action, int value)
{
	(void)value;
	if (action == TERM_XTRA_EVENT) quit("Unexpected cave ignore menu input request");
	return 0;
}

static int test_real_floor_ignore_and_context(void *unused)
{
	term terminal, *previous = Term, *previous_screen = term_screen;
	bool (*previous_item)(struct object **, const char *, const char *, cmd_code, item_tester, int) = get_item_hook;
	struct object *obj;
	struct chunk *native;
	(void)unused;
	require(cavern()); obj = ground(TV_FOOD, "Ration of Food", 1, 2); notnull(obj);
	term_init(&terminal, 80, 24, 64); Term_activate(&terminal); term_screen = &terminal;
	terminal.xtra_hook = unexpected_input;
	cmd_init();
	get_item_hook = textui_get_item; player->upkeep->command_wrk = USE_FLOOR;
	native = cave; cave = NULL;
	Term_keypress(KC_ENTER, 0); Term_keypress(KC_ENTER, 0);
	textui_cmd_ignore();
	cave = native;
	get_item_hook = previous_item;
	require(obj->known->notice & OBJ_NOTICE_IGNORE);
	cave = NULL;
	/* The carried-item context must not consult the retained dungeon/shop. */
	Term_keypress('k', 0); Term_keypress(KC_ENTER, 0);
	context_menu_object(carried_kind_object(player, obj->kind));
	cave = native;
	Term_activate(previous); term_screen = previous_screen; term_nuke(&terminal);
	require(carried_kind_object(player, obj->kind)->known->notice & OBJ_NOTICE_IGNORE);
	eq(player->upkeep->energy_use, 0);
	ok;
}

static int test_ground_autoinscription(void *unused)
{
	struct object *near, *far;
	struct chunk *native;
	int i;
	(void)unused;
	require(cavern());
	near = ground(TV_FOOD, "Ration of Food", 1, 2); notnull(near);
	far = ground(TV_FOOD, "Ration of Food", 1, 3); notnull(far);
	near->to_d = far->to_d = 1;
	player->obj_k->to_d = 1;
	for (i = 0; i < max_runes(); i++)
		if (object_has_rune(near, i) && player_knows_rune(player, i)) break;
	require(i < max_runes());
	rune_set_note(i, "=g");
	native = cave; cave = NULL;
	rune_autoinscribe(player, i);
	cave = native;
	require(near->note); eq(far->note, 0);
	near->note = 0;
	cave = NULL;
	autoinscribe_ground(player);
	cave = native;
	require(near->note); eq(far->note, 0);
	/* An invalid cave context must not fall back to the retained native map. */
	near->note = 0;
	player->spelunking->state.x = -1;
	cave = NULL;
	rune_autoinscribe(player, i);
	autoinscribe_ground(player);
	cave = native;
	eq(near->note, 0); eq(far->note, 0);
	player->spelunking->state.x = 2;
	rune_set_note(i, NULL);
	ok;
}

const char *suite_name = "game/spelunking-pickup";
struct test tests[] = {
	{ "shared options and floor inscriptions", test_policy },
	{ "stack cap splits known pair and leaves neighbours", test_stack_cap_and_neighbour },
	{ "gold free and mixed stacks cost one cave action", test_gold_and_mixed_batch },
	{ "ignore filters pile look and selector but show ignored recovers", test_ignore_view_and_recover },
	{ "flavour knowledge and protection rules unchanged", test_kind_ignore_knowledge },
	{ "movement queues pickup tied to exact arrival", test_arrival_and_stale_queue },
	{ "scheduler drops ignored gear locally without pickup loop", test_ignore_drop },
	{ "protected gear water and full ground retain ownership", test_protected_drop_and_capacity },
	{ "save roundtrip retains ignored piles caps and quantities", test_save_roundtrip },
	{ "real ignore menu cancellation ignore and unignore", test_ignore_menu_cancel_and_confirm },
	{ "selected pickup cannot reach elsewhere and dead player cannot collect", test_selected_pickup_and_dead },
	{ "full pack accepts only partial matching stack", test_full_pack_partial_stack },
	{ "equipped sticky and declined items remain protected", test_equipped_ignore_confirmation },
	{ "real floor selector and inventory context work without native map", test_real_floor_ignore_and_context },
	{ "ground rune inscriptions use only active cave cell", test_ground_autoinscription },
	{ NULL, NULL }
};
