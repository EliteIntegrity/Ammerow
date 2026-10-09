/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-fishing.c
 * \brief Semantic command boundary for the fishing activity.
 *
 */

#include "angband.h"
#include "cave.h"
#include "cmd-fishing.h"

#include "game-event.h"
#include "game-world.h"
#include "init.h"
#include "obj-gear.h"
#include "obj-knowledge.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "object.h"
#include "player.h"
#include "player-calcs.h"
#include "store.h"
#include "ui-fishing.h"
#include "world-larder.h"
#include "world-fishing-discovery.h"
#include "world-fishing-site.h"
#include "world-turn.h"

static void fishing_presentation_changed(void)
{
	if (textui_fishing_overlay_changed_hook) {
		(*textui_fishing_overlay_changed_hook)();
	} else if (player && player->upkeep) {
		player->upkeep->redraw |= PR_MAP;
	}
}

/** Choose the highest data-authored rig priority carried by the player. */
static const struct world_fishing_rig *best_carried_fishing_rig(
		const struct player *p)
{
	const struct world_fishing_rig *best = NULL;
	const struct object *obj;

	if (!p) return NULL;
	for (obj = p->gear; obj; obj = obj->next) {
		const struct world_fishing_rig *rig =
			world_fishing_rig_for_kind(obj->kind);

		if (rig && (!best || rig->priority > best->priority)) best = rig;
	}
	return best;
}

static void stop_fishing(void)
{
	if (!player || !player->fishing) return;
	mem_free(player->fishing);
	player->fishing = NULL;
	fishing_presentation_changed();
}

static struct object *make_caught_fish(world_fishing_kind kind)
{
	struct object_kind *object_kind;
	struct object *object;
	int tval = tval_find_idx("food");
	int sval = lookup_sval(tval, world_fishing_kind_name(kind));

	if (tval <= 0 || sval < 0) return NULL;
	object_kind = lookup_kind(tval, sval);
	if (!object_kind) return NULL;
	object = object_new();
	object_prep(object, object_kind, player ? player->depth : 0, MINIMISE);
	object->number = 1;
	object->origin = ORIGIN_FISHING;
	object->known = object_new();
	object_set_base_known(player, object);
	object_flavor_aware(player, object);
	object->known->pval = object->pval;
	object->known->effect = object->effect;
	object->known->notice |= OBJ_NOTICE_ASSESSED;
	object_kind->everseen = true;
	return object;
}

static void award_caught_fish(world_fishing_kind kind)
{
	struct object *object = make_caught_fish(kind);
	const char *name = world_fishing_kind_name(kind);

	if (!object) {
		msg("You land a %s, but its item definition is unavailable.", name);
		return;
	}
	if (inven_carry_num(player, object) > 0) {
		inven_carry(player, object, true, true);
		event_signal(EVENT_INVENTORY);
	} else {
		msg("Your pack is full; the %s falls at your feet.", name);
		if (!world_fishing_site_drop_catch(player, cave, &object)) {
			struct object *known = object->known;

			object->known = NULL;
			if (known) object_delete(NULL, NULL, &known);
			object_delete(NULL, NULL, &object);
			msg("There is no safe room for the catch; it slips away.");
		}
	}
}

static void explain_unavailable_site(enum world_fishing_site_result result)
{
	switch (result) {
	case WORLD_FISHING_SITE_NO_HABITAT:
		msg("This water has no known fishing habitat.");
		break;
	case WORLD_FISHING_SITE_UNSTABLE:
		msg("You must stand securely beside the water before casting.");
		break;
	case WORLD_FISHING_SITE_NO_WATER:
		msg("You need to be in or next to water to fish.");
		break;
	case WORLD_FISHING_SITE_INVALID:
	case WORLD_FISHING_SITE_OK:
	default:
		break;
	}
}

errr cmdq_push_fishing_action(enum world_fishing_action action)
{
	struct command *queued;
	cmd_code code;

	if (action < WORLD_FISHING_EXTEND || action > WORLD_FISHING_CANCEL) {
		return 1;
	}
	code = action == WORLD_FISHING_CANCEL ? CMD_FISHING_STOP :
		CMD_FISHING_ACTION;
	if (cmdq_push(code)) return 1;
	if (code == CMD_FISHING_ACTION) {
		queued = cmdq_peek();
		cmd_set_arg_choice(queued, "action", action);
	}
	return 0;
}

void do_cmd_fishing_start(struct command *cmd)
{
	const struct world_fishing_rig *rig;
	struct world_fishing_site site;
	enum world_fishing_site_result site_result;
	uint32_t seed;

	(void)cmd;
	cmd_disable_repeat();
	if (!player || !player->upkeep || player->is_dead || player->fishing ||
			!world_player_mode_has_capability(player,
				WORLD_MODE_CAP_FISHING)) {
		return;
	}
	site_result = world_fishing_site_query(player, cave, &site);
	if (site_result != WORLD_FISHING_SITE_OK) {
		explain_unavailable_site(site_result);
		return;
	}
	rig = best_carried_fishing_rig(player);
	if (!rig) {
		const char *hint = store_fishing_rig_hint();

		msg("%s", hint ? hint : "You need a fishing rod.");
		return;
	}
	player->fishing = mem_zalloc(sizeof(*player->fishing));
	seed = (uint32_t)randint0(0x10000000) ^
		(uint32_t)player->total_energy ^ 0x46495348U;
	world_fishing_runtime_start(player->fishing, seed,
		site.habitat, site.danger, rig->maximum_reach, rig->maximum_depth,
		rig->id, rig->name, site.origin);
	fishing_presentation_changed();
	msgt(MSG_FISHING_CAST,
		"You ready the %s over the %s (reach %d, depth %d).",
		rig->name, world_fishing_habitat_name(site.habitat),
		rig->maximum_reach, rig->maximum_depth);
}

