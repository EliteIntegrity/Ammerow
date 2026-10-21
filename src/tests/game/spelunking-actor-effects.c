/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#include "spelunking-integration-fixture.h"
#include "effect-handler.h"
#include "effects.h"
#include "game-input.h"
#include "mon-summon.h"
#include "obj-slays.h"
#include "option.h"
#include "project.h"
#include "ui-spelunking-list.h"
#include "world-spelunking-actor-effects.h"

#define RAT "core.spelunk.actor.cave-rat"
#define BEETLE "core.spelunk.actor.stone-beetle"

static bool cavern(void)
{
	enum world_spelunk_tile cells[120];
	struct level *level;
	int i;
	reset_before_load();
	if (!savefile_load("Test1", false)) return false;
	for (i=0;i<120;i++) cells[i] = i >= 100 ? WORLD_SPELUNK_ROCK : WORLD_SPELUNK_AIR;
	if (!install_spelunking_runtime(cells,20,6,2,4,90)) return false;
	level = (struct level *)world_player_level(player);
	if (!level) return false;
	level->mode = WORLD_MODE_SPELUNKING;
	my_strcpy(player->world_location.entry,"stair.main",sizeof(player->world_location.entry));
	player->spelunking->actor_roster_initialized = true;
	player->chp = player->mhp = 1000;
	world_spelunk_visibility_follow_player(player->spelunking);
	return true;
}

static bool effect(int type, int subtype, int power)
{
	effect_handler_context_t c = { .effect=type, .subtype=subtype,
		.value={ .base=power }, .origin=source_player(), .aware=true, .dir=6 };
	struct chunk *native = cave;
	enum world_spelunk_effect_dispatch result;
	cave=NULL;
	result=world_spelunk_effect_dispatch(&c);
	cave=native;
	return result == WORLD_SPELUNK_EFFECT_COMPLETED;
}

static int test_status_not_damage(void *unused)
{
	struct world_spelunk_actor *a;
	struct world_spelunk_actor_report reports[WORLD_SPELUNK_ACTOR_MAX];
	size_t count;
	int i, hp;
	(void)unused;
	require(cavern());
	require(world_spelunk_runtime_add_actor(player->spelunking,RAT,3,4));
	a=&player->spelunking->actors[0]; hp=a->hp;
	require(effect(EF_BOLT_STATUS,PROJ_SLEEP_ALL,2));
	eq(a->hp,hp); eq(a->status[WORLD_SPELUNK_STATUS_SLEEP],20);
	for (i=0;i<20;i++) {
		require(world_spelunk_process_actors(player,reports,N_ELEMENTS(reports),&count));
		eq(count,0); eq(a->energy,0);
	}
	eq(a->status[WORLD_SPELUNK_STATUS_SLEEP],0); eq(player->chp,1000);
	require(effect(EF_BOLT_STATUS,PROJ_MON_HOLD,2));
	eq(a->hp,hp); eq(a->status[WORLD_SPELUNK_STATUS_HOLD],20);
	require(effect(EF_BOLT_STATUS,PROJ_MON_CONF,3));
	require(effect(EF_BOLT_STATUS,PROJ_MON_STUN,4));
	require(effect(EF_BOLT_STATUS,PROJ_TURN_ALL,5));
	eq(a->status[WORLD_SPELUNK_STATUS_CONFUSED],30);
	eq(a->status[WORLD_SPELUNK_STATUS_STUN],40); eq(a->status[WORLD_SPELUNK_STATUS_FEAR],50);
	require(effect(EF_BOLT,PROJ_MISSILE,1)); eq(a->hp,hp-1);
	require(effect(EF_BOLT_STATUS,PROJ_MON_HEAL,100)); eq(a->hp,hp);
	ok;
}

