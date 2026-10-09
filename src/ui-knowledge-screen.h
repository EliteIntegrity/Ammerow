/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file ui-knowledge-screen.h
 * \brief Semantic presentation adapter for the knowledge gateway.
 */

#ifndef INCLUDED_UI_KNOWLEDGE_SCREEN_H
#define INCLUDED_UI_KNOWLEDGE_SCREEN_H

struct menu;

void ui_knowledge_screen_present(struct menu *menu, int store_first,
		int store_count);

#endif /* INCLUDED_UI_KNOWLEDGE_SCREEN_H */
