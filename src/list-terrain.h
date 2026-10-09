/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file list-terrain.h
 * \brief List the terrain (feature) types that can appear
 *
 * These are how the code and data files refer to terrain.  Any changes will
 * break savefiles.  Note that the terrain code is stored as an unsigned 8-bit
 * integer so there can be at most 256 types of terrain.  Flags below start
 * from zero on line 13, so a terrain's sequence number is its line number
 * minus 13.
 */

/* symbol */
FEAT(NONE) /* nothing/unknown */
FEAT(FLOOR) /* open floor */
FEAT(CLOSED) /* closed door */
FEAT(OPEN) /* open door */
FEAT(BROKEN) /* broken door */
FEAT(LESS) /* up staircase */
FEAT(MORE) /* down staircase */
FEAT(STORE_GENERAL)
FEAT(STORE_ARMOR)
FEAT(STORE_WEAPON)
FEAT(STORE_BOOK)
FEAT(STORE_ALCHEMY)
FEAT(STORE_MAGIC)
FEAT(STORE_BLACK)
FEAT(HOME)
FEAT(SECRET) /* secret door */
FEAT(RUBBLE) /* impassable rubble */
FEAT(MAGMA) /* magma vein wall */
FEAT(QUARTZ) /* quartz vein wall */
FEAT(MAGMA_K) /* magma vein wall with treasure */
FEAT(QUARTZ_K) /* quartz vein wall with treasure */
FEAT(GRANITE) /* granite wall */
FEAT(PERM) /* permanent wall */
FEAT(LAVA)
FEAT(PASS_RUBBLE)
FEAT(HOLE) /* authored descent into a side-view location */
/* The former supplier slot; its index remains stable after the Fishing Shop
 * rename so old saves require no feature remapping. */
FEAT(STORE_FISHING)
FEAT(STORE_BROKER)
FEAT(TREE) /* passable woodland; appended to preserve saved feature IDs */
FEAT(MERIDIAN_STONE) /* entrance landmark; keep existing saved IDs stable */
