/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "spelunking-integration-fixture.h"
#include "world-spelunking-terrain.h"
#include "world-spelunking-fixture.h"
#include "world-spelunking-door.h"
#include "world-spelunking-traversal-proof.h"
#include "world-spelunking-view-terrain.h"
#include "player-calcs.h"
#include "effect-handler.h"
#include "effects.h"
#include "project.h"
#include "datafile.h"

static bool cavern(void)
{
	enum world_spelunk_tile cells[240];
	struct level *level;
	int x,y;
	reset_before_load();
	if (!savefile_load("Test1",false)) return false;
	for (y=0;y<12;y++) for (x=0;x<20;x++)
		cells[y*20+x] = !x || x==19 || !y || y>=10 || y==4 ? WORLD_SPELUNK_ROCK : WORLD_SPELUNK_AIR;
	cells[3*20+4] = WORLD_SPELUNK_ROCK;
	if (!install_spelunking_runtime(cells,20,12,3,3,90)) return false;
	level = (struct level *)world_player_level(player);
	level->mode = WORLD_MODE_SPELUNKING;
	my_strcpy(player->world_location.entry,"stair.main",sizeof(player->world_location.entry));
	player->spelunking->actor_roster_initialized = true;
	player->state.skills[SKILL_DIGGING] = 2000;
	return world_spelunk_player_is_active(player);
}

static bool tunnel(int dir, int random)
{
	bool result;
	player->upkeep->energy_use=0;
	if (cmdq_push(CMD_TUNNEL)) return false;
	cmd_set_arg_direction(cmdq_peek(),"direction",dir);
	rand_fix(random);
	result=cmdq_pop(CTX_GAME);
	rand_unfix();
	return result;
}

static int test_command(void *unused)
{
	struct world_spelunk_runtime *r;
	struct randomizer_state before,after;
	int stamina,feat,cost;
	(void)unused;
	require(cavern()); r=player->spelunking;
	cost=z_info->stamina_dig_cost ? z_info->stamina_dig_cost : 5;
	stamina=player_stamina_current(player); feat=square(cave,player->grid)->feat;
	Rand_state_export(&before);
	eq(world_spelunk_cut_check(player,4,3,cost),SPELUNK_CUT_OK);
	Rand_state_export(&after); require(!memcmp(&before,&after,sizeof(before)));
	eq(r->cells[3*20+4],WORLD_SPELUNK_ROCK);
	require(tunnel(6,0)); eq(r->cells[3*20+4],WORLD_SPELUNK_AIR);
	eq(player_stamina_current(player),stamina-cost);
	eq(player->upkeep->energy_use,100); eq(cmd_get_nrepeats(),0);
	eq(square(cave,player->grid)->feat,feat);
	require(world_spelunk_runtime_is_valid(r));
	require(tunnel(6,0)); eq(player->upkeep->energy_use,0);
	eq(player_stamina_current(player),stamina-cost);
	ok;
}

static int test_failure_exhaustion_and_skill(void *unused)
{
	int stamina,cost;
	(void)unused;
	require(cavern());
	cost=z_info->stamina_dig_cost ? z_info->stamina_dig_cost : 5;
	player->state.skills[SKILL_DIGGING]=41;
	stamina=player_stamina_current(player);
	require(tunnel(6,100)); eq(player->upkeep->energy_use,100);
	eq(player_stamina_current(player),stamina-cost);
	eq(player->spelunking->cells[3*20+4],WORLD_SPELUNK_ROCK);
	player->resources.stamina.current=0;
	require(tunnel(6,0)); eq(player->upkeep->energy_use,0);
	eq(player_stamina_current(player),0);
	player->resources.stamina.current=50;
	player->state.skills[SKILL_DIGGING]=0;
	require(tunnel(6,0)); eq(player->upkeep->energy_use,0);
	eq(player_stamina_current(player),50);
	ok;
}

