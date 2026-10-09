/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-fishing.h
 * \brief Semantic command boundary for the fishing activity.
 */

#ifndef CMD_FISHING_H
#define CMD_FISHING_H

#include "cmd-core.h"
#include "world-fishing.h"

errr cmdq_push_fishing_action(enum world_fishing_action action);
void do_cmd_fishing_start(struct command *cmd);
void do_cmd_fishing_action(struct command *cmd);
void do_cmd_fishing_stop(struct command *cmd);

#endif /* !CMD_FISHING_H */
