/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-entry.h
 * \brief Named entry definitions and runtime resolution for world locations.
 *
 *
 */

#ifndef WORLD_ENTRY_H
#define WORLD_ENTRY_H

#include "z-type.h"

enum world_entry_kind {
	WORLD_ENTRY_GRID = 0,
	WORLD_ENTRY_UP_STAIR,
	WORLD_ENTRY_DOWN_STAIR
};

enum world_entry_result {
	WORLD_ENTRY_OK = 0,
	WORLD_ENTRY_INVALID,
	WORLD_ENTRY_WRONG_LOCATION,
	WORLD_ENTRY_NOT_FOUND,
	WORLD_ENTRY_NO_MATCH,
	WORLD_ENTRY_INVALID_CELL
};

/** Authored rock surround, relative to an entry resolved at runtime. */
struct world_entry_rock {
	struct loc offset;
	int feat;
	struct world_entry_rock *next;
};

/** One stable entry name and the rule for resolving it in a loaded chunk. */
struct world_entry {
	char *id;
	enum world_entry_kind kind;
	struct loc grid;
	struct world_entry_rock *rocks;
	struct world_entry *next;
};

struct chunk;
struct level;
struct world_route;

/** Destination of a visible/remembered stair or shaft; never reads live
 * terrain through a knowledge chunk or exposes undeclared deeper floors. */
const struct level *world_entry_known_destination(struct chunk *c,
		struct loc grid);

const struct world_entry *world_entry_by_id(const struct level *level,
		const char *id);
enum world_entry_result world_entry_locate(struct chunk *c, const char *id,
		struct loc *grid);
bool world_entry_contains(struct chunk *c, const char *id, struct loc grid);
enum world_entry_result world_entry_resolve(struct chunk *c, const char *id,
		struct loc *arrival);
bool world_entry_is_edge_route_entry(const struct level *level,
		const struct world_entry *entry);
bool world_entry_is_edge_exit_grid(const struct chunk *c, struct loc grid);
bool world_entry_is_overworld_gate_grid(const struct chunk *c, struct loc grid);
const struct world_route *world_entry_edge_route_for_step(
		const struct chunk *c, struct loc from, struct loc to);
const struct world_route *world_entry_edge_route_for_direction(
		const struct chunk *c, struct loc grid, int dir);
const struct world_route *world_entry_edge_route_for_stair_key(
		const struct chunk *c, struct loc grid, bool down);
const struct world_route *world_entry_stair_route_here(
		struct chunk *c, struct loc grid);
const struct world_route *world_entry_cross_mode_route_here(
		struct chunk *c, struct loc grid);

/**
 * Materialize fixed top-down shaft sources and connect newly carved cells to
 * the existing floor network.  Passing the level explicitly also permits use
 * during generation before the chunk receives its stable identity.
 */
bool world_entry_materialize_cross_mode_sources(struct chunk *c,
		const struct level *level);
/** Remove obsolete shaft terrain from actual or remembered saved chunks. */
void world_entry_retire_cross_mode_sources(struct chunk *c,
		const struct level *level);
bool world_entry_materialize_overworld_gates(struct chunk *c,
		const struct level *level);
/** Decorate entries without obstructing existing paths or replacing content. */
bool world_entry_materialize_landmarks(struct chunk *actual,
		struct chunk *known);

#endif /* !WORLD_ENTRY_H */
