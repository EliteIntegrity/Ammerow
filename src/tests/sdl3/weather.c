/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/weather.c */
/* Exercise deterministic, fixed-capacity town weather state. */

#include "unit-test.h"

#include "sdl3/weather.h"

int setup_tests(void **data)
{
	(void)data;
	return 0;
}

int teardown_tests(void *data)
{
	(void)data;
	return 0;
}

static int test_mode_names_and_navigation(void *state)
{
	(void)state;
	require(streq(sdl3_weather_mode_name(SDL3_WEATHER_OFF), "Off"));
	require(streq(sdl3_weather_mode_name(SDL3_WEATHER_AUTO), "Auto"));
	require(streq(sdl3_weather_mode_name(SDL3_WEATHER_RAIN), "Rain"));
	require(streq(sdl3_weather_mode_name(SDL3_WEATHER_SNOW), "Snow"));
	eq(sdl3_weather_mode_change(SDL3_WEATHER_OFF, -1), SDL3_WEATHER_SNOW);
	eq(sdl3_weather_mode_change(SDL3_WEATHER_SNOW, 1), SDL3_WEATHER_OFF);
	eq(sdl3_weather_mode_change(SDL3_WEATHER_AUTO, 0), SDL3_WEATHER_AUTO);
	ok;
}

static int test_reset_is_deterministic_and_forced_modes_hold(void *state)
{
	struct sdl3_weather_state first;
	struct sdl3_weather_state second;
	(void)state;

	sdl3_weather_reset(&first, SDL3_WEATHER_AUTO, 123456U);
	sdl3_weather_reset(&second, SDL3_WEATHER_AUTO, 123456U);
	eq(first.kind, second.kind);
	eq(first.intensity, second.intensity);
	eq(first.next_intensity_frame, second.next_intensity_frame);
	eq(first.next_kind_frame, second.next_kind_frame);
	eq(first.next_lightning_frame, second.next_lightning_frame);
	require(!first.visible);
	sdl3_weather_reset(&first, SDL3_WEATHER_RAIN, 9U);
	eq(first.kind, SDL3_WEATHER_KIND_RAIN);
	sdl3_weather_reset(&first, SDL3_WEATHER_SNOW, 9U);
	eq(first.kind, SDL3_WEATHER_KIND_SNOW);
	ok;
}

static int test_visibility_and_intensity_progression(void *state)
{
	struct sdl3_weather_state weather;
	uint8_t initial;
	(void)state;

	sdl3_weather_reset(&weather, SDL3_WEATHER_RAIN, 42U);
	require(!sdl3_weather_advance(&weather));
	eq(weather.frame, 0U);
	sdl3_weather_set_visible(&weather, true);
	require(weather.visible);
	initial = weather.intensity;
	weather.next_intensity_frame = 1U;
	require(sdl3_weather_advance(&weather));
	eq(weather.frame, 1U);
	require(weather.intensity != initial);
	sdl3_weather_set_visible(&weather, false);
	require(!weather.visible);
	require(!sdl3_weather_advance(&weather));
	eq(weather.frame, 1U);
	ok;
}

static int test_auto_transition_and_rare_lightning(void *state)
{
	struct sdl3_weather_state weather;
	enum sdl3_weather_kind initial;
	(void)state;

	sdl3_weather_reset(&weather, SDL3_WEATHER_AUTO, 7654U);
	sdl3_weather_set_visible(&weather, true);
	initial = weather.kind;
	weather.next_kind_frame = 1U;
	require(sdl3_weather_advance(&weather));
	require(weather.kind != initial);
	weather.kind = SDL3_WEATHER_KIND_RAIN;
	weather.next_lightning_frame = weather.frame + 1U;
	require(sdl3_weather_advance(&weather));
	eq(weather.flash_ticks, SDL3_WEATHER_FLASH_TICKS);
	require(weather.next_lightning_frame > weather.frame + 3000U);
	weather.kind = SDL3_WEATHER_KIND_SNOW;
	weather.flash_ticks = 0;
	weather.next_lightning_frame = weather.frame + 1U;
	require(sdl3_weather_advance(&weather));
	eq(weather.flash_ticks, 0U);
	ok;
}

const char *suite_name = "sdl3/weather";
struct test tests[] = {
	{ "mode names and navigation", test_mode_names_and_navigation },
	{ "deterministic reset and forced modes",
		test_reset_is_deterministic_and_forced_modes_hold },
	{ "visibility and intensity progression",
		test_visibility_and_intensity_progression },
	{ "auto transition and rare lightning",
		test_auto_transition_and_rare_lightning },
	{ NULL, NULL },
};
