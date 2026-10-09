/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-codec.c
 * \brief Bounded wire codec and migrations for owned spelunking runtimes.
 *
 *
 */

#include "world-spelunking-runtime.h"
#include "world-spelunking-actor-data.h"
#include "world-spelunking-visibility.h"
#include "z-util.h"
#include "z-virt.h"

#include <string.h>

static const uint8_t payload_magic[4] = { 'S', 'P', 'L', 'K' };

/* These are historical wire-format migrations, not current balance defaults. */
#define LEGACY_ROPE_MAX_LENGTH 20
#define LEGACY_ROPE_TURN_STAMINA_COST 1
#define LEGACY_BREATH_TURNS 6
#define LEGACY_DROWNING_DAMAGE 10
#define LEGACY_SWIM_TURN_STAMINA_COST 2

struct write_cursor {
	uint8_t *buffer;
	size_t position;
};

struct read_cursor {
	const uint8_t *buffer;
	size_t length;
	size_t position;
};

static uint16_t runtime_run_count(
		const struct world_spelunk_runtime *runtime)
{
	const struct world_spelunk_map *map = &runtime->state.map;
	size_t count = (size_t)map->width * (size_t)map->height;
	uint16_t runs = 0;
	size_t i;

	for (i = 0; i < count; i++) {
		if (i == 0 || runtime->cells[i] != runtime->cells[i - 1]) runs++;
	}
	return runs;
}

static uint16_t byte_run_count(const uint8_t *values, size_t count)
{
	uint16_t runs = 0;
	size_t i;

	for (i = 0; i < count; i++) {
		if (i == 0 || values[i] != values[i - 1]) runs++;
	}
	return runs;
}

size_t world_spelunk_runtime_encoded_size(
		const struct world_spelunk_runtime *runtime)
{
	size_t actor_bytes = 0;
	size_t geology_bytes = 1;
	size_t count;
	size_t i;

	if (!world_spelunk_runtime_is_valid(runtime)) return 0;
	for (i = 0; i < runtime->actor_count; i++) {
		actor_bytes += 11u + strlen(runtime->actors[i].id);
	}
	count = (size_t)runtime->state.map.width * runtime->state.map.height;
	if (runtime->geology_profile_id[0]) {
		geology_bytes += 7u + strlen(runtime->geology_profile_id) +
			3u * byte_run_count(runtime->material_tags, count) +
			3u * byte_run_count(runtime->decoration_tags, count);
	}
	return 75u + strlen(runtime->location_id) +
		3u * runtime_run_count(runtime) +
		3u * byte_run_count(runtime->pitons,
			count) +
		3u * byte_run_count(runtime->ropes,
			count) +
		3u * byte_run_count(runtime->explored,
			count) + 2u + actor_bytes + geology_bytes;
}

static void put_u8(struct write_cursor *cursor, uint8_t value)
{
	cursor->buffer[cursor->position++] = value;
}

static void put_u16(struct write_cursor *cursor, uint16_t value)
{
	put_u8(cursor, (uint8_t)(value & 0xff));
	put_u8(cursor, (uint8_t)((value >> 8) & 0xff));
}

static void put_s16(struct write_cursor *cursor, int16_t value)
{
	put_u16(cursor, (uint16_t)value);
}

static void put_u32(struct write_cursor *cursor, uint32_t value)
{
	put_u16(cursor, (uint16_t)(value & 0xffff));
	put_u16(cursor, (uint16_t)((value >> 16) & 0xffff));
}

static void put_byte_runs(struct write_cursor *cursor, const uint8_t *values,
		size_t count)
{
	size_t start = 0;

	put_u16(cursor, byte_run_count(values, count));
	while (start < count) {
		size_t end = start + 1;

		while (end < count && values[end] == values[start]) end++;
		put_u8(cursor, values[start]);
		put_u16(cursor, (uint16_t)(end - start));
		start = end;
	}
}

