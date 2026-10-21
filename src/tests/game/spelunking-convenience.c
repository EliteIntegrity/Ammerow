/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "spelunking-integration-fixture.h"
#include "world-spelunking-rest.h"
#include "world-spelunking-fixture.h"
#include "ui-spelunking-list.h"
#include "ui-knowledge.h"
#include "ui-game.h"
#include "ui-screen.h"
#include "ui-term.h"
#include "obj-ignore.h"

static bool cavern(void)
{
	enum world_spelunk_tile cells[192];
	int x,y;
	reset_before_load();
	if (!savefile_load("Test1",false)) return false;
	cmdq_flush();
	player_resting_cancel(player,false);
	for (y=0;y<8;y++) for (x=0;x<24;x++)
		cells[y*24+x] = !x || x==23 || !y || y>=6 || x==12 ? WORLD_SPELUNK_ROCK : WORLD_SPELUNK_AIR;
	if (!install_spelunking_runtime(cells,24,8,2,5,20)) return false;
	((struct level *)world_player_level(player))->mode = WORLD_MODE_SPELUNKING;
	my_strcpy(player->world_location.entry,"stair.main",sizeof(player->world_location.entry));
	player->spelunking->actor_roster_initialized = true;
	player->upkeep->energy_use = 0;
	player->timed[TMD_FOOD] = PY_FOOD_FULL-1;
	world_spelunk_visibility_follow_player(player->spelunking);
	return world_spelunk_player_is_active(player);
}

static bool start_rest(int count)
{
	player->upkeep->energy_use=0;
	if (cmdq_push(CMD_REST)) return false;
	cmd_set_arg_choice(cmdq_peek(),"choice",count);
	return cmdq_pop(CTX_GAME);
}

static int test_counted_rest(void *unused)
{
	struct chunk *native;
	int before;
	(void)unused;
	require(cavern()); native=cave; cave=NULL;
	before=player_stamina_current(player);
	require(start_rest(3));
	eq(player_resting_count(player),2);
	eq(player->upkeep->energy_use,100);
	eq(player_stamina_current(player),before+15);
	player_stamina_process_turn(player);
	eq(player_stamina_current(player),before+15); /* no double recovery */
	player->upkeep->energy_use=0; require(cmdq_pop(CTX_GAME));
	eq(player_resting_count(player),1);
	player->upkeep->energy_use=0; require(cmdq_pop(CTX_GAME));
	eq(player_resting_count(player),0); require(!cmdq_pop(CTX_GAME));
	eq(player->resting_turn,3);
	cave=native;
	require(start_rest(1)); eq(player_resting_count(player),0);
	require(!cmdq_pop(CTX_GAME)); eq(player->upkeep->energy_use,100);
	ok;
}

static int test_completion(void *unused)
{
	(void)unused;
	require(cavern()); player->chp=player->mhp; player->csp=player->msp;
	require(!world_spelunk_rest_is_complete(player,REST_ALL_POINTS));
	require(start_rest(REST_ALL_POINTS)); require(player_is_resting(player));
	player->resources.stamina.current=player->resources.stamina.maximum;
	player_air_refill(player);
	player_resting_complete_special(player);
	require(!player_is_resting(player));
	player->upkeep->energy_use=0; require(cmdq_pop(CTX_GAME));
	eq(player->upkeep->energy_use,0); require(!cmdq_pop(CTX_GAME));
	require(start_rest(REST_COMPLETE)); eq(player->upkeep->energy_use,0);
	player->msp=player->csp=0; player->chp=player->mhp-1;
	require(!world_spelunk_rest_is_complete(player,REST_SOME_POINTS));
	player->chp=player->mhp;
	require(world_spelunk_rest_is_complete(player,REST_SOME_POINTS));
	player->timed[TMD_SLOW]=5;
	require(!world_spelunk_rest_is_complete(player,REST_COMPLETE));
	ok;
}

