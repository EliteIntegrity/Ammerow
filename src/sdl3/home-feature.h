/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/home-feature.h
 * \brief Curated data-driven feature art for the SDL3 Home screen.
 */

#ifndef INCLUDED_SDL3_HOME_FEATURE_H
#define INCLUDED_SDL3_HOME_FEATURE_H

#include "h-basic.h"

#include <stdint.h>

struct file_parser;

#define SDL3_HOME_FEATURE_MAX_ASSETS 16

extern struct file_parser home_feature_parser;

unsigned int sdl3_home_feature_count(void);
const char *sdl3_home_feature_at(unsigned int index);
const char *sdl3_home_feature_for_seed(uint64_t seed);

#endif /* INCLUDED_SDL3_HOME_FEATURE_H */
