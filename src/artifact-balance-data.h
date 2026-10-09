/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file artifact-balance-data.h
 * \brief Anonymous aggregate calibration for generated artefacts.
 */

#ifndef ARTIFACT_BALANCE_DATA_H
#define ARTIFACT_BALANCE_DATA_H

#include "obj-randart.h"

struct file_parser;

extern struct file_parser artifact_balance_parser;

bool artifact_balance_data_is_loaded(void);
const char *artifact_balance_origin(void);
const char *artifact_balance_review(void);
int artifact_balance_generated_slots(void);
int artifact_balance_stable_slots(void);
int artifact_balance_cost_for_power(int power);

/** Copy the validated aggregate authority into an allocated generator state. */
bool artifact_balance_apply(struct artifact_set_data *data,
		int generated_slots, int stable_slots);

#endif /* !ARTIFACT_BALANCE_DATA_H */
