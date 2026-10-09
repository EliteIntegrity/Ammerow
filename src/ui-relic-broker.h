/* Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only */
#ifndef INCLUDED_UI_RELIC_BROKER_H
#define INCLUDED_UI_RELIC_BROKER_H
struct store;
void ui_relic_broker(struct store *store);
void ui_relic_broker_present(struct store *store, int cursor, int top);
#endif
