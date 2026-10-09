/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/**
 * \file sdl3/weather.c
 * \brief Fixed-capacity, presentation-only town weather state.
 */

#include "sdl3/weather.h"

#include <string.h>

#define INTENSITY_MIN_TICKS 300U
#define INTENSITY_SPAN_TICKS 600U
#define KIND_MIN_TICKS 2400U
#define KIND_SPAN_TICKS 3600U
#define LIGHTNING_MIN_TICKS 3600U
#define LIGHTNING_SPAN_TICKS 6000U

static uint32_t weather_hash(uint32_t value)
{
	value ^= value >> 16;
	value *= 0x7feb352dU;
	value ^= value >> 15;
	value *= 0x846ca68bU;
	value ^= value >> 16;
	return value;
}

static uint32_t weather_interval(const struct sdl3_weather_state *weather,
		uint32_t salt, uint32_t minimum, uint32_t span)
{
	return minimum + weather_hash(weather->seed ^ weather->frame ^ salt) % span;
}

static enum sdl3_weather_kind initial_kind(enum sdl3_weather_mode mode,
		uint32_t seed)
{
	if (mode == SDL3_WEATHER_RAIN) return SDL3_WEATHER_KIND_RAIN;
	if (mode == SDL3_WEATHER_SNOW) return SDL3_WEATHER_KIND_SNOW;
	if (mode == SDL3_WEATHER_AUTO) {
		return weather_hash(seed ^ 0x8b57a91dU) & 1U ?
			SDL3_WEATHER_KIND_RAIN : SDL3_WEATHER_KIND_SNOW;
	}
	return SDL3_WEATHER_KIND_NONE;
}

const char *sdl3_weather_mode_name(enum sdl3_weather_mode mode)
{
	switch (mode) {
	case SDL3_WEATHER_OFF:
		return "Off";
	case SDL3_WEATHER_RAIN:
		return "Rain";
	case SDL3_WEATHER_SNOW:
		return "Snow";
	case SDL3_WEATHER_AUTO:
	default:
		return "Auto";
	}
}

enum sdl3_weather_mode sdl3_weather_mode_change(
		enum sdl3_weather_mode mode, int delta)
{
	int changed = (int)mode + (delta < 0 ? -1 : 1);

	if (!delta) return mode;
	if (changed < SDL3_WEATHER_OFF) changed = SDL3_WEATHER_MODE_COUNT - 1;
	if (changed >= SDL3_WEATHER_MODE_COUNT) changed = SDL3_WEATHER_OFF;
	return (enum sdl3_weather_mode)changed;
}

void sdl3_weather_reset(struct sdl3_weather_state *weather,
		enum sdl3_weather_mode mode, uint32_t seed)
{
	if (!weather) return;
	memset(weather, 0, sizeof(*weather));
	if (mode < SDL3_WEATHER_OFF || mode >= SDL3_WEATHER_MODE_COUNT) {
		mode = SDL3_WEATHER_AUTO;
	}
	weather->mode = mode;
	weather->seed = seed ? seed : 1U;
	weather->kind = initial_kind(mode, weather->seed);
	weather->intensity = (uint8_t)(SDL3_WEATHER_LIGHT +
		weather_hash(weather->seed ^ 0x43dc19b7U) % 3U);
	weather->next_intensity_frame = weather_interval(weather, 0x6122d34fU,
		INTENSITY_MIN_TICKS, INTENSITY_SPAN_TICKS);
	weather->next_kind_frame = weather_interval(weather, 0xa0157c29U,
		KIND_MIN_TICKS, KIND_SPAN_TICKS);
	weather->next_lightning_frame = weather_interval(weather, 0xc33905ebU,
		LIGHTNING_MIN_TICKS, LIGHTNING_SPAN_TICKS);
}

void sdl3_weather_set_visible(struct sdl3_weather_state *weather,
		bool visible)
{
	if (!weather) return;
	weather->visible = visible && weather->mode != SDL3_WEATHER_OFF &&
		weather->kind != SDL3_WEATHER_KIND_NONE;
	if (!weather->visible) weather->flash_ticks = 0;
}

bool sdl3_weather_advance(struct sdl3_weather_state *weather)
{
	if (!weather || !weather->visible || weather->mode == SDL3_WEATHER_OFF ||
			weather->kind == SDL3_WEATHER_KIND_NONE) {
		return false;
	}
	weather->frame++;
	if (weather->flash_ticks) weather->flash_ticks--;

	if (weather->frame >= weather->next_intensity_frame) {
		uint8_t changed = (uint8_t)(SDL3_WEATHER_LIGHT +
			weather_hash(weather->seed ^ weather->frame ^ 0x7d61a85bU) % 3U);

		if (changed == weather->intensity) {
			changed = changed == SDL3_WEATHER_HEAVY ? SDL3_WEATHER_LIGHT :
				(uint8_t)(changed + 1U);
		}
		weather->intensity = changed;
		weather->next_intensity_frame = weather->frame +
			weather_interval(weather, 0x214ec753U, INTENSITY_MIN_TICKS,
				INTENSITY_SPAN_TICKS);
	}

	if (weather->mode == SDL3_WEATHER_AUTO &&
			weather->frame >= weather->next_kind_frame) {
		weather->kind = weather->kind == SDL3_WEATHER_KIND_RAIN ?
			SDL3_WEATHER_KIND_SNOW : SDL3_WEATHER_KIND_RAIN;
		weather->intensity = SDL3_WEATHER_LIGHT;
		weather->next_intensity_frame = weather->frame +
			weather_interval(weather, 0x1296ec43U, INTENSITY_MIN_TICKS,
				INTENSITY_SPAN_TICKS);
		weather->next_kind_frame = weather->frame +
			weather_interval(weather, 0xb63c20e1U, KIND_MIN_TICKS,
				KIND_SPAN_TICKS);
		weather->next_lightning_frame = weather->frame +
			weather_interval(weather, 0x4795f3abU, LIGHTNING_MIN_TICKS,
				LIGHTNING_SPAN_TICKS);
		weather->flash_ticks = 0;
	}

	if (weather->kind == SDL3_WEATHER_KIND_RAIN &&
			weather->frame >= weather->next_lightning_frame) {
		weather->flash_ticks = SDL3_WEATHER_FLASH_TICKS;
		weather->next_lightning_frame = weather->frame +
			weather_interval(weather, 0xda244417U, LIGHTNING_MIN_TICKS,
				LIGHTNING_SPAN_TICKS);
	}
	return true;
}

#undef INTENSITY_MIN_TICKS
#undef INTENSITY_SPAN_TICKS
#undef KIND_MIN_TICKS
#undef KIND_SPAN_TICKS
#undef LIGHTNING_MIN_TICKS
#undef LIGHTNING_SPAN_TICKS
