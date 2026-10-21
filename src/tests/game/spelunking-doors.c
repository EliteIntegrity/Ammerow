/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "spelunking-integration-fixture.h"
#include "world-spelunking-door.h"
#include "world-spelunking-traversal-proof.h"
#include "world-spelunking-view-terrain.h"
#include "cmd-mode-policy.h"
#include "datafile.h"
#include "project.h"
#include "ui-spelunking-appearance.h"

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
	if (!level) return false;
	level->mode = WORLD_MODE_SPELUNKING;
	my_strcpy(player->world_location.entry, "stair.main", sizeof(player->world_location.entry));
	player->spelunking->actor_roster_initialized = true;
	if (id && !world_spelunk_door_add(player->spelunking, id, 4, 3)) return false;
	memset(player->spelunking->explored, 0, 120);
	world_spelunk_visibility_follow_player(player->spelunking);
	return true;
}

static bool door_command(cmd_code code, int direction)
{
	player->upkeep->energy_use = 0;
	if (cmdq_push(code)) return false;
	if (direction) cmd_set_arg_direction(cmdq_peek(), "direction", direction);
	return cmdq_pop(CTX_GAME);
}

static int test_commands(void *unused)
{
	struct world_spelunk_runtime *r;
	int stamina, air, native_feat;
	(void)unused;
	require(corridor("core.spelunk.door.survey"));
	r = player->spelunking;
	stamina = player->resources.stamina.current; air = r->state.breath;
	native_feat = square(cave, player->grid)->feat;
	require(cmd_mode_policy_allows(WORLD_TURN_CONTEXT_SPELUNKING, CTX_GAME, CMD_OPEN));
	require(cmd_mode_policy_allows(WORLD_TURN_CONTEXT_SPELUNKING, CTX_GAME, CMD_CLOSE));
	require(door_command(CMD_OPEN, 0)); /* sole adjacent door: no prompt */
	eq(cmd_get_nrepeats(), 0);
	eq(r->doors[0].state, WORLD_SPELUNK_DOOR_OPEN);
	eq(player->upkeep->energy_use, 100);
	require(door_command(CMD_OPEN, 6));
	eq(player->upkeep->energy_use, 0);
	require(door_command(CMD_CLOSE, 0));
	eq(r->doors[0].state, WORLD_SPELUNK_DOOR_CLOSED);
	eq(player->upkeep->energy_use, 100);
	require(door_command(CMD_CLOSE, 6)); eq(player->upkeep->energy_use, 0);
	require(door_command(CMD_OPEN, 4)); eq(player->upkeep->energy_use, 0);
	eq(square(cave, player->grid)->feat, native_feat);
	eq(player->resources.stamina.current, stamina); eq(r->state.breath, air);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_locks(void *unused)
{
	bool executed;
	int32_t xp;
	(void)unused;
	require(corridor("core.spelunk.door.store"));
	xp = player->exp;
	eq(player->spelunking->doors[0].known, WORLD_SPELUNK_DOOR_CLOSED);
	player->state.skills[SKILL_DISARM_PHYS] = 0;
	rand_fix(100); executed = door_command(CMD_OPEN, 6); rand_unfix();
	require(executed);
	eq(player->spelunking->doors[0].state, WORLD_SPELUNK_DOOR_LOCKED);
	eq(player->spelunking->doors[0].known, WORLD_SPELUNK_DOOR_LOCKED);
	eq(player->upkeep->energy_use, 100);
	rand_fix(0); executed = door_command(CMD_OPEN, 6); rand_unfix();
	require(executed);
	eq(player->spelunking->doors[0].state, WORLD_SPELUNK_DOOR_OPEN);
	eq(player->spelunking->doors[0].known, WORLD_SPELUNK_DOOR_OPEN);
	require(door_command(CMD_CLOSE, 6));
	eq(player->spelunking->doors[0].state, WORLD_SPELUNK_DOOR_CLOSED);
	eq(player->exp, xp); /* no lock/unlock XP farm or automatic re-lock */
	ok;
}

static int test_obstruction(void *unused)
{
	struct world_spelunk_runtime *r;
	struct world_spelunk_command right = { WORLD_SPELUNK_COMMAND_MOVE, 1, 0 };
	struct world_spelunk_action_report action;
	struct world_spelunk_actor_report reports[WORLD_SPELUNK_ACTOR_MAX];
	struct world_spelunk_projectile_report shot;
	size_t hits, count;
	(void)unused;
	require(corridor("core.spelunk.door.survey")); r = player->spelunking;
	require(!world_spelunk_visibility_line_is_clear(r, 3, 3, 6, 3));
	eq(world_spelunk_apply_command(&r->state, &right, &action), WORLD_SPELUNK_ACTION_REJECTED);
	eq(r->state.x, 3);
	require(world_spelunk_runtime_add_actor(r, WORLD_SPELUNK_CHASM_SKITTER_ID, 6, 3));
	eq(world_spelunk_player_projectile(player, player->gear, NULL, loc(8,3),
		false, 10, 1, &shot), WORLD_SPELUNK_PROJECTILE_TURN);
	eq(shot.path_length, 0);
	player->upkeep->energy_use = 0;
	require(world_spelunk_player_project_ray(player, 1, 0, 10, 99, PROJ_MISSILE, true, &hits));
	eq(hits, 0); eq(r->actor_count, 1);
	r->actors[0].energy = 100;
	require(world_spelunk_process_actors(player, reports, WORLD_SPELUNK_ACTOR_MAX, &count));
	eq(r->actors[0].x, 6);
	require(world_spelunk_door_set_open(r, 4, 3, true));
	require(world_spelunk_visibility_line_is_clear(r, 3, 3, 6, 3));
	require(world_spelunk_process_actors(player, reports, WORLD_SPELUNK_ACTOR_MAX, &count));
	eq(r->actors[0].x, 5);
	r->actors[0].energy = 100;
	require(world_spelunk_process_actors(player, reports, WORLD_SPELUNK_ACTOR_MAX, &count));
	eq(r->actors[0].x, 4);
	require(door_command(CMD_CLOSE, 6)); eq(player->upkeep->energy_use, 0);
	require(world_spelunk_runtime_remove_actor(r, &r->actors[0]));
	eq(world_spelunk_apply_command(&r->state, &right, &action), WORLD_SPELUNK_ACTION_TURN);
	eq(r->state.x, 4);
	require(!world_spelunk_door_set_open(r, 4, 3, false));
	eq(world_spelunk_runtime_place_piton(r), WORLD_SPELUNK_INFRASTRUCTURE_NO_BRACE);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_knowledge_and_codec(void *unused)
{
	struct world_spelunk_runtime *r, *decoded = NULL;
	struct world_spelunk_inspect inspect;
	struct world_spelunk_view view;
	struct world_spelunk_view_terrain terrain;
	uint8_t payload[4096], view_cells[120];
	size_t n, i;
	int attr; wchar_t glyph;
	(void)unused;
	require(corridor("core.spelunk.door.survey")); r = player->spelunking;
	require(!world_spelunk_visibility_is_explored(r, 6, 3));
	require(world_spelunk_view_capture(r, 20, 6, view_cells, 120, &view));
	eq(world_spelunk_view_cell_at(&view, 4, 3), WORLD_SPELUNK_VIEW_DOOR_CLOSED);
	spelunking_view_cell_appearance(&view, 4, 3, &attr, &glyph); eq(glyph, L'+');
	world_spelunk_view_terrain_at(r, &view, 4, 3, &terrain);
	require(streq(terrain.overlay, "door-closed"));
	r->state.x = 18; world_spelunk_visibility_follow_player(r);
	require(world_spelunk_door_set_open(r, 4, 3, true));
	eq(r->doors[0].known, WORLD_SPELUNK_DOOR_CLOSED); /* remote change stays unknown */
	require(world_spelunk_view_inspect(r, 4, 3, &inspect));
	eq(inspect.knowledge, WORLD_SPELUNK_INSPECT_REMEMBERED);
	eq(inspect.kind, WORLD_SPELUNK_INSPECT_DOOR);
	eq(inspect.door_state, WORLD_SPELUNK_DOOR_CLOSED);
	eq(world_spelunk_runtime_encode(r, payload, sizeof(payload), &n), WORLD_SPELUNK_CODEC_OK);
	eq(n, world_spelunk_runtime_encoded_size(r));
	eq(world_spelunk_runtime_decode(payload, n, &decoded), WORLD_SPELUNK_CODEC_OK);
	eq(decoded->doors[0].state, WORLD_SPELUNK_DOOR_OPEN);
	eq(decoded->doors[0].known, WORLD_SPELUNK_DOOR_CLOSED);
	world_spelunk_runtime_free(decoded); decoded = NULL;
	for (i = n - (8 + strlen(r->doors[0].id)); i < n; i++) {
		require(world_spelunk_runtime_decode(payload, i, &decoded) != WORLD_SPELUNK_CODEC_OK);
		null(decoded);
	}
	payload[n-2] = 255;
	eq(world_spelunk_runtime_decode(payload, n, &decoded), WORLD_SPELUNK_CODEC_CORRUPT);
	r->state.x = 3; world_spelunk_visibility_follow_player(r);
	eq(r->doors[0].known, WORLD_SPELUNK_DOOR_OPEN);
	require(world_spelunk_view_capture(r, 20, 6, view_cells, 120, &view));
	spelunking_view_cell_appearance(&view, 4, 3, &attr, &glyph); eq(glyph, L'\'');
	world_spelunk_view_terrain_at(r, &view, 4, 3, &terrain);
	require(streq(terrain.overlay, "door-open"));
	ok;
}

static int test_old_payload(void *unused)
{
	uint8_t payload[4096]; size_t n;
	struct world_spelunk_runtime *decoded = NULL;
	(void)unused;
	require(corridor(NULL));
	eq(world_spelunk_runtime_encode(player->spelunking, payload, sizeof(payload), &n), WORLD_SPELUNK_CODEC_OK);
	eq(payload[n-1], 0);
	payload[4] = WORLD_SPELUNK_PAYLOAD_VERSION_DIRECTIONAL_STAMINA;
	eq(world_spelunk_runtime_decode(payload, n-2, &decoded), WORLD_SPELUNK_CODEC_OK);
	eq(decoded->door_count, 0);
	require(world_spelunk_runtime_is_valid(decoded));
	world_spelunk_runtime_free(decoded);
	ok;
}

static int test_save_and_invariants(void *unused)
{
	struct world_spelunk_runtime *r;
	struct object *obj;
	int i;
	(void)unused;
	require(corridor("core.spelunk.door.store")); r = player->spelunking;
	require(!world_spelunk_door_add(r, "missing.door", 8, 3));
	require(!world_spelunk_door_add(r, "core.spelunk.door.survey", 4, 3));
	require(!world_spelunk_door_add(r, "core.spelunk.door.survey", -1, 3));
	require(!world_spelunk_door_add(r, "core.spelunk.door.survey", 3, 3));
	r->blocked[3*20+4] = 0; require(!world_spelunk_runtime_is_valid(r));
	world_spelunk_doors_rebuild(r);
	require(world_spelunk_door_set_open(r, 4, 3, true));
	obj = make_test_object(lookup_kind(TV_FOOD, lookup_sval(TV_FOOD, "Ration of Food")), 1);
	notnull(obj);
	require(world_spelunk_runtime_add_ground_object(r, obj, 4, 3));
	require(door_command(CMD_CLOSE, 6)); eq(player->upkeep->energy_use, 0);
	eq(r->doors[0].state, WORLD_SPELUNK_DOOR_OPEN);
	/* The full save owns the loose object and the now-unlocked door together. */
	/* This small synthetic runtime is dormant, not a live cave envelope. */
	((struct level *)world_player_level(player))->mode = WORLD_MODE_TOP_DOWN;
	for (i = 0; i < 2; i++) {
		require(savefile_save("TestSpelunk"));
		reset_before_load(); require(savefile_load("TestSpelunk", false));
		r = player->spelunking; notnull(r);
		eq(r->door_count, 1);
		eq(r->doors[0].state, WORLD_SPELUNK_DOOR_OPEN);
		require(streq(r->doors[0].id, "core.spelunk.door.store"));
		notnull(world_spelunk_runtime_ground_object_at(r, 4, 3));
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
	int x, y, door_x = -1, door_y = -1, i;
	(void)unused;
	reset_before_load();
	require(savefile_load("Test1", false));
	level = level_by_id("core.cave.east.001");
	entry = world_entry_by_id(level, "shaft.spelunk");
	notnull(entry);
	cave->depth = player->cave->depth = level->danger;
	require(world_chunk_set_location(cave, level->id, false));
	require(world_chunk_set_location(player->cave, level->id, true));
	require(world_player_set_location(player, level->id, entry->id));
	square_set_mon(cave, player->grid, 0);
	square_set_feat(cave, entry->grid, FEAT_HOLE);
	square_set_feat(player->cave, entry->grid, FEAT_FLOOR);
	player_place(cave, player, entry->grid);
	eq(world_spelunking_entry_execute(player, cave,
		"core.route.spelunk.east.in", &report), WORLD_SPELUNKING_ENTRY_OK);
	require(world_spelunking_active_storage_is_valid(player));
	r = player->spelunking;
	/* Add a locked fixture to the real generated section for this test. */
	for (y = 1; y < r->state.map.height - 1 && door_x < 0; y++) {
		for (x = 1; x < r->state.map.width - 1; x++) {
			if (world_spelunk_door_add(r, "core.spelunk.door.store", x, y)) {
				door_x = x; door_y = y;
				break;
			}
		}
	}
	require(door_x >= 0);
	for (i = 0; i < 2; i++) {
		require(savefile_save("TestSpelunkActive"));
		reset_before_load(); require(savefile_load("TestSpelunkActive", false));
		require(world_spelunking_active_storage_is_valid(player));
		r = player->spelunking;
		notnull(world_spelunk_door_at(r, door_x, door_y));
		eq(world_spelunk_door_at(r, door_x, door_y)->state,
			i ? WORLD_SPELUNK_DOOR_OPEN : WORLD_SPELUNK_DOOR_LOCKED);
		if (!i) require(world_spelunk_door_set_open(r, door_x, door_y, true));
	}
	ok;
}

static int test_mapping(void *unused)
{
	struct world_spelunk_runtime *r;
	(void)unused;
	require(corridor("core.spelunk.door.store")); r = player->spelunking;
	r->state.x = 18;
	r->doors[0].known = WORLD_SPELUNK_DOOR_UNKNOWN;
	memset(r->explored, 0, 120);
	world_spelunk_visibility_follow_player(r);
	world_spelunk_visibility_reveal_area(r, 1, 2);
	eq(r->doors[0].known, WORLD_SPELUNK_DOOR_UNKNOWN);
	world_spelunk_visibility_reveal_layout(r);
	eq(r->doors[0].known, WORLD_SPELUNK_DOOR_CLOSED);
	require(!world_spelunk_visibility_is_visible(r, 4, 3));
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_generation(void *unused)
{
	const struct world_spelunk_system_profile *profile = world_spelunk_system_profile_by_id("deepwell");
	struct randomizer_state before, after;
	unsigned doors = 0, sections = 0;
	uint32_t seed;
	(void)unused;
	Rand_state_export(&before);
	for (seed = 0; seed < 16; seed++) {
		struct world_spelunk_system *s = NULL;
		size_t i, n;
		eq(world_spelunk_publish_system_candidate(profile, seed, 100, "shaft.surface", &s), WORLD_SPELUNK_PUBLICATION_OK);
		for (i = 0; i < s->edge_count; i++) {
			eq(world_spelunk_materialize_node_candidate(s, s->edges[i].node_b_id, 100,
				s->edges[i].endpoint_b_id), WORLD_SPELUNK_PUBLICATION_OK);
		}
		for (n = 0; n < s->node_count; n++) {
			struct world_spelunk_runtime *r = s->nodes[n].runtime;
			struct world_spelunk_generated_layout g;
			sections++; doors += (unsigned)r->door_count;
			require(r->door_count <= 1);
			eq(world_spelunk_reconstruct_node_candidate(s, s->nodes[n].id, 100, &g), WORLD_SPELUNK_RECONSTRUCTION_OK);
			for (i = 0; i < r->door_count; i++) {
				const struct world_spelunk_door *d = &r->doors[i];
				size_t j;
				require(world_spelunk_door_site(r, d->x, d->y));
				require(abs(d->x-r->state.x)+abs(d->y-r->state.y) > 4);
				/* Certified stations and endpoints remain entirely free. */
				for (j = 0; j < g.route_visit_count; j++) {
					require(abs(d->x-g.route_visits[j].x)+abs(d->y-g.route_visits[j].y) > 4);
				}
				for (j = 0; j < g.witness_step_count; j++) {
					require(abs(d->x-g.witness_steps[j].expected_x) > 1 ||
						abs(d->y-g.witness_steps[j].expected_y) > 1);
				}
				require(world_spelunk_door_set_open(r, d->x, d->y, true));
				require(world_spelunk_runtime_is_valid(r));
			}
			world_spelunk_generated_layout_dispose(&g);
		}
		world_spelunk_system_free(s);
	}
	Rand_state_export(&after); require(memcmp(&before, &after, sizeof(before)) == 0);
	if (verbose) printf("\nCave doors: %u in %u sections over 16 seeds\n", doors, sections);
	require(doors > 0);
	ok;
}

static int test_data(void *unused)
{
	struct parser *p = spelunking_door_parser.init();
	(void)unused;
	eq(parser_parse(p, "door:bad id:0:test"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "door:test.door:8:test"), PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p, "door:test.door:0:test"), PARSE_ERROR_NONE);
	eq(parser_parse(p, "door:test.door:0:duplicate"), PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(spelunking_door_parser.finish(p), 0);
	p = spelunking_door_parser.init();
	eq(spelunking_door_parser.run(p), 0); eq(spelunking_door_parser.finish(p), 0);
	notnull(world_spelunk_door_by_id("core.spelunk.door.survey"));
	ok;
}

const char *suite_name = "game/spelunking-doors";
struct test tests[] = {
	{ "open and close dispatch without native map changes", test_commands },
	{ "locks spend turns without duplicate XP", test_locks },
	{ "movement sight projectiles creatures and closure", test_obstruction },
	{ "knowledge art codec and corruption", test_knowledge_and_codec },
	{ "version thirteen migration", test_old_payload },
	{ "full save reload and occupied doorway invariants", test_save_and_invariants },
	{ "active generated cave save reload", test_active_save },
	{ "mapping learns outlines but not locks", test_mapping },
	{ "sixteen seed generation audit", test_generation },
	{ "door data bounds and duplicate IDs", test_data },
	{ NULL, NULL }
};
