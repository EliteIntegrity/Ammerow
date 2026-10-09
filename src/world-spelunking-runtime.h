/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-runtime.h
 * \brief Public ownership, invariant, and persistence API for spelunking.
 *
 *
 */

#ifndef WORLD_SPELUNKING_RUNTIME_H
#define WORLD_SPELUNKING_RUNTIME_H

#include "world-location.h"
#include "world-spelunking.h"
#include "world-spelunking-perception.h"

#include <stddef.h>
#include <stdint.h>

#define WORLD_SPELUNK_PAYLOAD_VERSION_LEGACY 1
#define WORLD_SPELUNK_PAYLOAD_VERSION_INFRASTRUCTURE 2
#define WORLD_SPELUNK_PAYLOAD_VERSION_ROPE 3
#define WORLD_SPELUNK_PAYLOAD_VERSION_WATER 4
#define WORLD_SPELUNK_PAYLOAD_VERSION_EXPLORATION 5
#define WORLD_SPELUNK_PAYLOAD_VERSION_BREATH 6
#define WORLD_SPELUNK_PAYLOAD_VERSION_ACTORS 7
#define WORLD_SPELUNK_PAYLOAD_VERSION_ACTOR_CADENCE 8
#define WORLD_SPELUNK_PAYLOAD_VERSION_STABLE_ACTOR_IDS 9
#define WORLD_SPELUNK_PAYLOAD_VERSION_LAYOUT_REVISION 10
#define WORLD_SPELUNK_PAYLOAD_VERSION_GEOLOGY 11
#define WORLD_SPELUNK_PAYLOAD_VERSION_SWIMMING 12
#define WORLD_SPELUNK_PAYLOAD_VERSION_DIRECTIONAL_STAMINA 13
#define WORLD_SPELUNK_PAYLOAD_VERSION \
	WORLD_SPELUNK_PAYLOAD_VERSION_DIRECTIONAL_STAMINA
#define WORLD_SPELUNK_WIDTH_MAX 198
#define WORLD_SPELUNK_HEIGHT_MAX 132
#define WORLD_SPELUNK_CELL_MAX \
	(WORLD_SPELUNK_WIDTH_MAX * WORLD_SPELUNK_HEIGHT_MAX)
#define WORLD_SPELUNK_ACTOR_MAX 16u
#define WORLD_SPELUNK_PAYLOAD_MAX \
	(80u + 2u * (WORLD_ID_LEN - 1u) + 18u * WORLD_SPELUNK_CELL_MAX + \
	(11u + WORLD_ID_LEN - 1u) * WORLD_SPELUNK_ACTOR_MAX)
#define WORLD_SPELUNK_OBJECT_MAX 1024u

struct object;

/** Small side-view actors use stable data IDs, not file-order indices. */
struct world_spelunk_actor {
	char id[WORLD_ID_LEN];
	int x;
	int y;
	int hp;
	int max_hp;
	uint32_t energy;
};

enum world_spelunk_infrastructure_result {
	WORLD_SPELUNK_INFRASTRUCTURE_OK = 0,
	WORLD_SPELUNK_INFRASTRUCTURE_INVALID,
	WORLD_SPELUNK_INFRASTRUCTURE_UNSTABLE,
	WORLD_SPELUNK_INFRASTRUCTURE_ALREADY_PRESENT,
	WORLD_SPELUNK_INFRASTRUCTURE_NO_BRACE,
	WORLD_SPELUNK_INFRASTRUCTURE_NO_ANCHOR,
	WORLD_SPELUNK_INFRASTRUCTURE_NO_SHAFT
};

struct world_spelunk_runtime {
	char location_id[WORLD_ID_LEN];
	/** Immutable authored sight and memory rules for this cave section. */
	struct world_spelunk_perception perception;
	/** Last append-only authored layout revision applied to this terrain. */
	uint16_t layout_revision;
	enum world_spelunk_tile *cells;
	uint8_t *pitons;
	uint8_t *ropes;
	uint8_t *explored;
	uint8_t *visible;
	/** Optional data-authored presentation layers for generated terrain. */
	char geology_profile_id[WORLD_ID_LEN];
	uint16_t geology_version;
	uint8_t *material_tags;
	uint8_t *decoration_tags;
	/** Transient occupant knowledge from detection effects.  This is cleared
	 * when the player moves and is deliberately not part of the save payload. */
	uint8_t *detected;
	struct object *ground_objects;
	struct world_spelunk_actor actors[WORLD_SPELUNK_ACTOR_MAX];
	size_t actor_count;
	bool actor_roster_initialized;
	struct world_spelunk_state state;
	int peek_x;
	int peek_y;
	bool peeking;
};

