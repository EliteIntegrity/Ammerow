/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file buildid.c
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

#include "buildid.h"

/*
 * Allow the build system to generate version.h (and define
 * the HAVE_VERSION_H preprocessor macro) or get the version via the BUILD_ID
 * preprocessor macro.  If neither is available, use a sensible default.
 */
#ifdef HAVE_VERSION_H
#include "version.h"
#elif defined(BUILD_ID)
#define STR(x) #x
#define XSTR(x) STR(x)
#define VERSION_STRING XSTR(BUILD_ID)
#endif
#ifndef VERSION_STRING
#define VERSION_STRING "unversioned"
#endif

const char *buildid = VERSION_NAME " " VERSION_STRING;
const char *buildver = VERSION_STRING;
const char *storage_buildid = VERSION_STORAGE_NAME " " VERSION_STRING;

/**
 * Link a copyright message into the executable
 */
const char *copyright =
	VERSION_TRADEMARK_NAME " " VERSION_STRING ".\n"
	"AMMEROW and AMMEROW: LANDS BEYOND are trade marks of John Horton.\n"
	"Ammerow artwork and audio copyright (c) 2026 John Horton; "
	"separately licensed under ASSET-LICENSE.md, not the GNU GPL.\n"
	"Based on Angband, copyright (c) 1987-2026 Angband contributors.\n"
	"\n"
	"The Ammerow program is free software; you may redistribute and/or modify\n"
	"it under the GNU General Public License, version 2 only.\n"
	"It is supplied without warranty to the extent permitted by law.\n"
	"See COPYING for the complete GPLv2 terms and warranty disclaimer.\n"
	"Inherited notices are preserved in docs/copying.rst and the source files.\n"
	"Third-party components retain their own licences; see THIRD_PARTY_NOTICES.md.\n";
