/*
 * Copyright (c) 2026 John Horton
 * SPDX-License-Identifier: GPL-2.0-only
 */

/* sdl3/audio.c */
/* Exercise pure SDL3 audio settings and classification policy. */

#include "unit-test.h"

#include "sdl3/audio.h"

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

static int test_defaults_and_volume_steps(void *state)
{
	struct sdl3_audio_settings settings;
	(void)state;

	sdl3_audio_settings_defaults(&settings);
	require(settings.enabled);
	require(settings.movement_enabled);
	require(settings.music_enabled);
	eq(settings.master_volume, 70);
	eq(settings.interface_volume, 60);
	eq(settings.gameplay_volume, 70);
	eq(settings.creature_volume, 65);
	eq(settings.ambient_volume, 40);
	eq(settings.shop_ambient_percent, 25);
	eq(settings.music_volume, 50);
	eq(sdl3_audio_change_volume(95, 1), 100);
	eq(sdl3_audio_change_volume(5, -1), 0);
	eq(sdl3_audio_change_volume(60, 1), 70);
	ok;
}

static int test_category_classification(void *state)
{
	(void)state;

	require(sdl3_audio_cue_id_is_valid("retro/interface/coin"));
	require(sdl3_audio_cue_id_is_valid("ammerow/ui/navigate_01"));
	require(sdl3_audio_cue_id_is_authored("ammerow/ui/navigate_01"));
	require(!sdl3_audio_cue_id_is_authored("retro/interface/coin"));
	require(!sdl3_audio_cue_id_is_valid("retro/"));
	require(!sdl3_audio_cue_id_is_valid("retro/../amb_thunder_rain"));
	require(!sdl3_audio_cue_id_is_valid("ammerow/../ui/navigate_01"));
	require(!sdl3_audio_cue_id_is_valid("retro//interface/coin"));
	require(!sdl3_audio_cue_id_is_valid("ammerow//ui/navigate_01"));
	require(!sdl3_audio_cue_id_is_valid("amb_thunder_rain"));
	require(!sdl3_audio_cue_id_is_valid(NULL));
	eq(sdl3_audio_category_for_path("retro/interface/coin"),
		SDL3_AUDIO_INTERFACE);
	eq(sdl3_audio_category_for_path("retro/ui/ui_accept"),
		SDL3_AUDIO_INTERFACE);
	eq(sdl3_audio_category_for_path("retro/creatures/frog"),
		SDL3_AUDIO_CREATURE);
	eq(sdl3_audio_category_for_path("ammerow/breath/fire_01"),
		SDL3_AUDIO_CREATURE);
	eq(sdl3_audio_category_for_path("retro/ambient/deep_drift"),
		SDL3_AUDIO_AMBIENT);
	eq(sdl3_audio_category_for_path("ammerow/music/home_theme_01"),
		SDL3_AUDIO_MUSIC);
	eq(sdl3_audio_category_for_path("retro/impacts/hit"),
		SDL3_AUDIO_GAMEPLAY);
	ok;
}

static int test_category_volumes(void *state)
{
	struct sdl3_audio_settings settings;
	(void)state;

	sdl3_audio_settings_defaults(&settings);
	eq(sdl3_audio_category_volume(&settings, SDL3_AUDIO_INTERFACE), 60);
	eq(sdl3_audio_category_volume(&settings, SDL3_AUDIO_GAMEPLAY), 70);
	eq(sdl3_audio_category_volume(&settings, SDL3_AUDIO_CREATURE), 65);
	eq(sdl3_audio_category_volume(&settings, SDL3_AUDIO_AMBIENT), 40);
	eq(sdl3_audio_category_volume(&settings, SDL3_AUDIO_MUSIC), 50);
	require(sdl3_audio_category_is_background(SDL3_AUDIO_AMBIENT));
	require(sdl3_audio_category_is_background(SDL3_AUDIO_MUSIC));
	require(!sdl3_audio_category_is_background(SDL3_AUDIO_GAMEPLAY));
	require(sdl3_audio_category_is_audible(&settings, SDL3_AUDIO_MUSIC));
	settings.music_enabled = false;
	require(!sdl3_audio_category_is_audible(&settings, SDL3_AUDIO_MUSIC));
	require(sdl3_audio_category_is_audible(&settings, SDL3_AUDIO_GAMEPLAY));
	ok;
}

static int test_shop_ambience_gain(void *state)
{
	struct sdl3_audio_settings settings;
	int category;
	float outside, inside;
	(void)state;
	sdl3_audio_settings_defaults(&settings);
	outside = sdl3_audio_category_gain(&settings, SDL3_AUDIO_AMBIENT, false);
	inside = sdl3_audio_category_gain(&settings, SDL3_AUDIO_AMBIENT, true);
	require(outside > 0.279f && outside < 0.281f);
	require(inside > 0.069f && inside < 0.071f);
	for (category = 0; category < SDL3_AUDIO_CATEGORY_COUNT; category++) {
		if (category == SDL3_AUDIO_AMBIENT) continue;
		eq(sdl3_audio_category_gain(&settings, category, true),
			sdl3_audio_category_gain(&settings, category, false));
	}
	/* Leaving restores exactly the user's gain, including mute; repeated
	 * entries never compound a reduction in the saved volume. */
	eq(sdl3_audio_category_gain(&settings, SDL3_AUDIO_AMBIENT, false), outside);
	eq(settings.ambient_volume, 40);
	settings.shop_ambient_percent = 0;
	eq(sdl3_audio_category_gain(&settings, SDL3_AUDIO_AMBIENT, true), 0.0f);
	settings.shop_ambient_percent = 100;
	eq(sdl3_audio_category_gain(&settings, SDL3_AUDIO_AMBIENT, true), outside);
	settings.ambient_volume = 0;
	eq(sdl3_audio_category_gain(&settings, SDL3_AUDIO_AMBIENT, false), 0.0f);
	settings.enabled = false;
	eq(sdl3_audio_category_gain(&settings, SDL3_AUDIO_INTERFACE, true), 0.0f);
	ok;
}

