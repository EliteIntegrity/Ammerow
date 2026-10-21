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

static int test_ticks_coalesce_and_resume(void *state)
{
	struct sdl3_weather_state weather;
	uint64_t next = 123;
	(void)state;
	sdl3_weather_reset(&weather, SDL3_WEATHER_RAIN, 42U);
	require(!sdl3_weather_tick(&weather, 500, &next));
	eq(next, 0);
	sdl3_weather_set_visible(&weather, true);
	require(!sdl3_weather_tick(&weather, 1000, &next));
	eq(next, 1000 + SDL3_WEATHER_TICK_MS);
	require(!sdl3_weather_tick(&weather, next - 1, &next));
	require(sdl3_weather_tick(&weather, next, &next));
	eq(weather.frame, 1);
	/* A long stall advances one frame, never an unbounded catch-up loop. */
	require(sdl3_weather_tick(&weather, 9000, &next));
	eq(weather.frame, 2);
	eq(next, 9000 + SDL3_WEATHER_TICK_MS);
	require(!sdl3_weather_tick(&weather, 9000, &next));
	sdl3_weather_set_visible(&weather, false);
	require(!sdl3_weather_tick(&weather, 9500, &next));
	eq(next, 0);
	sdl3_weather_set_visible(&weather, true);
	require(!sdl3_weather_tick(&weather, 9999, &next));
	eq(weather.frame, 2);
	require(!sdl3_weather_tick(NULL, 1, &next));
	require(!sdl3_weather_tick(&weather, 1, NULL));
	ok;
}

static int test_splashes_stay_small_across_zoom_and_dpi(void *state)
{
	(void)state;
	for (int width = 2; width <= 512; width *= 2) {
		for (int dpi = 1; dpi <= 3; dpi++) {
			float previous = 0;
			for (int frame = 0; frame <= 10; frame++) {
				float half = sdl3_weather_splash_half_width((float)width,
					frame / 10.0f, (float)dpi);
				require(half >= previous && half <= 4.0f * dpi);
				require(half <= width * 0.25f);
				previous = half;
			}
		}
	}
	require(sdl3_weather_splash_half_width(64, 1, 1) ==
		sdl3_weather_splash_half_width(256, 1, 1));
	require(sdl3_weather_splash_half_width(0, 1, 1) == 0);
	require(sdl3_weather_splash_half_width(64, -1, 0) == 1.5f);
	require(sdl3_weather_splash_half_width(64, 2, 1) == 4.0f);
	ok;
}

const char *suite_name = "sdl3/weather";
struct test tests[] = {
	{ "weather ticks coalesce and resume", test_ticks_coalesce_and_resume },
	{ "splash size bounded across zoom and DPI", test_splashes_stay_small_across_zoom_and_dpi },
	{ "mode names and navigation", test_mode_names_and_navigation },
	{ "deterministic reset and forced modes",
		test_reset_is_deterministic_and_forced_modes_hold },
	{ "visibility and intensity progression",
		test_visibility_and_intensity_progression },
	{ "auto transition and rare lightning",
		test_auto_transition_and_rare_lightning },
	{ NULL, NULL },
};