static int test_danger_and_no_hidden_reveal(void *unused)
{
	struct world_spelunk_runtime *r;
	int before;
	(void)unused;
	require(cavern()); r=player->spelunking;
	require(world_spelunk_runtime_add_actor(r,WORLD_SPELUNK_CHASM_SKITTER_ID,17,5));
	null(world_spelunk_rest_block_reason(player)); /* hidden, not omniscient */
	require(start_rest(5));
	before=player_stamina_current(player);
	require(world_spelunk_visibility_detect_cell(r,17,5));
	require(strstr(world_spelunk_rest_block_reason(player),"detected"));
	player->upkeep->energy_use=0; require(cmdq_pop(CTX_GAME));
	eq(player->upkeep->energy_use,0); eq(player_stamina_current(player),before);
	require(!player_is_resting(player)); require(!cmdq_pop(CTX_GAME));
	require(world_spelunk_runtime_remove_actor(r,&r->actors[0]));
	require(start_rest(1)); eq(player->upkeep->energy_use,100); /* no stale latch */
	require(world_spelunk_runtime_add_actor(r,WORLD_SPELUNK_CHASM_SKITTER_ID,4,5));
	require(strstr(world_spelunk_rest_block_reason(player),"sight"));
	require(start_rest(5)); eq(player->upkeep->energy_use,0);
	ok;
}

static int test_unsafe_places(void *unused)
{
	struct world_spelunk_runtime *r;
	(void)unused;
	require(cavern()); r=player->spelunking;
	r->cells[6*24+2]=WORLD_SPELUNK_AIR;
	r->pitons[5*24+2]=1;
	require(world_spelunk_runtime_is_valid(r));
	require(start_rest(3)); eq(player->upkeep->energy_use,0);
	require(!player_is_resting(player));
	r->pitons[5*24+2]=0; r->ropes[5*24+2]=1;
	r->state.movement=WORLD_SPELUNK_HANGING;
	require(start_rest(3)); eq(player->upkeep->energy_use,0);
	require(cavern()); r=player->spelunking;
	r->cells[5*24+2]=WORLD_SPELUNK_WATER;
	require(start_rest(3)); eq(player->upkeep->energy_use,0);
	require(cavern());
	player->timed[TMD_FOOD]=PY_FOOD_HUNGRY-1;
	require(start_rest(3)); eq(player->upkeep->energy_use,0);
	player->timed[TMD_FOOD]=PY_FOOD_FULL-1; player->word_recall=3;
	require(start_rest(3)); eq(player->upkeep->energy_use,0);
	ok;
}

static int test_disturbance_and_clock(void *unused)
{
	struct chunk *native;
	int32_t before;
	(void)unused;
	require(cavern());
	require(start_rest(10));
	disturb(player);
	player->upkeep->energy_use=0; require(cmdq_pop(CTX_GAME));
	eq(player->upkeep->energy_use,0); require(!cmdq_pop(CTX_GAME));
	require(!player_is_resting(player));
	/* Drive the complete shared scheduler without a native map. */
	player->energy=z_info->move_energy;
	player->upkeep->energy_use=0;
	player->timed[TMD_SLOW]=20;
	before=turn;
	eq(cmdq_push(CMD_REST),0); cmd_set_arg_choice(cmdq_peek(),"choice",4);
	native=cave; cave=NULL;
	run_game_loop();
	cave=native;
	require(turn>before); require(player->timed[TMD_SLOW]<20);
	require(!player_is_resting(player));
	require(!cmdq_pop(CTX_GAME));
	ok;
}

static int test_damage_and_traps(void *unused)
{
	struct world_spelunk_runtime *r;
	int hp;
	bool acted;
	(void)unused;
	require(cavern()); r=player->spelunking;
	/* Add away from the player, then stand on the still-hidden trap. */
	require(world_spelunk_fixture_add(r,"core.spelunk.trap.spring",4,5));
	r->state.x=4;
	world_spelunk_visibility_follow_player(r);
	null(world_spelunk_rest_block_reason(player));
	hp=player->chp;
	rand_fix(100); acted=start_rest(5); rand_unfix();
	require(acted);
	require(player->chp<hp); require(!player_is_resting(player));
	require(!cmdq_pop(CTX_GAME));
	require(start_rest(1)); eq(player->upkeep->energy_use,100);
	require(cavern());
	require(start_rest(5));
	take_hit(player,1,"test injury");
	player->upkeep->energy_use=0; require(cmdq_pop(CTX_GAME));
	eq(player->upkeep->energy_use,0); require(!cmdq_pop(CTX_GAME));
	ok;
}