static int test_rate_limits(void *state)
{
	(void)state;

	eq(sdl3_audio_rate_limit_ms("retro/ambient/deep_drift"), 15000);
	eq(sdl3_audio_rate_limit_ms("ammerow/music/home_theme_01"), 15000);
	eq(sdl3_audio_rate_limit_ms("retro/movement/player_step"), 70);
	eq(sdl3_audio_rate_limit_ms("retro/status/poison"), 200);
	eq(sdl3_audio_rate_limit_ms("ammerow/spelunk/piton_hammer_01"), 100);
	eq(sdl3_audio_rate_limit_ms("ammerow/fishing/reel_01"), 70);
	eq(sdl3_audio_rate_limit_ms("retro/impacts/hit"), 35);
	ok;
}

static int test_synth_profiles(void *state)
{
	struct sdl3_audio_synth_profile first;
	struct sdl3_audio_synth_profile repeated;
	struct sdl3_audio_synth_profile creature;
	struct sdl3_audio_synth_profile ambient;
	struct sdl3_audio_synth_profile music;
	(void)state;

	require(!sdl3_audio_make_synth_profile(NULL, &first));
	require(!sdl3_audio_make_synth_profile("", &first));
	require(!sdl3_audio_make_synth_profile("retro/interface/coin", NULL));
	require(sdl3_audio_make_synth_profile("synth:retro/interface/coin",
		&first));
	require(sdl3_audio_make_synth_profile("synth:retro/interface/coin",
		&repeated));
	eq(first.frequency_hz, repeated.frequency_hz);
	eq(first.end_frequency_hz, repeated.end_frequency_hz);
	eq(first.duration_ms, repeated.duration_ms);
	eq(first.amplitude_percent, repeated.amplitude_percent);
	require(first.frequency_hz >= 620 && first.frequency_hz < 1120);
	require(first.duration_ms >= 34 && first.duration_ms < 76);
	require(first.amplitude_percent >= 9 && first.amplitude_percent < 15);

	require(sdl3_audio_make_synth_profile("synth:retro/creatures/frog",
		&creature));
	require(creature.frequency_hz >= 105 && creature.frequency_hz < 325);
	require(creature.duration_ms >= 90 && creature.duration_ms < 210);
	require(creature.amplitude_percent >= 13 &&
		creature.amplitude_percent < 21);

	require(sdl3_audio_make_synth_profile("synth:retro/ambient/rain",
		&ambient));
	require(ambient.frequency_hz >= 115 && ambient.frequency_hz < 265);
	require(ambient.duration_ms >= 220 && ambient.duration_ms < 440);
	require(ambient.amplitude_percent >= 6 &&
		ambient.amplitude_percent < 12);
	require(sdl3_audio_make_synth_profile(
		"synth:ammerow/music/home_theme_01", &music));
	eq(music.duration_ms, 10);
	eq(music.amplitude_percent, 0);
	require(sdl3_audio_make_synth_profile(
		"ammerow/movement/step_01", &first));
	eq(first.frequency_hz, 262);
	require(sdl3_audio_make_synth_profile(
		"ammerow/movement/blocked_01", &first));
	eq(first.frequency_hz, 370);
	require(sdl3_audio_make_synth_profile(
		"ammerow/spelunk/rope_deploy_01", &first));
	require(first.end_frequency_hz < first.frequency_hz);
	require(sdl3_audio_make_synth_profile(
		"synth:retro/movement/player_step", &first));
	eq(first.frequency_hz, 262);
	eq(first.end_frequency_hz, 262);
	eq(first.amplitude_percent, 2);
	require(sdl3_audio_make_synth_profile(
		"synth:retro/movement/player_blocked", &first));
	eq(first.frequency_hz, 370);
	require(first.frequency_hz != 262);
	require(sdl3_audio_make_synth_profile(
		"synth:retro/interaction/rope_drop", &first));
	require(first.end_frequency_hz < first.frequency_hz);
	ok;
}

const char *suite_name = "sdl3/audio";
struct test tests[] = {
	{ "defaults and volume steps", test_defaults_and_volume_steps },
	{ "category classification", test_category_classification },
	{ "category volumes", test_category_volumes },
	{ "shop ambience gain", test_shop_ambience_gain },
	{ "rate limits", test_rate_limits },
	{ "synth profiles", test_synth_profiles },
	{ NULL, NULL },
};
