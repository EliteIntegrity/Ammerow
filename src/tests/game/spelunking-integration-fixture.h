/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

#ifndef TESTS_GAME_SPELUNKING_INTEGRATION_FIXTURE_H
#define TESTS_GAME_SPELUNKING_INTEGRATION_FIXTURE_H

#include "unit-test.h"
#include "unit-test-data.h"
#include "test-utils.h"

#include <stdio.h>
#include "cave.h"
#include "cmd-core.h"
#include "cmd-fishing.h"
#include "cmd-larder.h"
#include "cmd-spelunking.h"
#include "game-event.h"
#include "game-world.h"
#include "generate.h"
#include "init.h"
#include "message.h"
#include "mon-make.h"
#include "mon-timed.h"
#include "obj-curse.h"
#include "obj-desc.h"
#include "obj-gear.h"
#include "obj-make.h"
#include "obj-pile.h"
#include "obj-tval.h"
#include "obj-util.h"
#include "savefile.h"
#include "store.h"
#include "trap.h"
#include "player.h"
#include "player-attack.h"
#include "player-birth.h"
#include "player-resource.h"
#include "player-spell.h"
#include "player-timed.h"
#include "player-util.h"
#include "ui-travel.h"
#include "ui-mode-input.h"
#include "world-entry.h"
#include "world-fishing-site.h"
#include "world-fishing-discovery.h"
#include "world-map.h"
#include "world-object-transfer.h"
#include "world-overworld.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-data-util.h"
#include "world-spelunking-breath.h"
#include "world-spelunking-combat.h"
#include "world-spelunking-effects.h"
#include "world-spelunking-geology-data.h"
#include "world-spelunking-generation-route.h"
#include "world-spelunking-layout.h"
#include "world-spelunking-layout-data.h"
#include "world-spelunking-passage.h"
#include "world-spelunking-population-data.h"
#include "world-spelunking-publication.h"
#include "world-spelunking-recipe-data.h"
#include "world-spelunking-runtime.h"
#include "world-spelunking-system.h"
#include "world-spelunking-system-data.h"
#include "world-spelunking-transition.h"
#include "world-spelunking-view.h"
#include "world-spelunking-visibility.h"
#include "world-travel-action.h"
#include "world-travel-resources.h"
#include "world-turn.h"
#include "z-rand.h"
#include "z-util.h"
#include "game-fixture.h"

static void test_spelunk_rules(struct world_spelunk_rules *rules,
		unsigned int action_energy)
{
	memset(rules, 0, sizeof(*rules));
	rules->action_energy = action_energy;
	rules->safe_fall_tiles = 2;
	rules->fall_base_damage = 10;
	rules->jump_stamina_cost = 10;
	rules->grip_up_stamina_cost = 5;
	rules->grip_lateral_stamina_cost = 5;
	rules->grip_down_stamina_cost = 5;
	rules->rest_stamina_gain = 15;
	rules->rope_max_length = 20;
	rules->rope_up_stamina_cost = 1;
	rules->rope_lateral_stamina_cost = 1;
	rules->rope_down_stamina_cost = 1;
	rules->breath_turns = 6;
	rules->drowning_damage = 10;
	rules->swim_turn_stamina_cost = 2;
}

static struct object *make_test_object(struct object_kind *kind, int number)
{
	struct object *obj;

	if (!kind || number <= 0 || number > UCHAR_MAX) return NULL;
	obj = object_new();
	object_prep(obj, kind, 1, AVERAGE);
	obj->number = (uint8_t)number;
	obj->known = object_new();
	object_copy(obj->known, obj);
	obj->known->known = NULL;
	return obj;
}

static int carried_kind_count(const struct player *p,
		const struct object_kind *kind)
{
	const struct object *obj;
	int count = 0;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->kind == kind && !object_is_equipped(p->body, obj)) {
			count += obj->number;
		}
	}
	return count;
}

static struct object *carried_kind_object(struct player *p,
		const struct object_kind *kind)
{
	struct object *obj;

	for (obj = p->gear; obj; obj = obj->next) {
		if (obj->kind == kind && !object_is_equipped(p->body, obj)) return obj;
	}
	return NULL;
}

static bool install_spelunking_runtime(const enum world_spelunk_tile *cells,
		int width, int height, int x, int y, int stamina)
{
	struct world_spelunk_map map = { cells, width, height, width };
	struct world_spelunk_rules rules;
	struct world_spelunk_state spelunk_state;

	test_spelunk_rules(&rules, 100);
	if (!world_spelunk_state_init(&spelunk_state, &map, &rules, x, y,
			stamina)) {
		return false;
	}
	player_spelunking_clear(player);
	player->spelunking = world_spelunk_runtime_create(
		player->world_location.id, &spelunk_state);
	player_resources_ensure(player);
	player->resources.stamina.current = (int16_t)MIN(stamina,
		player_stamina_maximum(player));
	return player->spelunking != NULL;
}

#endif /* TESTS_GAME_SPELUNKING_INTEGRATION_FIXTURE_H */