void do_cmd_fishing_action(struct command *cmd)
{
	struct world_fishing_report report;
	struct world_fishing_site session_site;
	struct world_fishing_site current_site;
	int selected;

	cmd_disable_repeat();
	if (!cmd || cmd_get_arg_choice(cmd, "action", &selected) != CMD_OK ||
			selected < WORLD_FISHING_EXTEND ||
			selected >= WORLD_FISHING_CANCEL || !player ||
			!player->fishing) {
		return;
	}
	session_site.origin = player->fishing->origin;
	session_site.habitat = player->fishing->habitat;
	session_site.danger = player->fishing->location_danger;
	session_site.action_energy = 0;
	if (player->is_dead ||
			world_fishing_site_query(player, cave, &current_site) !=
				WORLD_FISHING_SITE_OK ||
			!loc_eq(current_site.origin, session_site.origin) ||
			current_site.habitat != session_site.habitat ||
			current_site.danger != session_site.danger) {
		stop_fishing();
		msg("You are pulled away from the water; the fishing session ends.");
		return;
	}
	if (!world_fishing_apply(player->fishing,
			(enum world_fishing_action)selected, &report)) {
		return;
	}
	switch (report.event) {
	case WORLD_FISHING_EVENT_BITE:
		msgt(MSG_FISHING_BITE,
			"Fish on the hook! Press Enter to strike; you have three waits of grace.");
		break;
	case WORLD_FISHING_EVENT_STRUCK:
		msgt(MSG_FISHING_REEL, "The hook sets. Reel with Up.");
		break;
	case WORLD_FISHING_EVENT_PULL:
		msgt(MSG_FISHING_REEL, "The fish pulls back!");
		break;
	case WORLD_FISHING_EVENT_LANDED:
		{
			const char *discovery_message = NULL;
			enum world_fishing_discovery_result discovery_result;
			int experience = world_fishing_kind_catch_experience(
				report.landed_kind);

			msgt(MSG_FISHING_CATCH,
				"You land a %s! It is worth %u village-larder portion%s.",
				world_fishing_kind_name(report.landed_kind),
				world_larder_fish_value(report.landed_kind),
				world_larder_fish_value(report.landed_kind) == 1 ? "" : "s");
			award_caught_fish(report.landed_kind);
			/* The landing event occurs once, regardless of pack space. */
			if (experience) {
				player_exp_gain(player, experience);
				msg("You gain %d XP for the catch.", experience);
			}
			discovery_result = world_fishing_apply_discovery(player,
				player->fishing, &report, &discovery_message);
			if (discovery_result == WORLD_FISHING_DISCOVERY_UNLOCKED) {
				msg("%s", discovery_message);
			} else if (discovery_result == WORLD_FISHING_DISCOVERY_ERROR) {
				msg("The catch reveals something, but your survey cannot record it.");
			}
		}
		break;
	case WORLD_FISHING_EVENT_ESCAPED:
		msgt(MSG_FISHING_ESCAPE, "The fish gets away.");
		break;
	case WORLD_FISHING_EVENT_WAITED:
		msg("You wait for a bite.");
		break;
	case WORLD_FISHING_EVENT_APPROACH:
		msgt(MSG_FISHING_MOVE, "A fish turns toward the hook.");
		break;
	case WORLD_FISHING_EVENT_MOVED:
		if (player->fishing->phase == WORLD_FISHING_WINDING) {
			sound(MSG_FISHING_REEL);
		} else {
			sound(MSG_FISHING_MOVE);
		}
		break;
	case WORLD_FISHING_EVENT_NONE:
	case WORLD_FISHING_EVENT_CANCELLED:
		break;
	case WORLD_FISHING_EVENT_BLOCKED:
		if (selected == WORLD_FISHING_LOWER) {
			msg("The %s line reaches no deeper (%d of %d).",
				player->fishing->rig_name, player->fishing->depth,
				player->fishing->maximum_depth);
		} else if (selected == WORLD_FISHING_EXTEND) {
			msg("The %s shaft reaches no farther (%d of %d).",
				player->fishing->rig_name, player->fishing->rod_reach,
				player->fishing->maximum_reach);
		}
		break;
	default:
		break;
	}
	if (report.consumes_turn) {
		player->upkeep->energy_use = current_site.action_energy;
	}
	fishing_presentation_changed();
}

void do_cmd_fishing_stop(struct command *cmd)
{
	(void)cmd;
	cmd_disable_repeat();
	if (!player || !player->fishing) return;
	stop_fishing();
	msg("You put away the fishing rig.");
}