enum world_spelunk_codec_result world_spelunk_runtime_encode(
		const struct world_spelunk_runtime *runtime, uint8_t *buffer,
		size_t capacity, size_t *written)
{
	struct write_cursor cursor = { buffer, 0 };
	const struct world_spelunk_state *state;
	size_t needed;
	size_t count;
	size_t i;
	size_t start;
	uint16_t runs;
	size_t id_length;

	if (written) *written = 0;
	if (!runtime || !buffer || !written) {
		return WORLD_SPELUNK_CODEC_INVALID_ARGUMENT;
	}
	needed = world_spelunk_runtime_encoded_size(runtime);
	if (!needed) return WORLD_SPELUNK_CODEC_INVALID_STATE;
	if (capacity < needed) return WORLD_SPELUNK_CODEC_BUFFER_TOO_SMALL;

	state = &runtime->state;
	id_length = strlen(runtime->location_id);
	runs = runtime_run_count(runtime);
	count = (size_t)state->map.width * (size_t)state->map.height;
	for (i = 0; i < sizeof(payload_magic); i++) {
		put_u8(&cursor, payload_magic[i]);
	}
	put_u16(&cursor, WORLD_SPELUNK_PAYLOAD_VERSION);
	put_u8(&cursor, (uint8_t)id_length);
	for (i = 0; i < id_length; i++) {
		put_u8(&cursor, (uint8_t)runtime->location_id[i]);
	}
	put_u16(&cursor, runtime->layout_revision);
	put_u16(&cursor, (uint16_t)state->map.width);
	put_u16(&cursor, (uint16_t)state->map.height);
	put_u32(&cursor, state->rules.action_energy);
	put_u16(&cursor, (uint16_t)state->rules.safe_fall_tiles);
	put_u16(&cursor, (uint16_t)state->rules.fall_base_damage);
	put_u16(&cursor, (uint16_t)state->rules.jump_stamina_cost);
	put_u16(&cursor, (uint16_t)state->rules.grip_lateral_stamina_cost);
	put_u16(&cursor, (uint16_t)state->rules.rest_stamina_gain);
	put_u16(&cursor, (uint16_t)state->rules.rope_max_length);
	put_u16(&cursor, (uint16_t)state->rules.rope_down_stamina_cost);
	put_u16(&cursor, (uint16_t)state->rules.breath_turns);
	put_u16(&cursor, (uint16_t)state->rules.drowning_damage);
	put_s16(&cursor, (int16_t)state->x);
	put_s16(&cursor, (int16_t)state->y);
	put_u16(&cursor, (uint16_t)state->stamina);
	put_u16(&cursor, (uint16_t)state->max_stamina);
	put_u16(&cursor, (uint16_t)state->breath);
	put_s16(&cursor, (int16_t)state->fall_start_y);
	put_s16(&cursor, (int16_t)state->grip_target_x);
	put_s16(&cursor, (int16_t)state->grip_target_y);
	put_u8(&cursor, (uint8_t)state->movement);
	put_u8(&cursor, state->jump_holding ? 1 : 0);
	put_u32(&cursor, (uint32_t)count);
	put_u16(&cursor, runs);
	start = 0;
	while (start < count) {
		size_t end = start + 1;

		while (end < count && runtime->cells[end] ==
				runtime->cells[start]) {
			end++;
		}
		put_u8(&cursor, (uint8_t)runtime->cells[start]);
		put_u16(&cursor, (uint16_t)(end - start));
		start = end;
	}
	put_byte_runs(&cursor, runtime->pitons, count);
	put_byte_runs(&cursor, runtime->ropes, count);
	put_byte_runs(&cursor, runtime->explored, count);
	put_u8(&cursor, runtime->actor_roster_initialized ? 1 : 0);
	put_u8(&cursor, (uint8_t)runtime->actor_count);
	for (i = 0; i < runtime->actor_count; i++) {
		const struct world_spelunk_actor *actor = &runtime->actors[i];
		size_t actor_id_length = strlen(actor->id);

		put_u8(&cursor, (uint8_t)actor_id_length);
		for (start = 0; start < actor_id_length; start++) {
			put_u8(&cursor, (uint8_t)actor->id[start]);
		}
		put_s16(&cursor, (int16_t)actor->x);
		put_s16(&cursor, (int16_t)actor->y);
		put_u16(&cursor, (uint16_t)actor->hp);
		put_u32(&cursor, actor->energy);
	}
	put_u8(&cursor, runtime->geology_profile_id[0] ? 1 : 0);
	if (runtime->geology_profile_id[0]) {
		size_t geology_id_length = strlen(runtime->geology_profile_id);

		put_u8(&cursor, (uint8_t)geology_id_length);
		for (i = 0; i < geology_id_length; i++) {
			put_u8(&cursor, (uint8_t)runtime->geology_profile_id[i]);
		}
		put_u16(&cursor, runtime->geology_version);
		put_byte_runs(&cursor, runtime->material_tags, count);
		put_byte_runs(&cursor, runtime->decoration_tags, count);
	}
	/* Version twelve appends rather than inserts the swimming rule so all
	 * earlier wire layouts retain their exact byte offsets. */
	put_u16(&cursor, (uint16_t)state->rules.swim_turn_stamina_cost);
	/* Version thirteen likewise appends directional differences.  The old
	 * grip field remains lateral and the old rope field remains descending. */
	put_u16(&cursor, (uint16_t)state->rules.grip_up_stamina_cost);
	put_u16(&cursor, (uint16_t)state->rules.grip_down_stamina_cost);
	put_u16(&cursor, (uint16_t)state->rules.rope_up_stamina_cost);
	put_u16(&cursor, (uint16_t)state->rules.rope_lateral_stamina_cost);
	*written = cursor.position;
	return cursor.position == needed ? WORLD_SPELUNK_CODEC_OK :
		WORLD_SPELUNK_CODEC_INVALID_STATE;
}

