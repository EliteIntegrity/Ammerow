/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-spelunking.h
 * \brief Semantic command boundary for side-view spelunking locations.
 *
 *
 */

#ifndef INCLUDED_CMD_SPELUNKING_H
#define INCLUDED_CMD_SPELUNKING_H

#include "cmd-core.h"
#include "world-spelunking.h"
#include "world-spelunking-adapter.h"
#include "world-spelunking-passage.h"
#include "world-spelunking-visibility.h"

errr cmdq_push_spelunk_action(enum world_spelunk_command_kind kind,
		int dx, int dy);
errr cmdq_push_spelunk_local(enum world_spelunk_local_action action);
errr cmdq_push_spelunk_passage(
		enum world_spelunk_passage_direction direction);
errr cmdq_push_spelunk_exit(void);
errr cmdq_push_spelunk_peek(enum world_spelunk_peek_action action,
	int dx, int dy);
void do_cmd_spelunk_action(struct command *cmd);
void do_cmd_spelunk_local(struct command *cmd);
void do_cmd_spelunk_passage(struct command *cmd);
void do_cmd_spelunk_exit(struct command *cmd);
void do_cmd_spelunk_peek(struct command *cmd);

#endif /* INCLUDED_CMD_SPELUNKING_H */
