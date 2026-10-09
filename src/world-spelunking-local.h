/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-local.h
 * \brief Internal ownership helpers for spelunking-local state.
 *
 *
 */

#ifndef WORLD_SPELUNKING_LOCAL_H
#define WORLD_SPELUNKING_LOCAL_H

#include "world-spelunking-runtime.h"

bool world_spelunk_runtime_local_is_valid(
		const struct world_spelunk_runtime *runtime);
void world_spelunk_runtime_local_free(struct world_spelunk_runtime *runtime);

#endif /* !WORLD_SPELUNKING_LOCAL_H */
