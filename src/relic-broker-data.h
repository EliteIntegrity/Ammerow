/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef INCLUDED_RELIC_BROKER_DATA_H
#define INCLUDED_RELIC_BROKER_DATA_H
#include "datafile.h"
#include "relic-broker.h"

struct relic_broker_band {
	uint8_t index;
	char *id, *name;
	int32_t minimum, maximum, price;
	unsigned int quantity;
};

extern struct file_parser relic_broker_parser;
int relic_broker_band_count(void);
const struct relic_broker_band *relic_broker_band(int index);
#endif