enum world_spelunk_codec_result {
	WORLD_SPELUNK_CODEC_OK = 0,
	WORLD_SPELUNK_CODEC_INVALID_ARGUMENT,
	WORLD_SPELUNK_CODEC_INVALID_STATE,
	WORLD_SPELUNK_CODEC_BUFFER_TOO_SMALL,
	WORLD_SPELUNK_CODEC_BAD_MAGIC,
	WORLD_SPELUNK_CODEC_UNSUPPORTED_VERSION,
	WORLD_SPELUNK_CODEC_TRUNCATED,
	WORLD_SPELUNK_CODEC_CORRUPT
};

/** Codec/test compatibility constructor using the canonical fallback profile. */
struct world_spelunk_runtime *world_spelunk_runtime_create(
		const char *location_id, const struct world_spelunk_state *source);
/** Production constructor: perception comes from the owning layout or recipe. */
struct world_spelunk_runtime *world_spelunk_runtime_create_with_perception(
		const char *location_id, const struct world_spelunk_state *source,
		const struct world_spelunk_perception *perception);
void world_spelunk_runtime_free(struct world_spelunk_runtime *runtime);
bool world_spelunk_runtime_is_valid(
		const struct world_spelunk_runtime *runtime);
/** Replace perception after loading a save, then recompute current sight. */
bool world_spelunk_runtime_set_perception(
		struct world_spelunk_runtime *runtime,
		const struct world_spelunk_perception *perception);
/** Copy validated generated presentation layers into the owned runtime. */
bool world_spelunk_runtime_set_geology(
		struct world_spelunk_runtime *runtime, const char *profile_id,
		uint16_t version, const uint8_t *material_tags,
		const uint8_t *decoration_tags);
bool world_spelunk_runtime_has_piton(
		const struct world_spelunk_runtime *runtime, int x, int y);
bool world_spelunk_runtime_has_rope(
		const struct world_spelunk_runtime *runtime, int x, int y);
enum world_spelunk_infrastructure_result world_spelunk_runtime_place_piton(
		struct world_spelunk_runtime *runtime);
enum world_spelunk_infrastructure_result world_spelunk_runtime_deploy_rope(
		struct world_spelunk_runtime *runtime, int available_segments,
		int *segments);
size_t world_spelunk_runtime_ground_object_count(
		const struct world_spelunk_runtime *runtime);
struct object *world_spelunk_runtime_ground_object_at(
		const struct world_spelunk_runtime *runtime, int x, int y);
bool world_spelunk_runtime_can_add_ground_object_at(
		const struct world_spelunk_runtime *runtime, int x, int y);
bool world_spelunk_runtime_add_ground_object(
		struct world_spelunk_runtime *runtime, struct object *obj, int x, int y);
bool world_spelunk_runtime_take_ground_object(
		struct world_spelunk_runtime *runtime, struct object *obj);
const struct world_spelunk_actor *world_spelunk_runtime_actor_at(
		const struct world_spelunk_runtime *runtime, int x, int y);
struct world_spelunk_actor *world_spelunk_runtime_actor_at_mutable(
		struct world_spelunk_runtime *runtime, int x, int y);
bool world_spelunk_runtime_add_actor(struct world_spelunk_runtime *runtime,
		const char *actor_id, int x, int y);
bool world_spelunk_runtime_remove_actor(struct world_spelunk_runtime *runtime,
		struct world_spelunk_actor *actor);
/** Public persistence facade; wire format and migrations are codec-private. */
size_t world_spelunk_runtime_encoded_size(
		const struct world_spelunk_runtime *runtime);
enum world_spelunk_codec_result world_spelunk_runtime_encode(
		const struct world_spelunk_runtime *runtime, uint8_t *buffer,
		size_t capacity, size_t *written);
enum world_spelunk_codec_result world_spelunk_runtime_decode(
		const uint8_t *buffer, size_t length,
		struct world_spelunk_runtime **decoded);

#endif /* !WORLD_SPELUNKING_RUNTIME_H */