static int test_traits(void *unused)
{
	struct world_spelunk_actor_definition d = {0};
	const struct world_spelunk_actor_definition *beetle;
	(void)unused;
	require(cavern());
	beetle=world_spelunk_actor_by_id(BEETLE); notnull(beetle);
	require(rf_has(beetle->flags,RF_ANIMAL));
	eq(world_spelunk_actor_damage(beetle,PROJ_POIS,18),2);
	eq(world_spelunk_actor_damage(beetle,PROJ_MISSILE,18),18);
	require(world_spelunk_runtime_add_actor(player->spelunking,BEETLE,4,4));
	require(effect(EF_BOLT_STATUS,PROJ_MON_CONF,80));
	eq(player->spelunking->actors[0].status[WORLD_SPELUNK_STATUS_CONFUSED],0);
	eq(player->spelunking->actors[0].hp,beetle->hitpoints);
	rf_on(d.flags,RF_HURT_FIRE); eq(world_spelunk_actor_damage(&d,PROJ_FIRE,9),18);
	rf_on(d.flags,RF_IM_FIRE); eq(world_spelunk_actor_damage(&d,PROJ_FIRE,9),1);
	eq(world_spelunk_actor_damage(&d,PROJ_DISP_UNDEAD,9),0);
	rf_on(d.flags,RF_UNDEAD); eq(world_spelunk_actor_damage(&d,PROJ_DISP_UNDEAD,9),9);
	eq(world_spelunk_actor_damage(&d,PROJ_NETHER,9),0);
	eq(world_spelunk_actor_damage(&d,PROJ_MON_DRAIN,9),0);
	require(!world_spelunk_actor_is_living(&d));
	ok;
}

static int test_cadence_and_fear(void *unused)
{
	struct world_spelunk_actor *a;
	struct world_spelunk_actor_report reports[WORLD_SPELUNK_ACTOR_MAX];
	size_t count;
	int baseline, slow, fast;
	(void)unused;
	require(cavern()); require(world_spelunk_runtime_add_actor(player->spelunking,RAT,4,4));
	a=&player->spelunking->actors[0];
	require(world_spelunk_process_actors(player,reports,N_ELEMENTS(reports),&count)); baseline=a->energy;
	a->energy=0; require(effect(EF_BOLT_STATUS,PROJ_MON_SLOW,10));
	require(world_spelunk_process_actors(player,reports,N_ELEMENTS(reports),&count)); slow=a->energy;
	a->energy=0; a->status[WORLD_SPELUNK_STATUS_SLOW]=0;
	require(effect(EF_BOLT_STATUS,PROJ_MON_SPEED,10));
	require(world_spelunk_process_actors(player,reports,N_ELEMENTS(reports),&count)); fast=a->energy;
	require(slow < baseline && fast > baseline);
	require(effect(EF_BOLT_STATUS,PROJ_TURN_ALL,10));
	a->x=3; a->energy=100;
	require(world_spelunk_process_actors(player,reports,N_ELEMENTS(reports),&count));
	eq(a->x,4); eq(player->chp,1000);
	/* Off-screen timers expire too, without banking additional attacks. */
	memset(player->spelunking->visible,0,120);
	a->status[WORLD_SPELUNK_STATUS_FEAR]=1;
	require(world_spelunk_process_actors(player,reports,N_ELEMENTS(reports),&count));
	eq(a->status[WORLD_SPELUNK_STATUS_FEAR],0); eq(count,0);
	ok;
}

static int test_persistence_and_v15(void *unused)
{
	uint8_t bytes[4096]; size_t written;
	struct world_spelunk_runtime *decoded=NULL;
	struct world_spelunk_actor *a;
	int i;
	(void)unused;
	require(cavern()); require(world_spelunk_runtime_add_actor(player->spelunking,RAT,4,4));
	a=&player->spelunking->actors[0]; a->reward=0;
	for (i=0;i<WORLD_SPELUNK_STATUS_MAX;i++) a->status[i]=(uint16_t)(100+i);
	eq(world_spelunk_runtime_encode(player->spelunking,bytes,sizeof(bytes),&written),WORLD_SPELUNK_CODEC_OK);
	eq(world_spelunk_runtime_decode(bytes,written,&decoded),WORLD_SPELUNK_CODEC_OK);
	eq(decoded->actors[0].reward,0);
	for (i=0;i<WORLD_SPELUNK_STATUS_MAX;i++) eq(decoded->actors[0].status[i],100+i);
	world_spelunk_runtime_free(decoded); decoded=NULL;
	eq(world_spelunk_runtime_decode(bytes,written-1,&decoded),WORLD_SPELUNK_CODEC_TRUNCATED); null(decoded);
	/* The appended v16 fields are absent in a genuine v15-shaped payload. */
	bytes[4]=15;
	eq(world_spelunk_runtime_decode(bytes,written-16,&decoded),WORLD_SPELUNK_CODEC_OK);
	eq(decoded->actors[0].reward,world_spelunk_actor_by_id(RAT)->experience);
	for (i=0;i<WORLD_SPELUNK_STATUS_MAX;i++) eq(decoded->actors[0].status[i],0);
	world_spelunk_runtime_free(decoded);
	ok;
}