static bool get_u8(struct read_cursor *cursor, uint8_t *value)
{
	if (cursor->position >= cursor->length) return false;
	*value = cursor->buffer[cursor->position++];
	return true;
}

static bool get_u16(struct read_cursor *cursor, uint16_t *value)
{
	uint8_t low;
	uint8_t high;

	if (!get_u8(cursor, &low) || !get_u8(cursor, &high)) return false;
	*value = (uint16_t)(low | ((uint16_t)high << 8));
	return true;
}

static bool get_s16(struct read_cursor *cursor, int16_t *value)
{
	uint16_t raw;

	if (!get_u16(cursor, &raw)) return false;
	*value = (int16_t)raw;
	return true;
}

static bool get_u32(struct read_cursor *cursor, uint32_t *value)
{
	uint16_t low;
	uint16_t high;

	if (!get_u16(cursor, &low) || !get_u16(cursor, &high)) return false;
	*value = (uint32_t)low | ((uint32_t)high << 16);
	return true;
}

/** Translate the short-lived numeric actor save identity used by versions 7-8. */
static const char *legacy_actor_id(uint8_t kind)
{
	return kind == 1 ? WORLD_SPELUNK_CHASM_SKITTER_ID : NULL;
}

static enum world_spelunk_codec_result get_byte_runs(
		struct read_cursor *cursor, uint8_t *values, size_t count)
{
	uint16_t runs;
	size_t filled = 0;
	unsigned int i;

