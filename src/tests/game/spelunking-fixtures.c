/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "spelunking-integration-fixture.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-door.h"
#include "world-spelunking-view-terrain.h"
#include "world-spelunking-traversal-proof.h"
#include "world-spelunking-generation-population.h"
#include "world-spelunking-inventory.h"
#include "ui-spelunking.h"
#include "effect-handler.h"
#include "effects.h"
#include "datafile.h"
#include "ui-spelunking-appearance.h"
#include "project.h"

#define SPRING "core.spelunk.trap.spring"
#define STONE "core.spelunk.trap.stone"
#define SUPPLIES "core.spelunk.chest.supplies"
#define STORE "core.spelunk.chest.store"
static const char *find_ids[] = {
	"core.spelunk.find.rope-camp", "core.spelunk.find.ration-camp",
	"core.spelunk.find.ledge-cache", "core.spelunk.find.counter-niche",
	"core.spelunk.find.lens-niche", "core.spelunk.find.stone-niche"
};

static bool corridor(const char *id)
{
	enum world_spelunk_tile cells[120];
	struct level *level;
	int i;
	reset_before_load();
	if (!savefile_load("Test1", false)) return false;
	for (i = 0; i < 120; i++) cells[i] = WORLD_SPELUNK_ROCK;
	for (i = 1; i < 19; i++) cells[3*20+i] = WORLD_SPELUNK_AIR;
	if (!install_spelunking_runtime(cells, 20, 6, 3, 3, 90)) return false;
	level = (struct level *)world_player_level(player);
	level->mode = WORLD_MODE_SPELUNKING;
	my_strcpy(player->world_location.entry, "stair.main", sizeof(player->world_location.entry));
	player->spelunking->actor_roster_initialized = true;
	if (id && !world_spelunk_fixture_add(player->spelunking, id, 4, 3)) return false;
	world_spelunk_visibility_follow_player(player->spelunking);
	return true;
}

static bool command(cmd_code code, int direction)
{
	player->upkeep->energy_use = 0;
	if (cmdq_push(code)) return false;
	if (direction) cmd_set_arg_direction(cmdq_peek(), "direction", direction);
	return cmdq_pop(CTX_GAME);
}

static bool fixed_command(cmd_code code, int direction, int random)
{
	bool result;
	rand_fix(random);
	result = command(code, direction);
	rand_unfix();
	return result;
}

static enum world_spelunk_player_result move(int dx)
{
	struct world_spelunk_command c = { WORLD_SPELUNK_COMMAND_MOVE, 0, 0 };
	struct world_spelunk_action_report report;
	c.kind = dx ? WORLD_SPELUNK_COMMAND_MOVE : WORLD_SPELUNK_COMMAND_WAIT;
	c.dx = dx;
	player->upkeep->energy_use = 0;
	return world_spelunk_player_apply(player, &c, &report);
}

static bool detect(int effect, int distance)
{
	effect_handler_context_t context = { .effect = effect,
		.origin = source_player(), .x = distance, .y = distance };
	return world_spelunk_effect_dispatch(&context) == WORLD_SPELUNK_EFFECT_COMPLETED && context.ident;
}

