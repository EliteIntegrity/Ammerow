/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/** \file world-fishing-discovery.h
 * \brief Data-authored fishing discoveries which alter persistent topology.
 */

#ifndef WORLD_FISHING_DISCOVERY_H
#define WORLD_FISHING_DISCOVERY_H

#include "world-fishing.h"

struct player;

enum world_fishing_discovery_result {
	WORLD_FISHING_DISCOVERY_NONE = 0,
	WORLD_FISHING_DISCOVERY_ALREADY_RECORDED,
	WORLD_FISHING_DISCOVERY_UNLOCKED,
	WORLD_FISHING_DISCOVERY_ERROR
};

/**
 * Match one landed catch against immutable discovery data and update the
 * route ledger plus its cave portal.  The returned message is borrowed from
 * the fishing-data registry and remains owned by it.
 */
enum world_fishing_discovery_result world_fishing_apply_discovery(
		struct player *p, const struct world_fishing_runtime *runtime,
		const struct world_fishing_report *report, const char **message);

#endif /* !WORLD_FISHING_DISCOVERY_H */