static int test_teleport_and_polymorph(void *unused)
{
	struct world_spelunk_runtime *r;
	struct world_spelunk_actor *a;
	size_t hits;
	int i, reward;
	(void)unused;
	require(cavern()); r=player->spelunking;
	require(world_spelunk_runtime_add_actor(r,RAT,4,4)); a=&r->actors[0]; reward=a->reward;
	/* Repeated random choices exercise forward/backward candidates; only one hit. */
	for (i=0;i<32;i++) {
		a->x=4; a->y=4;
		require(world_spelunk_player_project_ray(player,1,0,19,5,PROJ_AWAY_ALL,false,&hits));
		eq(hits,1); require(a->x!=4); eq(a->y,4); require(a->x!=r->state.x);
		require(world_spelunk_runtime_is_valid(r));
	}
	a->x=4; a->hp=1;
	for (i=0;i<16;i++) {
		require(effect(EF_BOLT_STATUS,PROJ_MON_POLY,100));
		require(a->reward<=reward); reward=a->reward;
		require(a->hp>=1 && a->hp<=a->max_hp);
		require(world_spelunk_runtime_is_valid(r));
	}
	/* No eligible floor means a completed, harmless no-op, not a native teleport. */
	for (i=0;i<20;i++) if (i!=4 && i!=2) r->cells[100+i]=WORLD_SPELUNK_AIR;
	require(effect(EF_BOLT_STATUS,PROJ_AWAY_ALL,20)); eq(a->x,4); eq(a->y,4);
	ok;
}

static int test_summon_and_banish_no_rewards(void *unused)
{
	struct world_spelunk_runtime *r;
	int32_t xp;
	size_t i;
	(void)unused;
	require(cavern()); r=player->spelunking; xp=player->exp;
	require(effect(EF_SUMMON,summon_name_to_idx("ANIMAL"),100));
	require(r->actor_count>0 && r->actor_count<=4);
	for (i=0;i<r->actor_count;i++) { eq(r->actors[i].reward,0); require(r->actors[i].x!=r->state.x); }
	require(effect(EF_PROJECT_LOS,PROJ_DISP_ALL,100)); eq(r->actor_count,0); eq(player->exp,xp);
	require(world_spelunk_runtime_add_actor(r,RAT,4,4));
	require(effect(EF_MASS_BANISH,0,0)); eq(r->actor_count,0); eq(player->exp,xp);
	require(player->chp<1000 && player->chp>=997);
	ok;
}

static int test_detection_and_los(void *unused)
{
	struct world_spelunk_runtime *r;
	(void)unused;
	require(cavern()); r=player->spelunking;
	require(world_spelunk_runtime_add_actor(r,RAT,4,4));
	require(world_spelunk_runtime_add_actor(r,RAT,17,4));
	r->cells[4*20+10]=WORLD_SPELUNK_ROCK;
	world_spelunk_visibility_follow_player(r);
	require(effect(EF_DETECT_EVIL,0,0)); require(!r->detected[4*20+4]);
	require(effect(EF_DETECT_SOUL,0,0)); require(!r->detected[4*20+17]);
	require(effect(EF_DETECT_INVISIBLE_MONSTERS,0,0)); require(!r->detected[4*20+17]);
	require(effect(EF_DETECT_LIVING_MONSTERS,0,0)); require(r->detected[4*20+17]);
	require(effect(EF_PROJECT_LOS,PROJ_DISP_EVIL,100)); eq(r->actor_count,2);
	require(effect(EF_PROJECT_LOS,PROJ_DISP_ALL,100)); eq(r->actor_count,1); eq(r->actors[0].x,17);
	require(effect(EF_PROBE,0,0)); eq(r->actor_count,1);
	ok;
}

static int test_chain_gate(void *unused)
{
	struct effect a={0}, b={0};
	(void)unused;
	require(cavern()); a.index=EF_HEAL_HP; a.next=&b;
	b.index=EF_BOLT_STATUS; b.subtype=PROJ_MON_SLOW;
	require(world_spelunk_effect_chain_is_supported(&a));
	b.subtype=PROJ_MON_CLONE; require(!world_spelunk_effect_chain_is_supported(&a));
	b.subtype=PROJ_MAKE_DOOR; require(!world_spelunk_effect_chain_is_supported(&a));
	b.index=EF_SUMMON; b.subtype=summon_name_to_idx("UNIQUE");
	require(!world_spelunk_effect_chain_is_supported(&a));
	b.index=EF_BANISH; require(!world_spelunk_effect_chain_is_supported(&a));
	require(world_spelunk_effect_chain_is_supported(&b));
	b.index=EF_BOLT; b.subtype=PROJ_KILL_WALL; require(!world_spelunk_effect_chain_is_supported(&a));
	ok;
}

