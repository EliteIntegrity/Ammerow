/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file buildid.h
 * \brief Compile in build details
 *
 * Copyright (c) 2011 Andi Sidwell
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#ifndef BUILDID
#define BUILDID

#define VERSION_NAME	"Ammerow: Lands Beyond"

/* Public-facing marks live here so front ends and documents do not invent
 * competing spellings. VERSION_NAME remains plain for save labels and
 * command output. */
#define VERSION_TRADEMARK_WORD	"AMMEROW(TM)"
#define VERSION_TRADEMARK_NAME	"AMMEROW(TM): LANDS BEYOND"
#define VERSION_TRADEMARK_HOME	"A M M E R O W  (TM)"

/* Stable application identity used by save headers and per-user storage. */
#define VERSION_STORAGE_NAME	"Ammerow"

extern const char *buildid;
extern const char *buildver;
extern const char *storage_buildid;
extern const char *copyright;

#endif /* BUILDID */
