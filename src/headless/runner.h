/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file headless/runner.h
 * \brief Semantic command runner for deterministic development scenarios.
 */

#ifndef HEADLESS_RUNNER_H
#define HEADLESS_RUNNER_H

#include "headless/scenario.h"

bool headless_run_scenario(const struct headless_scenario *scenario,
		const char *output_dir, char *error, size_t error_size);

#endif /* HEADLESS_RUNNER_H */