static int test_brands_slays(void *unused)
{
	struct object *o;
	const struct world_spelunk_actor_definition *rat, *beetle;
	int i, animal=0, poison=0;
	(void)unused;
	require(cavern()); rat=world_spelunk_actor_by_id(RAT); beetle=world_spelunk_actor_by_id(BEETLE);
	o=make_test_object(lookup_kind(TV_SWORD,lookup_sval(TV_SWORD,"Dagger")),1); notnull(o);
	for (i=1;i<z_info->slay_max;i++) if (slays[i].race_flag==RF_ANIMAL) animal=i;
	for (i=1;i<z_info->brand_max;i++) if (brands[i].resist_flag==RF_IM_POIS) poison=i;
	require(animal && poison);
	require(append_slay(&o->slays,animal));
	OPT(player,birth_percent_damage)=false;
	eq(world_spelunk_attack_modifier(player,rat,o,NULL,false,false),slays[animal].multiplier);
	require(append_brand(&o->brands,poison));
	eq(world_spelunk_attack_modifier(player,rat,o,NULL,false,false),MAX(slays[animal].multiplier,brands[poison].multiplier));
	eq(world_spelunk_attack_modifier(player,beetle,o,NULL,false,false),slays[animal].multiplier);
	OPT(player,birth_percent_damage)=true;
	eq(world_spelunk_attack_modifier(player,beetle,o,NULL,false,false),slays[animal].o_multiplier);
	object_delete(NULL,NULL,&o->known); object_delete(NULL,NULL,&o);
	ok;
}

static int sound_count;
static void count_sound(game_event_type type, game_event_data *data, void *user)
{
	(void)type; (void)data; (void)user;
	sound_count++;
}

static int test_rejected_device_chain(void *unused)
{
	struct object_kind *kind;
	struct object *wand;
	struct effect heal = {0}, clone = {0}, *original;
	struct randomizer_state before, after;
	struct chunk *native;
	bool processed;
	(void)unused;
	require(cavern());
	kind = lookup_kind(TV_WAND, lookup_sval(TV_WAND, "Slow Monster"));
	wand = make_test_object(kind, 1); notnull(wand);
	inven_carry(player, wand, true, false);
	wand = carried_kind_object(player, kind); notnull(wand);
	wand->pval = 3;
	original = wand->effect;
	heal.index = EF_HEAL_HP; heal.next = &clone;
	clone.index = EF_BOLT_STATUS; clone.subtype = PROJ_MON_CLONE;
	wand->effect = &heal;
	player->chp = 500;
	player->upkeep->energy_use = 0;
	OPT(player, use_sound) = true;
	eq(cmdq_push(CMD_USE_WAND), 0);
	cmd_set_arg_item(cmdq_peek(), "item", wand);
	cmd_set_arg_target(cmdq_peek(), "target", 6);
	Rand_state_export(&before);
	sound_count = 0;
	event_add_handler(EVENT_SOUND, count_sound, NULL);
	native = cave; cave = NULL;
	processed = cmdq_pop(CTX_GAME);
	cave = native;
	event_remove_handler(EVENT_SOUND, count_sound, NULL);
	Rand_state_export(&after);
	wand->effect = original;
	require(processed);
	eq(wand->pval, 3); eq(player->chp, 500);
	eq(player->upkeep->energy_use, 0); eq(sound_count, 0);
	require(!memcmp(&before, &after, sizeof(before)));
	ok;
}

static bool cancel_banish(const char *prompt, char *symbol)
{
	(void)prompt; (void)symbol;
	return false;
}

static bool choose_rat(const char *prompt, char *symbol)
{
	(void)prompt;
	*symbol = 'r';
	return true;
}

