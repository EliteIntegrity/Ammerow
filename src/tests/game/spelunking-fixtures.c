/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "spelunking-integration-fixture.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-door.h"
#include "world-spelunking-view-terrain.h"
#include "world-spelunking-traversal-proof.h"
#include "effect-handler.h"
#include "effects.h"
#include "datafile.h"
#include "ui-spelunking-appearance.h"

#define SPRING "core.spelunk.trap.spring"
#define STONE "core.spelunk.trap.stone"
#define SUPPLIES "core.spelunk.chest.supplies"
#define STORE "core.spelunk.chest.store"

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

static int test_active_save(void *unused)
{
	const struct level *level;
	const struct world_entry *entry;
	struct world_spelunk_runtime *r;
	struct world_spelunking_entry_report report;
	size_t chest, i;
	reset_before_load(); require(savefile_load("Test1", false));
	(void)unused;
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
	for (i = 0; i < 2; i++) {
		require(savefile_save("TestSpelunkActive"));
		reset_before_load(); require(savefile_load("TestSpelunkActive", false));
		require(world_spelunking_active_storage_is_valid(player)); r = player->spelunking;
		eq(r->fixtures[chest].state, i ? SPELUNK_FIXTURE_SPENT : SPELUNK_FIXTURE_LOCKED);
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
	ok;
}

static int test_generation(void *unused)
{
	const struct world_spelunk_system_profile *profile = world_spelunk_system_profile_by_id("deepwell");
	struct randomizer_state before, after;
	unsigned traps = 0, chests = 0, sections = 0; uint32_t seed;
	(void)unused;
	Rand_state_export(&before);
	for (seed = 0; seed < 16; seed++) {
		struct world_spelunk_system *s = NULL; size_t i, n;
		eq(world_spelunk_publish_system_candidate(profile, seed, 100, "shaft.surface", &s), WORLD_SPELUNK_PUBLICATION_OK);
		for (i = 0; i < s->edge_count; i++) {
			eq(world_spelunk_materialize_node_candidate(s, s->edges[i].node_b_id, 100,
				s->edges[i].endpoint_b_id), WORLD_SPELUNK_PUBLICATION_OK);
		}
		for (n = 0; n < s->node_count; n++) {
			struct world_spelunk_runtime *r = s->nodes[n].runtime;
			struct world_spelunk_generated_layout g;
			sections++; require(r->fixture_count <= 2);
			eq(world_spelunk_reconstruct_node_candidate(s, s->nodes[n].id, 100, &g), WORLD_SPELUNK_RECONSTRUCTION_OK);
			for (i = 0; i < r->fixture_count; i++) {
				const struct world_spelunk_fixture *f = &r->fixtures[i]; size_t j;
				if (world_spelunk_fixture_by_id(f->id)->kind == SPELUNK_TRAP) traps++; else chests++;
				require(abs(f->x-r->state.x)+abs(f->y-r->state.y) > 4);
				for (j = 0; j < g.route_visit_count; j++) {
					require(abs(f->x-g.route_visits[j].x)+abs(f->y-g.route_visits[j].y) > 4);
				}
				for (j = 0; j < g.witness_step_count; j++) {
					require(abs(f->x-g.witness_steps[j].expected_x) > 1 || abs(f->y-g.witness_steps[j].expected_y) > 1);
				}
			}
			require(world_spelunk_runtime_is_valid(r));
			world_spelunk_generated_layout_dispose(&g);
		}
		world_spelunk_system_free(s);
	}
	Rand_state_export(&after); require(memcmp(&before, &after, sizeof(before)) == 0);
	if (verbose) printf("\nCave fixtures: %u traps, %u chests in %u sections over 16 seeds\n", traps, chests, sections);
	require(traps > 0); require(chests > 0);
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
	{ "sixteen seed fixture placement and route audit", test_generation },
	{ "definition bounds and runtime invariants", test_data_and_invariants },
	{ NULL, NULL }
};
