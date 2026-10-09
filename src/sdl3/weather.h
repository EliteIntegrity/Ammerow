/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/weather.h
 * \brief Fixed-capacity, presentation-only town weather state.
 */

#ifndef INCLUDED_SDL3_WEATHER_H
#define INCLUDED_SDL3_WEATHER_H

#include <stdbool.h>
#include <stdint.h>

#define SDL3_WEATHER_TICK_MS 50U
#define SDL3_WEATHER_FLASH_TICKS 4U

enum sdl3_weather_mode {
	SDL3_WEATHER_OFF = 0,
	SDL3_WEATHER_AUTO,
	SDL3_WEATHER_RAIN,
	SDL3_WEATHER_SNOW,
	SDL3_WEATHER_MODE_COUNT
};

enum sdl3_weather_kind {
	SDL3_WEATHER_KIND_NONE = 0,
	SDL3_WEATHER_KIND_RAIN,
	SDL3_WEATHER_KIND_SNOW
};

enum sdl3_weather_intensity {
	SDL3_WEATHER_LIGHT = 1,
	SDL3_WEATHER_STEADY,
	SDL3_WEATHER_HEAVY
};

struct sdl3_weather_state {
	enum sdl3_weather_mode mode;
	enum sdl3_weather_kind kind;
	uint32_t seed;
	uint32_t frame;
	uint32_t next_intensity_frame;
	uint32_t next_kind_frame;
	uint32_t next_lightning_frame;
	uint8_t intensity;
	uint8_t flash_ticks;
	bool visible;
};

const char *sdl3_weather_mode_name(enum sdl3_weather_mode mode);
enum sdl3_weather_mode sdl3_weather_mode_change(
		enum sdl3_weather_mode mode, int delta);
void sdl3_weather_reset(struct sdl3_weather_state *weather,
		enum sdl3_weather_mode mode, uint32_t seed);
void sdl3_weather_set_visible(struct sdl3_weather_state *weather,
		bool visible);
bool sdl3_weather_advance(struct sdl3_weather_state *weather);

#endif /* INCLUDED_SDL3_WEATHER_H */