static int test_nearby_knowledge(void *unused)
{
	struct world_spelunk_runtime *r;
	struct ui_spelunking_list_entry rows[16];
	struct randomizer_state before,after;
	struct chunk *native;
	(void)unused;
	require(cavern()); r=player->spelunking;
	require(world_spelunk_runtime_add_actor(r,WORLD_SPELUNK_CHASM_SKITTER_ID,17,5));
	require(world_spelunk_runtime_add_actor(r,"core.spelunk.actor.cave-rat",5,5));
	Rand_state_export(&before);
	native=cave; cave=NULL;
	eq(textui_spelunking_collect_nearby(player,false,rows,16),1);
	require(streq(rows[0].label,"cave rat")); require(!rows[0].detected);
	require(streq(rows[0].offset,"3 right"));
	world_spelunk_visibility_reveal_layout(r);
	eq(textui_spelunking_collect_nearby(player,false,rows,16),1);
	require(world_spelunk_visibility_detect_cell(r,17,5));
	eq(textui_spelunking_collect_nearby(player,false,rows,16),2);
	require(rows[1].detected); require(streq(rows[1].offset,"15 right"));
	Rand_state_export(&after); require(!memcmp(&before,&after,sizeof(before)));
	eq(player->upkeep->energy_use,0);
	player->timed[TMD_IMAGE]=10;
	eq(textui_spelunking_collect_nearby(player,false,rows,16),0);
	cave=native;
	ok;
}

static int test_item_piles_and_knowledge(void *unused)
{
	struct world_spelunk_runtime *r;
	struct ui_spelunking_list_entry rows[8];
	struct object *obj,*other;
	struct chunk *native;
	(void)unused;
	require(cavern()); r=player->spelunking;
	obj=make_test_object(lookup_kind(TV_FOOD,lookup_sval(TV_FOOD,"Ration of Food")),3);
	other=make_test_object(lookup_kind(TV_POTION,lookup_sval(TV_POTION,"Cure Light Wounds")),1);
	require(world_spelunk_runtime_add_ground_object(r,obj,4,5));
	require(world_spelunk_runtime_add_ground_object(r,other,4,5));
	obj->kind->everseen=false;
	other->kind->aware=false;
	native=cave; cave=NULL;
	eq(textui_spelunking_collect_nearby(player,true,rows,8),2);
	require(!obj->kind->everseen);
	require(!other->kind->aware);
	require(!strstr(rows[0].label,"Cure Light Wounds"));
	require(!strstr(rows[1].label,"Cure Light Wounds"));
	obj->known->notice |= OBJ_NOTICE_IGNORE;
	eq(textui_spelunking_collect_nearby(player,true,rows,8),1);
	player->unignoring=true;
	eq(textui_spelunking_collect_nearby(player,true,rows,8),2);
	other->grid.x=other->known->grid.x=17;
	eq(textui_spelunking_collect_nearby(player,true,rows,8),1);
	require(world_spelunk_visibility_detect_cell(r,17,5));
	eq(textui_spelunking_collect_nearby(player,true,rows,8),2);
	require(rows[1].detected);
	eq(world_spelunk_runtime_ground_object_count(r),2);
	cave=native;
	ok;
}

static int screens;
static bool screen_valid;
static void capture(const struct ui_screen *screen)
{
	if (!screen) return;
	screens++;
	screen_valid = screen->kind==UI_SCREEN_NEARBY && screen->row_count>0 &&
		strstr(screen->subtitle,"Current section") &&
		strstr(screen->rows[0].detail,"right");
	Term_keypress(screens==1 ? KC_PGDOWN : ESCAPE,0);
}

