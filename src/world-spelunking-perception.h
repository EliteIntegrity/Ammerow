/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file world-spelunking-perception.h
 * \brief Renderer-independent cave perception authored by either data path.
 */

#ifndef WORLD_SPELUNKING_PERCEPTION_H
#define WORLD_SPELUNKING_PERCEPTION_H

#include <stdint.h>
#include <wchar.h>

/** These values affect knowledge, never pixels or display size. */
struct world_spelunk_perception {
	uint16_t sight_horizontal_reach;
	uint16_t sight_upward_reach;
	uint16_t sight_downward_reach;
	uint16_t peek_reach;
	uint16_t downward_peek_reach;
	uint16_t peek_lateral_reach;
	uint16_t peek_close_reach;
	uint8_t remembered_brightness_percent;
	wchar_t visible_air_glyph;
	uint8_t visible_air_attr;
	wchar_t remembered_air_glyph;
	uint8_t remembered_air_attr;
};

#endif /* !WORLD_SPELUNKING_PERCEPTION_H */
