/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-data.h
 * \brief Data-file interface for the explorable world model.
 */

#ifndef WORLD_DATA_H
#define WORLD_DATA_H

struct file_parser;

/** Parser lifecycle for lib/gamedata/world.txt. */
extern struct file_parser world_parser;

#endif /* !WORLD_DATA_H */