static int test_falls(void *unused)
{
	struct world_spelunk_runtime *r;
	int hp;
	(void)unused;
	require(cavern()); r=player->spelunking; player->chp=1000; hp=player->chp;
	require(tunnel(2,0)); eq(r->state.y,9); eq(player->chp,hp-80);
	eq(r->state.movement,WORLD_SPELUNK_STANDING);
	require(world_spelunk_runtime_is_valid(r));
	require(cavern()); r=player->spelunking; player->chp=1;
	require(tunnel(2,0)); require(player->is_dead); eq(r->state.y,9);
	eq(r->cells[4*20+3],WORLD_SPELUNK_AIR);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_rope_catch_and_last_stamina(void *unused)
{
	struct world_spelunk_runtime *r;
	int hp,cost;
	(void)unused;
	require(cavern()); r=player->spelunking;
	/* A previously placed rope passing the ledge arrests the new fall. */
	r->pitons[2*20+3]=1; r->cells[2*20+2]=WORLD_SPELUNK_ROCK;
	r->ropes[3*20+3]=1;
	hp=player->chp; require(tunnel(2,0)); eq(r->state.y,3);
	eq(r->state.movement,WORLD_SPELUNK_HANGING); eq(player->chp,hp);
	require(tunnel(6,0)); eq(player->upkeep->energy_use,0);
	require(cavern()); r=player->spelunking; r->ropes[3*20+3]=1;
	cost=z_info->stamina_dig_cost ? z_info->stamina_dig_cost : 5;
	player->resources.stamina.current=(int16_t)cost; player->chp=1000;
	require(tunnel(2,0)); eq(r->state.y,9); eq(player_stamina_current(player),0);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_anchor_protection(void *unused)
{
	struct world_spelunk_runtime *r;
	(void)unused;
	require(cavern()); r=player->spelunking;
	r->cells[2*20+4]=WORLD_SPELUNK_ROCK;
	r->pitons[2*20+5]=1; r->ropes[3*20+5]=1;
	eq(world_spelunk_cut_check(player,4,2,0),SPELUNK_CUT_ANCHOR);
	eq(r->cells[2*20+4],WORLD_SPELUNK_ROCK);
	r->cells[1*20+5]=WORLD_SPELUNK_ROCK;
	eq(world_spelunk_cut_rock(player,4,2),SPELUNK_CUT_OK);
	eq(r->pitons[2*20+5],1); eq(r->ropes[3*20+5],1);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_water_and_perimeter(void *unused)
{
	struct world_spelunk_runtime *r;
	(void)unused;
	require(cavern()); r=player->spelunking;
	r->cells[3*20+8]=WORLD_SPELUNK_WATER;
	eq(world_spelunk_cut_check(player,8,4,0),SPELUNK_CUT_WATER);
	r->cells[3*20+7]=WORLD_SPELUNK_ROCK;
	eq(world_spelunk_cut_check(player,7,3,0),SPELUNK_CUT_WATER);
	eq(world_spelunk_cut_check(player,0,3,0),SPELUNK_CUT_BOUNDARY);
	eq(world_spelunk_cut_check(player,3,11,0),SPELUNK_CUT_BOUNDARY);
	eq(world_spelunk_cut_check(player,-1,3,0),SPELUNK_CUT_BOUNDARY);
	eq(r->cells[3*20+8],WORLD_SPELUNK_WATER);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_fixture_foundations(void *unused)
{
	struct world_spelunk_runtime *r;
	(void)unused;
	require(cavern()); r=player->spelunking;
	require(world_spelunk_fixture_add(r,"core.spelunk.chest.supplies",6,3));
	require(world_spelunk_fixture_add(r,"core.spelunk.trap.spring",8,3));
	eq(world_spelunk_cut_check(player,6,4,0),SPELUNK_CUT_FIXTURE);
	eq(world_spelunk_cut_check(player,8,4,0),SPELUNK_CUT_FIXTURE);
	r->cells[2*20+12]=WORLD_SPELUNK_ROCK;
	require(world_spelunk_door_add(r,"core.spelunk.door.store",12,3));
	eq(world_spelunk_cut_check(player,12,2,0),SPELUNK_CUT_FIXTURE);
	eq(world_spelunk_cut_check(player,11,4,0),SPELUNK_CUT_FIXTURE);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_objects_and_creatures(void *unused)
{
	struct world_spelunk_runtime *r;
	struct object *obj;
	(void)unused;
	require(cavern()); r=player->spelunking;
	obj=make_test_object(lookup_kind(TV_POTION,lookup_sval(TV_POTION,"Cure Light Wounds")),2); notnull(obj);
	require(world_spelunk_runtime_add_ground_object(r,obj,6,3));
	eq(world_spelunk_cut_rock(player,6,4),SPELUNK_CUT_OK);
	eq(obj->grid.y,9); eq(obj->known->grid.y,9); eq(obj->number,2);
	require(world_spelunk_runtime_add_actor(r,WORLD_SPELUNK_CHASM_SKITTER_ID,8,3));
	r->cells[7*20+8]=WORLD_SPELUNK_ROCK; /* Three-cell fall: 10 HP. */
	eq(world_spelunk_cut_rock(player,8,4),SPELUNK_CUT_OK);
	if (r->actor_count) { eq(r->actors[0].y,6); eq(r->actors[0].hp,r->actors[0].max_hp-10); }
	require(world_spelunk_runtime_add_actor(r,WORLD_SPELUNK_CHASM_SKITTER_ID,10,3));
	eq(world_spelunk_cut_rock(player,10,4),SPELUNK_CUT_OK);
	null(world_spelunk_runtime_actor_at(r,10,3)); null(world_spelunk_runtime_actor_at(r,10,9));
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_unsafe_landings(void *unused)
{
	struct world_spelunk_runtime *r;
	struct object *obj;
	(void)unused;
	require(cavern()); r=player->spelunking;
	obj=make_test_object(lookup_kind(TV_POTION,lookup_sval(TV_POTION,"Cure Light Wounds")),1); notnull(obj);
	require(world_spelunk_runtime_add_ground_object(r,obj,6,3));
	r->cells[9*20+6]=WORLD_SPELUNK_WATER;
	eq(world_spelunk_cut_check(player,6,4,0),SPELUNK_CUT_OCCUPIED); eq(obj->grid.y,3);
	require(world_spelunk_runtime_add_actor(r,WORLD_SPELUNK_CHASM_SKITTER_ID,3,9));
	eq(world_spelunk_cut_check(player,3,4,0),SPELUNK_CUT_OCCUPIED);
	eq(r->cells[4*20+3],WORLD_SPELUNK_ROCK);
	ok;
}

static int test_geology_and_persistence(void *unused)
{
	struct world_spelunk_runtime *r,*decoded=NULL;
	uint8_t mats[240]={0},decs[240]={0},payload[8192];
	size_t n; int i;
	(void)unused;
	require(cavern()); r=player->spelunking;
	eq(world_spelunk_dig_chance(r,4,3,INT_MAX),1600);
	for(i=0;i<240;i++) if(r->cells[i]==WORLD_SPELUNK_ROCK) mats[i]=1;
	mats[3*20+4]=2; decs[2*20+4]=3;
	require(world_spelunk_runtime_set_geology(r,"deepwell.limestone-shale",1,mats,decs));
	eq(world_spelunk_dig_chance(r,4,3,60),200);
	eq(world_spelunk_dig_chance(r,3,4,60),80);
	eq(world_spelunk_dig_chance(r,4,3,INT_MAX),1600);
	eq(world_spelunk_cut_rock(player,4,3),SPELUNK_CUT_OK);
	eq(r->material_tags[3*20+4],0); eq(r->decoration_tags[3*20+4],0);
	eq(r->decoration_tags[2*20+4],0);
	require(world_spelunk_runtime_is_valid(r));
	eq(world_spelunk_runtime_encode(r,payload,sizeof(payload),&n),WORLD_SPELUNK_CODEC_OK);
	eq(world_spelunk_runtime_decode(payload,n,&decoded),WORLD_SPELUNK_CODEC_OK);
	eq(decoded->cells[3*20+4],WORLD_SPELUNK_AIR); eq(decoded->material_tags[3*20+4],0);
	world_spelunk_runtime_free(decoded);
	((struct level *)world_player_level(player))->mode=WORLD_MODE_TOP_DOWN;
	for(i=0;i<2;i++) {
		require(savefile_save("TestSpelunk")); reset_before_load(); require(savefile_load("TestSpelunk",false));
		eq(player->spelunking->cells[3*20+4],WORLD_SPELUNK_AIR);
		eq(player->spelunking->decoration_tags[2*20+4],0);
	}
	ok;
}

static int test_magic_policy(void *unused)
{
	struct effect wall={0},heal={0};
	effect_handler_context_t context={.effect=EF_LINE,.origin=source_player(),.dir=6,.subtype=PROJ_KILL_WALL};
	(void)unused;
	require(cavern()); wall.index=EF_LINE; wall.subtype=PROJ_KILL_WALL;
	require(world_spelunk_effect_chain_is_supported(&wall));
	heal.index=EF_HEAL_HP; wall.next=&heal;
	require(!world_spelunk_effect_chain_is_supported(&wall));
	wall.next=NULL; heal.next=&wall;
	require(!world_spelunk_effect_chain_is_supported(&heal));
	wall.index=EF_SHORT_BEAM; require(world_spelunk_effect_chain_is_supported(&wall));
	wall.next=&heal; heal.next=NULL; require(!world_spelunk_effect_chain_is_supported(&wall));
	wall.next=NULL; wall.index=EF_BALL; require(!world_spelunk_effect_chain_is_supported(&wall));
	eq(world_spelunk_effect_dispatch(&context),WORLD_SPELUNK_EFFECT_COMPLETED);
	eq(player->spelunking->cells[3*20+4],WORLD_SPELUNK_AIR);
	require(context.ident); require(world_spelunk_runtime_is_valid(player->spelunking));
	ok;
}

static int test_magic_limits(void *unused)
{
	struct world_spelunk_runtime *r;
	effect_handler_context_t short_ray={.effect=EF_SHORT_BEAM,.origin=source_player(),.dir=6,
		.subtype=PROJ_KILL_WALL,.radius=1};
	effect_handler_context_t ray={.effect=EF_LINE,.origin=source_player(),.dir=6,.subtype=PROJ_KILL_WALL};
	(void)unused;
	require(cavern()); r=player->spelunking;
	r->cells[3*20+4]=WORLD_SPELUNK_AIR; r->cells[3*20+5]=WORLD_SPELUNK_ROCK;
	eq(world_spelunk_effect_dispatch(&short_ray),WORLD_SPELUNK_EFFECT_COMPLETED);
	eq(r->cells[3*20+5],WORLD_SPELUNK_ROCK);
	r->cells[3*20+6]=WORLD_SPELUNK_WATER;
	eq(world_spelunk_effect_dispatch(&ray),WORLD_SPELUNK_EFFECT_COMPLETED);
	eq(r->cells[3*20+5],WORLD_SPELUNK_ROCK); eq(r->cells[3*20+6],WORLD_SPELUNK_WATER);
	r->cells[3*20+6]=WORLD_SPELUNK_AIR;
	eq(world_spelunk_effect_dispatch(&ray),WORLD_SPELUNK_EFFECT_COMPLETED);
	eq(r->cells[3*20+5],WORLD_SPELUNK_AIR);
	require(world_spelunk_runtime_is_valid(r));
	ok;
}

static int test_wand_command(void *unused)
{
	struct object_kind *kind;
	struct object *wand;
	struct chunk *native_cave;
	struct effect *effect, extra = {0};
	int stamina, charges;
	bool executed;
	(void)unused;
	require(cavern());
	kind = lookup_kind(TV_WAND, lookup_sval(TV_WAND, "Stone to Mud"));
	notnull(kind);
	wand = make_test_object(kind, 1); notnull(wand);
	wand->pval = wand->known->pval = 3;
	inven_carry(player, wand, true, false);
	wand = carried_kind_object(player, kind); notnull(wand);
	stamina = player_stamina_current(player);
	charges = wand->pval;
	native_cave = cave;
	eq(cmdq_push(CMD_USE_WAND), 0);
	cmd_set_arg_item(cmdq_peek(), "item", wand);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	/* No native map is available: the real item command must use cave terrain. */
	cave = NULL;
	rand_fix(100);
	executed = cmdq_pop(CTX_GAME);
	rand_unfix();
	cave = native_cave;
	require(executed);
	eq(player->spelunking->cells[3*20+4], WORLD_SPELUNK_AIR);
	eq(wand->pval, charges-1);
	eq(player->upkeep->energy_use, z_info->move_energy);
	eq(player_stamina_current(player), stamina);
	/* Unsafe chains must be rejected before device chance, charge or time. */
	effect = object_effect(wand); notnull(effect); null(effect->next);
	extra.index = EF_HEAL_HP;
	player->upkeep->energy_use = 0;
	eq(cmdq_push(CMD_USE_WAND), 0);
	cmd_set_arg_item(cmdq_peek(), "item", wand);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	effect->next = &extra;
	executed = cmdq_pop(CTX_GAME);
	effect->next = NULL;
	require(executed);
	eq(wand->pval, charges-1);
	eq(player->upkeep->energy_use, 0);
	ok;
}

static bool find_cut(struct world_spelunk_generated_layout *g,int *cx,int *cy)
{
	int x,y;
	for(y=2;y<g->height-2;y++) for(x=2;x<g->width-2;x++) {
		size_t i; bool near=false;
		if(g->cells[y*g->width+x]!=WORLD_SPELUNK_ROCK ||
			g->cells[y*g->width+x-1]!=WORLD_SPELUNK_AIR ||
			g->cells[(y+1)*g->width+x-1]!=WORLD_SPELUNK_ROCK) continue;
		for(i=0;i<g->witness_step_count;i++) if(abs(x-g->witness_steps[i].expected_x)<=1 &&
			abs(y-g->witness_steps[i].expected_y)<=1) { near=true; break; }
		if(!near && world_spelunk_cut_check(player,x,y,0)==SPELUNK_CUT_OK) {
			*cx=x; *cy=y; return true;
		}
	}
	return false;
}

static int test_generated_routes(void *unused)
{
	uint32_t seed; int cuts=0;
	(void)unused;
	for(seed=0;seed<16;seed++) {
		struct world_spelunk_system *s=NULL;
		struct world_spelunk_generated_layout g;
		struct world_spelunk_traversal_witness witness;
		struct world_spelunk_proof_report proof;
		struct world_spelunk_runtime *r;
		size_t n, edge;
		int x,y;
		require(cavern()); player_spelunking_clear(player);
		eq(world_spelunk_publish_system_candidate(world_spelunk_system_profile_by_id("deepwell"),seed,100,
			"shaft.surface",&s),WORLD_SPELUNK_PUBLICATION_OK);
		player->spelunking_system=s; player->spelunking=r=world_spelunk_system_active_runtime(s);
		((struct level *)world_player_level(player))->mode=WORLD_MODE_TOP_DOWN;
		require(world_player_set_location(player,level_by_id(r->location_id)->id,"shaft.surface"));
		require(world_spelunk_player_is_active(player));
		for (edge=0;edge<s->edge_count;edge++) {
			eq(world_spelunk_materialize_node_candidate(s,s->edges[edge].node_b_id,100,
				s->edges[edge].endpoint_b_id),WORLD_SPELUNK_PUBLICATION_OK);
		}
		for (n=0;n<s->node_count;n++) {
			require(world_spelunk_system_set_active_node(s,s->nodes[n].id));
			player->spelunking=r=world_spelunk_system_active_runtime(s);
			eq(world_spelunk_reconstruct_node_candidate(s,s->active_node_id,100,&g),WORLD_SPELUNK_RECONSTRUCTION_OK);
			require(find_cut(&g,&x,&y));
			eq(world_spelunk_cut_rock(player,x,y),SPELUNK_CUT_OK); cuts++;
			memcpy(g.cells,r->cells,(size_t)g.width*g.height*sizeof(*g.cells));
			memcpy(g.material_tags,r->material_tags,(size_t)g.width*g.height);
			memcpy(g.decoration_tags,r->decoration_tags,(size_t)g.width*g.height);
			witness.start_x=g.entrance_x; witness.start_y=g.entrance_y;
			witness.steps=g.witness_steps; witness.step_count=g.witness_step_count;
			witness.required_checkpoint_mask=g.witness_required_checkpoint_mask;
			eq(world_spelunk_replay_traversal_witness(&g,world_spelunk_traversal_profile_by_id(g.traversal_profile_id),
				&witness,&proof),WORLD_SPELUNK_PROOF_OK);
			world_spelunk_generated_layout_dispose(&g);
			eq(world_spelunk_reconstruct_node_candidate(s,s->active_node_id,100,&g),WORLD_SPELUNK_RECONSTRUCTION_OK);
			eq(world_spelunk_cut_check(player,g.entrance_x,g.entrance_y+1,0),SPELUNK_CUT_ROUTE);
			world_spelunk_generated_layout_dispose(&g);
			require(world_spelunk_system_is_valid(s));
		}
	}
	if (verbose) printf("\nExcavation: %d sections across 16 systems retain their certified return paths\n", cuts);
	require(cuts>=16);
	ok;
}

static int test_active_save_and_revisit(void *unused)
{
	const struct level *level;
	const struct world_entry *entry;
	struct world_spelunking_entry_report report;
	struct world_spelunk_player_passage_report travel;
	struct world_spelunk_generated_layout g;
	struct world_spelunk_runtime *r;
	struct world_spelunk_system *s;
	int x,y,i,ex,ey;
	(void)unused;
	reset_before_load(); require(savefile_load("Test1",false));
	level=level_by_id("core.cave.east.001"); entry=world_entry_by_id(level,"shaft.spelunk"); notnull(entry);
	cave->depth=player->cave->depth=level->danger;
	require(world_chunk_set_location(cave,level->id,false));
	require(world_chunk_set_location(player->cave,level->id,true));
	require(world_player_set_location(player,level->id,entry->id));
	square_set_mon(cave,player->grid,0); square_set_feat(cave,entry->grid,FEAT_HOLE);
	square_set_feat(player->cave,entry->grid,FEAT_FLOOR); player_place(cave,player,entry->grid);
	eq(world_spelunking_entry_execute(player,cave,"core.route.spelunk.east.in",&report),WORLD_SPELUNKING_ENTRY_OK);
	s=player->spelunking_system; r=player->spelunking;
	eq(world_spelunk_reconstruct_node_candidate(s,s->active_node_id,100,&g),WORLD_SPELUNK_RECONSTRUCTION_OK);
	require(find_cut(&g,&x,&y)); world_spelunk_generated_layout_dispose(&g);
	eq(world_spelunk_cut_rock(player,x,y),SPELUNK_CUT_OK);
	for(i=0;i<2;i++) {
		require(savefile_save("TestSpelunkActive")); reset_before_load();
		require(savefile_load("TestSpelunkActive",false));
		require(world_spelunking_active_storage_is_valid(player));
		s=player->spelunking_system; r=player->spelunking;
		eq(r->cells[y*r->state.map.width+x],WORLD_SPELUNK_AIR);
		eq(r->material_tags[y*r->state.map.width+x],0);
		eq(world_spelunk_reconstruct_node_candidate(s,s->active_node_id,100,&g),WORLD_SPELUNK_RECONSTRUCTION_OK);
		world_spelunk_generated_layout_dispose(&g);
	}
	/* The real reciprocal passage path, including first-visit child generation. */
	ex=s->edges[0].endpoint_a_x; ey=s->edges[0].endpoint_a_y;
	r->state.x=ex; r->state.y=ey; r->state.fall_start_y=ey;
	r->state.movement=WORLD_SPELUNK_STANDING;
	world_spelunk_visibility_follow_player(r); player->upkeep->energy_use=0;
	eq(world_spelunk_player_use_passage(player,WORLD_SPELUNK_PASSAGE_DOWN,&travel),WORLD_SPELUNK_PLAYER_PASSAGE_OK);
	player->upkeep->energy_use=0;
	eq(world_spelunk_player_use_passage(player,WORLD_SPELUNK_PASSAGE_UP,&travel),WORLD_SPELUNK_PLAYER_PASSAGE_OK);
	require(player->spelunking==r); eq(r->cells[y*r->state.map.width+x],WORLD_SPELUNK_AIR);
	require(world_spelunking_active_storage_is_valid(player));
	ok;
}

static int test_geology_parser(void *unused)
{
	struct parser *p;
	(void)unused;
	p=spelunking_geology_parser.init();
	eq(parser_parse(p,"digging:1:10:4"),PARSE_ERROR_MISSING_RECORD_HEADER);
	eq(parser_parse(p,"profile:test.digging:1"),PARSE_ERROR_NONE);
	eq(parser_parse(p,"strata:1:2"),PARSE_ERROR_NONE);
	eq(parser_parse(p,"decoration-budget:0:0"),PARSE_ERROR_NONE);
	eq(parser_parse(p,"material:1:stone:1:limestone:Stone"),PARSE_ERROR_NONE);
	eq(parser_parse(p,"digging:2:10:4"),PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p,"digging:1:-1:4"),PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p,"digging:1:10:0"),PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p,"digging:1:10:17"),PARSE_ERROR_INVALID_VALUE);
	eq(parser_parse(p,"digging:1:10:4"),PARSE_ERROR_NONE);
	eq(parser_parse(p,"digging:1:10:4"),PARSE_ERROR_REPEATED_DIRECTIVE);
	eq(spelunking_geology_parser.finish(p),0);
	eq(world_spelunk_geology_profile_by_id("test.digging")->materials[0].dig_threshold,10);
	p=spelunking_geology_parser.init();
	eq(spelunking_geology_parser.run(p),0); eq(spelunking_geology_parser.finish(p),0);
	ok;
}

const char *suite_name="game/spelunking-terrain";
struct test tests[]={
	{"command costs no repeats and native map unchanged",test_command},
	{"failure exhaustion and unsuitable tool",test_failure_exhaustion_and_skill},
	{"support removal resolves damaging and lethal falls",test_falls},
	{"rope catches but no catch at zero stamina",test_rope_catch_and_last_stamina},
	{"piton last brace protected with alternate brace allowed",test_anchor_protection},
	{"water retaining rock and map perimeter protected",test_water_and_perimeter},
	{"trap chest and door foundations protected",test_fixture_foundations},
	{"loose objects and creatures fall after support removal",test_objects_and_creatures},
	{"wet and occupied landings reject without mutation",test_unsafe_landings},
	{"material difficulty decoration pruning and repeated saves",test_geology_and_persistence},
	{"wall magic adapted but multi-effect destruction rejected",test_magic_policy},
	{"short wall ray range and protected pool intact",test_magic_limits},
	{"real stone to mud wand costs and unsafe chain rejection",test_wand_command},
	{"sixteen seed edits preserve certified return paths",test_generated_routes},
	{"active cave repeated saves and reciprocal passage retain excavations",test_active_save_and_revisit},
	{"digging data bounds and defaults",test_geology_parser},
	{NULL,NULL}
};