static int test_hidden_and_search(void *unused)
{
	struct world_spelunk_runtime *r;
	struct world_spelunk_inspect inspect;
	struct randomizer_state before, after;
	(void)unused;
	require(corridor(SPRING)); r = player->spelunking;
	eq(r->fixtures[0].known, 0);
	require(world_spelunk_view_inspect(r, 4, 3, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_AIR);
	world_spelunk_visibility_reveal_layout(r);
	eq(r->fixtures[0].known, 0);
	player->state.skills[SKILL_SEARCH] = 19;
	Rand_state_export(&before);
	world_spelunk_fixture_notice(player);
	eq(r->fixtures[0].known, 0);
	player->state.skills[SKILL_SEARCH] = 100;
	player->timed[TMD_BLIND] = 1; world_spelunk_fixture_notice(player);
	eq(r->fixtures[0].known, 0);
	player->timed[TMD_BLIND] = 0;
	player->timed[TMD_CONFUSED] = 1; world_spelunk_fixture_notice(player);
	eq(r->fixtures[0].known, 0);
	player->timed[TMD_CONFUSED] = 0;
	player->state.skills[SKILL_SEARCH] = 20;
	world_spelunk_fixture_notice(player);
	eq(r->fixtures[0].known, SPELUNK_FIXTURE_READY);
	eq(r->fixtures[0].trap_known, SPELUNK_TRAP_ARMED);
	Rand_state_export(&after); require(memcmp(&before, &after, sizeof(before)) == 0);
	require(world_spelunk_view_inspect(r, 4, 3, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_TRAP);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_trigger_once(void *unused)
{
	struct world_spelunk_runtime *r;
	int hp;
	(void)unused;
	require(corridor(SPRING)); r = player->spelunking;
	player->state.skills[SKILL_SEARCH] = 0;
	hp = player->chp;
	eq(move(1), WORLD_SPELUNK_PLAYER_TURN);
	eq(player->chp, hp - 4);
	eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
	eq(r->fixtures[0].armed, 0);
	eq(r->fixtures[0].trap_known, SPELUNK_TRAP_SAFE);
	eq(move(0), WORLD_SPELUNK_PLAYER_TURN);
	eq(move(-1), WORLD_SPELUNK_PLAYER_TURN);
	eq(move(1), WORLD_SPELUNK_PLAYER_TURN);
	eq(player->chp, hp - 4);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_lethal_trigger(void *unused)
{
	(void)unused;
	require(corridor(STONE)); player->chp = 1;
	eq(move(1), WORLD_SPELUNK_PLAYER_DEAD);
	require(player->is_dead);
	eq(player->spelunking->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
	eq(player->spelunking->fixtures[0].armed, 0);
	ok;
}

static int test_disarm(void *unused)
{
	struct world_spelunk_runtime *r;
	int32_t xp;
	int hp, native_feat;
	(void)unused;
	require(corridor(SPRING)); r = player->spelunking;
	xp = player->exp; hp = player->chp; native_feat = square(cave, player->grid)->feat;
	require(command(CMD_DISARM, 6)); eq(player->upkeep->energy_use, 0);
	require(detect(EF_DETECT_TRAPS, 1));
	player->state.skills[SKILL_DISARM_PHYS] = 100;
	require(fixed_command(CMD_DISARM, 0, 0));
	eq(player->upkeep->energy_use, 100); eq(cmd_get_nrepeats(), 0);
	eq(player->exp, xp + 1); eq(player->chp, hp);
	eq(r->fixtures[0].armed, 0);
	require(command(CMD_DISARM, 6)); eq(player->upkeep->energy_use, 0);
	eq(player->exp, xp + 1);
	eq(square(cave, player->grid)->feat, native_feat);
	require(corridor(SPRING)); r = player->spelunking; hp = player->chp;
	require(detect(EF_DETECT_TRAPS, 1));
	player->state.skills[SKILL_DISARM_PHYS] = 100;
	player->timed[TMD_BLIND] = 1;
	require(fixed_command(CMD_DISARM, 6, 50));
	eq(player->chp, hp - 4); eq(player->upkeep->energy_use, 100);
	eq(r->fixtures[0].armed, 0);
	ok;
}

static int test_safe_failure(void *unused)
{
	struct randomizer_state chosen;
	uint32_t seed;
	int hp, xp;
	(void)unused;
	require(corridor(SPRING)); require(detect(EF_DETECT_TRAPS, 1));
	player->state.skills[SKILL_DISARM_PHYS] = 50;
	hp = player->chp; xp = player->exp;
	/* Find a deterministic failure/recovery pair rather than weakening the
	 * production randomizer or assuming a platform-specific seed sequence. */
	for (seed = 1; seed < 1000; seed++) {
		Rand_state_init(seed); Rand_state_export(&chosen);
		if (randint0(100) >= 40 && randint0(100) < 40) break;
	}
	require(seed < 1000); Rand_state_import(&chosen);
	require(command(CMD_DISARM, 6));
	eq(player->upkeep->energy_use, 100); eq(player->chp, hp); eq(player->exp, xp);
	eq(player->spelunking->fixtures[0].armed, 1);
	ok;
}

static int test_landing_and_teleport(void *unused)
{
	struct world_spelunk_runtime *r;
	int hp, x;
	(void)unused;
	require(corridor(SPRING)); r = player->spelunking; hp = player->chp;
	r->cells[2*20+4] = WORLD_SPELUNK_AIR;
	r->state.x = 4; r->state.y = 2; r->state.fall_start_y = 2;
	r->state.movement = WORLD_SPELUNK_FALLING;
	world_spelunk_visibility_follow_player(r);
	eq(move(0), WORLD_SPELUNK_PLAYER_TURN);
	eq(r->state.y, 3); eq(player->chp, hp - 4);
	require(corridor(SPRING)); r = player->spelunking; hp = player->chp;
	for (x = 1; x < 19; x++) {
		if (x != 3 && x != 4) r->cells[3*20+x] = WORLD_SPELUNK_ROCK;
	}
	world_spelunk_visibility_follow_player(r);
	require(world_spelunk_runtime_is_valid(r));
	effect_simple(EF_TELEPORT, source_player(), "5", 0, 0, 0, 0, 0, NULL);
	eq(r->state.x, 4); eq(player->chp, hp - 4);
	eq(r->fixtures[0].armed, 0);
	ok;
}

static int test_chest_loot_once(void *unused)
{
	struct world_spelunk_runtime *r;
	const struct object *obj;
	int32_t xp;
	(void)unused;
	require(corridor(SUPPLIES)); r = player->spelunking; xp = player->exp;
	eq(r->fixtures[0].known, SPELUNK_FIXTURE_READY);
	eq(r->fixtures[0].trap_known, SPELUNK_TRAP_UNKNOWN);
	player->state.skills[SKILL_DISARM_PHYS] = 0;
	require(fixed_command(CMD_OPEN, 0, 100));
	eq(player->upkeep->energy_use, 100);
	eq(r->fixtures[0].known, SPELUNK_FIXTURE_LOCKED);
	eq(world_spelunk_runtime_ground_object_count(r), 0);
	require(detect(EF_DETECT_OBJECTS, 2));
	eq(r->fixtures[0].known, SPELUNK_FIXTURE_LOCKED);
	require(fixed_command(CMD_OPEN, 6, 0));
	eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
	obj = world_spelunk_runtime_ground_object_at(r, 4, 3); notnull(obj);
	eq(obj->number, 1); eq(obj->tval, TV_POTION);
	eq(obj->sval, lookup_sval(TV_POTION, "Cure Light Wounds"));
	eq(obj->origin, ORIGIN_CHEST); eq(player->exp, xp);
	require(command(CMD_OPEN, 6)); eq(player->upkeep->energy_use, 0);
	require(command(CMD_CLOSE, 6)); eq(player->upkeep->energy_use, 0);
	eq(world_spelunk_runtime_ground_object_count(r), 1);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_trapped_chest(void *unused)
{
	struct world_spelunk_runtime *r;
	int hp;
	(void)unused;
	require(corridor(STORE)); r = player->spelunking; hp = player->chp;
	require(fixed_command(CMD_OPEN, 6, 0));
	eq(player->chp, hp - 4); eq(r->fixtures[0].armed, 0);
	eq(r->fixtures[0].state, SPELUNK_FIXTURE_READY);
	eq(world_spelunk_runtime_ground_object_count(r), 0);
	require(command(CMD_OPEN, 6)); eq(player->chp, hp - 4);
	eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
	eq(world_spelunk_runtime_ground_object_at(r, 4, 3)->number, 2);
	require(corridor(STORE)); r = player->spelunking; hp = player->chp;
	require(detect(EF_DETECT_TRAPS, 1));
	require(fixed_command(CMD_DISARM, 6, 0));
	eq(r->fixtures[0].state, SPELUNK_FIXTURE_LOCKED);
	eq(r->fixtures[0].armed, 0);
	/* Standing on a chest is safe and permits explicit direction 5. */
	eq(move(1), WORLD_SPELUNK_PLAYER_TURN);
	require(fixed_command(CMD_OPEN, 5, 0)); eq(player->chp, hp);
	eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
	ok;
}

static int test_obstruction_and_capacity(void *unused)
{
	struct world_spelunk_runtime *r;
	struct randomizer_state before, after;
	int i;
	(void)unused;
	require(corridor(SUPPLIES)); r = player->spelunking;
	require(world_spelunk_runtime_add_actor(r, WORLD_SPELUNK_CHASM_SKITTER_ID, 4, 3));
	require(command(CMD_OPEN, 6)); eq(player->upkeep->energy_use, 0);
	require(world_spelunk_runtime_remove_actor(r, &r->actors[0]));
	for (i = 0; i < WORLD_SPELUNK_OBJECT_MAX; i++) {
		struct object *obj = make_test_object(lookup_kind(TV_FOOD,
			lookup_sval(TV_FOOD, "Ration of Food")), 1);
		require(world_spelunk_runtime_add_ground_object(r, obj, 6 + i % 8, 3));
	}
	Rand_state_export(&before);
	require(command(CMD_OPEN, 6)); eq(player->upkeep->energy_use, 0);
	Rand_state_export(&after); require(memcmp(&before, &after, sizeof(before)) == 0);
	eq(r->fixtures[0].state, SPELUNK_FIXTURE_LOCKED);
	ok;
}

static int test_detection_chain(void *unused)
{
	struct world_spelunk_runtime *r;
	struct effect a = { 0 }, b = { 0 }, c = { 0 };
	struct world_spelunk_inspect inspect;
	(void)unused;
	require(corridor(NULL)); r = player->spelunking;
	require(world_spelunk_fixture_add(r, STORE, 17, 3));
	require(world_spelunk_runtime_add_actor(r, WORLD_SPELUNK_CHASM_SKITTER_ID, 17, 3));
	require(world_spelunk_door_add(r, "core.spelunk.door.store", 9, 3));
	memset(r->explored, 0, 120);
	world_spelunk_visibility_follow_player(r);
	eq(r->fixtures[0].known, 0);
	require(detect(EF_DETECT_TRAPS, 2)); eq(r->fixtures[0].known, 0);
	require(detect(EF_DETECT_OBJECTS, 20));
	eq(r->fixtures[0].known, SPELUNK_FIXTURE_READY);
	eq(r->fixtures[0].trap_known, SPELUNK_TRAP_UNKNOWN);
	require(world_spelunk_view_inspect(r, 17, 3, &inspect));
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_CHEST); null(inspect.actor);
	require(detect(EF_DETECT_TRAPS, 20));
	eq(r->fixtures[0].trap_known, SPELUNK_TRAP_ARMED);
	a.index = EF_DETECT_TRAPS; a.next = &b;
	b.index = EF_DETECT_DOORS; b.next = &c;
	c.index = EF_DETECT_STAIRS;
	require(world_spelunk_effect_chain_is_supported(&a));
	require(detect(EF_DETECT_DOORS, 20));
	eq(r->doors[0].known, WORLD_SPELUNK_DOOR_CLOSED);
	require(detect(EF_DETECT_STAIRS, 20));
	c.index = EF_EARTHQUAKE;
	require(!world_spelunk_effect_chain_is_supported(&a));
	ok;
}

static int test_art_and_memory(void *unused)
{
	struct world_spelunk_runtime *r;
	struct world_spelunk_view view;
	struct world_spelunk_view_terrain terrain;
	uint8_t cells[120]; int attr; wchar_t glyph;
	(void)unused;
	require(corridor(SUPPLIES)); r = player->spelunking;
	require(world_spelunk_view_capture(r, 20, 6, cells, 120, &view));
	eq(world_spelunk_view_cell_at(&view, 4, 3), WORLD_SPELUNK_VIEW_CHEST);
	world_spelunk_view_terrain_at(r, &view, 4, 3, &terrain);
	require(streq(terrain.overlay, "chest"));
	require(fixed_command(CMD_OPEN, 6, 0));
	/* Ground loot covers the empty marker while it is visible; memory
	 * retains only the observed chest, not transient unseen objects. */
	r->state.x = 18; world_spelunk_visibility_follow_player(r);
	require(world_spelunk_view_capture(r, 20, 6, cells, 120, &view));
	eq(world_spelunk_view_cell_at(&view, 4, 3), WORLD_SPELUNK_VIEW_REMEMBERED_CHEST_EMPTY);
	spelunking_view_cell_appearance(&view, 4, 3, &attr, &glyph); eq(glyph, L'_');
	require(corridor(SPRING)); r = player->spelunking;
	require(detect(EF_DETECT_TRAPS, 1));
	require(world_spelunk_view_capture(r, 20, 6, cells, 120, &view));
	world_spelunk_view_terrain_at(r, &view, 4, 3, &terrain);
	require(streq(terrain.overlay, "trap"));
	require(fixed_command(CMD_DISARM, 6, 0));
	require(world_spelunk_view_capture(r, 20, 6, cells, 120, &view));
	eq(world_spelunk_view_cell_at(&view, 4, 3), WORLD_SPELUNK_VIEW_TRAP_SPENT);
	world_spelunk_view_terrain_at(r, &view, 4, 3, &terrain);
	null(terrain.overlay); require(terrain.preserve_glyph);
	ok;
}

static int test_codec_and_old_save(void *unused)
{
	struct world_spelunk_runtime *r, *decoded = NULL;
	uint8_t payload[4096], copy[4096]; size_t n, i, start;
	(void)unused;
	require(corridor(STORE)); r = player->spelunking;
	require(detect(EF_DETECT_TRAPS, 1));
	require(fixed_command(CMD_DISARM, 6, 0));
	eq(world_spelunk_runtime_encode(r, payload, sizeof(payload), &n), WORLD_SPELUNK_CODEC_OK);
	eq(n, world_spelunk_runtime_encoded_size(r));
	eq(world_spelunk_runtime_decode(payload, n, &decoded), WORLD_SPELUNK_CODEC_OK);
	eq(decoded->fixtures[0].state, SPELUNK_FIXTURE_LOCKED);
	eq(decoded->fixtures[0].armed, 0); eq(decoded->fixtures[0].trap_known, SPELUNK_TRAP_SAFE);
	world_spelunk_runtime_free(decoded); decoded = NULL;
	start = n - (10 + strlen(STORE));
	for (i = start; i < n; i++) {
		require(world_spelunk_runtime_decode(payload, i, &decoded) != WORLD_SPELUNK_CODEC_OK);
		null(decoded);
	}
	memcpy(copy, payload, n); copy[start] = 255;
	eq(world_spelunk_runtime_decode(copy, n, &decoded), WORLD_SPELUNK_CODEC_CORRUPT);
	memcpy(copy, payload, n); copy[start+2] = '!';
	eq(world_spelunk_runtime_decode(copy, n, &decoded), WORLD_SPELUNK_CODEC_CORRUPT);
	memcpy(copy, payload, n); copy[n-4] = 255;
	eq(world_spelunk_runtime_decode(copy, n, &decoded), WORLD_SPELUNK_CODEC_CORRUPT);
	memcpy(copy, payload, n); copy[n-2] = 2;
	eq(world_spelunk_runtime_decode(copy, n, &decoded), WORLD_SPELUNK_CODEC_CORRUPT);
	/* Historical v14 has doors but no fixture tail. No retroactive contents. */
	payload[4] = WORLD_SPELUNK_PAYLOAD_VERSION_DOORS;
	eq(world_spelunk_runtime_decode(payload, start, &decoded), WORLD_SPELUNK_CODEC_OK);
	eq(decoded->fixture_count, 0);
	require(world_spelunk_runtime_is_valid(decoded));
	world_spelunk_runtime_free(decoded);
	ok;
}

static int test_save_loot_and_traps(void *unused)
{
	struct world_spelunk_runtime *r;
	int i;
	(void)unused;
	require(corridor(STORE)); r = player->spelunking;
	require(world_spelunk_fixture_add(r, SPRING, 6, 3));
	require(detect(EF_DETECT_TRAPS, 10));
	require(fixed_command(CMD_DISARM, 6, 0));
	require(fixed_command(CMD_OPEN, 6, 0));
	r->state.x = 6; world_spelunk_fixture_step(player);
	((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
	for (i = 0; i < 2; i++) {
		require(savefile_save("TestSpelunk"));
		reset_before_load(); require(savefile_load("TestSpelunk", false));
		r = player->spelunking; notnull(r); eq(r->fixture_count, 2);
		eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
		eq(r->fixtures[1].state, SPELUNK_FIXTURE_SPENT);
		eq(world_spelunk_runtime_ground_object_count(r), 1);
		eq(world_spelunk_runtime_ground_object_at(r, 4, 3)->number, 2);
		require(!world_spelunk_fixture_interact(player, &r->fixtures[0], false));
		require(world_spelunk_runtime_is_valid(r));
	}
	ok;
}

static int check_active_save(bool discovery)
{
	const struct level *level;
	const struct world_entry *entry;
	struct world_spelunk_runtime *r;
	struct world_spelunking_entry_report report;
	size_t chest, i;
	reset_before_load(); require(savefile_load("Test1", false));
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
	r = player->spelunking;
	for (chest = 0; chest < r->fixture_count; chest++) {
		if (world_spelunk_fixture_by_id(r->fixtures[chest].id)->kind == SPELUNK_CHEST) break;
	}
	require(chest < r->fixture_count);
	if (discovery) {
		my_strcpy(r->fixtures[chest].id, find_ids[3], sizeof(r->fixtures[chest].id));
		r->fixtures[chest].state = SPELUNK_FIXTURE_READY;
		require(r->actor_count > 0);
		r->actors[0].hp--;
		r->actors[0].energy = 77;
		r->actors[0].status[WORLD_SPELUNK_STATUS_SLOW] = 12;
	}
	for (i = 0; i < 2; i++) {
		size_t fixture_count = r->fixture_count;
		size_t actor_count = r->actor_count;
		struct world_spelunk_actor actors[WORLD_SPELUNK_ACTOR_MAX];
		memcpy(actors, r->actors, sizeof(actors));
		require(savefile_save("TestSpelunkActive"));
		reset_before_load(); require(savefile_load("TestSpelunkActive", false));
		require(world_spelunking_active_storage_is_valid(player)); r = player->spelunking;
		eq(r->fixture_count, fixture_count); /* Loading never adds new discoveries. */
		eq(r->actor_count, actor_count);
		require(memcmp(actors, r->actors, actor_count*sizeof(*actors)) == 0);
		eq(r->fixtures[chest].state, i ? SPELUNK_FIXTURE_SPENT :
			(discovery ? SPELUNK_FIXTURE_READY : SPELUNK_FIXTURE_LOCKED));
		if (!i) {
			r->state.x = r->fixtures[chest].x; r->state.y = r->fixtures[chest].y;
			r->state.fall_start_y = r->state.y; r->state.movement = WORLD_SPELUNK_STANDING;
			world_spelunk_visibility_follow_player(r);
			require(fixed_command(CMD_OPEN, 5, 0));
		} else {
			notnull(world_spelunk_runtime_ground_object_at(r, r->fixtures[chest].x, r->fixtures[chest].y));
			require(command(CMD_OPEN, 5)); eq(player->upkeep->energy_use, 0);
		}
	}
	return 0;
}

static int test_active_save(void *unused)
{
	(void)unused;
	eq(check_active_save(false), 0);
	eq(check_active_save(true), 0);
	ok;
}

/* Independently search back from a find to the witness. Replay every edge
 * both ways through real commands, including grip selection, with no damage. */
static bool shelf_move(struct world_spelunk_state *state, int dx, int dy)
{
	struct world_spelunk_command c = { WORLD_SPELUNK_COMMAND_MOVE, dx, dy };
	struct world_spelunk_action_report report;
	int i, x = state->x, y = state->y;
	for (i = 0; i <= 8; i++) {
		struct world_spelunk_state trial = *state;
		if (world_spelunk_apply_command(&trial, &c, &report) != WORLD_SPELUNK_ACTION_REJECTED &&
			trial.x == x + dx && trial.y == y + dy && !report.damage &&
			trial.movement == WORLD_SPELUNK_STANDING) { *state = trial; return true; }
		{
			struct world_spelunk_command grip = { WORLD_SPELUNK_COMMAND_GRIP, 0, 0 };
			if (world_spelunk_apply_command(state, &grip, &report) == WORLD_SPELUNK_ACTION_REJECTED) break;
		}
	}
	return false;
}

static bool discovery_return_path(const struct world_spelunk_runtime *r,
	const struct world_spelunk_generated_layout *g, const struct world_spelunk_fixture *find)
{
	size_t count = (size_t)g->width*g->height, head = 0, tail = 0, j;
	size_t *queue = mem_alloc(count*sizeof(*queue));
	uint8_t *distance = mem_zalloc(count);
	bool found = false;
	size_t start = (size_t)find->y*g->width+find->x;
	distance[start] = 1; queue[tail++] = start;
	while (head < tail && !found) {
		size_t index = queue[head++];
		int x = (int)(index % g->width), y = (int)(index / g->width), dx, dy;
		for (j = 0; j < g->witness_step_count; j++) {
			if (x == g->witness_steps[j].expected_x && y == g->witness_steps[j].expected_y) found = true;
		}
		if (found || distance[index] >= 13) continue;
		for (dx = -1; dx <= 1; dx++) for (dy = -1; dy <= 1; dy++) {
			int nx = x + dx, ny = y + dy;
			size_t next;
			struct world_spelunk_state walk;
			if (nx <= 0 || nx >= g->width-1 || ny <= 0 || ny >= g->height-1) continue;
			next = (size_t)ny*g->width+nx;
			if (distance[next] || r->cells[next] != WORLD_SPELUNK_AIR ||
				r->cells[next+g->width] != WORLD_SPELUNK_ROCK ||
				world_spelunk_door_at(r, nx, ny) || world_spelunk_fixture_at(r, nx, ny) ||
				world_spelunk_runtime_actor_at(r, nx, ny)) continue;
			if (!world_spelunk_state_init(&walk, &r->state.map, &r->state.rules, x, y, 100) ||
				!shelf_move(&walk, dx, dy) || !shelf_move(&walk, -dx, -dy)) continue;
			distance[next] = distance[index] + 1; queue[tail++] = next;
		}
	}
	mem_free(queue); mem_free(distance);
	return found;
}

/* Test-only comparison with the previous population policy. Restore all
 * registry values before any assertion, including failed publication. */
static struct world_spelunk_system *guardian_disabled_system(
	const struct world_spelunk_system_profile *profile, uint32_t seed)
{
	uint16_t chances[WORLD_SPELUNK_POPULATION_PROFILE_MAX];
	struct world_spelunk_system *s = NULL;
	int i, count = world_spelunk_population_profile_count();
	bool valid;
	for (i = 0; i < count; i++) {
		struct world_spelunk_population_profile *p = (struct world_spelunk_population_profile *)
			world_spelunk_population_profile_by_index(i);
		chances[i] = p->discovery_guardian_chance;
		p->discovery_guardian_chance = 0;
	}
	valid = world_spelunk_publish_system_candidate(profile, seed, 100,
		"shaft.surface", &s) == WORLD_SPELUNK_PUBLICATION_OK;
	if (valid) {
		size_t n;
		for (n = 0; n < s->edge_count && valid; n++)
			valid = world_spelunk_materialize_node_candidate(s, s->edges[n].node_b_id,
				100, s->edges[n].endpoint_b_id) == WORLD_SPELUNK_PUBLICATION_OK;
	}
	for (i = 0; i < count; i++) {
		struct world_spelunk_population_profile *p = (struct world_spelunk_population_profile *)
			world_spelunk_population_profile_by_index(i);
		p->discovery_guardian_chance = chances[i];
	}
	if (!valid) { world_spelunk_system_free(s); return NULL; }
	return s;
}

static int check_encounter(struct world_spelunk_runtime *r,
	const struct world_spelunk_runtime *baseline,
	const struct world_spelunk_generated_layout *g, unsigned *counts)
{
	const struct world_spelunk_population_profile *p =
		world_spelunk_population_profile_by_id(g->population_profile_id);
	const struct world_spelunk_fixture *find = NULL;
	const struct object *a, *b;
	size_t i;
	eq(r->fixture_count, baseline->fixture_count);
	require(memcmp(r->fixtures, baseline->fixtures, r->fixture_count*sizeof(*r->fixtures)) == 0);
	eq(r->door_count, baseline->door_count);
	require(memcmp(r->doors, baseline->doors, r->door_count*sizeof(*r->doors)) == 0);
	require(r->actor_count >= baseline->actor_count && r->actor_count <= baseline->actor_count+1);
	require(memcmp(r->actors, baseline->actors, baseline->actor_count*sizeof(*r->actors)) == 0);
	for (a = r->ground_objects, b = baseline->ground_objects; a && b; a = a->next, b = b->next) {
		ptreq(a->kind, b->kind); eq(a->number, b->number); require(loc_eq(a->grid, b->grid));
	}
	null(a); null(b);
	if (r->actor_count == baseline->actor_count) return 0;
	for (i = 0; i < r->fixture_count; i++) {
		if (world_spelunk_fixture_by_id(r->fixtures[i].id)->description[0]) find = &r->fixtures[i];
	}
	notnull(find);
	{
		const struct world_spelunk_actor *actor = &r->actors[r->actor_count-1];
		const struct world_spelunk_actor_definition *d = world_spelunk_actor_by_id(actor->id);
		int dx = actor->x-find->x, direction = dx > 0 ? 1 : -1;
		struct world_spelunk_state before = r->state;
		require(streq(actor->id, p->discovery_guardian_id)); notnull(d);
		eq(actor->y, find->y); require(abs(dx) >= 2 && abs(dx) <= 4);
		eq(actor->hp, d->hitpoints); eq(actor->max_hp, d->hitpoints);
		eq(actor->reward, d->experience); eq(actor->energy, 0);
		for (i = 0; i < baseline->actor_count; i++)
			require(abs(find->x-baseline->actors[i].x)+abs(find->y-baseline->actors[i].y) > 6);
		for (i = 0; i < g->witness_step_count; i++)
			require(abs(actor->x-g->witness_steps[i].expected_x) > 2 ||
				abs(actor->y-g->witness_steps[i].expected_y) > 2);
		r->state.x = find->x-direction; r->state.y = find->y;
		r->state.movement = WORLD_SPELUNK_STANDING; r->state.fall_start_y = find->y;
		world_spelunk_visibility_follow_player(r);
		require(world_spelunk_visibility_is_visible(r, actor->x, actor->y));
		/* The player can see both reward and danger before stepping onto it. */
		require(world_spelunk_visibility_is_visible(r, find->x, find->y));
		require(discovery_return_path(r, g, find));
		r->state = before; world_spelunk_visibility_follow_player(r);
		for (i = 0; i < (size_t)world_spelunk_population_profile_count(); i++)
			if (p == world_spelunk_population_profile_by_index((int)i)) counts[i]++;
	}
	return 0;
}

static int test_generation(void *unused)
{
	const struct world_spelunk_system_profile *profile = world_spelunk_system_profile_by_id("deepwell");
	struct randomizer_state before, after;
	unsigned traps = 0, chests = 0, sections = 0, finds = 0;
	unsigned variants[6] = { 0 }; uint32_t seed;
	unsigned guardians[WORLD_SPELUNK_POPULATION_PROFILE_MAX] = { 0 };
	(void)unused;
	Rand_state_export(&before);
	for (seed = 0; seed < 128; seed++) {
		struct world_spelunk_system *s = NULL; size_t i, n;
		struct world_spelunk_system *baseline = guardian_disabled_system(profile, seed);
		notnull(baseline);
		eq(world_spelunk_publish_system_candidate(profile, seed, 100, "shaft.surface", &s), WORLD_SPELUNK_PUBLICATION_OK);
		for (i = 0; i < s->edge_count; i++) {
			eq(world_spelunk_materialize_node_candidate(s, s->edges[i].node_b_id, 100,
				s->edges[i].endpoint_b_id), WORLD_SPELUNK_PUBLICATION_OK);
		}
		for (n = 0; n < s->node_count; n++) {
			struct world_spelunk_runtime *r = s->nodes[n].runtime;
			struct world_spelunk_generated_layout g;
			unsigned local_finds = 0;
			sections++; require(r->fixture_count <= 3);
			eq(world_spelunk_reconstruct_node_candidate(s, s->nodes[n].id, 100, &g), WORLD_SPELUNK_RECONSTRUCTION_OK);
			eq(check_encounter(r, baseline->nodes[n].runtime, &g, guardians), 0);
			for (i = 0; i < r->fixture_count; i++) {
				const struct world_spelunk_fixture *f = &r->fixtures[i]; size_t j;
				const struct world_spelunk_fixture_definition *d = world_spelunk_fixture_by_id(f->id);
				if (d->kind == SPELUNK_TRAP) traps++;
				else if (d->description[0]) {
					finds++; local_finds++;
					for (j = 0; j < N_ELEMENTS(find_ids); j++) if (streq(f->id, find_ids[j])) variants[j]++;
					require(discovery_return_path(r, &g, f));
				} else chests++;
				require(abs(f->x-r->state.x)+abs(f->y-r->state.y) > 4);
				for (j = 0; j < g.route_visit_count; j++) {
					require(abs(f->x-g.route_visits[j].x)+abs(f->y-g.route_visits[j].y) > 4);
				}
				for (j = 0; j < g.witness_step_count; j++) {
					require(abs(f->x-g.witness_steps[j].expected_x) > 1 || abs(f->y-g.witness_steps[j].expected_y) > 1);
				}
			}
			require(local_finds <= 1);
			require(memcmp(r->cells, g.cells, (size_t)g.width*g.height*sizeof(*g.cells)) == 0);
			require(world_spelunk_runtime_is_valid(r));
			world_spelunk_generated_layout_dispose(&g);
		}
		world_spelunk_system_free(s);
		world_spelunk_system_free(baseline);
	}
	Rand_state_export(&after); require(memcmp(&before, &after, sizeof(before)) == 0);
	if (verbose) printf("\nCave fixtures: %u traps, %u chests, %u discoveries in %u sections over 128 seeds\n", traps, chests, finds, sections);
	require(traps > 0); require(chests > 0);
	require(finds > 0 && finds < sections);
	for (seed = 0; seed < N_ELEMENTS(find_ids); seed++) {
		if (verbose) printf("  %s: %u\n", find_ids[seed], variants[seed]);
		require(variants[seed] > 0);
	}
	for (seed = 0; seed < (uint32_t)world_spelunk_population_profile_count(); seed++) {
		if (verbose) printf("  %s guardians: %u\n", world_spelunk_population_profile_by_index(seed)->id, guardians[seed]);
		require(guardians[seed] > 0);
	}
	ok;
}

static int test_discovery_loot_notes_and_save(void *unused)
{
	size_t i;
	(void)unused;
	for (i = 0; i < N_ELEMENTS(find_ids); i++) {
		struct world_spelunk_runtime *r;
		const struct world_spelunk_fixture_definition *d;
		struct object *obj;
		struct object_kind *kind;
		struct randomizer_state before, after;
		struct world_spelunk_inspect inspect;
		char description[256];
		int carried, quantity, xp;
		require(corridor(find_ids[i])); r = player->spelunking;
		d = world_spelunk_fixture_by_id(find_ids[i]); notnull(d);
		require(d->description[0] && d->note[0] && !d->lock_power && !d->trap_id[0]);
		require(world_spelunk_view_inspect(r, 4, 3, &inspect));
		ptreq(inspect.fixture_description, d->description);
		require(textui_spelunking_describe(player, 4, 3, description, sizeof(description)));
		notnull(strstr(description, "Use o to search"));
		xp = player->exp;
		/* Retain a ready pocket through a full save before it is opened. */
		((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
		require(savefile_save("TestDiscovery"));
		reset_before_load(); require(savefile_load("TestDiscovery", false));
		r = player->spelunking; notnull(r); eq(r->fixture_count, 1);
		eq(r->fixtures[0].state, SPELUNK_FIXTURE_READY);
		((struct level *)world_player_level(player))->mode = WORLD_MODE_SPELUNKING;
		Rand_state_export(&before);
		require(command(CMD_OPEN, 6)); eq(player->upkeep->energy_use, 100);
		eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
		notnull(strstr(message_str(0), d->note));
		require(textui_spelunking_describe(player, 4, 3, description, sizeof(description)));
		/* Loose contents have normal foreground priority; the pocket remains. */
		obj = world_spelunk_runtime_ground_object_at(r, 4, 3); notnull(obj);
		kind = obj->kind; quantity = obj->number; eq(quantity, d->quantity);
		eq(obj->origin, ORIGIN_CHEST);
		carried = carried_kind_count(player, kind);
		require(command(CMD_OPEN, 0)); eq(player->upkeep->energy_use, 0);
		notnull(strstr(message_str(0), d->note));
		Rand_state_export(&after); require(memcmp(&before, &after, sizeof(before)) == 0);
		eq(player->exp, xp);
		/* Save with loose loot, pick it up, then save with an empty pocket. */
		((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
		require(savefile_save("TestDiscovery"));
		reset_before_load(); require(savefile_load("TestDiscovery", false));
		r = player->spelunking; obj = world_spelunk_runtime_ground_object_at(r, 4, 3); notnull(obj);
		kind = obj->kind; eq(obj->number, quantity);
		((struct level *)world_player_level(player))->mode = WORLD_MODE_SPELUNKING;
		r->state.x = 4; world_spelunk_visibility_follow_player(r);
		require(command(CMD_PICKUP, 0));
		eq(carried_kind_count(player, kind), carried + quantity);
		eq(world_spelunk_runtime_ground_object_count(r), 0);
		((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
		require(savefile_save("TestDiscovery"));
		reset_before_load(); require(savefile_load("TestDiscovery", false));
		r = player->spelunking;
		((struct level *)world_player_level(player))->mode = WORLD_MODE_SPELUNKING;
		require(command(CMD_OPEN, 5)); eq(player->upkeep->energy_use, 0);
		eq(world_spelunk_runtime_ground_object_count(r), 0);
		eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
		/* Moving off the empty find exposes its description and reread hint. */
		r->state.x = 3; world_spelunk_visibility_follow_player(r);
		require(textui_spelunking_describe(player, 4, 3, description, sizeof(description)));
		notnull(strstr(description, "Use o to reread"));
		player->timed[TMD_BLIND] = 2;
		require(command(CMD_OPEN, 6)); eq(player->upkeep->energy_use, 0);
		notnull(strstr(message_str(0), "cannot read"));
		player->timed[TMD_BLIND] = 0;
		require(command(CMD_OPEN, 6)); eq(player->upkeep->energy_use, 0);
		notnull(strstr(message_str(0), d->note));
		require(world_spelunk_runtime_is_valid(r));
	}
	require(file_delete("TestDiscovery"));
	ok;
}

static int test_discovery_generation_skip_and_repeat(void *unused)
{
	int variant;
	(void)unused;
	for (variant = 0; variant < 3; variant++) {
		struct world_spelunk_population_profile profile = { 0 };
		struct world_spelunk_generated_layout g = { 0 };
		struct world_spelunk_witness_step step = { 0 };
		struct world_spelunk_runtime *r;
		enum world_spelunk_tile before[120];
		require(corridor(NULL)); r = player->spelunking;
		r->actor_roster_initialized = false;
		profile.id = "test.discovery"; profile.version = 1;
		profile.entrance_exclusion_radius = 5;
		profile.discovery_chance = 100; profile.discovery_entry_count = 1;
		profile.discoveries[0].fixture_id = find_ids[0];
		profile.discoveries[0].weight = 1; profile.discoveries[0].maximum_depth_percent = 100;
		profile.discovery_guardian_id = "core.spelunk.actor.cave-rat";
		profile.discovery_guardian_chance = 100;
		profile.actor_entry_count = 1; profile.actors[0].actor_id = profile.discovery_guardian_id;
		profile.actors[0].maximum_depth_percent = 100;
		my_strcpy(g.population_profile_id, profile.id, sizeof(g.population_profile_id));
		g.width = 20; g.height = 6; g.entrance_x = 3; g.entrance_y = 3;
		step.expected_x = 3; step.expected_y = 3;
		g.witness_steps = &step; g.witness_step_count = variant == 1 ? 0 : 1;
		/* Missing witness or interrupted shelf cannot be fixed by carving. */
		if (variant == 2) r->cells[3*20+8] = WORLD_SPELUNK_ROCK;
		memcpy(before, r->cells, sizeof(before));
		require(world_spelunk_populate_generated_runtime(r, &g, &profile));
		eq(r->fixture_count, variant ? 0 : 1);
		if (variant) eq(r->actor_count, 0);
		require(memcmp(before, r->cells, sizeof(before)) == 0);
		require(!world_spelunk_populate_generated_runtime(r, &g, &profile));
		eq(r->fixture_count, variant ? 0 : 1);
	}
	ok;
}

static int test_guardian_lifecycle(void *unused)
{
	const char *ids[] = { "core.spelunk.actor.cave-rat", WORLD_SPELUNK_CHASM_SKITTER_ID,
		"core.spelunk.actor.stone-beetle" };
	size_t type;
	(void)unused;
	for (type = 0; type < N_ELEMENTS(ids); type++) {
		struct world_spelunk_population_profile profile = { 0 };
		struct world_spelunk_generated_layout g = { 0 };
		struct world_spelunk_witness_step step = { 0 };
		struct world_spelunk_runtime *r;
		struct world_spelunk_actor saved;
		struct world_spelunk_actor_report reports[WORLD_SPELUNK_ACTOR_MAX];
		size_t written;
		int seed, xp, hp, fx, fy, direction;
		profile.id = "test.encounter"; profile.version = 1;
		profile.entrance_exclusion_radius = 5;
		profile.discovery_chance = 100; profile.discovery_entry_count = 1;
		profile.discoveries[0].fixture_id = find_ids[0];
		profile.discoveries[0].weight = 1; profile.discoveries[0].maximum_depth_percent = 100;
		profile.discovery_guardian_id = ids[type]; profile.discovery_guardian_chance = 100;
		/* Suppress ordinary actors in this isolated placement fixture. */
		profile.actor_entry_count = 1; profile.actors[0].actor_id = ids[type];
		profile.actors[0].maximum_depth_percent = 100;
		my_strcpy(g.population_profile_id, profile.id, sizeof(g.population_profile_id));
		g.width = 20; g.height = 6; g.entrance_x = 3; g.entrance_y = 3;
		step.expected_x = 3; step.expected_y = 3; g.witness_steps = &step; g.witness_step_count = 1;
		for (seed = 0; seed < 64; seed++) {
			require(corridor(NULL)); r = player->spelunking; r->actor_roster_initialized = false;
			g.seed = seed;
			require(world_spelunk_populate_generated_runtime(r, &g, &profile));
			if (r->actor_count) break;
		}
		require(seed < 64); eq(r->actor_count, 1); eq(r->fixture_count, 1);
		fx = r->fixtures[0].x; fy = r->fixtures[0].y;
		direction = r->actors[0].x > fx ? 1 : -1;
		r->state.x = fx-direction; world_spelunk_visibility_follow_player(r);
		require(world_spelunk_visibility_is_visible(r, r->actors[0].x, fy));
		hp = player->chp;
		/* Even a ready actor must walk towards us first, not attack on sight. */
		saved = r->actors[0]; r->actors[0].energy = 100;
		require(world_spelunk_process_actors(player, reports, N_ELEMENTS(reports), &written));
		eq(written, 1); eq(reports[0].event, WORLD_SPELUNK_ACTOR_EVENT_MOVED);
		eq(player->chp, hp); r->actors[0] = saved;
		/* A normal step back remains available before committing to the cache. */
		eq(move(-direction), WORLD_SPELUNK_PLAYER_TURN); eq(player->chp, hp);
		eq(move(direction), WORLD_SPELUNK_PLAYER_TURN);
		require(world_spelunk_actor_project(player, &r->actors[0], 1, PROJ_MISSILE));
		r->actors[0].energy = 77; r->actors[0].status[WORLD_SPELUNK_STATUS_SLOW] = 12;
		saved = r->actors[0]; xp = player->exp;
		((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
		require(savefile_save("TestEncounter"));
		reset_before_load(); require(savefile_load("TestEncounter", false)); r = player->spelunking;
		eq(r->actor_count, 1); require(memcmp(&saved, &r->actors[0], sizeof(saved)) == 0);
		((struct level *)world_player_level(player))->mode = WORLD_MODE_SPELUNKING;
		require(!world_spelunk_populate_generated_runtime(r, &g, &profile));
		require(world_spelunk_actor_project(player, &r->actors[0], 100, PROJ_MISSILE));
		eq(r->actor_count, 0); eq(player->exp, xp+saved.reward);
		require(command(CMD_OPEN, direction > 0 ? 6 : 4));
		eq(r->fixtures[0].state, SPELUNK_FIXTURE_SPENT);
		eq(world_spelunk_runtime_ground_object_count(r), 1);
		((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
		require(savefile_save("TestEncounter"));
		reset_before_load(); require(savefile_load("TestEncounter", false)); r = player->spelunking;
		((struct level *)world_player_level(player))->mode = WORLD_MODE_SPELUNKING;
		eq(r->actor_count, 0); eq(player->exp, xp+saved.reward);
		require(!world_spelunk_populate_generated_runtime(r, &g, &profile));
		require(command(CMD_OPEN, direction > 0 ? 6 : 4)); eq(player->upkeep->energy_use, 0);
		eq(r->actor_count, 0); eq(world_spelunk_runtime_ground_object_count(r), 1);
		eq(player->exp, xp+saved.reward);
		/* Disabling guardians or excluding their depth leaves the SAME find. */
		for (seed = 0; seed < 2; seed++) {
			require(corridor(NULL)); r = player->spelunking; r->actor_roster_initialized = false;
			if (!seed) profile.discovery_guardian_chance = 0;
			else { profile.discovery_guardian_chance = 100; profile.actors[0].maximum_depth_percent = 0; }
			require(world_spelunk_populate_generated_runtime(r, &g, &profile));
			eq(r->actor_count, 0); eq(r->fixture_count, 1);
			eq(r->fixtures[0].x, fx); eq(r->fixtures[0].y, fy);
		}
	}
	require(file_delete("TestEncounter"));
	ok;
}

static int test_legacy_encounter_save(void *unused)
{
	const char *path = getenv("AMMEROW_TEST_LEGACY_CAVE_SAVE");
	struct world_spelunk_actor actors[WORLD_SPELUNK_ACTOR_MAX];
	struct world_spelunk_fixture fixtures[WORLD_SPELUNK_FIXTURE_MAX];
	size_t actor_count, fixture_count;
	(void)unused;
	if (!path || !*path) ok;
	reset_before_load(); require(savefile_load(path, false));
	require(world_spelunking_active_storage_is_valid(player));
	actor_count = player->spelunking->actor_count; fixture_count = player->spelunking->fixture_count;
	memcpy(actors, player->spelunking->actors, sizeof(actors));
	memcpy(fixtures, player->spelunking->fixtures, sizeof(fixtures));
	require(savefile_save("TestEncounter"));
	reset_before_load(); require(savefile_load("TestEncounter", false));
	require(world_spelunking_active_storage_is_valid(player));
	eq(player->spelunking->actor_count, actor_count); eq(player->spelunking->fixture_count, fixture_count);
	require(memcmp(actors, player->spelunking->actors, actor_count*sizeof(*actors)) == 0);
	require(memcmp(fixtures, player->spelunking->fixtures, fixture_count*sizeof(*fixtures)) == 0);
	require(file_delete("TestEncounter"));
	ok;
}

static int test_data_and_invariants(void *unused)
{
	struct parser *p;
	struct world_spelunk_runtime *r;
	(void)unused;
	require(corridor(SPRING)); r = player->spelunking;
	require(world_spelunk_fixture_data_validate());
	require(!world_spelunk_fixture_add(r, "missing", 6, 3));
	require(!world_spelunk_fixture_add(r, SUPPLIES, 4, 3));
	require(!world_spelunk_fixture_add(r, SUPPLIES, -1, 3));
	require(!world_spelunk_fixture_add(r, SUPPLIES, 3, 3));
	require(!world_spelunk_door_add(r, "core.spelunk.door.store", 4, 3));
	r->fixtures[0].state = SPELUNK_FIXTURE_SPENT;
	require(!world_spelunk_runtime_is_valid(r));
	r->fixtures[0].armed = 0; require(world_spelunk_runtime_is_valid(r));
	r->fixtures[1] = r->fixtures[0]; r->fixture_count = 2;
	require(!world_spelunk_runtime_is_valid(r)); r->fixture_count = 1;
	p = spelunking_fixture_parser.init();
	eq(parser_parse(p, "trap:bad id:20:10:4:test"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "trap:test.trap:20:10:0:test"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "trap:test.trap:20:10:4:test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "trap:test.trap:20:10:4:test"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(parser_parse(p, "chest:test.chest:8:none:potion:1:Cure Light Wounds:test"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "discovery:missing:description:note"), PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p, "discovery:test.trap:description:note"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "chest:test.chest:0:none:potion:1:Cure Light Wounds:test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "discovery:test.chest:description:note"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "discovery:test.chest:description:note"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(spelunking_fixture_parser.finish(p), 0);
	p = spelunking_fixture_parser.init();
	eq(spelunking_fixture_parser.run(p), 0); eq(spelunking_fixture_parser.finish(p), 0);
	require(world_spelunk_fixture_data_validate());
	ok;
}

const char *suite_name = "game/spelunking-fixtures";
struct test tests[] = {
	{ "hidden traps search thresholds and no random redraws", test_hidden_and_search },
	{ "movement triggers once including waiting on spent trap", test_trigger_once },
	{ "lethal trap is spent before death", test_lethal_trigger },
	{ "disarm command skill penalty turn and one-time XP", test_disarm },
	{ "failed disarm can recover without triggering", test_safe_failure },
	{ "falling and teleport landing trigger floor traps", test_landing_and_teleport },
	{ "lock attempt and one-time chest loot", test_chest_loot_once },
	{ "chest trap opening disarming and underfoot access", test_trapped_chest },
	{ "creature obstruction and full loot capacity", test_obstruction_and_capacity },
	{ "detection chain range and hidden occupant safety", test_detection_chain },
	{ "trap chest art and empty memory", test_art_and_memory },
	{ "fixture codec corruption and v14 migration", test_codec_and_old_save },
	{ "repeated save reload retains consumed traps and loot", test_save_loot_and_traps },
	{ "active generated cave save retains opened chest", test_active_save },
	{ "128 seed fixture placement and route audit", test_generation },
	{ "discovery loot notes pickup and save without duplication", test_discovery_loot_notes_and_save },
	{ "discovery skips unsafe shelves and cannot repopulate", test_discovery_generation_skip_and_repeat },
	{ "guardian warning retreat wounds save kill and one-time reward", test_guardian_lifecycle },
	{ "external pre-encounter active cave save", test_legacy_encounter_save },
	{ "definition bounds and runtime invariants", test_data_and_invariants },
	{ NULL, NULL }
};
