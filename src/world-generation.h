/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-generation.h
 * \brief Narrow generation interface for explorable world locations.
 */

#ifndef WORLD_GENERATION_H
#define WORLD_GENERATION_H

struct chunk;
struct player;

/** Generate one retained top-down outdoor location. */
struct chunk *world_generate_outdoor(struct player *p, int min_height,
		int min_width, const char **p_error);

#endif /* !WORLD_GENERATION_H */
