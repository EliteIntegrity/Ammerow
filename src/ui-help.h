/*
 * Ammerow modifications Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
   \file ui-help.h
   \brief In-game help
 */

#ifndef UI_HELP_H
#define UI_HELP_H

extern bool show_file(const char *name, const char *what, int line, int mode);
extern bool ui_help_preview(const char *name, int line);
extern void do_cmd_help(void);

#endif /* UI_HELP_H */