static int test_symbol_banish(void *unused)
{
	bool (*previous)(const char *, char *) = get_com_hook;
	struct randomizer_state before, after;
	bool cancelled, completed;
	int32_t xp;
	(void)unused;
	require(cavern()); xp = player->exp;
	require(world_spelunk_runtime_add_actor(player->spelunking, RAT,4,4));
	require(world_spelunk_runtime_add_actor(player->spelunking, BEETLE,6,4));
	Rand_state_export(&before);
	get_com_hook = cancel_banish;
	cancelled = !effect(EF_BANISH,0,0);
	get_com_hook = previous;
	Rand_state_export(&after);
	require(cancelled); eq(player->spelunking->actor_count,2);
	eq(player->chp,1000); require(!memcmp(&before,&after,sizeof(before)));
	get_com_hook = choose_rat;
	completed = effect(EF_BANISH,0,0);
	get_com_hook = previous;
	require(completed); eq(player->spelunking->actor_count,1);
	require(streq(player->spelunking->actors[0].id,BEETLE));
	eq(player->exp,xp); require(player->chp < 1000 && player->chp >= 996);
	ok;
}

static int test_ranged_damage_modifier(void *unused)
{
	struct object *missile;
	int option, plain, enhanced;
	uint32_t message;
	bool hit_plain, hit_enhanced;
	struct randomizer_state before;
	(void)unused;
	require(cavern());
	missile = make_test_object(lookup_kind(TV_SWORD,lookup_sval(TV_SWORD,"Dagger")),1);
	notnull(missile);
	for (option = 0; option < 2; option++) {
		OPT(player,birth_percent_damage) = option != 0;
		Rand_state_export(&before);
		rand_fix(100);
		hit_plain = player_resolve_ranged_attack_with_modifier(player,missile,NULL,0,0,true,0,&plain,&message);
		rand_unfix();
		require(Rand_state_import(&before));
		rand_fix(100);
		hit_enhanced = player_resolve_ranged_attack_with_modifier(player,missile,NULL,0,0,true,option ? 30 : 3,&enhanced,&message);
		rand_unfix();
		require(hit_plain && hit_enhanced);
		require(enhanced > plain);
	}
	object_delete(NULL,NULL,&missile->known); object_delete(NULL,NULL,&missile);
	ok;
}

static int test_status_knowledge(void *unused)
{
	struct ui_spelunking_list_entry rows[4];
	struct world_spelunk_runtime *r;
	(void)unused;
	require(cavern()); r = player->spelunking;
	require(world_spelunk_runtime_add_actor(r,RAT,4,4));
	require(world_spelunk_runtime_add_actor(r,RAT,17,4));
	r->actors[0].status[WORLD_SPELUNK_STATUS_SLEEP] = 100;
	r->actors[1].status[WORLD_SPELUNK_STATUS_SLEEP] = 100;
	r->cells[4*20+10] = WORLD_SPELUNK_ROCK;
	world_spelunk_visibility_follow_player(r);
	require(world_spelunk_visibility_detect_cell(r,17,4));
	eq(textui_spelunking_collect_nearby(player,false,rows,4),2);
	require(strstr(rows[0].label,"asleep"));
	require(!strstr(rows[1].label,"asleep")); require(rows[1].detected);
	/* Effect text must not disclose a creature through blindness either. */
	player->timed[TMD_BLIND] = 10;
	eq(textui_spelunking_collect_nearby(player,false,rows,4),2);
	require(!strstr(rows[0].label,"asleep"));
	msg("effect knowledge sentinel");
	require(effect(EF_BOLT_STATUS,PROJ_MON_SLOW,5));
	require(streq(message_str(0),"effect knowledge sentinel"));
	require(effect(EF_PROBE,0,0));
	require(streq(message_str(0),"No cave creatures are in sight to probe."));
	ok;
}

const char *suite_name="game/spelunking-actor-effects";
struct test tests[]={
	{"statuses are not damage and expire on world ticks",test_status_not_damage},
	{"authored filters resistances and immunities",test_traits},
	{"speed changes fear and offscreen expiry",test_cadence_and_fear},
	{"persistent statuses rewards and v15 migration",test_persistence_and_v15},
	{"teleport snapshot safety polymorph and placement",test_teleport_and_polymorph},
	{"summoning and banishment cannot farm XP",test_summon_and_banish_no_rewards},
	{"filtered detection and occluded line of sight",test_detection_and_los},
	{"whole chain subtype preflight",test_chain_gate},
	{"nonstacking brands and slays both combat options",test_brands_slays},
	{"device preflight precedes all mutation and costs",test_rejected_device_chain},
	{"symbol banishment and cancellation",test_symbol_banish},
	{"ranged modifier affects both damage options",test_ranged_damage_modifier},
	{"status text respects sight versus detection",test_status_knowledge},
	{NULL,NULL}
};