static int test_list_controller(void *unused)
{
	term terminal;
	term *previous=Term,*previous_screen=term_screen;
	struct chunk *native;
	(void)unused;
	require(cavern());
	require(world_spelunk_runtime_add_actor(player->spelunking,WORLD_SPELUNK_CHASM_SKITTER_ID,4,5));
	term_init(&terminal,80,24,64); Term_activate(&terminal); term_screen=&terminal;
	terminal.screen_hook=capture; screens=0; screen_valid=false;
	native=cave; cave=NULL;
	do_cmd_monlist();
	cave=native;
	Term_activate(previous); term_screen=previous_screen; term_nuke(&terminal);
	require(screen_valid); eq(screens,2); eq(player->upkeep->energy_use,0);
	ok;
}

static int test_key_cancels_rest(void *unused)
{
	term terminal;
	term *previous=Term,*previous_screen=term_screen;
	int32_t saved_turn;
	bool stopped;
	(void)unused;
	require(cavern()); require(start_rest(10));
	term_init(&terminal,80,24,64); Term_activate(&terminal); term_screen=&terminal;
	saved_turn=turn; turn=1; /* Not a native 128-turn polling boundary. */
	Term_keypress(ESCAPE,0);
	check_for_player_interrupt(EVENT_CHECK_INTERRUPT,NULL,NULL);
	stopped=!player_is_resting(player);
	turn=saved_turn;
	Term_activate(previous); term_screen=previous_screen; term_nuke(&terminal);
	require(stopped);
	player->upkeep->energy_use=0; require(cmdq_pop(CTX_GAME));
	eq(player->upkeep->energy_use,0); require(!cmdq_pop(CTX_GAME));
	ok;
}

static void capture_pages(const struct ui_screen *screen)
{
	if (!screen) return;
	screens++;
	screen_valid = screen_valid && screen->kind==UI_SCREEN_NEARBY &&
		screen->row_count==14 && strstr(screen->subtitle,
			screens==1 ? "Showing 1-14 of 20" : "Showing 7-20 of 20");
	Term_keypress(screens==1 ? KC_PGDOWN : ESCAPE,0);
}

static int test_item_list_paging(void *unused)
{
	term terminal;
	term *previous=Term,*previous_screen=term_screen;
	struct chunk *native;
	int i;
	(void)unused;
	require(cavern());
	for (i=0;i<20;i++) {
		struct object *obj=make_test_object(lookup_kind(TV_FOOD,
			lookup_sval(TV_FOOD,"Ration of Food")),i+1);
		require(world_spelunk_runtime_add_ground_object(player->spelunking,obj,4,5));
	}
	term_init(&terminal,80,24,64); Term_activate(&terminal); term_screen=&terminal;
	terminal.screen_hook=capture_pages; screens=0; screen_valid=true;
	native=cave; cave=NULL;
	do_cmd_itemlist();
	cave=native;
	Term_activate(previous); term_screen=previous_screen; term_nuke(&terminal);
	require(screen_valid); eq(screens,2); eq(player->upkeep->energy_use,0);
	ok;
}

const char *suite_name="game/spelunking-convenience";
struct test tests[]={
	{"counted rest uses cave wait and no double recovery",test_counted_rest},
	{"conditional rest includes stamina and drains queued continuation",test_completion},
	{"rest stops for known creatures without hidden reveal",test_danger_and_no_hidden_reveal},
	{"no automated rest on ropes pitons water hungry or pending recall",test_unsafe_places},
	{"disturbance cancels queue and shared world clock advances",test_disturbance_and_clock},
	{"trap and damage stop rest with no extra turn",test_damage_and_traps},
	{"creature list is knowledge filtered sorted and read only",test_nearby_knowledge},
	{"item lists include full piles but respect knowledge and ignoring",test_item_piles_and_knowledge},
	{"real nearby controller pages and closes without native map",test_list_controller},
	{"player key cancels cave rest at every input boundary",test_key_cancels_rest},
	{"large item pile paginates without dropping rows",test_item_list_paging},
	{NULL,NULL}
};