	if (!get_u16(cursor, &runs)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	if (!runs || runs > count) return WORLD_SPELUNK_CODEC_CORRUPT;
	for (i = 0; i < runs; i++) {
		uint8_t value;
		uint16_t run_length;
		size_t j;

		if (!get_u8(cursor, &value) || !get_u16(cursor, &run_length)) {
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		if (value > 1 || !run_length || filled + run_length > count) {
			return WORLD_SPELUNK_CODEC_CORRUPT;
		}
		for (j = 0; j < run_length; j++) values[filled++] = value;
	}
	return filled == count ? WORLD_SPELUNK_CODEC_OK :
		WORLD_SPELUNK_CODEC_CORRUPT;
}

static enum world_spelunk_codec_result get_tag_runs(
		struct read_cursor *cursor, uint8_t *values, size_t count)
{
	uint16_t runs;
	size_t filled = 0;
	unsigned int i;

	if (!get_u16(cursor, &runs)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	if (!runs || runs > count) return WORLD_SPELUNK_CODEC_CORRUPT;
	for (i = 0; i < runs; i++) {
		uint8_t value;
		uint16_t run_length;
		size_t j;

		if (!get_u8(cursor, &value) || !get_u16(cursor, &run_length)) {
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		if (!run_length || filled + run_length > count) {
			return WORLD_SPELUNK_CODEC_CORRUPT;
		}
		for (j = 0; j < run_length; j++) values[filled++] = value;
	}
	return filled == count ? WORLD_SPELUNK_CODEC_OK :
		WORLD_SPELUNK_CODEC_CORRUPT;
}

enum world_spelunk_codec_result world_spelunk_runtime_decode(
		const uint8_t *buffer, size_t length,
		struct world_spelunk_runtime **decoded)
{
	struct read_cursor cursor = { buffer, length, 0 };
	struct world_spelunk_runtime *runtime;
	struct world_spelunk_map map = { 0 };
	struct world_spelunk_rules rules;
	struct world_spelunk_state state;
	enum world_spelunk_tile *cells;
	uint8_t *pitons = NULL;
	uint8_t *ropes = NULL;
	uint8_t *explored = NULL;
	uint8_t *material_tags = NULL;
	uint8_t *decoration_tags = NULL;
	struct world_spelunk_actor actors[WORLD_SPELUNK_ACTOR_MAX] = { 0 };
	char location_id[WORLD_ID_LEN];
	char geology_profile_id[WORLD_ID_LEN] = "";
	uint8_t byte;
	uint8_t id_length;
	uint8_t movement;
	uint8_t jump_holding;
	uint8_t actor_roster_initialized = 0;
	uint8_t actor_count = 0;
	uint16_t version;
	uint16_t layout_revision = 0;
	uint16_t geology_version = 0;
	uint16_t width;
	uint16_t height;
	uint16_t value16;
	uint16_t stamina;
	uint16_t max_stamina;
	uint16_t breath = 0;
	uint16_t runs;
	uint32_t cell_count;
	int16_t x;
	int16_t y;
	int16_t fall_start_y;
	int16_t grip_target_x;
	int16_t grip_target_y;
	size_t filled = 0;
	unsigned int i;

	if (!buffer || !decoded) return WORLD_SPELUNK_CODEC_INVALID_ARGUMENT;
	*decoded = NULL;
	for (i = 0; i < sizeof(payload_magic); i++) {
		if (!get_u8(&cursor, &byte)) return WORLD_SPELUNK_CODEC_TRUNCATED;
		if (byte != payload_magic[i]) return WORLD_SPELUNK_CODEC_BAD_MAGIC;
	}
	if (!get_u16(&cursor, &version)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	if (version != WORLD_SPELUNK_PAYLOAD_VERSION &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_SWIMMING &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_GEOLOGY &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_LAYOUT_REVISION &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_STABLE_ACTOR_IDS &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_ACTOR_CADENCE &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_ACTORS &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_BREATH &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_EXPLORATION &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_WATER &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_ROPE &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_INFRASTRUCTURE &&
			version != WORLD_SPELUNK_PAYLOAD_VERSION_LEGACY) {
		return WORLD_SPELUNK_CODEC_UNSUPPORTED_VERSION;
	}
	if (!get_u8(&cursor, &id_length)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	if (!id_length || id_length >= WORLD_ID_LEN) {
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	for (i = 0; i < id_length; i++) {
		if (!get_u8(&cursor, &byte)) return WORLD_SPELUNK_CODEC_TRUNCATED;
		location_id[i] = (char)byte;
	}
	location_id[id_length] = '\0';
	if (!world_id_is_valid(location_id)) return WORLD_SPELUNK_CODEC_CORRUPT;
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_LAYOUT_REVISION &&
			!get_u16(&cursor, &layout_revision)) {
		return WORLD_SPELUNK_CODEC_TRUNCATED;
	}
	if (!get_u16(&cursor, &width) || !get_u16(&cursor, &height)) {
		return WORLD_SPELUNK_CODEC_TRUNCATED;
	}
	if (!width || width > WORLD_SPELUNK_WIDTH_MAX || !height ||
			height > WORLD_SPELUNK_HEIGHT_MAX) {
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	if (!get_u32(&cursor, &rules.action_energy) ||
			!get_u16(&cursor, &value16)) {
		return WORLD_SPELUNK_CODEC_TRUNCATED;
	}
	rules.safe_fall_tiles = value16;
	if (!get_u16(&cursor, &value16)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	rules.fall_base_damage = value16;
	if (!get_u16(&cursor, &value16)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	rules.jump_stamina_cost = value16;
	if (!get_u16(&cursor, &value16)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	rules.grip_lateral_stamina_cost = value16;
	rules.grip_up_stamina_cost = value16;
	rules.grip_down_stamina_cost = value16;
	if (!get_u16(&cursor, &value16)) return WORLD_SPELUNK_CODEC_TRUNCATED;
	rules.rest_stamina_gain = value16;
	if (version >= 2) {
		if (!get_u16(&cursor, &value16)) {
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		rules.rope_max_length = value16;
	} else {
		rules.rope_max_length = LEGACY_ROPE_MAX_LENGTH;
	}
	if (version >= 3) {
		if (!get_u16(&cursor, &value16)) {
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		rules.rope_down_stamina_cost = value16;
	} else {
		rules.rope_down_stamina_cost = LEGACY_ROPE_TURN_STAMINA_COST;
	}
	rules.rope_up_stamina_cost = rules.rope_down_stamina_cost;
	rules.rope_lateral_stamina_cost = rules.rope_down_stamina_cost;
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_BREATH) {
		if (!get_u16(&cursor, &value16)) {
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		rules.breath_turns = value16;
		if (!get_u16(&cursor, &value16)) {
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		rules.drowning_damage = value16;
	} else {
		rules.breath_turns = LEGACY_BREATH_TURNS;
		rules.drowning_damage = LEGACY_DROWNING_DAMAGE;
	}
	rules.swim_turn_stamina_cost = LEGACY_SWIM_TURN_STAMINA_COST;
	if (!get_s16(&cursor, &x) || !get_s16(&cursor, &y) ||
			!get_u16(&cursor, &stamina) ||
			!get_u16(&cursor, &max_stamina)) {
		return WORLD_SPELUNK_CODEC_TRUNCATED;
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_BREATH &&
			!get_u16(&cursor, &breath)) {
		return WORLD_SPELUNK_CODEC_TRUNCATED;
	}
	if (!get_s16(&cursor, &fall_start_y) ||
			!get_s16(&cursor, &grip_target_x) ||
			!get_s16(&cursor, &grip_target_y) ||
			!get_u8(&cursor, &movement) ||
			!get_u8(&cursor, &jump_holding) ||
			!get_u32(&cursor, &cell_count) ||
			!get_u16(&cursor, &runs)) {
		return WORLD_SPELUNK_CODEC_TRUNCATED;
	}
	if (cell_count != (uint32_t)width * (uint32_t)height || !runs ||
			runs > cell_count || jump_holding > 1) {
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	cells = mem_alloc((size_t)cell_count * sizeof(*cells));
	for (i = 0; i < runs; i++) {
		uint8_t tile;
		uint16_t run_length;
		size_t j;

		if (!get_u8(&cursor, &tile) || !get_u16(&cursor, &run_length)) {
			mem_free(cells);
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		if ((tile != WORLD_SPELUNK_AIR && tile != WORLD_SPELUNK_ROCK &&
				(version < WORLD_SPELUNK_PAYLOAD_VERSION_WATER ||
				tile != WORLD_SPELUNK_WATER)) ||
				!run_length || filled + run_length > cell_count) {
			mem_free(cells);
			return WORLD_SPELUNK_CODEC_CORRUPT;
		}
		for (j = 0; j < run_length; j++) cells[filled++] = tile;
	}
	if (filled != cell_count) {
		mem_free(cells);
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	if (version >= 2) {
		enum world_spelunk_codec_result overlay_result;

		pitons = mem_zalloc((size_t)cell_count * sizeof(*pitons));
		ropes = mem_zalloc((size_t)cell_count * sizeof(*ropes));
		overlay_result = get_byte_runs(&cursor, pitons, cell_count);
		if (overlay_result == WORLD_SPELUNK_CODEC_OK) {
			overlay_result = get_byte_runs(&cursor, ropes, cell_count);
		}
		if (overlay_result != WORLD_SPELUNK_CODEC_OK) {
			mem_free(ropes);
			mem_free(pitons);
			mem_free(cells);
			return overlay_result;
		}
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_EXPLORATION) {
		enum world_spelunk_codec_result explored_result;

		explored = mem_zalloc((size_t)cell_count * sizeof(*explored));
		explored_result = get_byte_runs(&cursor, explored, cell_count);
		if (explored_result != WORLD_SPELUNK_CODEC_OK) {
			mem_free(explored);
			mem_free(ropes);
			mem_free(pitons);
			mem_free(cells);
			return explored_result;
		}
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_ACTORS) {
		if (!get_u8(&cursor, &actor_roster_initialized) ||
				!get_u8(&cursor, &actor_count)) {
			mem_free(explored);
			mem_free(ropes);
			mem_free(pitons);
			mem_free(cells);
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		if (actor_roster_initialized > 1 ||
				actor_count > WORLD_SPELUNK_ACTOR_MAX ||
				(!actor_roster_initialized && actor_count != 0)) {
			mem_free(explored);
			mem_free(ropes);
			mem_free(pitons);
			mem_free(cells);
			return WORLD_SPELUNK_CODEC_CORRUPT;
		}
		for (i = 0; i < actor_count; i++) {
			const struct world_spelunk_actor_definition *definition;
			char actor_id[WORLD_ID_LEN];
			int16_t actor_x;
			int16_t actor_y;
			uint16_t hp;
			uint16_t legacy_max_hp = 0;
			uint32_t energy = 0;
			uint8_t actor_id_length;
			unsigned int j;

			if (!get_u8(&cursor, &actor_id_length)) {
				mem_free(explored);
				mem_free(ropes);
				mem_free(pitons);
				mem_free(cells);
				return WORLD_SPELUNK_CODEC_TRUNCATED;
			}
			if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_STABLE_ACTOR_IDS) {
				if (!actor_id_length || actor_id_length >= WORLD_ID_LEN) {
					mem_free(explored);
					mem_free(ropes);
					mem_free(pitons);
					mem_free(cells);
					return WORLD_SPELUNK_CODEC_CORRUPT;
				}
				for (j = 0; j < actor_id_length; j++) {
					if (!get_u8(&cursor, &byte)) {
						mem_free(explored);
						mem_free(ropes);
						mem_free(pitons);
						mem_free(cells);
						return WORLD_SPELUNK_CODEC_TRUNCATED;
					}
					actor_id[j] = (char)byte;
				}
				actor_id[actor_id_length] = '\0';
				if (!world_id_is_valid(actor_id)) {
					mem_free(explored);
					mem_free(ropes);
					mem_free(pitons);
					mem_free(cells);
					return WORLD_SPELUNK_CODEC_CORRUPT;
				}
			} else {
				const char *legacy_id = legacy_actor_id(actor_id_length);

				if (!legacy_id) {
					mem_free(explored);
					mem_free(ropes);
					mem_free(pitons);
					mem_free(cells);
					return WORLD_SPELUNK_CODEC_CORRUPT;
				}
				my_strcpy(actor_id, legacy_id, sizeof(actor_id));
			}
			if (!get_s16(&cursor, &actor_x) ||
					!get_s16(&cursor, &actor_y) ||
					!get_u16(&cursor, &hp) ||
					(version <
						WORLD_SPELUNK_PAYLOAD_VERSION_STABLE_ACTOR_IDS &&
					!get_u16(&cursor, &legacy_max_hp)) ||
					(version >=
						WORLD_SPELUNK_PAYLOAD_VERSION_ACTOR_CADENCE &&
					!get_u32(&cursor, &energy))) {
				mem_free(explored);
				mem_free(ropes);
				mem_free(pitons);
				mem_free(cells);
				return WORLD_SPELUNK_CODEC_TRUNCATED;
			}
			definition = world_spelunk_actor_by_id(actor_id);
			if (!definition || !hp ||
					(version <
						WORLD_SPELUNK_PAYLOAD_VERSION_STABLE_ACTOR_IDS &&
					legacy_max_hp < hp)) {
				mem_free(explored);
				mem_free(ropes);
				mem_free(pitons);
				mem_free(cells);
				return WORLD_SPELUNK_CODEC_CORRUPT;
			}
			my_strcpy(actors[i].id, definition->id, sizeof(actors[i].id));
			actors[i].x = actor_x;
			actors[i].y = actor_y;
			/* Balance data may lower maximum HP between releases.  Keep the save
			 * identity and wound, but never let persisted state override data. */
			actors[i].hp = MIN((int)hp, definition->hitpoints);
			actors[i].max_hp = definition->hitpoints;
			actors[i].energy = energy;
		}
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_GEOLOGY) {
		uint8_t has_geology;

		if (!get_u8(&cursor, &has_geology)) {
			mem_free(explored);
			mem_free(ropes);
			mem_free(pitons);
			mem_free(cells);
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		if (has_geology > 1) {
			mem_free(explored);
			mem_free(ropes);
			mem_free(pitons);
			mem_free(cells);
			return WORLD_SPELUNK_CODEC_CORRUPT;
		}
		if (has_geology) {
			uint8_t geology_id_length;
			enum world_spelunk_codec_result tag_result;

			if (!get_u8(&cursor, &geology_id_length) ||
					!geology_id_length || geology_id_length >= WORLD_ID_LEN) {
				mem_free(explored);
				mem_free(ropes);
				mem_free(pitons);
				mem_free(cells);
				return WORLD_SPELUNK_CODEC_CORRUPT;
			}
			for (i = 0; i < geology_id_length; i++) {
				if (!get_u8(&cursor, &byte)) {
					mem_free(explored);
					mem_free(ropes);
					mem_free(pitons);
					mem_free(cells);
					return WORLD_SPELUNK_CODEC_TRUNCATED;
				}
				geology_profile_id[i] = (char)byte;
			}
			geology_profile_id[geology_id_length] = '\0';
			if (!world_id_is_valid(geology_profile_id) ||
					!get_u16(&cursor, &geology_version) ||
					!geology_version) {
				mem_free(explored);
				mem_free(ropes);
				mem_free(pitons);
				mem_free(cells);
				return WORLD_SPELUNK_CODEC_CORRUPT;
			}
			material_tags = mem_alloc((size_t)cell_count);
			decoration_tags = mem_alloc((size_t)cell_count);
			tag_result = get_tag_runs(&cursor, material_tags, cell_count);
			if (tag_result == WORLD_SPELUNK_CODEC_OK) {
				tag_result = get_tag_runs(&cursor, decoration_tags,
					cell_count);
			}
			if (tag_result != WORLD_SPELUNK_CODEC_OK) {
				mem_free(decoration_tags);
				mem_free(material_tags);
				mem_free(explored);
				mem_free(ropes);
				mem_free(pitons);
				mem_free(cells);
				return tag_result;
			}
		}
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_SWIMMING) {
		if (!get_u16(&cursor, &value16)) {
			mem_free(decoration_tags);
			mem_free(material_tags);
			mem_free(explored);
			mem_free(ropes);
			mem_free(pitons);
			mem_free(cells);
			return WORLD_SPELUNK_CODEC_TRUNCATED;
		}
		rules.swim_turn_stamina_cost = value16;
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_DIRECTIONAL_STAMINA) {
		if (!get_u16(&cursor, &value16)) goto directional_truncated;
		rules.grip_up_stamina_cost = value16;
		if (!get_u16(&cursor, &value16)) goto directional_truncated;
		rules.grip_down_stamina_cost = value16;
		if (!get_u16(&cursor, &value16)) goto directional_truncated;
		rules.rope_up_stamina_cost = value16;
		if (!get_u16(&cursor, &value16)) goto directional_truncated;
		rules.rope_lateral_stamina_cost = value16;
	}
	if (cursor.position != cursor.length) {
		mem_free(decoration_tags);
		mem_free(material_tags);
		mem_free(explored);
		mem_free(ropes);
		mem_free(pitons);
		mem_free(cells);
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	goto directional_complete;

directional_truncated:
	mem_free(decoration_tags);
	mem_free(material_tags);
	mem_free(explored);
	mem_free(ropes);
	mem_free(pitons);
	mem_free(cells);
	return WORLD_SPELUNK_CODEC_TRUNCATED;

directional_complete:
	map.cells = cells;
	map.width = width;
	map.height = height;
	map.stride = width;
	map.pitons = pitons;
	map.ropes = ropes;
	map.infrastructure_stride = pitons && ropes ? width : 0;
	if (!world_spelunk_state_init(&state, &map, &rules, x, y,
			max_stamina)) {
		mem_free(explored);
		mem_free(ropes);
		mem_free(pitons);
		mem_free(cells);
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	state.stamina = stamina;
	state.breath = version >= WORLD_SPELUNK_PAYLOAD_VERSION_BREATH ? breath :
		rules.breath_turns;
	state.fall_start_y = fall_start_y;
	state.grip_target_x = grip_target_x;
	state.grip_target_y = grip_target_y;
	state.movement = (enum world_spelunk_movement)movement;
	state.jump_holding = jump_holding != 0;
	if (version < 3) {
		size_t player_index = (size_t)state.y * width + (size_t)state.x;

		if ((pitons && pitons[player_index]) ||
				(ropes && ropes[player_index] &&
				world_spelunk_is_grounded(&state))) {
			state.movement = WORLD_SPELUNK_STANDING;
			state.fall_start_y = state.y;
			state.jump_holding = false;
		} else if (ropes && ropes[player_index]) {
			state.movement = state.stamina > 0 ? WORLD_SPELUNK_HANGING :
				WORLD_SPELUNK_FALLING;
			state.fall_start_y = state.y;
			state.jump_holding = false;
		}
	}
	runtime = world_spelunk_runtime_create(location_id, &state);
	mem_free(cells);
	if (!runtime) {
		mem_free(decoration_tags);
		mem_free(material_tags);
		mem_free(explored);
		mem_free(ropes);
		mem_free(pitons);
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	runtime->layout_revision = layout_revision;
	if (version >= 2) {
		memcpy(runtime->pitons, pitons,
			(size_t)cell_count * sizeof(*runtime->pitons));
		memcpy(runtime->ropes, ropes,
			(size_t)cell_count * sizeof(*runtime->ropes));
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_EXPLORATION) {
		memcpy(runtime->explored, explored,
			(size_t)cell_count * sizeof(*runtime->explored));
	} else {
		/* Earlier builds displayed the complete side-view map.  Preserve that
		 * knowledge when upgrading rather than making remembered terrain vanish. */
		memset(runtime->explored, 1,
			(size_t)cell_count * sizeof(*runtime->explored));
	}
	if (version >= WORLD_SPELUNK_PAYLOAD_VERSION_ACTORS) {
		runtime->actor_roster_initialized =
			actor_roster_initialized != 0;
		runtime->actor_count = actor_count;
		memcpy(runtime->actors, actors,
			(size_t)actor_count * sizeof(runtime->actors[0]));
	}
	if (geology_profile_id[0] &&
			!world_spelunk_runtime_set_geology(runtime, geology_profile_id,
				geology_version, material_tags, decoration_tags)) {
		world_spelunk_runtime_free(runtime);
		mem_free(decoration_tags);
		mem_free(material_tags);
		mem_free(explored);
		mem_free(ropes);
		mem_free(pitons);
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	world_spelunk_visibility_follow_player(runtime);
	mem_free(decoration_tags);
	mem_free(material_tags);
	mem_free(explored);
	mem_free(ropes);
	mem_free(pitons);
	if (!world_spelunk_runtime_is_valid(runtime)) {
		world_spelunk_runtime_free(runtime);
		return WORLD_SPELUNK_CODEC_CORRUPT;
	}
	*decoded = runtime;
	return WORLD_SPELUNK_CODEC_OK;
}
