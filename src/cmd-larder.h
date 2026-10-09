/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file cmd-larder.h
 * \brief Authoritative command boundary for village-larder donations.
 */

#ifndef CMD_LARDER_H
#define CMD_LARDER_H

#include "cmd-core.h"

#include <stdint.h>

struct object;

bool world_larder_object_is_catch(const struct object *obj);
/** Per-item authored larder value, or zero when the object is not a catch. */
uint32_t world_larder_object_value(const struct object *obj);
void do_cmd_larder_donate(struct command *cmd);

#endif /* !CMD_LARDER_H */
